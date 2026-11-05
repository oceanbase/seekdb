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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/px/p2p_datahub/ob_runtime_filter_query_range.h"

namespace oceanbase
{
namespace sql
{
OB_SERIALIZE_MEMBER(ObPxRFStaticInfo, is_inited_, is_shared_, p2p_dh_ids_);
OB_SERIALIZE_MEMBER(ObPxQueryRangeInfo, table_id_, range_column_cnt_, prefix_col_idxs_,
                    prefix_col_obj_metas_);

int ObPxRFStaticInfo::init(const ObIArray<int64_t> &p2p_dh_ids, bool is_shared)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("twice init bf static info", K(ret));
  } else if (OB_FAIL(p2p_dh_ids_.assign(p2p_dh_ids))) {
    LOG_WARN("failed to assign", K(ret));
  } else {
    is_shared_ = is_shared;
    is_inited_ = true;
  }
  return ret;
}

int ObPxRFStaticInfo::assign(const ObPxRFStaticInfo &other)
{
  int ret = OB_SUCCESS;
  is_inited_ = other.is_inited_;
  is_shared_ = other.is_shared_;
  if (OB_FAIL(p2p_dh_ids_.assign(other.p2p_dh_ids_))) {
    LOG_WARN("failed to assign p2p_dh_ids_");
  }
  return ret;
}

int ObPxQueryRangeInfo::init(int64_t table_id, int64_t range_column_cnt,
                             const ObIArray<int64_t> &prefix_col_idxs,
                             const ObIArray<ObObjMeta> &prefix_col_obj_metas)
{
  int ret = OB_SUCCESS;
  table_id_ = table_id;
  range_column_cnt_ = range_column_cnt;
  if (OB_FAIL(prefix_col_idxs_.assign(prefix_col_idxs))) {
    LOG_WARN("failed to assign prefix_col_idxs");
  } else if (OB_FAIL(prefix_col_obj_metas_.assign(prefix_col_obj_metas))) {
    LOG_WARN("failed to assign prefix_col_obj_metas");
  }
  return ret;
}

int ObPxQueryRangeInfo::assign(const ObPxQueryRangeInfo &other)
{
  int ret = OB_SUCCESS;
  table_id_ = other.table_id_;
  range_column_cnt_ = other.range_column_cnt_;
  if (OB_FAIL(prefix_col_idxs_.assign(other.prefix_col_idxs_))) {
    LOG_WARN("failed to assign prefix_col_idxs");
  } else if (OB_FAIL(prefix_col_obj_metas_.assign(other.prefix_col_obj_metas_))) {
    LOG_WARN("failed to assign prefix_col_obj_metas");
  }
  return ret;
}

} // namespace sql

} // namespace oceanbase
