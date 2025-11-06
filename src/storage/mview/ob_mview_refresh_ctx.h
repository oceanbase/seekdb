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

#pragma once

#include "share/ob_table_range.h"
#include "share/schema/ob_dependency_info.h"
#include "share/schema/ob_mlog_info.h"
#include "share/schema/ob_mview_info.h"
#include "share/schema/ob_mview_refresh_stats_params.h"

namespace oceanbase
{
namespace storage
{
class ObMViewTransaction;

struct ObMViewRefreshCtx
{
public:
  ObMViewRefreshCtx()
    : allocator_("MVRefCtx"),
      tenant_id_(OB_INVALID_TENANT_ID),
      mview_id_(OB_INVALID_ID),
      trans_(nullptr),
      refresh_type_(share::schema::ObMVRefreshType::MAX),
      is_oracle_mode_(false),
      refresh_parallelism_(0),
      target_data_sync_scn_(),
      mview_refresh_scn_range_(),
      base_table_scn_range_()
  {
  }
  ~ObMViewRefreshCtx() = default;
  DISABLE_COPY_ASSIGN(ObMViewRefreshCtx);

  void reuse()
  {
    trans_ = nullptr;
    mview_info_.reset();
    refresh_stats_params_.reset();
    dependency_infos_.reset();
    based_schema_object_infos_.reset();
    mlog_infos_.reset();
    // refresh_scn_range_.reset();
    refresh_type_ = share::schema::ObMVRefreshType::MAX;
    refresh_sqls_.reset();
    is_oracle_mode_ = false;
    refresh_parallelism_ = 0;
    allocator_.reuse();
    target_data_sync_scn_.reset();
    mview_refresh_scn_range_.reset();
    base_table_scn_range_.reset();
  }

  TO_STRING_KV(K_(tenant_id), K_(mview_id), KP_(trans), K_(mview_info), K_(refresh_stats_params),
               K_(dependency_infos), K_(based_schema_object_infos), K_(mlog_infos),
               K_(refresh_type), K_(refresh_sqls), K_(is_oracle_mode), K_(refresh_parallelism),
               K_(target_data_sync_scn), K_(mview_refresh_scn_range), K_(base_table_scn_range));

public:
  ObArenaAllocator allocator_;
  uint64_t tenant_id_;
  uint64_t mview_id_;
  ObMViewTransaction *trans_;
  share::schema::ObMViewInfo mview_info_;
  share::schema::ObMViewRefreshStatsParams refresh_stats_params_;
  ObArray<share::schema::ObDependencyInfo> dependency_infos_;
  ObArray<share::schema::ObBasedSchemaObjectInfo> based_schema_object_infos_;
  ObArray<share::schema::ObMLogInfo> mlog_infos_;
  // share::ObScnRange refresh_scn_range_; // [last_refresh_scn, current_refresh_scn]
  share::schema::ObMVRefreshType refresh_type_;
  ObArray<ObString> refresh_sqls_;
  bool is_oracle_mode_;
  int64_t refresh_parallelism_;
  share::SCN target_data_sync_scn_;
  share::ObScnRange mview_refresh_scn_range_;
  share::ObScnRange base_table_scn_range_;
};

} // namespace storage
} // namespace oceanbase
