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

#define USING_LOG_PREFIX SQL_PC

#include "ob_pc_ref_handle.h"

namespace oceanbase
{
namespace sql
{
using namespace common;
const char* ObCacheRefHandleMgr::handle_name(const CacheRefHandleID handle_id)
{
  OB_ASSERT(handle_id < MAX_HANDLE && handle_id >= 0);
  static const char* handle_names[] = {
    "pc_ref_plan_local_handle",
    "pc_ref_plan_remote_handle",
    "pc_ref_plan_dist_handle",
    "pc_ref_plan_arr_handle",
    "pc_ref_plan_stat_handle",
    "pc_ref_pl_handle",
    "pc_ref_pl_stat_handle",
    "plan_gen_handle",
    "cli_query_handle",
    "outline_exec_handle",
    "plan_explain_handle",
    "check_evolution_plan_handle",
    "load_baseline_handle",
    "ps_exec_handle",
    "gv_sql_handle",
    "pl_anon_handle",
    "pl_routine_handle",
    "package_var_handle",
    "package_type_handle",
    "package_spec_handle",
    "package_body_handle",
    "package_resv_handle",
    "get_pkg_handle",
    "index_builder_handle",
    "pcv_set_handle",
    "pcv_wr_handle",
    "pcv_rd_handle",
    "pcv_get_plan_key_handle",
    "pcv_get_pl_key_handle",
    "pcv_expire_by_used_handle",
    "pcv_expire_by_mem_handle",
    "lc_ref_cache_node_handle",
    "lc_node_handle",
    "lc_node_rd_handle",
    "lc_node_wr_handle",
    "lc_ref_cache_obj_stat_handle",
    "plan_baseline_handle",
    "tableapi_node_handle",
    "sql_plan_handle",
    "callstmt_handle",
    "pc_diag_handle",
    "sql_stat_handle",
    "virtual_table_sql_stat_handle",
    "kv_schema_info_handle",
    "udf_result_cache"
  };
  static_assert(sizeof(handle_names)/sizeof(const char*) == MAX_HANDLE, "invalid handle name array");
  if (handle_id < MAX_HANDLE) {
    return handle_names[handle_id];
  } else {
    return "invalid handle";
  }
}

} // end namespace sql

} // end namespace oceanbase


