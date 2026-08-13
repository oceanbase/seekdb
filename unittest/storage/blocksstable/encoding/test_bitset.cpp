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
#include "storage/blocksstable/encoding/ob_encoding_bitset.h"

namespace oceanbase
{
namespace blocksstable
{

using namespace common;

TEST(BitSet, set_get)
{
  const int64_t sset_cnt = 50;
  const int64_t lset_cnt = 400;

  uint64_t s[1];
  uint64_t l[8];

  std::vector<int64_t> s_pos;
  std::vector<int64_t> l_pos;

  BitSet sset;
  ASSERT_EQ(OB_SUCCESS, sset.init(s, sset_cnt));
  BitSet lset;
  ASSERT_EQ(OB_SUCCESS, lset.init(l, lset_cnt));

  for (int64_t tn = 0; tn < 1000; ++tn) {
    sset.reset();
    lset.reset();
    s_pos.clear();
    l_pos.clear();

    int64_t i = 0;
    while(i < sset_cnt) {
      sset.set(i);
      s_pos.push_back(i);
      i += random() % 5 + 1;
    }

    for(i = 0; i < sset_cnt; ++i) {
      std::vector<int64_t>::iterator iter = std::find(s_pos.begin(), s_pos.end(), i);
      if (s_pos.end() != iter) {
        ASSERT_TRUE(sset.get(i))
          << "i: " << i << std::endl;
        ASSERT_EQ(iter - s_pos.begin(), sset.get_ref(i))
          << "i: " << i << std::endl;
      } else {
        ASSERT_FALSE(sset.get(i))
          << "i: " << i << std::endl;
      }
    }

    i = 0;
    while(i < lset_cnt) {
      lset.set(i);
      l_pos.push_back(i);
      i += random() % 5 + 1;
    }

    for(i = 0; i < lset_cnt; ++i) {
      std::vector<int64_t>::iterator iter = std::find(l_pos.begin(), l_pos.end(), i);
      if (l_pos.end() != iter) {
        ASSERT_TRUE(lset.get(i))
          << "i: " << i << std::endl;
        ASSERT_EQ(iter - l_pos.begin(), lset.get_ref(i))
          << "i: " << i << std::endl;
      } else {
        ASSERT_FALSE(lset.get(i))
          << "i: " << i << std::endl;
      }
    }
  }
}

} // end namespace blocksstable
} // end namespace oceanbase
