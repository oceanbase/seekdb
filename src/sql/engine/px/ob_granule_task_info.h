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

#ifndef OCEANBASE_SQL_ENGINE_PX_OB_GRANULE_TASK_INFO_H_
#define OCEANBASE_SQL_ENGINE_PX_OB_GRANULE_TASK_INFO_H_

#include "common/ob_range.h"
#include "lib/hash/ob_hashmap.h"
#include "sql/engine/px/ob_granule_util.h"

namespace oceanbase
{
namespace sql
{

class ObGranuleTaskInfo
{
public:
  ObGranuleTaskInfo()
    : ranges_(),
      tablet_loc_(nullptr),
      task_id_(0),
      granule_type_(OB_GRANULE_UNINITIALIZED)
  {}
  ~ObGranuleTaskInfo() = default;
  int assign(const ObGranuleTaskInfo &other);
  TO_STRING_KV(K_(ranges), K_(task_id), K_(granule_type), "tablet_id: ",
               OB_ISNULL(tablet_loc_) ? OB_INVALID_ID : tablet_loc_->tablet_id_.id());

  common::ObSEArray<common::ObNewRange, 1> ranges_;
  ObDASTabletLoc *tablet_loc_;
  int64_t task_id_;
  ObGranuleType granule_type_;
};

class ObGIPruningInfo
{
public:
  ObGIPruningInfo() : part_id_(common::OB_INVALID_ID) {}

  int64_t get_part_id() const { return part_id_; }
  void set_part_id(int64_t part_id) { part_id_ = part_id; }

private:
  int64_t part_id_;
};

typedef common::hash::ObHashMap<uint64_t, ObGranuleTaskInfo,
    common::hash::NoPthreadDefendMode> GIPrepareTaskMap;

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_PX_OB_GRANULE_TASK_INFO_H_
