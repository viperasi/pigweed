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

// This file defines classes for managing the in-flash format for KVS entires.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "pw_kvs/alignment.h"
#include "pw_kvs/checksum.h"
#include "pw_kvs/flash_memory.h"
#include "pw_kvs/format.h"
#include "pw_kvs/internal/key_descriptor.h"
#include "pw_span/span.h"

namespace pw::kvs::internal {

// Entry represents a key-value entry in a flash partition.
class Entry {
 public:
  static constexpr size_t kMinAlignmentBytes = sizeof(EntryHeader);
  static constexpr size_t kMaxKeyLength = 0b111111;

  using Address = FlashPartition::Address;

  // Buffer capable of holding any valid key (without a null terminator);
  using KeyBuffer = std::array<char, kMaxKeyLength>;

  // Returns flash partition Read error codes, or one of the following:
  //
  //          OK: successfully read the header and initialized the Entry
  //   NOT_FOUND: read the header, but the data appears to be erased
  //   DATA_LOSS: read the header, but it contained invalid data
  //
  static Status Read(FlashPartition& partition, Address address, Entry* entry);

  // Reads a key into a buffer, which must be at least key_length bytes.
  static Status ReadKey(FlashPartition& partition,
                        Address address,
                        size_t key_length,
                        char* key);

  // Creates a new Entry for a valid (non-deleted) entry.
  static Entry Valid(FlashPartition& partition,
                     Address address,
                     const EntryFormat& format,
                     std::string_view key,
                     span<const std::byte> value,
                     uint32_t transaction_id) {
    return Entry(
        partition, address, format, key, value, value.size(), transaction_id);
  }

  // Creates a new Entry for a tombstone entry, which marks a deleted key.
  static Entry Tombstone(FlashPartition& partition,
                         Address address,
                         const EntryFormat& format,
                         std::string_view key,
                         uint32_t transaction_id) {
    return Entry(partition,
                 address,
                 format,
                 key,
                 {},
                 kDeletedValueLength,
                 transaction_id);
  }

  Entry() = default;

  KeyDescriptor descriptor(std::string_view key) const {
    return KeyDescriptor(
        key,
        transaction_id(),
        address_,
        deleted() ? KeyDescriptor::kDeleted : KeyDescriptor::kValid);
  }

  void UpdateDescriptor(KeyDescriptor* kd) {
    kd->transaction_id_ = transaction_id();
    kd->addresses_.assign(1, address_);
    kd->state_ = deleted() ? KeyDescriptor::kDeleted : KeyDescriptor::kValid;
  }

  StatusWithSize Write(std::string_view key, span<const std::byte> value) const;

  // Reads a key into a buffer, which must be large enough for a max-length key.
  // If successful, the size is returned in the StatusWithSize. The key is not
  // null terminated.
  template <size_t kSize>
  StatusWithSize ReadKey(std::array<char, kSize>& key) const {
    static_assert(kSize >= kMaxKeyLength);
    return StatusWithSize(
        ReadKey(partition(), address_, key_length(), key.data()), key_length());
  }

  StatusWithSize ReadValue(span<std::byte> buffer,
                           size_t offset_bytes = 0) const;

  Status VerifyChecksum(ChecksumAlgorithm* algorithm,
                        std::string_view key,
                        span<const std::byte> value) const;

  Status VerifyChecksumInFlash(ChecksumAlgorithm* algorithm) const;

  // Calculates the total size of an entry, including padding.
  static size_t size(const FlashPartition& partition,
                     std::string_view key,
                     span<const std::byte> value) {
    return AlignUp(sizeof(EntryHeader) + key.size() + value.size(),
                   std::max(partition.alignment_bytes(), kMinAlignmentBytes));
  }

  // The address at which the next possible entry could be located.
  Address next_address() const { return address_ + size(); }

  // Total size of this entry, including padding.
  size_t size() const { return AlignUp(content_size(), alignment_bytes()); }

  // The length of the key in bytes. Keys are not null terminated.
  size_t key_length() const { return header_.key_length_bytes; }

  // The size of the value, without padding. The size is 0 if this is a
  // tombstone entry.
  size_t value_size() const {
    return deleted() ? 0u : header_.value_size_bytes;
  }

  uint32_t magic() const { return header_.magic; }

  uint32_t transaction_id() const { return header_.transaction_id; }

  // True if this is a tombstone entry.
  bool deleted() const {
    return header_.value_size_bytes == kDeletedValueLength;
  }

  void DebugLog();

 private:
  static constexpr uint16_t kDeletedValueLength = 0xFFFF;

  FlashPartition& partition() const { return *partition_; }

  uint32_t checksum() const { return header_.checksum; }

  size_t alignment_bytes() const { return (header_.alignment_units + 1) * 16; }

  // The total size of the entry, excluding padding.
  size_t content_size() const {
    return sizeof(EntryHeader) + key_length() + value_size();
  }

  Entry(FlashPartition& partition,
        Address address,
        const EntryFormat& format,
        std::string_view key,
        span<const std::byte> value,
        uint16_t value_size_bytes,
        uint32_t transaction_id);

  constexpr Entry(FlashPartition* partition,
                  Address address,
                  EntryHeader header)
      : partition_(partition), address_(address), header_(header) {}

  span<const std::byte> checksum_bytes() const {
    return as_bytes(span(&header_.checksum, 1));
  }

  span<const std::byte> CalculateChecksum(ChecksumAlgorithm* algorithm,
                                          std::string_view key,
                                          span<const std::byte> value) const;

  static constexpr uint8_t alignment_bytes_to_units(size_t alignment_bytes) {
    return (alignment_bytes + 15) / 16 - 1;  // An alignment of 0 is invalid.
  }

  FlashPartition* partition_;
  Address address_;
  EntryHeader header_;
};

}  // namespace pw::kvs::internal
