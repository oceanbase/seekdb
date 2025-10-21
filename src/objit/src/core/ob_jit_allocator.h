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

#ifndef OB_JIT_ALLOCATOR_H
#define OB_JIT_ALLOCATOR_H

#include <sys/mman.h>
#include "lib/oblog/ob_log.h"
#include "lib/alloc/alloc_assist.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase {
namespace jit {
namespace core {

enum JitMemType{
  JMT_RO,
  JMT_RW,
  JMT_RWE
};


class ObJitMemoryBlock;
class ObJitMemoryGroup
{
public:
  ObJitMemoryGroup()
      : header_(nullptr),
        tailer_(nullptr),
        block_cnt_(0),
        used_(0),
        total_(0)
  {
  }
  ~ObJitMemoryGroup() { free(); }
  // Traverse block_list when allocating, if there is a block with available size, then directly get memory from the block
  void *alloc_align(int64_t sz, int64_t align, int64_t p_flags = PROT_READ | PROT_WRITE);
  int finalize(int64_t p_flags);
  //free all
  void free();
  void reset();
  void reserve(int64_t sz, int64_t align, int64_t p_flags);

  DECLARE_TO_STRING;
private:
  ObJitMemoryBlock *alloc_new_block(int64_t sz, int64_t p_flags);
private:
  DISALLOW_COPY_AND_ASSIGN(ObJitMemoryGroup);

private:
  ObJitMemoryBlock *header_;
  ObJitMemoryBlock *tailer_;
  int64_t block_cnt_;       // number of block allocated
  int64_t used_;        // total number of bytes allocated by users
  int64_t total_;       // total number of bytes occupied by pages
};

class ObJitAllocator
{
public:
  ObJitAllocator()
      : code_mem_(),
        rw_data_mem_(),
        ro_data_mem_()
  {}

  void *alloc(const JitMemType mem_type, int64_t sz, int64_t align);
  bool finalize();
  void reserve(const JitMemType mem_type, int64_t sz, int64_t align);

private:
  ObJitMemoryGroup code_mem_;
  ObJitMemoryGroup rw_data_mem_;
  ObJitMemoryGroup ro_data_mem_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObJitAllocator);
};

}  // core
}  // jit
}  // oceanbase

#endif /* OB_JIT_ALLOCATOR_H */
