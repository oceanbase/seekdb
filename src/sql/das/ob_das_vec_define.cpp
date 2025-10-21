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

#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_vec_define.h"

namespace oceanbase
{
namespace sql
{

ObSPIVBlockMaxSpec::ObSPIVBlockMaxSpec(common::ObIAllocator &alloc)
  : col_types_(alloc),
    col_store_idxes_(alloc),
    scan_col_proj_(alloc),
    min_id_idx_(-1),
    max_id_idx_(-1),
    value_idx_(-1) {}

bool ObSPIVBlockMaxSpec::is_valid() const
{
  return SKIP_INDEX_COUNT == col_types_.count()
      && SKIP_INDEX_COUNT == col_store_idxes_.count()
      && SKIP_INDEX_COUNT == scan_col_proj_.count()
      && MIN_ID_IDX == min_id_idx_
      && MAX_ID_IDX == max_id_idx_
      && VALUE_IDX == value_idx_;
}

OB_SERIALIZE_MEMBER(ObSPIVBlockMaxSpec,
    col_types_,
    col_store_idxes_,
    scan_col_proj_,
    min_id_idx_,
    max_id_idx_,
    value_idx_);

OB_SERIALIZE_MEMBER((ObDASVecAuxScanCtDef, ObDASAttachCtDef),
                    inv_scan_vec_id_col_, vec_index_param_, dim_, vec_type_, 
                    algorithm_type_, selectivity_, row_count_, access_pk_, 
                    can_use_vec_pri_opt_, extra_column_count_, spiv_scan_docid_col_, 
                    spiv_scan_value_col_, vector_index_param_, vec_query_param_,
                    adaptive_try_path_, // FARM COMPAT WHITELIST
                    is_multi_value_index_, // FARM COMPAT WHITELIST
                    is_spatial_index_, // FARM COMPAT WHITELIST
                    can_extract_range_, // FARM COMPAT WHITELIST
                    relevance_col_cnt_, // FARM COMPAT WHITELIST
                    is_hybrid_ // FARM COMPAT WHITELIST
                    , use_rowkey_vid_tbl_);
OB_SERIALIZE_MEMBER(ObDASVecAuxScanRtDef);

} // sql
} // oceanbase

