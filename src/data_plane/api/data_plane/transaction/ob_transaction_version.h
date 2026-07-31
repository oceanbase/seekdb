/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_VERSION_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_VERSION_H_

#include <cstdint>

namespace oceanbase
{
namespace transaction
{

class ObTransVersion
{
public:
  static const int64_t INVALID_TRANS_VERSION = -1;
  static const int64_t MAX_TRANS_VERSION = INT64_MAX;
  static bool is_valid(const int64_t trans_version) { return trans_version >= 0; }
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TRANSACTION_VERSION_H_
