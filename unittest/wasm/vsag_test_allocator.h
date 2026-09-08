// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "vsag/allocator.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <new>

class VsagTestAllocator : public vsag::Allocator {
public:
  std::string Name() override { return "WasmVsagTest"; }
  void *Allocate(uint64_t size) override {
    if (size > SIZE_MAX) { throw std::bad_alloc(); }
    void *p = std::malloc(static_cast<size_t>(size == 0 ? 1 : size));
    if (!p) { throw std::bad_alloc(); }
    outstanding_.fetch_add(1);
    return p;
  }
  void *Reallocate(void *p, uint64_t size) override {
    if (!p) { return Allocate(size); }
    if (size > SIZE_MAX) { throw std::bad_alloc(); }
    void *next = std::realloc(p, static_cast<size_t>(size == 0 ? 1 : size));
    if (!next) { throw std::bad_alloc(); }
    return next;
  }
  void Deallocate(void *p) override {
    if (p) {
      assert(outstanding_.fetch_sub(1) > 0);
      std::free(p);
    }
  }
  ~VsagTestAllocator() override { assert(outstanding_.load() == 0); }
private:
  std::atomic<size_t> outstanding_{0};
};
