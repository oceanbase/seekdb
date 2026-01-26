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

#define USING_LOG_PREFIX SQL_CG

#include "core/ob_jit_allocator.h"
#include "common/ob_clock_generator.h"

#if defined(__APPLE__) && defined(__MACH__)
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif

using namespace oceanbase::common;

namespace oceanbase {
namespace jit {
namespace core {

class ObJitMemoryBlock
{
public:
  ObJitMemoryBlock()
      : next_(nullptr),
        addr_(nullptr),
        size_(0),
        alloc_end_(nullptr)
  {
  }

  ObJitMemoryBlock(char *st, int64_t sz)
      : next_(nullptr),
        addr_(st),
        size_(sz),
        alloc_end_(st)
  {
  }

  void *alloc_align(int64_t sz, int64_t align);
  int64_t remain() const { return size_ - (alloc_end_ - addr_); }
  int64_t size() const { return size_; }
  const char *block_end() const { return addr_ + size_; }
  void reset()
  {
    next_ = nullptr;
    addr_ = nullptr;
    size_ = 0;
    alloc_end_ = nullptr;
  }

  TO_STRING_KV(KP_(addr), KP_(alloc_end), K_(size));

public:
  ObJitMemoryBlock *next_;
  char *addr_;
  int64_t size_;
  char *alloc_end_;   // the first position not allocated
};

DEF_TO_STRING(ObJitMemoryGroup)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(header), K_(tailer), K_(block_cnt), K_(used), K_(total));
  J_OBJ_END();
  return pos;
}

class ObJitMemory {
public:
  // num_bytes bytes of virtual memory is made.
  // flags is used to set the initial protection flags for the block
  // is_code_memory: on macOS, code memory needs special handling with MAP_JIT
  static ObJitMemoryBlock allocate_mapped_memory(int64_t num_bytes, int64_t p_flags, bool is_code_memory = false);

  // block describes the memory to be released.
  static int release_mapped_memory(ObJitMemoryBlock &block);

  // set memory protection state.
  // is_code_memory: on macOS, code memory uses pthread_jit_write_protect_np instead of mprotect
  static int protect_mapped_memory(const ObJitMemoryBlock &block, int64_t p_flags, bool is_code_memory = false);
};

ObJitMemoryBlock ObJitMemory::allocate_mapped_memory(int64_t num_bytes,
                                                     int64_t p_flags,
                                                     bool is_code_memory)
{
  if (num_bytes == 0) {
    return ObJitMemoryBlock();
  }
  static const  int64_t page_size = ::getpagesize();
  const int64_t num_pages = (num_bytes+page_size-1)/page_size;
  int fd = -1;
  uintptr_t start = 0;
  int64_t mm_flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(__APPLE__) && defined(__MACH__)
  if (is_code_memory) {
    // On macOS, use MAP_JIT for JIT code to enable W^X switching
    // This is required because macOS enforces W^X (Write XOR Execute) policy
    mm_flags |= MAP_JIT;
    // For MAP_JIT on macOS, we need to use PROT_READ | PROT_WRITE | PROT_EXEC initially
    // The actual W^X protection is managed via pthread_jit_write_protect_np()
    p_flags = PROT_READ | PROT_WRITE | PROT_EXEC;
  }
#else
  (void)is_code_memory;  // Suppress unused parameter warning on non-macOS
#endif
  void *addr = nullptr;
  // Loop mmap memory, until memory is allocated, avoid core dump by llvm internal if memory allocation fails
  do {
    addr = ::mmap(reinterpret_cast<void*>(start), page_size*num_pages,
                  p_flags, mm_flags, fd, 0);
    if (MAP_FAILED == addr) {
      if (REACH_TIME_INTERVAL(10000000)) { // interval 10s print log
        LOG_ERROR_RET(common::OB_ALLOCATE_MEMORY_FAILED, "allocate jit memory failed", K(addr), K(num_bytes), K(page_size), K(num_pages));
      }
      ::usleep(100000); //100ms
    } else {
      LOG_DEBUG("allocate mapped memory success!",
                K(addr), K(start),
                K(page_size), K(num_pages), K(num_pages*page_size), K(num_bytes), K(p_flags), K(is_code_memory));
    }
  } while (MAP_FAILED == addr);

#if defined(__APPLE__) && defined(__MACH__)
  if (is_code_memory && addr != MAP_FAILED) {
    // After allocating MAP_JIT memory, we need to enable write mode
    // so that LLVM can write code into it. The default state might be
    // executable (not writable), so we explicitly switch to writable mode.
    pthread_jit_write_protect_np(false);
  }
#endif

  return ObJitMemoryBlock((char *)addr, num_pages*page_size);
}

int ObJitMemory::release_mapped_memory(ObJitMemoryBlock &block)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(block.addr_) || 0 == block.size_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(block), K(ret));
  } else if (0 != ::munmap(block.addr_, block.size_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("jit block munmap failed", K(block), K(ret));
  } else {
    LOG_DEBUG("release mapped memory done!", KP((void*)block.addr_), K(block.size_));
    block.reset();
  }

  return ret;
}

