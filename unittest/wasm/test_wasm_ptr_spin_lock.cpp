// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>
#include "lib/lock/ob_small_spin_lock.h"

int main()
{
  using Lock = oceanbase::common::ObPtrSpinLock<int>;
  // Ring-buffer PtrSlot overlays this lock on a pointer, without extra space.
  struct alignas(8) Guarded {
    int *value = nullptr;
    uint32_t guard = 0xabcdef12;
  } slot;
  auto &lock = reinterpret_cast<Lock &>(slot.value);
  lock.init();
  assert(slot.guard == 0xabcdef12);
  assert(sizeof(Lock) == sizeof(slot.value));
  int value = 0;
  assert(lock.try_lock());
  lock.set_ptr(&value);
  assert(!lock.try_lock());
  assert(lock.get_ptr() == &value);
  lock.unlock();
  assert(slot.guard == 0xabcdef12);
  auto increment = [&] {
    for (int i = 0; i < 10000; ++i) {
      lock.lock();
      ++*lock.get_ptr();
      lock.unlock();
    }
  };
  std::thread first(increment), second(increment);
  first.join();
  second.join();
  assert(value == 20000);
  assert(slot.guard == 0xabcdef12);
#ifdef __wasm32__
  lock.lock();
  lock.set_ptr(reinterpret_cast<int *>(UINT32_C(0xf2345678)));
  assert(reinterpret_cast<uintptr_t>(lock.get_ptr()) == UINT32_C(0xf2345678));
  lock.unlock();
  assert(slot.guard == 0xabcdef12);
#endif
  std::puts("pointer lock: adjacent storage, pointer bits and concurrent updates passed");
}
