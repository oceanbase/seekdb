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
#include "rust/vector_kernels.h"

namespace oceanbase
{
namespace common
{

TEST(VectorKernels, pq_table_update_in_place)
{
  const float norms[] = {1.0f, 4.0f, 9.0f, 16.0f, 25.0f};
  float table[] = {0.5f, -2.0f, 3.0f, -4.0f, 0.0f};
  const float expected[] = {2.0f, 0.0f, 15.0f, 8.0f, 25.0f};

  fvec_madd_in_place_f32(rust::Slice<const float>(norms, 5), 2.0f,
                       rust::Slice<float>(table, 5));

  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(expected[i], table[i]);
  }
}

TEST(VectorKernels, query_table_uses_separate_output)
{
  const float precomputed[] = {2.0f, 0.0f, 15.0f, 8.0f, 25.0f};
  const float products[] = {1.0f, 3.0f, -2.0f, 4.0f, 0.5f};
  const float expected[] = {0.0f, -6.0f, 19.0f, 0.0f, 24.0f};
  float table[5] = {};

  fvec_madd_f32(rust::Slice<const float>(precomputed, 5), -2.0f,
                rust::Slice<const float>(products, 5), rust::Slice<float>(table, 5));

  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(expected[i], table[i]);
  }
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