int ObJitMemory::protect_mapped_memory(const ObJitMemoryBlock &block,
                                       int64_t p_flags,
                                       bool is_code_memory)
{
  int ret = OB_SUCCESS;
  int tmp_ret = 0;
  static const int64_t page_size = ::getpagesize();
  if (OB_ISNULL(block.addr_) || 0 == block.size_ || (!p_flags)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(block), K(p_flags), K(ret));
  } else {
#if defined(__APPLE__) && defined(__MACH__)
    if (is_code_memory) {
      // On macOS with MAP_JIT, we use pthread_jit_write_protect_np() to switch
      // between writable and executable modes instead of mprotect
      // When p_flags contains PROT_EXEC, we want to make the memory executable (disable write)
      if (p_flags & PROT_EXEC) {
        // Switch from writable to executable mode
        pthread_jit_write_protect_np(true);
        // Flush instruction cache to ensure coherency
        sys_icache_invalidate(block.addr_, block.size_);
        LOG_DEBUG("macOS JIT: switched to executable mode", K(block));
      } else {
        // Switch to writable mode
        pthread_jit_write_protect_np(false);
        LOG_DEBUG("macOS JIT: switched to writable mode", K(block));
      }
    } else {
      // For data memory on macOS, we still use mprotect (no MAP_JIT)
      do {
        tmp_ret = 0;
        if (0 != (tmp_ret = ::mprotect((void*)((uintptr_t)block.addr_& ~(page_size-1)),
                                       page_size*((block.size_+page_size-1)/page_size),
                                       p_flags))) {
          if (REACH_TIME_INTERVAL(10000000)) {
            LOG_ERROR("jit block mprotect failed", K(block), K(errno), K(tmp_ret),
                      K((uintptr_t)block.addr_& ~(page_size - 1)),
                      K(page_size * ((block.size_ + page_size - 1) / page_size)),
                      K(p_flags));
          }
        }
      } while (-1 == tmp_ret && 12 == errno);
    }
#else
    (void)is_code_memory;  // Suppress unused parameter warning on non-macOS
    do {
      tmp_ret = 0;
      if (0 != (tmp_ret = ::mprotect((void*)((uintptr_t)block.addr_& ~(page_size-1)),
                                     page_size*((block.size_+page_size-1)/page_size),
                                     p_flags))) {
        if (REACH_TIME_INTERVAL(10000000)) {
          LOG_ERROR("jit block mprotect failed", K(block), K(errno), K(tmp_ret),
                    K((uintptr_t)block.addr_& ~(page_size - 1)),
                    K(page_size * ((block.size_ + page_size - 1) / page_size)),
                    K(p_flags));
        }
      }
    } while (-1 == tmp_ret && 12 == errno);
    // `-1 == tmp_ret` means `mprotect` failed, `12 == errno` means `Cannot allocate memory`
#endif
  }

  return ret;
}

void *ObJitMemoryBlock::alloc_align(int64_t sz, int64_t align)
{
  void *ret = NULL;
  if (remain() < sz + align) {
    //do nothing
  } else {
    char *ptr = alloc_end_;
    //align address
    char *align_ptr = (char *)(((int64_t)ptr + align - 1) & ~(align - 1));
    if (align_ptr + sz > block_end()) {
      //do nothing
    } else {
      ret = align_ptr;
      alloc_end_ = align_ptr + sz;
    }
  }

  return ret;
}

void *ObJitMemoryGroup::alloc_align(int64_t sz, int64_t align, int64_t p_flags, bool is_code_memory)
{
   void *ret = NULL;
   ObJitMemoryBlock *cur = header_;
   ObJitMemoryBlock *avail_block = NULL;

   //find available block
   for (int64_t i = 0; i < block_cnt_ && NULL != cur; i++) {
     if (cur->remain() > sz + align) {
       avail_block = cur;
       break;
     }
     cur = cur->next_;
   }
   if (NULL == avail_block) {
     if (NULL != (cur = alloc_new_block(sz + align, p_flags, is_code_memory))) {
       if (NULL == header_) {
         header_ = tailer_ = cur;
       } else {
         tailer_->next_ = cur;
         cur->next_ = NULL;
         tailer_ = cur;
       }

       avail_block = cur;

       block_cnt_++;
       total_ += cur->size();
     }
   }

   //alloc memory
   if (NULL != avail_block) {
     ret = cur->alloc_align(sz, align);
   }

   if (NULL != ret) {
     used_ += sz;
   }

   return ret;
}

ObJitMemoryBlock *ObJitMemoryGroup::alloc_new_block(int64_t sz, int64_t p_flags, bool is_code_memory)
{
  ObJitMemoryBlock *ret = NULL;
  ObJitMemoryBlock block = ObJitMemory::allocate_mapped_memory(sz, p_flags, is_code_memory);
  if (OB_ISNULL(block.addr_) || 0 == block.size_) {
    ret = NULL;
  } else if (NULL != (ret = new ObJitMemoryBlock(block.addr_,
                                                 block.size_))) { //TODO This module's new method will be replaced with OB's allocator
    //do nothing
  } else { // new failed, release the memory mmaped out
    ObJitMemory::release_mapped_memory(block);
  }

  return ret;
}

