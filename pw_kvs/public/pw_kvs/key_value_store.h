// Copyright 2020 The Pigweed Authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "pw_containers/vector.h"
#include "pw_kvs/checksum.h"
#include "pw_kvs/flash_memory.h"
#include "pw_kvs/format.h"
#include "pw_kvs/internal/entry.h"
#include "pw_kvs/internal/entry_cache.h"
#include "pw_kvs/internal/key_descriptor.h"
#include "pw_kvs/internal/sectors.h"
#include "pw_kvs/internal/span_traits.h"
#include "pw_span/span.h"
#include "pw_status/status.h"
#include "pw_status/status_with_size.h"

namespace pw::kvs {

enum class GargbageCollectOnWrite {
  // Disable all automatic garbage collection on write.
  kDisabled,

  // Allow up to a single sector, if needed, to be garbage collected on write.
  kOneSector,

  // Allow as many sectors as needed be garbage collected on write.
  kAsManySectorsNeeded,
};

enum class ErrorRecovery {
  // Immediately do full recovery of any errors that are detected.
  kImmediate,

  // Recover from errors, but do some time consuming steps at a later time. Such
  // as waiting to garbage collect sectors with corrupt entries until the next
  // garbage collection.
  kLazy,
};

struct Options {
  // Perform garbage collection if necessary when writing. If not kDisabled,
  // garbage collection is attempted if space for an entry cannot be found. This
  // is a relatively lengthy operation. If kDisabled, Put calls that would
  // require garbage collection fail with RESOURCE_EXHAUSTED.
  GargbageCollectOnWrite gc_on_write = GargbageCollectOnWrite::kOneSector;

  // When the KVS handles errors that are discovered, such as corrupt entries,
  // not enough redundant copys of an entry, etc.
  ErrorRecovery recovery = ErrorRecovery::kLazy;

  // Verify an entry's checksum after reading it from flash.
  bool verify_on_read = true;

  // Verify an in-flash entry's checksum after writing it.
  bool verify_on_write = true;
};

class KeyValueStore {
 public:
  // KeyValueStores are declared as instances of
  // KeyValueStoreBuffer<MAX_ENTRIES, MAX_SECTORS>, which allocates buffers for
  // tracking entries and flash sectors.

  // Initializes the key-value store. Must be called before calling other
  // functions.
  //
  // Return values:
  //
  //          OK: KVS successfully initialized.
  //   DATA_LOSS: KVS initialized and is usable, but contains corrupt data.
  //     UNKNOWN: Unknown error. KVS is not initialized.
  //
  Status Init();

  bool initialized() const { return initialized_; }

  // Reads the value of an entry in the KVS. The value is read into the provided
  // buffer and the number of bytes read is returned. If desired, the read can
  // be started at an offset.
  //
  // If the output buffer is too small for the value, Get returns
  // RESOURCE_EXHAUSTED with the number of bytes read. The remainder of the
  // value can be read by calling get with an offset.
  //
  //                    OK: the entry was successfully read
  //             NOT_FOUND: the key is not present in the KVS
  //             DATA_LOSS: found the entry, but the data was corrupted
  //    RESOURCE_EXHAUSTED: the buffer could not fit the entire value, but as
  //                        many bytes as possible were written to it
  //   FAILED_PRECONDITION: the KVS is not initialized
  //      INVALID_ARGUMENT: key is empty or too long or value is too large
  //
  StatusWithSize Get(std::string_view key,
                     span<std::byte> value,
                     size_t offset_bytes = 0) const;

  // This overload of Get accepts a pointer to a trivially copyable object.
  // If the value is an array, call Get with as_writable_bytes(span(array)),
  // or pass a pointer to the array instead of the array itself.
  template <typename Pointer,
            typename = std::enable_if_t<std::is_pointer_v<Pointer>>>
  Status Get(const std::string_view& key, const Pointer& pointer) const {
    using T = std::remove_reference_t<std::remove_pointer_t<Pointer>>;
    CheckThatObjectCanBePutOrGet<T>();
    return FixedSizeGet(key, pointer, sizeof(T));
  }

  // Adds a key-value entry to the KVS. If the key was already present, its
  // value is overwritten.
  //
  // The value may be a span of bytes or a trivially copyable object.
  //
  // In the current implementation, all keys in the KVS must have a unique hash.
  // If Put is called with a key whose hash matches an existing key, nothing
  // is added and ALREADY_EXISTS is returned.
  //
  //                    OK: the entry was successfully added or updated
  //             DATA_LOSS: checksum validation failed after writing the data
  //    RESOURCE_EXHAUSTED: there is not enough space to add the entry
  //        ALREADY_EXISTS: the entry could not be added because a different key
  //                        with the same hash is already in the KVS
  //   FAILED_PRECONDITION: the KVS is not initialized
  //      INVALID_ARGUMENT: key is empty or too long or value is too large
  //
  template <typename T>
  Status Put(const std::string_view& key, const T& value) {
    if constexpr (ConvertsToSpan<T>::value) {
      return PutBytes(key, as_bytes(span(value)));
    } else {
      CheckThatObjectCanBePutOrGet<T>();
      return PutBytes(key, as_bytes(span(&value, 1)));
    }
  }

