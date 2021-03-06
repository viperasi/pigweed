# Copyright 2020 The Pigweed Authors
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

load(
    "//pw_build:pigweed.bzl",
    "pw_cc_library",
    "pw_cc_test",
)

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # Apache License 2.0

pw_cc_library(
    name = "pw_kvs",
    srcs = [
        "alignment.cc",
        "checksum.cc",
        "entry.cc",
        "entry_cache.cc",
        "flash_memory.cc",
        "format.cc",
        "key_value_store.cc",
        "public/pw_kvs/internal/entry.h",
        "public/pw_kvs/internal/entry_cache.h",
        "public/pw_kvs/internal/hash.h",
        "public/pw_kvs/internal/key_descriptor.h",
        "public/pw_kvs/internal/sectors.h",
        "public/pw_kvs/internal/span_traits.h",
        "pw_kvs_private/macros.h",
    ],
    hdrs = [
        "public/pw_kvs/alignment.h",
        "public/pw_kvs/checksum.h",
        "public/pw_kvs/crc16_checksum.h",
        "public/pw_kvs/flash_memory.h",
        "public/pw_kvs/format.h",
        "public/pw_kvs/io.h",
        "public/pw_kvs/key_value_store.h",
    ],
    includes = ["public"],
    deps = [
        "//pw_checksum",
        "//pw_containers",
        "//pw_log",
        "//pw_status",
    ],
)

pw_cc_library(
    name = "crc16",
    hdrs = [
        "public/pw_kvs/crc16_checksum.h",
    ],
    deps = [
        ":pw_kvs",
        "//pw_checksum",
    ],
)

pw_cc_library(
    name = "test_utils",
    srcs = [
        "in_memory_fake_flash.cc",
    ],
    hdrs = [
        "public/pw_kvs/in_memory_fake_flash.h",
        "pw_kvs_private/byte_utils.h",
    ],
    includes = ["public"],
    visibility = ["//visibility:private"],
    deps = [
        "//pw_kvs",
        "//pw_log",
        "//pw_status",
    ],
)

pw_cc_test(
    name = "alignment_test",
    srcs = [
        "alignment_test.cc",
    ],
    deps = [
        ":pw_kvs",
    ],
)

pw_cc_test(
    name = "checksum_test",
    srcs = ["checksum_test.cc"],
    deps = [
        ":crc16",
        ":pw_kvs",
        "//pw_checksum",
        "//pw_log",
    ],
)

pw_cc_test(
    name = "entry_test",
    srcs = [
        "entry_test.cc",
    ],
    deps = [
        ":pw_kvs",
        ":test_utils",
    ],
)

pw_cc_test(
    name = "entry_cache_test",
    srcs = ["entry_cache_test.cc"],
    deps = [
        ":pw_kvs",
        ":test_utils",
    ],
)

pw_cc_test(
    name = "key_value_store_test",
    srcs = ["key_value_store_test.cc"],
    deps = [
        ":crc16",
        ":pw_kvs",
        ":test_utils",
        "//pw_log",
    ],
)

pw_cc_test(
    name = "key_value_store_binary_format_test",
    srcs = [
        "key_value_store_binary_format_test.cc",
    ],
    deps = [
        ":crc16",
        ":pw_kvs",
        ":test_utils",
        "//pw_log",
    ],
)

pw_cc_test(
    name = "key_value_store_fuzz_test",
    srcs = ["key_value_store_fuzz_test.cc"],
    deps = [
        ":crc16",
        ":pw_kvs",
        ":test_utils",
        "//pw_checksum",
    ],
)

pw_cc_test(
    name = "key_value_store_map_test",
    srcs = ["key_value_store_map_test.cc"],
    deps = [
        ":crc16",
        ":pw_kvs",
        ":test_utils",
        "//pw_checksum",
    ],
)

# TODO: This binary is not building due to a linker error. The error does not occur in GN Builds.
# A filegroup is used below so that the file is included in the Bazel build.
# cc_binary(
#     name = "debug_cli",
#     srcs = ["debug_cli.cc"],
#     copts = ["-std=c++17"],
#     deps = [
#         ":crc16",
#         ":pw_kvs",
#         ":test_utils",
#     ],
# )

filegroup(
    name = "debug_cli",
    srcs = ["debug_cli.cc"],
)
