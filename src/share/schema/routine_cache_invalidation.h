/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once
#include <cstdint>

namespace oceanbase { namespace rootserver {
// Host effect boundary, never a plugin callback. Caller-owned DDL records the
// request in its transaction participant; delivery requires known commit.
// Legacy Root may flush immediately. No SQL/transaction or DROP authority.
class IRoutineCacheInvalidation
{
public:
  virtual ~IRoutineCacheInvalidation() = default;
  virtual int on_drop(uint64_t routine_id, uint64_t database_id) = 0;
};
} }