  // Removes a key-value entry from the KVS.
  //
  //                    OK: the entry was successfully added or updated
  //             NOT_FOUND: the key is not present in the KVS
  //             DATA_LOSS: checksum validation failed after recording the erase
  //    RESOURCE_EXHAUSTED: insufficient space to mark the entry as deleted
  //   FAILED_PRECONDITION: the KVS is not initialized
  //      INVALID_ARGUMENT: key is empty or too long
  //
  Status Delete(std::string_view key);

  // Returns the size of the value corresponding to the key.
  //
  //                    OK: the size was returned successfully
  //             NOT_FOUND: the key is not present in the KVS
  //             DATA_LOSS: checksum validation failed after reading the entry
  //   FAILED_PRECONDITION: the KVS is not initialized
  //      INVALID_ARGUMENT: key is empty or too long
  //
  StatusWithSize ValueSize(std::string_view key) const;

  // Perform garbage collection of all reclaimable space in the KVS.
  Status GarbageCollectFull();

  // Perform garbage collection of part of the KVS, typically a single sector or
  // similar unit that makes sense for the KVS implementation.
  Status GarbageCollectPartial() {
    return GarbageCollectPartial(span<const Address>());
  }

  void LogDebugInfo() const;

  // Classes and functions to support STL-style iteration.
  class iterator;

  class Item {
   public:
    // The key as a null-terminated string.
    const char* key() const { return key_buffer_.data(); }

    // Gets the value referred to by this iterator. Equivalent to
    // KeyValueStore::Get.
    StatusWithSize Get(span<std::byte> value_buffer,
                       size_t offset_bytes = 0) const {
      return kvs_.Get(key(), *iterator_, value_buffer, offset_bytes);
    }

    template <typename Pointer,
              typename = std::enable_if_t<std::is_pointer_v<Pointer>>>
    Status Get(const Pointer& pointer) const {
      using T = std::remove_reference_t<std::remove_pointer_t<Pointer>>;
      CheckThatObjectCanBePutOrGet<T>();
      return kvs_.FixedSizeGet(key(), *iterator_, pointer, sizeof(T));
    }

    // Reads the size of the value referred to by this iterator. Equivalent to
    // KeyValueStore::ValueSize.
    StatusWithSize ValueSize() const { return kvs_.ValueSize(*iterator_); }

   private:
    friend class iterator;

    constexpr Item(const KeyValueStore& kvs,
                   const internal::EntryCache::iterator& iterator)
        : kvs_(kvs), iterator_(iterator), key_buffer_{} {}

    void ReadKey();

    const KeyValueStore& kvs_;
    internal::EntryCache::iterator iterator_;

    // Buffer large enough for a null-terminated version of any valid key.
    std::array<char, internal::Entry::kMaxKeyLength + 1> key_buffer_;
  };

  class iterator {
   public:
    iterator& operator++();

    iterator& operator++(int) { return operator++(); }

    // Reads the entry's key from flash.
    const Item& operator*() {
      item_.ReadKey();
      return item_;
    }

    const Item* operator->() {
      return &operator*();  // Read the key into the Item object.
    }

    constexpr bool operator==(const iterator& rhs) const {
      return item_.iterator_ == rhs.item_.iterator_;
    }

    constexpr bool operator!=(const iterator& rhs) const {
      return item_.iterator_ != rhs.item_.iterator_;
    }

   private:
    friend class KeyValueStore;

    constexpr iterator(const KeyValueStore& kvs,
                       const internal::EntryCache::iterator& iterator)
        : item_(kvs, iterator) {}

    Item item_;
  };

  using const_iterator = iterator;  // Standard alias for iterable types.

  iterator begin() const;
  iterator end() const { return iterator(*this, entry_cache_.end()); }

  // Returns the number of valid entries in the KeyValueStore.
  size_t size() const { return entry_cache_.present_entries(); }

  size_t max_size() const { return entry_cache_.max_entries(); }

  size_t empty() const { return size() == 0u; }

  // Returns the number of transactions that have occurred since the KVS was
  // first used. This value is retained across initializations, but is reset if
  // the underlying flash is erased.
  uint32_t transaction_count() const { return last_transaction_id_; }

