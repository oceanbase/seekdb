/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_ISOLATION_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_ISOLATION_H_

#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace transaction
{

class ObTransIsolation
{
public:
  enum {
    UNKNOWN = -1,
    READ_UNCOMMITTED = 0,
    READ_COMMITED = 1,
    REPEATABLE_READ = 2,
    SERIALIZABLE = 3,
    MAX_LEVEL
  };
  static const common::ObString LEVEL_NAME[MAX_LEVEL];

  static bool is_valid(const int32_t level)
  {
    return level == READ_UNCOMMITTED
        || level == READ_COMMITED
        || level == REPEATABLE_READ
        || level == SERIALIZABLE;
  }
  static int32_t get_level(const common::ObString &level_name);
  static const common::ObString &get_name(int32_t level);

private:
  ObTransIsolation() {}
  ~ObTransIsolation() {}
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_ISOLATION_H_
