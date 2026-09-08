// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
// DefaultAllocator has NDEBUG-dependent members. Match the dependency's release
// layout in this TU; assertions stay enabled in the test that calls it.
#ifndef NDEBUG
#define NDEBUG
#endif
#include "impl/allocator/default_allocator.h"

vsag::Allocator *make_vsag_default_allocator()
{
  return new vsag::DefaultAllocator();
}
