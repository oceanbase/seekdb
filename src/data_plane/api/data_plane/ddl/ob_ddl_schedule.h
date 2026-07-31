/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_DDL_OB_DDL_SCHEDULE_H_
#define OCEANBASE_DATA_PLANE_DDL_OB_DDL_SCHEDULE_H_

#include <stdint.h>

#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{
template <typename T>
class ObIArray;
}
namespace data_plane
{

// The only schedule detail Storage needs in order to assign local DDL slices.
// Rootserver's persisted task record and SQL range representation stay private
// to the coordinator adapter.
struct ObDDLTabletSliceCount final
{
  ObDDLTabletSliceCount() : tablet_id_(0), slice_count_(0) {}
  ObDDLTabletSliceCount(const int64_t tablet_id, const int64_t slice_count)
      : tablet_id_(tablet_id), slice_count_(slice_count) {}
  TO_STRING_KV(K_(tablet_id), K_(slice_count));

  int64_t tablet_id_;
  int64_t slice_count_;
};

// Load a schedule that is valid for idempotent DDL execution. The output is
// reset before use; a non-idempotent schedule is rejected by the adapter.
int load_idempotent_ddl_tablet_slice_counts(
    int64_t task_id,
    common::ObIArray<ObDDLTabletSliceCount> &slice_counts);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_DDL_OB_DDL_SCHEDULE_H_
