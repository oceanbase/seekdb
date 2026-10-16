/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/engine/graph/graph_expand_scan_desc.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
GraphExpandScanDesc::GraphExpandScanDesc(ObIAllocator &allocator)
    : startup_filters_(&allocator),
      filters_(&allocator),
      payload_exprs_(&allocator),
      owned_ctdef_(allocator)
{
}

int GraphExpandScanDesc::init(const ObTableScanSpec &scan_spec)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(scan_spec.get_id() == OB_INVALID_ID
                  || scan_spec.get_table_loc_id() == OB_INVALID_ID
                  || scan_spec.ref_table_id_ == OB_INVALID_ID
                  || scan_spec.tsc_ctdef_.scan_ctdef_.ref_table_id_
                         == OB_INVALID_ID)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph expand table scan spec", K(ret), K(scan_spec));
  } else if (OB_FAIL(startup_filters_.assign(scan_spec.startup_filters_))) {
    LOG_WARN("failed to copy graph scan startup filters", K(ret));
  } else if (OB_FAIL(filters_.assign(scan_spec.filters_))) {
    LOG_WARN("failed to copy graph scan filters", K(ret));
  } else {
    scan_op_id_ = scan_spec.get_id();
    table_loc_id_ = scan_spec.get_table_loc_id();
    ref_table_id_ = scan_spec.ref_table_id_;
    frozen_version_ = scan_spec.frozen_version_;
    rows_ = scan_spec.rows_;
    width_ = scan_spec.width_;
    need_check_output_datum_ = scan_spec.need_check_output_datum_;
    codegen_ctdef_ = &scan_spec.tsc_ctdef_;
  }
  return ret;
}

bool GraphExpandScanDesc::is_valid() const
{
  const ObTableScanCtDef &ctdef = get_tsc_ctdef();
  return scan_op_id_ != OB_INVALID_ID
      && table_loc_id_ != OB_INVALID_ID
      && ref_table_id_ != OB_INVALID_ID
      && ctdef.scan_ctdef_.ref_table_id_ != OB_INVALID_ID;
}

OB_DEF_SERIALIZE(GraphExpandScanDesc)
{
  int ret = OB_SUCCESS;
  const ObTableScanCtDef &tsc_ctdef = get_tsc_ctdef();
  LST_DO_CODE(OB_UNIS_ENCODE,
              scan_op_id_,
              table_loc_id_,
              ref_table_id_,
              frozen_version_,
              rows_,
              width_,
              need_check_output_datum_,
              startup_filters_,
              filters_,
              payload_exprs_,
              tsc_ctdef);
  return ret;
}

OB_DEF_DESERIALIZE(GraphExpandScanDesc)
{
  int ret = OB_SUCCESS;
  codegen_ctdef_ = nullptr;
  LST_DO_CODE(OB_UNIS_DECODE,
              scan_op_id_,
              table_loc_id_,
              ref_table_id_,
              frozen_version_,
              rows_,
              width_,
              need_check_output_datum_,
              startup_filters_,
              filters_,
              payload_exprs_,
              owned_ctdef_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(GraphExpandScanDesc)
{
  int64_t len = 0;
  const ObTableScanCtDef &tsc_ctdef = get_tsc_ctdef();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              scan_op_id_,
              table_loc_id_,
              ref_table_id_,
              frozen_version_,
              rows_,
              width_,
              need_check_output_datum_,
              startup_filters_,
              filters_,
              payload_exprs_,
              tsc_ctdef);
  return len;
}


} // namespace sql
} // namespace oceanbase
