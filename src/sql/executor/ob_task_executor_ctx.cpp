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

#define USING_LOG_PREFIX SQL_EXE

#include "ob_task_executor_ctx.h"
#include "observer/ob_server.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
namespace oceanbase
{
namespace sql
{

int ObTaskExecutorCtx::CalcVirtualPartitionIdParams::init(uint64_t ref_table_id)
{
  int ret = common::OB_SUCCESS;
  if (true == inited_) {
    ret = common::OB_INIT_TWICE;
    LOG_ERROR("init twice", K(ret), K(inited_), K(ref_table_id));
  } else {
    inited_ = true;
    ref_table_id_ = ref_table_id;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTaskExecutorCtx,
                    table_locations_,
                    retry_times_,
                    expected_worker_cnt_,
                    admited_worker_cnt_,
                    query_begin_schema_version_,
                    query_sys_begin_schema_version_,
                    minimal_worker_cnt_);

ObTaskExecutorCtx::ObTaskExecutorCtx(ObExecContext &exec_context)
    : exec_ctx_(&exec_context),
      expected_worker_cnt_(0),
      minimal_worker_cnt_(0),
      admited_worker_cnt_(0),
      retry_times_(0),
      sys_job_id_(-1),
      query_begin_schema_version_(-1),
      query_sys_begin_schema_version_(-1),
      schema_service_(GCTX.schema_service_)
{
}

ObTaskExecutorCtx::~ObTaskExecutorCtx()
{
}


int ObTaskExecutorCtx::set_table_locations(const ObTablePartitionInfoArray &table_partition_infos)
{
  int ret = OB_SUCCESS;
  //table_locations_ must be reset here to ensure that table partition infos are not added repeatedly
  table_locations_.reset();
  ObPhyTableLocation phy_table_loc;
  int64_t N = table_partition_infos.count();
  if (OB_FAIL(table_locations_.reserve(N))) {
    LOG_WARN("fail reserve locations", K(ret), K(N));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
    phy_table_loc.reset();
    ObTablePartitionInfo *partition_info = table_partition_infos.at(i);
    if (OB_ISNULL(partition_info)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error. table partition info is null", K(ret), K(i));
    } else if (partition_info->get_table_location().use_das()) {
      //do nothing, DAS's location is maintained and calculated by itself
    } else if (OB_FAIL(phy_table_loc.assign_from_phy_table_loc_info(partition_info->get_phy_tbl_location_info()))) {
      LOG_WARN("fail to assign_from_phy_table_loc_info", K(ret), K(i), K(partition_info->get_phy_tbl_location_info()), K(N));
    } else if (OB_FAIL(table_locations_.push_back(phy_table_loc))) {
      LOG_WARN("fail to push back into table locations", K(ret), K(i), K(phy_table_loc), K(N));
    }
  }
  return ret;
}

int ObTaskExecutorCtx::append_table_location(const ObCandiTableLoc &phy_location_info)
{
  int ret = OB_SUCCESS;
  ObPhyTableLocation phy_table_loc;
  if (OB_FAIL(phy_table_loc.assign_from_phy_table_loc_info(phy_location_info))) {
    LOG_WARN("assign from physical table location info failed", K(ret));
  } else if (OB_FAIL(table_locations_.push_back(phy_table_loc))) {
    LOG_WARN("store table location failed", K(ret));
  }
  return ret;
}

}/* ns sql*/
}/* ns oceanbase */
