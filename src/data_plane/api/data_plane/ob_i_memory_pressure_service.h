/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_MEMORY_PRESSURE_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_MEMORY_PRESSURE_SERVICE_H_

#include <stdint.h>

namespace oceanbase
{
namespace data_plane
{

class ObIMemoryPressureService
{
public:
  virtual ~ObIMemoryPressureService() {}
  virtual int64_t memstore_limit_percentage() const = 0;
  virtual int get_memstore_condition(
      int64_t &active_memstore_used,
      int64_t &total_memstore_used,
      int64_t &memstore_freeze_trigger,
      int64_t &memstore_limit,
      int64_t &freeze_count) = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_MEMORY_PRESSURE_SERVICE_H_
