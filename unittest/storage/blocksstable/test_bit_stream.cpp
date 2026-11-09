/*
 * Copyright (c) 2025 OceanBase.
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
#include <sys/mman.h>
#include <unistd.h>
#include "storage/blocksstable/encoding/ob_bit_stream.h"

namespace oceanbase
{
namespace blocksstable
{

class GuardedBitBuffer
{
public:
  GuardedBitBuffer()
  {
    page_size_ = sysconf(_SC_PAGESIZE);
    if (page_size_ > 0) {
      mapping_ = mmap(nullptr, page_size_ * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (mapping_ != MAP_FAILED) {
        protected_ = 0 == mprotect(static_cast<char *>(mapping_) + page_size_, page_size_, PROT_NONE);
      }
    }
  }
  ~GuardedBitBuffer()
  {
    if (mapping_ != MAP_FAILED) {
      munmap(mapping_, page_size_ * 2);
    }
  }
  bool valid() const { return protected_; }
  unsigned char *data(const int64_t bytes)
  {
    return reinterpret_cast<unsigned char *>(mapping_) + page_size_ - bytes;
  }
private:
  GuardedBitBuffer(const GuardedBitBuffer &) = delete;
  GuardedBitBuffer &operator=(const GuardedBitBuffer &) = delete;
  void *mapping_ = MAP_FAILED;
  int64_t page_size_ = 0;
  bool protected_ = false;
};

// Deliberately independent of ObBitStream's masks and word-reading code.
uint64_t read_reference(const unsigned char *data, const int64_t offset, const int64_t count)
{
  uint64_t value = 0;
  for (int64_t bit = 0; bit < count; ++bit) {
    const int64_t pos = offset + bit;
    value |= static_cast<uint64_t>((data[pos / CHAR_BIT] >> (pos % CHAR_BIT)) & 1) << bit;
  }
  return value;
}

void fill_bytes(unsigned char *data, const int64_t bytes, uint64_t &state)
{
  for (int64_t i = 0; i < bytes; ++i) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    data[i] = static_cast<unsigned char>(state);
  }
}

TEST(BitStream, guarded_widths_alignments_and_tails)
{
  GuardedBitBuffer buffer;
  ASSERT_TRUE(buffer.valid());
  uint64_t state = 0x93ea73428fa2091bULL;
  const int64_t tails[] = {0, 1, 7, 8, 9, 16, 64};
  for (int64_t width = 0; width <= 64; ++width) {
    for (int64_t alignment = 0; alignment < 8; ++alignment) {
      for (int64_t prefix = 0; prefix < 8; ++prefix) {
        for (int64_t tail : tails) {
          const int64_t offset = prefix * CHAR_BIT + alignment;
          const int64_t bit_length = offset + width + tail;
          const int64_t bytes = (bit_length + CHAR_BIT - 1) / CHAR_BIT;
          unsigned char *data = buffer.data(bytes);
          fill_bytes(data, bytes, state);
          const uint64_t expected = read_reference(data, offset, width);
          // The field remains valid even when a caller reports a smaller bound.
          const int64_t bounds[] = {bit_length, bit_length - tail, width, 0, -1};
          for (int64_t bound : bounds) {
            int64_t actual = 0;
            ASSERT_EQ(common::OB_SUCCESS, ObBitStream::get<ObBitStream::DEFAULT>(
                data, offset, width, bound, actual));
            ASSERT_EQ(expected, static_cast<uint64_t>(actual))
                << "width=" << width << " offset=" << offset << " length=" << bound;
          }
        }
      }
    }
  }
}

TEST(BitStream, sparse_rows_with_conservative_dictionary_and_null_prefix_bounds)
{
  GuardedBitBuffer buffer;
  ASSERT_TRUE(buffer.valid());
  uint64_t state = 0x5dd4a109d2387f69ULL;
  constexpr int64_t ROWS = 129;
  const int64_t rows[] = {128, 0, 64, 1, 127, 2, 3, 65, 33, 128};
  for (int64_t width = 26; width <= 64; ++width) {
    for (int64_t prefix_bits : {int64_t(0), ROWS, ROWS * 2}) {
      const int64_t bits = prefix_bits + ROWS * width;
      const int64_t bytes = (bits + CHAR_BIT - 1) / CHAR_BIT;
      unsigned char *data = buffer.data(bytes);
      fill_bytes(data, bytes, state);
      for (int64_t row : rows) {
        const int64_t offset = prefix_bits + row * width;
        const uint64_t expected = read_reference(data, offset, width);
        for (int64_t bound : {bits, ROWS * width, width * 3}) {
          int64_t actual = 0;
          ASSERT_EQ(common::OB_SUCCESS, ObBitStream::get_unpack_func(width)(
              data, offset, width, bound, actual));
          ASSERT_EQ(expected, static_cast<uint64_t>(actual));
        }
      }
    }
  }
}

TEST(BitStream, full_width_and_zero_length)
{
  GuardedBitBuffer buffer;
  ASSERT_TRUE(buffer.valid());
  unsigned char *data = buffer.data(9);
  MEMSET(data, 0xff, 9);
  for (int64_t offset = 0; offset < 8; ++offset) {
    int64_t actual = 0;
    ASSERT_EQ(common::OB_SUCCESS, ObBitStream::get<ObBitStream::DEFAULT>(data, offset, 64, 72, actual));
    ASSERT_EQ(UINT64_MAX, static_cast<uint64_t>(actual));
  }
  int64_t actual = -1;
  ASSERT_EQ(common::OB_SUCCESS, ObBitStream::get<ObBitStream::DEFAULT>(buffer.data(0), 0, 0, 0, actual));
  ASSERT_EQ(0, actual);
}

} // namespace blocksstable
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