int ObJitMemoryGroup::finalize(int64_t p_flags, bool is_code_memory)
{
  int ret = OB_SUCCESS;
  ObJitMemoryBlock *cur = header_;
  for (int64_t i = 0;
       OB_SUCCESS == ret && i < block_cnt_ && NULL != cur;
       i++) {
    if (OB_FAIL(ObJitMemory::protect_mapped_memory(*cur, p_flags, is_code_memory))) {
      LOG_WARN("jit fail to finalize memory", K(p_flags), K(*cur), K(ret), K(is_code_memory));
    }
    cur = cur->next_;
  }

  return ret;
}

void ObJitMemoryGroup::reserve(int64_t sz, int64_t align, int64_t p_flags, bool is_code_memory)
{
  ObJitMemoryBlock *cur  = NULL;
  if (NULL != (cur = alloc_new_block(sz + align, p_flags, is_code_memory))) {
    if (NULL == header_) {
      header_ = tailer_ = cur;
    } else {
      tailer_->next_ = cur;
      cur->next_ = NULL;
      tailer_ = cur;
    }

    block_cnt_++;
    total_ += cur->size();
    LOG_INFO("AARCH64: reserve ObJitMemoryGroup successed",
             K(header_), K(total_), K(block_cnt_), K(*cur));
  } else {
    LOG_ERROR_RET(common::OB_ERR_UNEXPECTED, "AARCH64: reserve ObJitMemoryGroup failed", K(header_), K(total_), K(*cur));
  }
}

void ObJitMemoryGroup::free()
{
  int ret = OB_SUCCESS;
  ObJitMemoryBlock *cur = header_;
  ObJitMemoryBlock *tmp = NULL;
  ObJitMemoryBlock *next = NULL;
  for (int64_t i = 0;
       NULL != cur; //ignore ret
       i++) {
    next = cur->next_;
    if (OB_FAIL(ObJitMemory::release_mapped_memory(*cur))) {
      LOG_WARN("jit fail to free mem", K(*cur), K(ret));
    }
    tmp = cur;
    cur = next;
    delete tmp;
  }
  MEMSET(this, 0, sizeof(*this));
}

void *ObJitAllocator::alloc(const JitMemType mem_type, int64_t sz, int64_t align)
{
  void *ret = NULL;
  switch (mem_type) {
    case JMT_RO: {
      ret =
#if defined(__aarch64__)
          // On ARM64, use code_mem_ with is_code_memory=true
          code_mem_.alloc_align(sz, align, PROT_WRITE | PROT_READ, true /*is_code_memory*/);
#else
          ro_data_mem_.alloc_align(sz, align, PROT_WRITE | PROT_READ, false /*is_code_memory*/);
#endif
    } break;
    case JMT_RW: {
      ret =
#if defined(__aarch64__)
          // On ARM64, use code_mem_ with is_code_memory=true
          code_mem_.alloc_align(sz, align, PROT_WRITE | PROT_READ, true /*is_code_memory*/);
#else
          rw_data_mem_.alloc_align(sz, align, PROT_WRITE | PROT_READ, false /*is_code_memory*/);
#endif
    } break;
    case JMT_RWE: {
      ret = code_mem_.alloc_align(sz, align, PROT_WRITE | PROT_READ, true /*is_code_memory*/);
    } break;
    default : {
      LOG_WARN_RET(OB_ERR_UNEXPECTED, "invalid mem type", K(mem_type));
    } break;
  }

  return ret;
}

void ObJitAllocator::reserve(const JitMemType mem_type, int64_t sz, int64_t align)
{
  int ret = OB_SUCCESS;
  switch (mem_type) {
    case JMT_RO: {
      ro_data_mem_.reserve(sz, align, PROT_WRITE | PROT_READ, false /*is_code_memory*/);
    } break;
    case JMT_RW: {
      rw_data_mem_.reserve(sz, align, PROT_WRITE | PROT_READ, false /*is_code_memory*/);
    } break;
    case JMT_RWE: {
      code_mem_.reserve(sz, align, PROT_WRITE | PROT_READ, true /*is_code_memory*/);
    } break;
    default : {
      LOG_WARN("invalid mem type", K(mem_type));
    }
  }
}

// Returns true if an error occurred, false otherwise.
bool ObJitAllocator::finalize()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ro_data_mem_.finalize(PROT_READ, false /*is_code_memory*/))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to finalize ro data memory", K(ret));
  } else if (OB_FAIL(rw_data_mem_.finalize(PROT_READ | PROT_WRITE, false /*is_code_memory*/))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to finalize rw data memory", K(ret));
  } else if (OB_FAIL(code_mem_.finalize(PROT_READ | PROT_WRITE | PROT_EXEC, true /*is_code_memory*/))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to finalize code memory", K(ret));
  }

  return OB_FAIL(ret);
}


}  // core
}  // jit
}  // oceanbase
