// Copyright 2019 The Pigweed Authors
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

#include "pw_protobuf/encoder.h"

#include "gtest/gtest.h"

namespace pw::protobuf {
namespace {

// The tests in this file use the following proto message schemas.
//
//   message TestProto {
//     uint32 magic_number = 1;
//     sint32 ziggy = 2;
//     fixed64 cycles = 3;
//     float ratio = 4;
//     string error_message = 5;
//     NestedProto nested = 6;
//   }
//
//   message NestedProto {
//     string hello = 1;
//     uint32 id = 2;
//     repeated DoubleNestedProto pair = 3;
//   }
//
//   message DoubleNestedProto {
//     string key = 1;
//     string value = 2;
//   }
//

constexpr uint32_t kTestProtoMagicNumberField = 1;
constexpr uint32_t kTestProtoZiggyField = 2;
constexpr uint32_t kTestProtoCyclesField = 3;
constexpr uint32_t kTestProtoRatioField = 4;
constexpr uint32_t kTestProtoErrorMessageField = 5;
constexpr uint32_t kTestProtoNestedField = 6;

constexpr uint32_t kNestedProtoHelloField = 1;
constexpr uint32_t kNestedProtoIdField = 2;
constexpr uint32_t kNestedProtoPairField = 3;

constexpr uint32_t kDoubleNestedProtoKeyField = 1;
constexpr uint32_t kDoubleNestedProtoValueField = 2;

TEST(Encoder, EncodePrimitives) {
  // TestProto tp;
  // tp.magic_number = 42;
  // tp.ziggy = -13;
  // tp.cycles = 0xdeadbeef8badf00d;
  // tp.ratio = 1.618034;
  // tp.error_message = "broken 💩";

  // Hand-encoded version of the above.
  // clang-format off
  constexpr uint8_t encoded_proto[] = {
    // magic_number [varint k=1]
    0x08, 0x2a,
    // ziggy [varint k=2]
    0x10, 0x19,
    // cycles [fixed64 k=3]
    0x19, 0x0d, 0xf0, 0xad, 0x8b, 0xef, 0xbe, 0xad, 0xde,
    // ratio [fixed32 k=4]
    0x25, 0xbd, 0x1b, 0xcf, 0x3f,
    // error_message [delimited k=5],
    0x2a, 0x0b, 'b', 'r', 'o', 'k', 'e', 'n', ' ',
    // poop!
    0xf0, 0x9f, 0x92, 0xa9,
  };
  // clang-format on

  std::byte encode_buffer[32];
  NestedEncoder encoder(encode_buffer);

  EXPECT_EQ(encoder.WriteUint32(kTestProtoMagicNumberField, 42), Status::OK);
  EXPECT_EQ(encoder.WriteSint32(kTestProtoZiggyField, -13), Status::OK);
  EXPECT_EQ(encoder.WriteFixed64(kTestProtoCyclesField, 0xdeadbeef8badf00d),
            Status::OK);
  EXPECT_EQ(encoder.WriteFloat(kTestProtoRatioField, 1.618034), Status::OK);
  EXPECT_EQ(encoder.WriteString(kTestProtoErrorMessageField, "broken 💩"),
            Status::OK);

  span<const std::byte> encoded;
  EXPECT_EQ(encoder.Encode(&encoded), Status::OK);
  EXPECT_EQ(encoded.size(), sizeof(encoded_proto));
  EXPECT_EQ(std::memcmp(encoded.data(), encoded_proto, encoded.size()), 0);
}

TEST(Encoder, EncodeInsufficientSpace) {
  std::byte encode_buffer[12];
  NestedEncoder encoder(encode_buffer);

  // 2 bytes.
  EXPECT_EQ(encoder.WriteUint32(kTestProtoMagicNumberField, 42), Status::OK);
  // 2 bytes.
  EXPECT_EQ(encoder.WriteSint32(kTestProtoZiggyField, -13), Status::OK);
  // 9 bytes; not enough space! The encoder will start writing the field but
  // should rollback when it realizes it doesn't have enough space.
  EXPECT_EQ(encoder.WriteFixed64(kTestProtoCyclesField, 0xdeadbeef8badf00d),
            Status::RESOURCE_EXHAUSTED);
  // Any further write operations should fail.
  EXPECT_EQ(encoder.WriteFloat(kTestProtoRatioField, 1.618034),
            Status::RESOURCE_EXHAUSTED);

  span<const std::byte> encoded;
  EXPECT_EQ(encoder.Encode(&encoded), Status::RESOURCE_EXHAUSTED);
  EXPECT_EQ(encoded.size(), 0u);
}

TEST(Encoder, EncodeInvalidArguments) {
  std::byte encode_buffer[12];
  NestedEncoder encoder(encode_buffer);

  EXPECT_EQ(encoder.WriteUint32(kTestProtoMagicNumberField, 42), Status::OK);
  // Invalid proto field numbers.
  EXPECT_EQ(encoder.WriteUint32(0, 1337), Status::INVALID_ARGUMENT);
  encoder.Clear();

  EXPECT_EQ(encoder.WriteString(1u << 31, "ha"), Status::INVALID_ARGUMENT);
  encoder.Clear();

  EXPECT_EQ(encoder.WriteBool(19091, false), Status::INVALID_ARGUMENT);
  span<const std::byte> encoded;
  EXPECT_EQ(encoder.Encode(&encoded), Status::INVALID_ARGUMENT);
  EXPECT_EQ(encoded.size(), 0u);
}

TEST(Encoder, Nested) {
  std::byte encode_buffer[128];
  NestedEncoder<5, 10> encoder(encode_buffer);

  // TestProto test_proto;
  // test_proto.magic_number = 42;
  EXPECT_EQ(encoder.WriteUint32(kTestProtoMagicNumberField, 42), Status::OK);

  {
    // NestedProto& nested_proto = test_proto.nested;
    EXPECT_EQ(encoder.Push(kTestProtoNestedField), Status::OK);
    // nested_proto.hello = "world";
    EXPECT_EQ(encoder.WriteString(kNestedProtoHelloField, "world"), Status::OK);
    // nested_proto.id = 999;
    EXPECT_EQ(encoder.WriteUint32(kNestedProtoIdField, 999), Status::OK);

    {
      // DoubleNestedProto& double_nested_proto = nested_proto.append_pair();
      EXPECT_EQ(encoder.Push(kNestedProtoPairField), Status::OK);
      // double_nested_proto.key = "version";
      EXPECT_EQ(encoder.WriteString(kDoubleNestedProtoKeyField, "version"),
                Status::OK);
      // double_nested_proto.value = "2.9.1";
      EXPECT_EQ(encoder.WriteString(kDoubleNestedProtoValueField, "2.9.1"),
                Status::OK);

      EXPECT_EQ(encoder.Pop(), Status::OK);
    }  // end DoubleNestedProto

    {
      // DoubleNestedProto& double_nested_proto = nested_proto.append_pair();
      EXPECT_EQ(encoder.Push(kNestedProtoPairField), Status::OK);
      // double_nested_proto.key = "device";
      EXPECT_EQ(encoder.WriteString(kDoubleNestedProtoKeyField, "device"),
                Status::OK);
      // double_nested_proto.value = "left-soc";
      EXPECT_EQ(encoder.WriteString(kDoubleNestedProtoValueField, "left-soc"),
                Status::OK);

      EXPECT_EQ(encoder.Pop(), Status::OK);
    }  // end DoubleNestedProto

    EXPECT_EQ(encoder.Pop(), Status::OK);
  }  // end NestedProto

  // test_proto.ziggy = -13;
  EXPECT_EQ(encoder.WriteSint32(kTestProtoZiggyField, -13), Status::OK);

  // clang-format off
  constexpr uint8_t encoded_proto[] = {
    // magic_number
    0x08, 0x2a,
    // nested header (key, size)
    0x32, 0x30,
    // nested.hello
    0x0a, 0x05, 'w', 'o', 'r', 'l', 'd',
    // nested.id
    0x10, 0xe7, 0x07,
    // nested.pair[0] header (key, size)
    0x1a, 0x10,
    // nested.pair[0].key
    0x0a, 0x07, 'v', 'e', 'r', 's', 'i', 'o', 'n',
    // nested.pair[0].value
    0x12, 0x05, '2', '.', '9', '.', '1',
    // nested.pair[1] header (key, size)
    0x1a, 0x12,
    // nested.pair[1].key
    0x0a, 0x06, 'd', 'e', 'v', 'i', 'c', 'e',
    // nested.pair[1].value
    0x12, 0x08, 'l', 'e', 'f', 't', '-', 's', 'o', 'c',
    // ziggy
    0x10, 0x19
  };
  // clang-format on

  span<const std::byte> encoded;
  EXPECT_EQ(encoder.Encode(&encoded), Status::OK);
  EXPECT_EQ(encoded.size(), sizeof(encoded_proto));
  EXPECT_EQ(std::memcmp(encoded.data(), encoded_proto, encoded.size()), 0);
}

TEST(Encoder, NestedDepthLimit) {
  std::byte encode_buffer[128];
  NestedEncoder<2, 10> encoder(encode_buffer);

  // One level of nesting.
  EXPECT_EQ(encoder.Push(2), Status::OK);
  // Two levels of nesting.
  EXPECT_EQ(encoder.Push(1), Status::OK);
  // Three levels of nesting: error!
  EXPECT_EQ(encoder.Push(1), Status::RESOURCE_EXHAUSTED);

  // Further operations should fail.
  EXPECT_EQ(encoder.Pop(), Status::RESOURCE_EXHAUSTED);
  EXPECT_EQ(encoder.Pop(), Status::RESOURCE_EXHAUSTED);
  EXPECT_EQ(encoder.Pop(), Status::RESOURCE_EXHAUSTED);
}

TEST(Encoder, NestedBlobLimit) {
  std::byte encode_buffer[128];
  NestedEncoder<5, 3> encoder(encode_buffer);

  // Write first blob.
  EXPECT_EQ(encoder.Push(1), Status::OK);
  EXPECT_EQ(encoder.Pop(), Status::OK);

  // Write second blob.
  EXPECT_EQ(encoder.Push(2), Status::OK);

  // Write nested third blob.
  EXPECT_EQ(encoder.Push(3), Status::OK);
  EXPECT_EQ(encoder.Pop(), Status::OK);

  // End second blob.
  EXPECT_EQ(encoder.Pop(), Status::OK);

  // Write fourth blob: error!.
  EXPECT_EQ(encoder.Push(4), Status::RESOURCE_EXHAUSTED);
  // Nothing to pop.
  EXPECT_EQ(encoder.Pop(), Status::RESOURCE_EXHAUSTED);
}

TEST(Encoder, RepeatedField) {
  std::byte encode_buffer[32];
  NestedEncoder encoder(encode_buffer);

  // repeated uint32 values = 1;
  constexpr uint32_t values[] = {0, 50, 100, 150, 200};
  for (int i = 0; i < 5; ++i) {
    encoder.WriteUint32(1, values[i]);
  }

  constexpr uint8_t encoded_proto[] = {
      0x08, 0x00, 0x08, 0x32, 0x08, 0x64, 0x08, 0x96, 0x01, 0x08, 0xc8, 0x01};

  span<const std::byte> encoded;
  EXPECT_EQ(encoder.Encode(&encoded), Status::OK);
  EXPECT_EQ(encoded.size(), sizeof(encoded_proto));
  EXPECT_EQ(std::memcmp(encoded.data(), encoded_proto, encoded.size()), 0);
}

TEST(Encoder, PackedVarint) {
  std::byte encode_buffer[32];
  NestedEncoder encoder(encode_buffer);

  // repeated uint32 values = 1;
  constexpr uint32_t values[] = {0, 50, 100, 150, 200};
  encoder.WritePackedUint32(1, values);

  constexpr uint8_t encoded_proto[] = {
      0x0a, 0x07, 0x00, 0x32, 0x64, 0x96, 0x01, 0xc8, 0x01};
  //  key   size  v[0]  v[1]  v[2]  v[3]        v[4]

  span<const std::byte> encoded;
  EXPECT_EQ(encoder.Encode(&encoded), Status::OK);
  EXPECT_EQ(encoded.size(), sizeof(encoded_proto));
  EXPECT_EQ(std::memcmp(encoded.data(), encoded_proto, encoded.size()), 0);
}

TEST(Encoder, PackedVarintInsufficientSpace) {
  std::byte encode_buffer[8];
  NestedEncoder encoder(encode_buffer);

  constexpr uint32_t values[] = {0, 50, 100, 150, 200};
  encoder.WritePackedUint32(1, values);

  span<const std::byte> encoded;
  EXPECT_EQ(encoder.Encode(&encoded), Status::RESOURCE_EXHAUSTED);
  EXPECT_EQ(encoded.size(), 0u);
}

TEST(Encoder, PackedFixed) {
  std::byte encode_buffer[32];
  NestedEncoder encoder(encode_buffer);

  // repeated fixed32 values = 1;
  constexpr uint32_t values[] = {0, 50, 100, 150, 200};
  encoder.WritePackedFixed32(1, values);

  constexpr uint8_t encoded_proto[] = {
      0x0a, 0x14, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x64,
      0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00};

  span<const std::byte> encoded;
  EXPECT_EQ(encoder.Encode(&encoded), Status::OK);
  EXPECT_EQ(encoded.size(), sizeof(encoded_proto));
  EXPECT_EQ(std::memcmp(encoded.data(), encoded_proto, encoded.size()), 0);
}

TEST(Encoder, PackedZigzag) {
  std::byte encode_buffer[32];
  NestedEncoder encoder(encode_buffer);

  // repeated sint32 values = 1;
  constexpr int32_t values[] = {-100, -25, -1, 0, 1, 25, 100};
  encoder.WritePackedSint32(1, values);

  constexpr uint8_t encoded_proto[] = {
      0x0a, 0x09, 0xc7, 0x01, 0x31, 0x01, 0x00, 0x02, 0x32, 0xc8, 0x01};

  span<const std::byte> encoded;
  EXPECT_EQ(encoder.Encode(&encoded), Status::OK);
  EXPECT_EQ(encoded.size(), sizeof(encoded_proto));
  EXPECT_EQ(std::memcmp(encoded.data(), encoded_proto, encoded.size()), 0);
}

}  // namespace
}  // namespace pw::protobuf
