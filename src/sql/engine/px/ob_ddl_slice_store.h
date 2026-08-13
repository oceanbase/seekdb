/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OCEANBASE_SQL_ENGINE_PX_OB_DDL_SLICE_STORE_H_
#define OCEANBASE_SQL_ENGINE_PX_OB_DDL_SLICE_STORE_H_

#include "sql/engine/px/ob_px_dtl_msg.h"

namespace oceanbase
{
namespace sql
{

class ObIDdlSliceStore
{
public:
  virtual ~ObIDdlSliceStore() = default;
  virtual int get_or_insert_schedule_info(
      int64_t task_id,
      common::ObIAllocator &allocator,
      common::Ob2DArray<ObPxTabletRange> &part_ranges,
      bool &is_idempotent_mode) = 0;
};

inline ObIDdlSliceStore *&ddl_slice_store_slot()
{
  static ObIDdlSliceStore *store = nullptr;
  return store;
}

inline void register_ddl_slice_store(ObIDdlSliceStore *store)
{
  ddl_slice_store_slot() = store;
}

inline ObIDdlSliceStore *ddl_slice_store()
{
  return ddl_slice_store_slot();
}

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_PX_OB_DDL_SLICE_STORE_H_
