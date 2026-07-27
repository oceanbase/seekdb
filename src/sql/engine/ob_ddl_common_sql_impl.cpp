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
// this file was  share/ob_ddl_common.cpp created by function-level splitting from:these ObDDLUtil static methods
// implementation depends on this module,callers are all in upper layers;declaration remains in share/ob_ddl_common.h。
#define USING_LOG_PREFIX SHARE

#include "share/ob_ddl_common.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_ddl_checksum.h"
#include "share/ob_ddl_sim_point.h"
#include "common/object/ob_object.h"
#include "share/tablet/ob_tablet_table_operator.h"
#include "share/storage/ob_tablet_local_checksum_table_storage.h"
#include "sql/engine/table/ob_table_scan_op.h"

using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::obcall;
using namespace oceanbase::sql;

int ObDDLUtil::clear_ddl_checksum(ObPhysicalPlan *phy_plan)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t execution_id = phy_plan->get_ddl_execution_id();
    const ObOpSpec *root_op_spec = phy_plan->get_root_op_spec();
    uint64_t table_scan_table_id = OB_INVALID_ID;
    if (OB_FAIL(find_table_scan_table_id(root_op_spec, table_scan_table_id))) {
      LOG_WARN("failed to get table scan table id", K(ret));
    } else if (OB_FAIL(ObDDLChecksumOperator::delete_checksum(
                                                              execution_id,
                                                              table_scan_table_id,
                                                              phy_plan->get_ddl_table_id(),
                                                              phy_plan->get_ddl_task_id(),
                                                              *GCTX.sql_proxy_))) {
      LOG_WARN("failed to delete checksum", K(ret));
    }
  }
  return ret;
}

int ObDDLUtil::find_table_scan_table_id(const ObOpSpec *spec, uint64_t &table_id) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(spec) || OB_INVALID_ID != table_id) {
    /*do nothing*/
  } else if (spec->is_table_scan()) {
    table_id = static_cast<const ObTableScanSpec *>(spec)->get_ref_table_id();
  } else {
    for (uint32_t i = 0; OB_SUCC(ret) && i < spec->get_child_cnt(); ++i) {
      if (OB_FAIL(SMART_CALL(find_table_scan_table_id(spec->get_child(i), table_id)))) {
        LOG_WARN("fail to find sample scan", K(ret));
      }
    }
  }
  return ret;
}
