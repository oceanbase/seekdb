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

#include "lib/random/ob_random.h"
#include "src/sql/engine/ob_bit_vector.h"

#define private public
#define WordType uint64_t

using namespace std;
namespace oceanbase
{
namespace sql
{
class ObTestBitVector : public ::testing::Test
{
public:
  ObTestBitVector()
  {}
  ~ObTestBitVector()
  {}
  virtual void SetUp()
  {}
  virtual void TearDown()
  {}

private:
  DISALLOW_COPY_AND_ASSIGN(ObTestBitVector);
};

void expect_range(ObBitVector *dest_bit_vector, int64_t start, int64_t middle, int64_t end)
{
  for (int64_t i = 0; i < start; i++) {
    EXPECT_EQ(0, dest_bit_vector->at(i));
  }
  for (int64_t i = start; i < middle; i++) {
    EXPECT_EQ(1, dest_bit_vector->at(i));
    dest_bit_vector->unset(i);
  }
  EXPECT_EQ(0, dest_bit_vector->at(middle));
  for (int64_t i = middle + 1; i < end; i++) {
    EXPECT_EQ(1, dest_bit_vector->at(i));
    dest_bit_vector->unset(i);
  }
  for (int64_t i = end; i < end + 100; i++) {
    EXPECT_EQ(0, dest_bit_vector->at(i));
  }
}

void test_range(ObBitVector *dest_bit_vector, ObBitVector *src_bit_vector, int64_t start,
                int64_t end)
{
  for (int i = 0; i < 2000; i++) {
    src_bit_vector->set(i);
  }

  int64_t middle = (start + end) / 2;
  dest_bit_vector->set_all(start, end);
  dest_bit_vector->unset(middle);
  expect_range(dest_bit_vector, start, middle, end);

  dest_bit_vector->set_all(start, end);
  dest_bit_vector->unset_all(start, end);
  for (int64_t i = 0; i < end + 100; i++) {
    EXPECT_EQ(0, dest_bit_vector->at(i));
  }

  src_bit_vector->unset(middle);
  dest_bit_vector->deep_copy(*src_bit_vector, start, end);
  expect_range(dest_bit_vector, start, middle, end);

  dest_bit_vector->bit_or(*src_bit_vector, start, end);
  expect_range(dest_bit_vector, start, middle, end);
  src_bit_vector->set(middle);

  for (int64_t i = start; i < end; i++) {
    dest_bit_vector->set(i);
  }
  EXPECT_EQ(1, dest_bit_vector->is_all_true(start, end));
  if (start > 0) {
    EXPECT_EQ(0, dest_bit_vector->is_all_true(start - 1, end));
  }
  EXPECT_EQ(0, dest_bit_vector->is_all_true(start, end + 1));
  for (int64_t i = start; i < end; i++) {
    dest_bit_vector->unset(i);
  }
}

TEST(ObTestBitVector, bit_or_range)
{
  char src_buf[1024];
  char dest_buf[1024];
  MEMSET(src_buf, 0, 1024);
  MEMSET(dest_buf, 0, 1024);
  ObBitVector *src_bit_vector = new (src_buf) ObBitVector;
  ObBitVector *dest_bit_vector = new (dest_buf) ObBitVector;

  test_range(dest_bit_vector, src_bit_vector, 13, 40);
  test_range(dest_bit_vector, src_bit_vector, 13, 63);
  test_range(dest_bit_vector, src_bit_vector, 13, 64);
  test_range(dest_bit_vector, src_bit_vector, 13, 127);
  test_range(dest_bit_vector, src_bit_vector, 13, 128);
  test_range(dest_bit_vector, src_bit_vector, 13, 258);
  test_range(dest_bit_vector, src_bit_vector, 0, 50);
  test_range(dest_bit_vector, src_bit_vector, 0, 100);
  test_range(dest_bit_vector, src_bit_vector, 0, 63);
  test_range(dest_bit_vector, src_bit_vector, 0, 64);
  test_range(dest_bit_vector, src_bit_vector, 0, 0);
  test_range(dest_bit_vector, src_bit_vector, 64, 64);
  test_range(dest_bit_vector, src_bit_vector, 64, 127);

}

// copy from the previos version ObBitVectorImpl, for check result
template <bool IS_FLIP, typename OP>
void copied_inner_foreach(const ObBitVectorImpl<WordType> &skip, int64_t size, OP op)
{
  int ret = OB_SUCCESS;
  int64_t tmp_step = 0;
  typedef uint16_t StepType;
  const int64_t step_size = sizeof(StepType) * CHAR_BIT;
  int64_t word_cnt = ObBitVectorImpl<WordType>::word_count(size);
  int64_t step = 0;
  const int64_t remain = size % ObBitVectorImpl<WordType>::WORD_BITS;
  for (int64_t i = 0; i < word_cnt && OB_SUCC(ret); ++i) {
    WordType s_word = (IS_FLIP ? ~skip.data_[i] : skip.data_[i]);
    // bool all_bits = (false ? skip.data_[i] == 0 : (~skip.data_[i]) == 0);
    if (i >= word_cnt - 1 && remain > 0) {
      // all_bits = ((false ? skip.data_[i] : ~skip.data_[i]) & ((1LU << remain) - 1)) == 0;
      s_word = s_word & ((1LU << remain) - 1);
    }
    if (s_word > 0) {
      WordType tmp_s_word = s_word;
      tmp_step = step;
      do {
        uint16_t step_val = tmp_s_word & 0xFFFF;
        if (0xFFFF == step_val) {
          // no skip
          // last batch ?
          int64_t mini_cnt = step_size;
          if (tmp_step + step_size > size) {
            mini_cnt = size - tmp_step;
          }
          for (int64_t j = 0; OB_SUCC(ret) && j < mini_cnt; j++) {
            int64_t k = j + tmp_step;
            ret = op(k);
          }
        } else if (step_val > 0) {
          do {
            int64_t start_bit_idx = __builtin_ctz(step_val);
            int64_t k = start_bit_idx + tmp_step;
            ret = op(k);
            step_val &= (step_val - 1);
          } while (step_val > 0 && OB_SUCC(ret)); // end for, for one step size
        }
        tmp_step += step_size;
        tmp_s_word >>= step_size;
      } while (tmp_s_word > 0 && OB_SUCC(ret)); // one word-uint64_t
    }
    step += ObBitVectorImpl<WordType>::WORD_BITS;
  } // end for
}
// This part of the code should not be deleted, used for debugging new interfaces, because ob's unit tests need to compile a lot of invalid files, and the ob_bit_vector.h header file is referenced by many places,
// Cause the compilation speed to be extremely slow, try not to modify the code directly in ob_bit_vector.h for debugging, but first correct the interface here, and then move it to ob_bit_vector.h
// perform debugging
template <bool IS_FLIP, typename OP>
void my_foreach_bound(const ObBitVectorImpl<WordType> &skip, int64_t start_idx, int64_t end_idx, OP op)
{
  int ret = OB_SUCCESS;
  int64_t tmp_step = 0;
  typedef uint16_t StepType;
  const int64_t step_size = sizeof(StepType) * CHAR_BIT;

  int64_t start_cnt = start_idx / ObBitVectorImpl<WordType>::WORD_BITS; // start_idx is included
  const int64_t begin_remain = start_idx % ObBitVectorImpl<WordType>::WORD_BITS;
  const int64_t begin_mask = (-1LU << begin_remain);

  int64_t end_cnt = ObBitVectorImpl<WordType>::word_count(end_idx);     // end_idx is not included
  const int64_t end_remain = end_idx % ObBitVectorImpl<WordType>::WORD_BITS;
  const int64_t end_mask = (1LU << end_remain) - 1;

  int64_t step = ObBitVectorImpl<WordType>::WORD_BITS * start_cnt;
  for (int64_t i = start_cnt; i < end_cnt && OB_SUCC(ret); ++i) {
    WordType s_word = (IS_FLIP ? ~skip.data_[i] : skip.data_[i]);
    if (start_cnt == end_cnt - 1) {
      // if only one word, both begin_mask and end_mask should be used
      if (begin_remain > 0) {
        s_word = s_word & begin_mask;
      }
      if (end_remain > 0) {
        s_word = s_word & end_mask;
      }
    } else if (i == start_cnt && begin_remain > 0) {
      // add begin_mask for first word, remove the bit less than start_idx
      s_word = s_word & begin_mask;
    } else if (i == end_cnt - 1 && end_remain > 0) {
      // add end_mask for last word, remove the bit greater equal than end_idx
      s_word = s_word & end_mask;
    }
    if (s_word > 0) {
      WordType tmp_s_word = s_word;
      tmp_step = step;
      do {
        uint16_t step_val = tmp_s_word & 0xFFFF;
        if (0xFFFF == step_val) {
          for (int64_t j = 0; OB_SUCC(ret) && j < step_size; j++) {
            int64_t k = j + tmp_step;
            ret = op(k);
          }
        } else if (step_val > 0) {
          do {
            int64_t start_bit_idx = __builtin_ctz(step_val);
            int64_t k = start_bit_idx + tmp_step;
            ret = op(k);
            step_val &= (step_val - 1);
          } while (step_val > 0 && OB_SUCC(ret)); // end for, for one step size
        }
        tmp_step += step_size;
        tmp_s_word >>= step_size;
      } while (tmp_s_word > 0 && OB_SUCC(ret)); // one word-uint64_t
    }
    step += ObBitVectorImpl<WordType>::ObBitVectorImpl<WordType>::WORD_BITS;
  } // end for
}

void test_foreach_result_random(int64_t batch_size, int64_t start_idx, int64_t end_idx)
{
  void *buf = malloc(batch_size);
  ObBitVector *bit_vector = to_bit_vector(buf);
  bit_vector->init(batch_size);

  int64_t true_start_idx = common::ObRandom::rand(0, batch_size);
  int64_t true_end_idx = common::ObRandom::rand(0, batch_size);
  if (true_start_idx > true_end_idx) {
    swap(true_start_idx, true_end_idx);
  }

  bit_vector->set_all(true_start_idx, true_end_idx);
  EvalBound bound(batch_size, start_idx, end_idx, false);

  // cout << "start_idx: " << start_idx << "\nend_idx: " << end_idx
  //      << "\ntrue_start_idx: " << true_start_idx << "\ntrue_end_idx: " << true_end_idx << endl;

  // test foreach
  std::vector<int> result_foreach_ori(batch_size, 0);
  std::vector<int> result_foreach_batch(batch_size, 0);
  std::vector<int> result_foreach_bound(batch_size, 0);
  copied_inner_foreach<false>(*bit_vector, end_idx, [&](int64_t idx) __attribute__((always_inline)) {
    result_foreach_ori[idx] = 1;
    return OB_SUCCESS;
  });
  ObBitVector::foreach (*bit_vector, end_idx, [&](int64_t idx) __attribute__((always_inline)) {
    result_foreach_batch[idx] = 1;
    return OB_SUCCESS;
  });
  ObBitVector::foreach (*bit_vector, bound, [&](int64_t idx) __attribute__((always_inline)) {
    result_foreach_bound[idx] = 1;
    return OB_SUCCESS;
  });

  // test flip_foreach
  std::vector<int> result_flip_foreach_ori(batch_size, 0);
  std::vector<int> result_flip_foreach_batch(batch_size, 0);
  std::vector<int> result_flip_foreach_bound(batch_size, 0);
  copied_inner_foreach<true>(*bit_vector, end_idx, [&](int64_t idx) __attribute__((always_inline)) {
    result_flip_foreach_ori[idx] = 1;
    return OB_SUCCESS;
  });
  ObBitVector::flip_foreach(*bit_vector, end_idx, [&](int64_t idx) __attribute__((always_inline)) {
    result_flip_foreach_batch[idx] = 1;
    return OB_SUCCESS;
  });

  ObBitVector::flip_foreach(*bit_vector, bound, [&](int64_t idx) __attribute__((always_inline)) {
    result_flip_foreach_bound[idx] = 1;
    return OB_SUCCESS;
  });
  // result, 0 indicates unprocessed, 1 indicates processed
  for (int64_t i = 0; i < batch_size; ++i) {
    // Fixed check new batch interface results are the same as old batch interface results
    EXPECT_EQ(result_foreach_ori[i], result_foreach_batch[i]);
    EXPECT_EQ(result_flip_foreach_ori[i], result_flip_foreach_batch[i]);
    // 1. For the part where i < start_idx, the bound interface will not handle it, only the batch interface and copied interface will handle it
    // 2. For the part where start_idx <= i < end_idx, all interfaces will be processed
    // 3. For i >= end_idx part, all interfaces will not process
    if (i < start_idx) {
      if (i < true_start_idx) {
        // This part of the bit vector is 0, therefore foreach result is 0, flip foreach result is 1
        EXPECT_EQ(0, result_foreach_batch[i]);
        EXPECT_EQ(1, result_flip_foreach_batch[i]);
      } else if (i >= true_start_idx && i < true_end_idx) {
        // This part of the bit vector is 1, therefore the foreach result is 1, flip foreach result is 0
        EXPECT_EQ(1, result_foreach_batch[i]);
        EXPECT_EQ(0, result_flip_foreach_batch[i]);
      } else if (i >= true_end_idx) {
        // This part of the bit vector is 0, therefore the foreach result is 0, flip foreach result is 1
        EXPECT_EQ(0, result_foreach_batch[i]);
        EXPECT_EQ(1, result_flip_foreach_batch[i]);
      }
      // bound interface will not process this part of the data, therefore all results are 0
      EXPECT_EQ(0, result_foreach_bound[i]);
      EXPECT_EQ(0, result_flip_foreach_bound[i]);
    } else if (i >= start_idx && i < end_idx) {
      if (i < true_start_idx) {
        // This part of the bit vector is 0, therefore foreach result is 0, flip foreach result is 1
        EXPECT_EQ(0, result_foreach_batch[i]);
        EXPECT_EQ(1, result_flip_foreach_batch[i]);
        EXPECT_EQ(0, result_foreach_bound[i]);
        EXPECT_EQ(1, result_flip_foreach_bound[i]);
      } else if (i >= true_start_idx && i < true_end_idx) {
        // This part of the bit vector is 1, therefore foreach result is 1, flip foreach result is 0
        EXPECT_EQ(1, result_foreach_batch[i]);
        EXPECT_EQ(0, result_flip_foreach_batch[i]);
        EXPECT_EQ(1, result_foreach_bound[i]);
        EXPECT_EQ(0, result_flip_foreach_bound[i]);
      } else if (i >= true_end_idx) {
        // This part of the bit vector is 0, therefore foreach result is 0, flip foreach result is 1
        EXPECT_EQ(0, result_foreach_batch[i]);
        EXPECT_EQ(1, result_flip_foreach_batch[i]);
        EXPECT_EQ(0, result_foreach_bound[i]);
        EXPECT_EQ(1, result_flip_foreach_bound[i]);
      }
    } else if (i >= end_idx) {
      // All interfaces will not process this part of the data, therefore all results are 0
      EXPECT_EQ(0, result_foreach_batch[i]);
      EXPECT_EQ(0, result_flip_foreach_batch[i]);
      EXPECT_EQ(0, result_foreach_bound[i]);
      EXPECT_EQ(0, result_flip_foreach_bound[i]);
    }
  }
}

TEST(ObTestBitVector, test_foreach)
{
  int64_t batch_size = common::ObRandom::rand(0, 1024);
  int64_t round = 100;
  for (int64_t i = 0; i < round; ++i) {
    int64_t start_idx = common::ObRandom::rand(0, batch_size);
    int64_t end_idx = common::ObRandom::rand(0, batch_size);
    if (start_idx > end_idx) {
      swap(start_idx, end_idx);
    }
    test_foreach_result_random(batch_size, start_idx, end_idx);
  }
}

}
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
