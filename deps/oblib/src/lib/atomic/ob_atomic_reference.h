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

#ifndef OB_ATOMIC_REFERENCE_H_
#define OB_ATOMIC_REFERENCE_H_
#include <stdint.h>

namespace oceanbase
{
namespace common
{

union AtomicInt64
{
  uint64_t atomic;
  struct
  {
    uint32_t buffer;
    uint32_t pairs;
  };
  struct
  {
    uint32_t ref;
    uint32_t seq;
  };
};


class ObAtomicReference final
{
public:
  ObAtomicReference();
  ~ObAtomicReference();
  void reset();
  int inc_ref_cnt();
  int check_seq_num_and_inc_ref_cnt(const uint32_t seq_num);
  int check_and_inc_ref_cnt();
  int dec_ref_cnt_and_inc_seq_num(uint32_t &ref_cnt);
  inline uint32_t get_seq_num() const { return atomic_num_.seq; }
  inline uint32_t get_ref_cnt() const { return atomic_num_.ref; }
private:
  AtomicInt64 atomic_num_;
};

}
}

#endif /* OB_ATOMIC_REFERENCE_H_ */
