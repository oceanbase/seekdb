/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_ACCESS_OB_DATA_ACCESS_RETRY_H_
#define OCEANBASE_DATA_PLANE_ACCESS_OB_DATA_ACCESS_RETRY_H_

#include <stdint.h>

namespace oceanbase
{
namespace data_plane
{

class ObDataAccessRetry
{
public:
  static int wait(const int64_t retry_count);
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_ACCESS_OB_DATA_ACCESS_RETRY_H_
