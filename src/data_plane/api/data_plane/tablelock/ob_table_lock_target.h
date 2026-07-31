/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_TABLE_LOCK_TARGET_H_
#define OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_TABLE_LOCK_TARGET_H_

#include <stdint.h>

#include "data_plane/tablelock/ob_table_lock_mode.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace data_plane
{

// Value-semantic lock intent shared by query and the data plane. Resolver
// objects such as TableItem must be converted to this type on the query side.
struct ObTableLockTarget
{
  ObTableLockTarget()
    : table_id_(0), lock_mode_(transaction::tablelock::NO_LOCK)
  {}
  ObTableLockTarget(
      const uint64_t table_id,
      const transaction::tablelock::ObTableLockMode lock_mode)
    : table_id_(table_id), lock_mode_(lock_mode)
  {}

  uint64_t table_id_;
  transaction::tablelock::ObTableLockMode lock_mode_;

  TO_STRING_KV(K_(table_id), K_(lock_mode));
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_TABLE_LOCK_TARGET_H_
