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
#include <algorithm>
#include <sstream>
#include "query/vector/ob_vector_index_serialize.h"

namespace oceanbase
{
namespace share
{
namespace
{
using namespace common;

enum class ChunkFault { NONE, NULL_DATA, ZERO_SIZE, NEGATIVE_SIZE };

struct ChunkState : ObIStreamBuf::CbParam
{
  std::string bytes_ = "abcdefgh";
  int64_t position_ = 0;
  int64_t chunk_size_ = 3;
  int calls_ = 0;
  int fault_call_ = 1;
  ChunkFault fault_ = ChunkFault::NONE;
  int callback_error_ = OB_SUCCESS;
};

int load_chunk(char *&data, const int64_t, int64_t &size, ObIStreamBuf::CbParam &param)
{
  auto &state = static_cast<ChunkState &>(param);
  ++state.calls_;
  if (state.callback_error_ != OB_SUCCESS) {
    return state.callback_error_;
  } else if (state.fault_ != ChunkFault::NONE && state.calls_ >= state.fault_call_) {
    // Bound the unfixed zero-progress loop so regression tests cannot hang.
    if (state.calls_ >= state.fault_call_ + 8) {
      return OB_TIMEOUT;
    }
    data = state.fault_ == ChunkFault::NULL_DATA ? nullptr : state.bytes_.data();
    size = state.fault_ == ChunkFault::ZERO_SIZE ? 0 :
           (state.fault_ == ChunkFault::NEGATIVE_SIZE ? -1 : 3);
  } else if (state.position_ == state.bytes_.size()) {
    return OB_ITER_END;
  } else {
    size = std::min(state.chunk_size_, static_cast<int64_t>(state.bytes_.size()) - state.position_);
    data = state.bytes_.data() + state.position_;
    state.position_ += size;
  }
  return OB_SUCCESS;
}

struct Input
{
  Input() : callback_(load_chunk), buffer_(nullptr, 0, state_, callback_), stream_(&buffer_) {}
  ChunkState state_;
  ObIStreamBuf::Callback callback_;
  ObIStreamBuf buffer_;
  std::istream stream_;
};

TEST(VectorIndexInputChunks, rejects_null_chunk_at_initialization)
{
  Input input;
  input.state_.fault_ = ChunkFault::NULL_DATA;
  ASSERT_EQ(OB_INVALID_DATA, input.buffer_.init());
  EXPECT_EQ(OB_INVALID_DATA, input.buffer_.get_error_code());
  EXPECT_EQ(1, input.state_.calls_);
}

TEST(VectorIndexInputChunks, rejects_zero_length_chunk_at_initialization)
{
  Input input;
  input.state_.fault_ = ChunkFault::ZERO_SIZE;
  ASSERT_EQ(OB_INVALID_DATA, input.buffer_.init());
  EXPECT_EQ(OB_INVALID_DATA, input.buffer_.get_error_code());
  EXPECT_EQ(1, input.state_.calls_);
}

TEST(VectorIndexInputChunks, rejects_negative_length_before_reading)
{
  Input input;
  input.state_.fault_ = ChunkFault::NEGATIVE_SIZE;
  // ASSERT prevents the unfixed implementation from copying a negative length.
  ASSERT_EQ(OB_INVALID_DATA, input.buffer_.init());
  char byte = 'x';
  EXPECT_EQ(0, input.buffer_.sgetn(&byte, 1));
  EXPECT_EQ('x', byte);
  EXPECT_EQ(OB_INVALID_DATA, input.buffer_.get_error_code());
  EXPECT_EQ(1, input.state_.calls_);
}

TEST(VectorIndexInputChunks, lazy_bulk_read_rejects_zero_progress)
{
  Input input;
  input.state_.fault_ = ChunkFault::ZERO_SIZE;
  char byte = 'x';
  input.stream_.read(&byte, 1);
  EXPECT_EQ(0, input.stream_.gcount());
  EXPECT_TRUE(input.stream_.fail());
  EXPECT_EQ('x', byte);
  EXPECT_EQ(OB_INVALID_DATA, input.buffer_.get_error_code());
  EXPECT_EQ(1, input.state_.calls_);
}

TEST(VectorIndexInputChunks, lazy_bulk_read_rejects_null_data)
{
  Input input;
  input.state_.fault_ = ChunkFault::NULL_DATA;
  char byte = 'x';
  input.stream_.read(&byte, 1);
  EXPECT_TRUE(input.stream_.fail());
  EXPECT_EQ('x', byte);
  EXPECT_EQ(OB_INVALID_DATA, input.buffer_.get_error_code());
  EXPECT_EQ(1, input.state_.calls_);
}

TEST(VectorIndexInputChunks, character_read_distinguishes_bad_chunk_from_eof)
{
  for (const auto fault : {ChunkFault::NULL_DATA, ChunkFault::ZERO_SIZE, ChunkFault::NEGATIVE_SIZE}) {
    Input input;
    input.state_.fault_ = fault;
    EXPECT_EQ(std::char_traits<char>::eof(), input.stream_.get());
    EXPECT_EQ(OB_INVALID_DATA, input.buffer_.get_error_code());
    EXPECT_EQ(1, input.state_.calls_);
  }
}

TEST(VectorIndexInputChunks, bulk_read_preserves_only_bytes_before_bad_chunk)
{
  for (const auto fault : {ChunkFault::NULL_DATA, ChunkFault::ZERO_SIZE}) {
    Input input;
    input.state_.fault_ = fault;
    input.state_.fault_call_ = 2;
    char output[] = "xxxxxxxx";
    input.stream_.read(output, 8);
    EXPECT_EQ(3, input.stream_.gcount());
    EXPECT_EQ("abcxxxxx", std::string(output, 8));
    EXPECT_EQ(OB_INVALID_DATA, input.buffer_.get_error_code());
    EXPECT_EQ(2, input.state_.calls_);
  }
}

TEST(VectorIndexInputChunks, character_read_rejects_empty_chunk_after_valid_data)
{
  Input input;
  input.state_.fault_ = ChunkFault::ZERO_SIZE;
  input.state_.fault_call_ = 2;
  EXPECT_EQ('a', input.stream_.get());
  EXPECT_EQ('b', input.stream_.get());
  EXPECT_EQ('c', input.stream_.get());
  EXPECT_EQ(std::char_traits<char>::eof(), input.stream_.get());
  EXPECT_EQ(OB_INVALID_DATA, input.buffer_.get_error_code());
  EXPECT_EQ(2, input.state_.calls_);
}

TEST(VectorIndexInputChunks, clearing_stream_flags_does_not_retry_invalid_data)
{
  Input input;
  input.state_.fault_ = ChunkFault::ZERO_SIZE;
  ASSERT_EQ(OB_INVALID_DATA, input.buffer_.init());
  input.state_.fault_ = ChunkFault::NONE;
  input.stream_.clear();
  EXPECT_EQ(std::char_traits<char>::eof(), input.stream_.get());
  EXPECT_EQ(OB_INVALID_DATA, input.buffer_.get_error_code());
  EXPECT_EQ(1, input.state_.calls_);
}

TEST(VectorIndexInputChunks, preserves_callback_error_codes)
{
  Input input;
  input.state_.callback_error_ = OB_TIMEOUT;
  EXPECT_EQ(OB_TIMEOUT, input.buffer_.init());
  EXPECT_EQ(OB_TIMEOUT, input.buffer_.get_error_code());
  EXPECT_EQ(std::char_traits<char>::eof(), input.stream_.get());
  EXPECT_EQ(1, input.state_.calls_);
}

TEST(VectorIndexInputChunks, preserves_explicit_end_of_input)
{
  Input input;
  input.state_.bytes_.clear();
  EXPECT_EQ(OB_ITER_END, input.buffer_.init());
  EXPECT_EQ(OB_ITER_END, input.buffer_.get_error_code());
  EXPECT_EQ(1, input.state_.calls_);
}

TEST(VectorIndexInputChunks, zero_byte_read_does_not_call_loader)
{
  Input input;
  char byte = 'x';
  EXPECT_EQ(0, input.buffer_.sgetn(&byte, 0));
  EXPECT_EQ(0, input.state_.calls_);
  EXPECT_EQ(OB_SUCCESS, input.buffer_.get_error_code());
}

TEST(VectorIndexInputChunks, normal_bulk_read_crosses_variable_sized_chunks)
{
  for (const int size : {1, 3, 8, 32}) {
    Input input;
    input.state_.chunk_size_ = size;
    char output[9] = {};
    input.stream_.read(output, 9);
    EXPECT_EQ(8, input.stream_.gcount());
    EXPECT_EQ(input.state_.bytes_, std::string(output, 8));
    EXPECT_EQ(OB_ITER_END, input.buffer_.get_error_code());
  }
}

TEST(VectorIndexInputChunks, normal_character_read_crosses_chunks)
{
  Input input;
  std::string output;
  for (int ch = input.stream_.get(); ch != std::char_traits<char>::eof(); ch = input.stream_.get()) {
    output.push_back(static_cast<char>(ch));
  }
  EXPECT_EQ(input.state_.bytes_, output);
  EXPECT_EQ(OB_ITER_END, input.buffer_.get_error_code());
}

} // namespace
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