  struct StorageStats {
    size_t writable_bytes;
    size_t in_use_bytes;
    size_t reclaimable_bytes;
  };

  StorageStats GetStorageStats() const;

  // Level of redundancy to use for writing entries.
  size_t redundancy() const { return entry_cache_.redundancy(); }

 protected:
  using Address = FlashPartition::Address;
  using Entry = internal::Entry;
  using KeyDescriptor = internal::KeyDescriptor;
  using SectorDescriptor = internal::SectorDescriptor;

  // In the future, will be able to provide additional EntryFormats for
  // backwards compatibility.
  KeyValueStore(FlashPartition* partition,
                span<const EntryFormat> formats,
                const Options& options,
                size_t redundancy,
                Vector<SectorDescriptor>& sector_descriptor_list,
                const SectorDescriptor** temp_sectors_to_skip,
                Vector<KeyDescriptor>& key_descriptor_list,
                Address* addresses);

 private:
  using EntryMetadata = internal::EntryMetadata;
  using EntryState = internal::EntryState;

  template <typename T>
  static constexpr void CheckThatObjectCanBePutOrGet() {
    static_assert(
        std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>,
        "Only trivially copyable, non-pointer objects may be Put and Get by "
        "value. Any value may be stored by converting it to a byte span with "
        "as_bytes(span(&value, 1)) or as_writable_bytes(span(&value, 1)).");
  }

  Status LoadEntry(Address entry_address, Address* next_entry_address);
  Status ScanForEntry(const SectorDescriptor& sector,
                      Address start_address,
                      Address* next_entry_address);

  Status PutBytes(std::string_view key, span<const std::byte> value);

  StatusWithSize ValueSize(const EntryMetadata& metadata) const;

  StatusWithSize Get(std::string_view key,
                     const EntryMetadata& metadata,
                     span<std::byte> value_buffer,
                     size_t offset_bytes) const;

  Status FixedSizeGet(std::string_view key,
                      void* value,
                      size_t size_bytes) const;

  Status FixedSizeGet(std::string_view key,
                      const EntryMetadata& descriptor,
                      void* value,
                      size_t size_bytes) const;

  Status CheckOperation(std::string_view key) const;

  Status WriteEntryForExistingKey(EntryMetadata& metadata,
                                  EntryState new_state,
                                  std::string_view key,
                                  span<const std::byte> value);

  Status WriteEntryForNewKey(std::string_view key, span<const std::byte> value);

  Status WriteEntry(std::string_view key,
                    span<const std::byte> value,
                    EntryState new_state,
                    EntryMetadata* prior_metadata = nullptr,
                    size_t prior_size = 0);

  EntryMetadata UpdateKeyDescriptor(const Entry& new_entry,
                                    std::string_view key,
                                    EntryMetadata* prior_metadata,
                                    size_t prior_size);

  Status GetSectorForWrite(SectorDescriptor** sector,
                           size_t entry_size,
                           span<const Address> addresses_to_skip);

  Status AppendEntry(const Entry& entry,
                     std::string_view key,
                     span<const std::byte> value);

  Status RelocateEntry(const EntryMetadata& metadata,
                       KeyValueStore::Address& address,
                       span<const Address> addresses_to_skip);

  enum FindSectorMode { kAppendEntry, kGarbageCollect };

  Status FindSectorWithSpace(SectorDescriptor** found_sector,
                             size_t size,
                             FindSectorMode find_mode,
                             span<const Address> addresses_to_skip,
                             span<const Address> reserved_addresses);

  SectorDescriptor* FindSectorToGarbageCollect(
      span<const Address> addresses_to_avoid);

  Status GarbageCollectPartial(span<const Address> addresses_to_skip);

  Status RelocateKeyAddressesInSector(SectorDescriptor& sector_to_gc,
                                      const EntryMetadata& descriptor,
                                      span<const Address> addresses_to_skip);

  Status GarbageCollectSector(SectorDescriptor* sector_to_gc,
                              span<const Address> addresses_to_skip);

  Status Repair() { return Status::UNIMPLEMENTED; }

  bool AddressInSector(const SectorDescriptor& sector, Address address) const {
    const Address sector_base = SectorBaseAddress(&sector);
    const Address sector_end = sector_base + partition_.sector_size_bytes();

    return ((address >= sector_base) && (address < sector_end));
  }

  unsigned SectorIndex(const SectorDescriptor* sector) const {
    return sector - sectors_.begin();
  }

  Address SectorBaseAddress(const SectorDescriptor* sector) const {
    return SectorIndex(sector) * partition_.sector_size_bytes();
  }

