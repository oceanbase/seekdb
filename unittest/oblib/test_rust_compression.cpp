/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <zlib.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include "lib/compress/zlib/ob_zlib_compressor.h"
#include "rust/compression.h"

namespace oceanbase::compression
{
namespace
{

rust::Slice<const uint8_t> input_view(const std::vector<uint8_t> &input)
{
  return {input.data(), input.size()};
}

rust::Slice<uint8_t> output_view(std::vector<uint8_t> &output)
{
  return {output.data(), output.size()};
}

std::vector<uint8_t> make_input(size_t size)
{
  std::vector<uint8_t> input(size);
  for (size_t index = 0; index < size; ++index) {
    input[index] = static_cast<uint8_t>((index * 37 + index / 11) % 251);
  }
  return input;
}

std::vector<uint8_t> abi_compress(const std::vector<uint8_t> &input, int level, bool gzip)
{
  std::vector<uint8_t> output(compressBound(input.size()) + 32);
  if (!gzip) {
    uLongf output_size = output.size();
    EXPECT_EQ(Z_OK, compress2(output.data(), &output_size, input.data(), input.size(), level));
    output.resize(output_size);
    return output;
  }

  z_stream stream = {};
  EXPECT_EQ(Z_OK, deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY));
  stream.next_in = const_cast<Bytef *>(input.data());
  stream.avail_in = input.size();
  stream.next_out = output.data();
  stream.avail_out = output.size();
  EXPECT_EQ(Z_STREAM_END, deflate(&stream, Z_FINISH));
  output.resize(stream.total_out);
  EXPECT_EQ(Z_OK, deflateEnd(&stream));
  return output;
}

void expect_abi_decompress(const std::vector<uint8_t> &compressed,
                           const std::vector<uint8_t> &expected,
                           bool gzip = false)
{
  std::vector<uint8_t> output(expected.size());
  if (!gzip) {
    uLongf output_size = output.size();
    ASSERT_EQ(Z_OK, uncompress(output.data(), &output_size, compressed.data(), compressed.size()));
    ASSERT_EQ(expected.size(), output_size);
  } else {
    z_stream stream = {};
    ASSERT_EQ(Z_OK, inflateInit2(&stream, 15 + 16));
    stream.next_in = const_cast<Bytef *>(compressed.data());
    stream.avail_in = compressed.size();
    stream.next_out = output.data();
    stream.avail_out = output.size();
    ASSERT_EQ(Z_STREAM_END, inflate(&stream, Z_NO_FLUSH));
    ASSERT_EQ(expected.size(), stream.total_out);
    ASSERT_EQ(Z_OK, inflateEnd(&stream));
  }
  ASSERT_EQ(expected, output);
}

TEST(RustCompression, CxxAndCAbiZlibInteroperateInBothDirections)
{
  for (size_t size : {size_t(1), size_t(8), size_t(4096), size_t(65536), size_t(100000)}) {
    const auto input = make_input(size);
    for (int level : {0, 1, 6, 9}) {
      const auto native = abi_compress(input, level, false);
      std::vector<uint8_t> decoded(input.size());
      auto result = zlib_decompress(input_view(native), output_view(decoded));
      ASSERT_EQ(Code::Ok, result.code) << size << " " << level;
      ASSERT_EQ(input.size(), result.size);
      ASSERT_EQ(input, decoded);

      std::vector<uint8_t> encoded(zlib_compress_bound(input.size()));
      result = zlib_compress(input_view(input), output_view(encoded), level);
      ASSERT_EQ(Code::Ok, result.code) << size << " " << level;
      encoded.resize(result.size);
      expect_abi_decompress(encoded, input);
    }
  }
}

TEST(RustCompression, NativeCompressorBoundCoversIncompressibleDataAtEveryLevel)
{
  for (size_t size : {size_t(4096), size_t(65536)}) {
    std::vector<uint8_t> input(size);
    uint64_t state = 0x12345678;
    for (auto &byte : input) {
      state ^= state << 13;
      state ^= state >> 7;
      state ^= state << 17;
      byte = static_cast<uint8_t>(state);
    }
    for (int level = -1; level <= 9; ++level) {
      common::ObZlibCompressor compressor(level);
      int64_t overflow = 0;
      ASSERT_EQ(0, compressor.get_max_overflow_size(input.size(), overflow));
      ASSERT_GE(overflow, 0);
      std::vector<char> encoded(input.size() + overflow);
      int64_t encoded_size = 0;
      ASSERT_EQ(0, compressor.compress(reinterpret_cast<const char *>(input.data()), input.size(),
                                       encoded.data(), encoded.size(), encoded_size)) << size << " " << level;
      std::vector<uint8_t> decoded(input.size());
      int64_t decoded_size = 0;
      ASSERT_EQ(0, compressor.decompress(encoded.data(), encoded_size,
                                         reinterpret_cast<char *>(decoded.data()), decoded.size(), decoded_size));
      ASSERT_EQ(input.size(), decoded_size);
      ASSERT_EQ(input, decoded);
    }
  }
}

TEST(RustCompression, ErrorAndTrailingByteSemantics)
{
  const auto input = make_input(8192);
  auto native = abi_compress(input, 6, false);
  native.insert(native.end(), {0, 1, 2, 3});
  std::vector<uint8_t> decoded(input.size());
  auto result = zlib_decompress(input_view(native), output_view(decoded));
  ASSERT_EQ(Code::Ok, result.code);
  ASSERT_EQ(input, decoded);

  const auto truncated = std::vector<uint8_t>(native.begin(), native.begin() + 9);
  result = zlib_decompress(input_view(truncated), output_view(decoded));
  ASSERT_EQ(Code::InvalidData, result.code);

  std::vector<uint8_t> small(7);
  result = zlib_decompress(input_view(native), output_view(small));
  ASSERT_EQ(Code::OutputTooSmall, result.code);
}

TEST(RustCompression, Crc32MatchesCAbi)
{
  for (size_t size : {size_t(0), size_t(1), size_t(63), size_t(64), size_t(4097)}) {
    const auto input = make_input(size);
    ASSERT_EQ(::crc32(0, input.data(), input.size()), crc32(input_view(input)));
  }
}

TEST(RustCompression, CxxAndCAbiGzipInteroperateInBothDirections)
{
  const auto input = make_input(100000);
  const auto native = abi_compress(input, 5, true);
  auto decoder = new_stream_decoder();
  std::vector<uint8_t> decoded(input.size());
  auto progress = decoder_process(*decoder, input_view(native), output_view(decoded));
  ASSERT_EQ(Code::Ok, progress.code);
  ASSERT_TRUE(progress.stream_end);
  ASSERT_EQ(native.size(), progress.consumed);
  ASSERT_EQ(input.size(), progress.produced);
  ASSERT_EQ(input, decoded);

  auto encoder = new_gzip_encoder();
  std::vector<uint8_t> encoded;
  size_t consumed = 0;
  for (;;) {
    uint8_t buffer[37];
    rust::Slice<const uint8_t> remaining(input.data() + consumed, input.size() - consumed);
    rust::Slice<uint8_t> output(buffer, sizeof(buffer));
    progress = gzip_process(*encoder, remaining, output, true);
    ASSERT_EQ(Code::Ok, progress.code);
    consumed += progress.consumed;
    encoded.insert(encoded.end(), buffer, buffer + progress.produced);
    if (progress.stream_end) {
      break;
    }
    ASSERT_LT(0, progress.consumed + progress.produced);
  }
  ASSERT_EQ(input.size(), consumed);
  expect_abi_decompress(encoded, input, true);
}

} // namespace
} // namespace oceanbase::compression

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
