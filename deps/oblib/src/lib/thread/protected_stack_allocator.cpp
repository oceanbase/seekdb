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

#define USING_LOG_PREFIX LIB

#include "lib/thread/protected_stack_allocator.h"
#include "lib/alloc/alloc_struct.h"
#ifdef _WIN32
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#else
#include <sys/mman.h>
#endif

namespace oceanbase
{
using namespace common;

namespace lib
{
ProtectedStackAllocator g_stack_allocer;
StackMgr g_stack_mgr;

ssize_t ProtectedStackAllocator::page_size()
{
  return get_page_size();
}

/*
  protected_page & user_stack must be aligned with page_size.

  before
      low_addr                           <<<                         high_addr
      --------------------------------------------------------------
      | StackHeader | padding |    protected page     | user_stack |
      --------------------------------------------------------------
      |       page_size       |       page_size       | stack_size |
      --------------------------------------------------------------
       aligned with page_size-^                       ^-return

  now
      low_addr                           <<<                         high_addr
      ------------------------------------------------------------------------
      | padding | StackHeader |    protected page     | user_stack | padding |
      ------------------------------------------------------------------------
      |                       |       page_size       | stack_size |         |
      ------------------------------------------------------------------------
       aligned with page_size-^                       ^-return
*/

void *ProtectedStackAllocator::alloc(const uint64_t tenant_id,
                                     const ssize_t stack_size)
{
  return _alloc(tenant_id, ObCtxIds::CO_STACK, stack_size, true);
}

void *ProtectedStackAllocator::smart_call_alloc(const uint64_t tenant_id,
                                                const ssize_t stack_size)
{
  return _alloc(tenant_id, ObCtxIds::DEFAULT_CTX_ID, stack_size, false);
}

void *ProtectedStackAllocator::_alloc(const uint64_t tenant_id,
                                      const uint64_t ctx_id,
                                      const ssize_t stack_size,
                                      const bool guard_page)
{
  void *ptr = nullptr;

  int ret = OB_SUCCESS;
  const ssize_t ps = page_size();
  const ssize_t alloc_size = stack_size + ps * 2 + sizeof(ObStackHeader);
  if (stack_size < ps || ACHUNK_PURE_HEADER_SIZE + sizeof(ObStackHeader) > ps) {
    LOG_ERROR("invalid arg", K(stack_size), K(alloc_size));
  } else if (OB_ISNULL(ptr = __alloc(tenant_id, ctx_id, alloc_size, guard_page))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("alloc failed", K(ret));
  } else {
    // do nothing
  }

  return ptr;
}

void *ProtectedStackAllocator::__alloc(const uint64_t tenant_id,
                                       const uint64_t ctx_id,
                                       const ssize_t size,
                                       const bool guard_page)
{
  void *ptr = nullptr;

  (void)ctx_id;
  const ssize_t ps = page_size();
  const ssize_t mmap_size = align_up2(size, ps);
#ifdef _WIN32
  char *buffer = reinterpret_cast<char *>(
      VirtualAlloc(nullptr, mmap_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (OB_ISNULL(buffer)) {
    LOG_ERROR_RET(OB_ALLOCATE_MEMORY_FAILED, "CO_STACK mmap failed", K(size), K(mmap_size), K(GetLastError()));
  } else {
#else
  char *buffer = static_cast<char *>(::mmap(nullptr, mmap_size, PROT_READ | PROT_WRITE,
                                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (MAP_FAILED == buffer) {
    ptr = nullptr;
    LOG_ERROR_RET(OB_ALLOCATE_MEMORY_FAILED, "CO_STACK mmap failed", K(size), K(mmap_size), K(errno));
  } else {
#endif
    char *guard_page_addr = buffer + ps;
    ObStackHeader *header = new (guard_page_addr - sizeof(ObStackHeader)) ObStackHeader;
    header->tenant_id_ = tenant_id;
    header->size_ = mmap_size;
    header->pth_ = 0;
    header->base_ = buffer;
    header->has_guarded_page_ = guard_page;
    g_stack_mgr.insert(header);

#ifdef _WIN32
    DWORD old_prot;
    if (guard_page && !VirtualProtect(guard_page_addr, ps, PAGE_NOACCESS, &old_prot)) {
      LOG_WARN_RET(OB_ERR_SYS, "VirtualProtect failed", K(GetLastError()), KP(guard_page_addr), K(ps));
    }
#else
    if (guard_page && 0 != mprotect(guard_page_addr, ps, PROT_NONE)) {
      LOG_WARN_RET(OB_ERR_SYS, "mprotect failed", K(errno), KP(guard_page_addr), K(ps));
    }
#endif
    ptr = guard_page_addr + ps;
  }
  return ptr;
}

void ProtectedStackAllocator::dealloc(void *ptr)
{
  if (OB_ISNULL(ptr)) {
    // do nothing
  } else {
    ObStackHeader *header = stack_header(ptr);
    abort_unless(header->check_magic());
    char *base = (char *)header->base_;
    const ssize_t ps = page_size();
#ifdef _WIN32
    DWORD old_prot;
    if (header->has_guarded_page_
        && !VirtualProtect((char *)header + sizeof(ObStackHeader), ps, PAGE_READWRITE, &old_prot)) {
      LOG_WARN_RET(OB_ERR_SYS, "VirtualProtect failed", K(GetLastError()), K(header), K(ps));
    } else {
#else
    if (header->has_guarded_page_
        && 0 != mprotect((char *)header + sizeof(ObStackHeader), ps, PROT_READ | PROT_WRITE)) {
      LOG_WARN_RET(OB_ERR_SYS, "mprotect failed", K(errno), K(header), K(ps));
    } else {
#endif
      const uint64_t tenant_id = header->tenant_id_;
      const ssize_t size = header->size_;
      g_stack_mgr.erase(header);
#ifdef _WIN32
      if (!VirtualFree(base, 0, MEM_RELEASE)) {
        LOG_WARN_RET(OB_ERR_SYS, "VirtualFree failed", K(GetLastError()), KP(base), K(size), K(tenant_id));
      }
#else
      if (0 != ::munmap(base, size)) {
        LOG_WARN_RET(OB_ERR_SYS, "munmap failed", K(errno), KP(base), K(size), K(tenant_id));
      }
#endif
    }
  }
}

ObStackHeader *ProtectedStackAllocator::stack_header(void *ptr)
{
  return (ObStackHeader *)((char *)ptr - page_size() - sizeof(ObStackHeader));
}

void StackMgr::insert(ObStackHeader *header)
{
  if (header != nullptr) {
    abort_unless(header->check_magic());
    rwlock_.wrlock(common::ObLatchIds::DEFAULT_SPIN_RWLOCK);
    header->prev_ = &dummy_;
    header->next_ = dummy_.next_;
    dummy_.next_->prev_ = header;
    dummy_.next_ = header;
    rwlock_.unlock();
  }
}

void StackMgr::erase(ObStackHeader *header)
{
  if (header != nullptr) {
    abort_unless(header->check_magic());
    rwlock_.wrlock(common::ObLatchIds::DEFAULT_SPIN_RWLOCK);
    header->prev_->next_ = header->next_;
    header->next_->prev_ = header->prev_;
    header->prev_ = header->next_ = header;
    rwlock_.unlock();
  }
}

ObStackHeaderGuard::ObStackHeaderGuard()
{
#ifdef _WIN32
  header_.pth_ = (uint64_t)GetCurrentThreadId();
#else
  header_.pth_ = (uint64_t)pthread_self();
#endif
  g_stack_mgr.insert(&header_);
}

ObStackHeaderGuard::~ObStackHeaderGuard()
{
  g_stack_mgr.erase(&header_);
}

}  // lib
}  // oceanbase