  SectorDescriptor* SectorFromAddress(Address address) {
    const size_t index = address / partition_.sector_size_bytes();
    // TODO: Add boundary checking once asserts are supported.
    // DCHECK_LT(index, sector_map_size_);`
    return &sectors_[index];
  }

  const SectorDescriptor* SectorFromAddress(Address address) const {
    return &sectors_[address / partition_.sector_size_bytes()];
  }

  Address NextWritableAddress(const SectorDescriptor* sector) const {
    return SectorBaseAddress(sector) + partition_.sector_size_bytes() -
           sector->writable_bytes();
  }

  internal::Entry CreateEntry(Address address,
                              std::string_view key,
                              span<const std::byte> value,
                              EntryState state);

  void LogSectors() const;
  void LogKeyDescriptor() const;

  FlashPartition& partition_;
  const internal::EntryFormats formats_;

  // Unordered list of KeyDescriptors. Finding a key requires scanning and
  // verifying a match by reading the actual entry.
  internal::EntryCache entry_cache_;

  // List of sectors used by this KVS.
  Vector<SectorDescriptor>& sectors_;

  // Temp buffer with redundancy() * 2 - 1 entries for use in RelocateEntry.
  // Used in FindSectorWithSpace and FindSectorToGarbageCollect.
  const SectorDescriptor** const temp_sectors_to_skip_;

  Options options_;

  bool initialized_;

  bool error_detected_;

  // The last sector that was selected as the "new empty sector" to write to.
  // This last new sector is used as the starting point for the next "find a new
  // empty sector to write to" operation. By using the last new sector as the
  // start point we will cycle which empty sector is selected next, spreading
  // the wear across all the empty sectors, rather than putting more wear on the
  // lower number sectors.
  //
  // Use SectorDescriptor* for the persistent storage rather than sector index
  // because SectorDescriptor* is the standard way to identify a sector.
  SectorDescriptor* last_new_sector_;
  uint32_t last_transaction_id_;
};

template <size_t kMaxEntries,
          size_t kMaxUsableSectors,
          size_t kRedundancy = 1,
          size_t kEntryFormats = 1>
class KeyValueStoreBuffer : public KeyValueStore {
 public:
  // Constructs a KeyValueStore on the partition, with support for one
  // EntryFormat (kEntryFormats must be 1).
  KeyValueStoreBuffer(FlashPartition* partition,
                      const EntryFormat& format,
                      const Options& options = {})
      : KeyValueStoreBuffer(
            partition,
            span(reinterpret_cast<const EntryFormat (&)[1]>(format)),
            options) {
    static_assert(kEntryFormats == 1,
                  "kEntryFormats EntryFormats must be specified");
  }

  // Constructs a KeyValueStore on the partition. Supports multiple entry
  // formats. The first EntryFormat is used for new entries.
  KeyValueStoreBuffer(FlashPartition* partition,
                      span<const EntryFormat, kEntryFormats> formats,
                      const Options& options = {})
      : KeyValueStore(partition,
                      formats_,
                      options,
                      kRedundancy,
                      sectors_,
                      temp_sectors_to_skip_,
                      key_descriptors_,
                      addresses_) {
    std::copy(formats.begin(), formats.end(), formats_.begin());
  }

 private:
  static_assert(kMaxEntries > 0u);
  static_assert(kMaxUsableSectors > 0u);
  static_assert(kRedundancy > 0u);
  static_assert(kEntryFormats > 0u);

  Vector<SectorDescriptor, kMaxUsableSectors> sectors_;

  // Allocate space for an list of SectorDescriptors to avoid while searching
  // for space to write entries. This is used to avoid changing sectors that are
  // reserved for a new entry or marked as having other redundant copies of an
  // entry. Up to kRedundancy sectors are reserved for a new entry, and up to
  // kRedundancy - 1 sectors can have redundant copies of an entry, giving a
  // maximum of 2 * kRedundancy - 1 sectors to avoid.
  const SectorDescriptor* temp_sectors_to_skip_[2 * kRedundancy - 1];

  // KeyDescriptors for use by the KVS's EntryCache.
  Vector<KeyDescriptor, kMaxEntries> key_descriptors_;

  // An array of addresses associated with the KeyDescriptors for use with the
  // EntryCache. To support having KeyValueStores with different redundancies,
  // the addresses are stored in a parallel array instead of inside
  // KeyDescriptors.
  internal::EntryCache::AddressList<kRedundancy, kMaxEntries> addresses_;

  // EntryFormats that can be read by this KeyValueStore.
  std::array<EntryFormat, kEntryFormats> formats_;
};

}  // namespace pw::kvs
