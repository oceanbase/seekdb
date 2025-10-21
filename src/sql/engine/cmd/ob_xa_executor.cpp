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
#include "sql/engine/cmd/ob_xa_executor.h"
#include "pl/ob_pl.h"

namespace oceanbase
{
using namespace common;
using namespace transaction;
namespace sql
{


// for mysql xa start
int ObXaStartExecutor::execute(ObExecContext &ctx, ObXaStartStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa start", K(ret));
  return ret;
}

// for mysql xa end
int ObXaEndExecutor::execute(ObExecContext &ctx, ObXaEndStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa end", K(ret));
  return ret;
}

// for mysql xa prepare
int ObXaPrepareExecutor::execute(ObExecContext &ctx, ObXaPrepareStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa prepare", K(ret));
  return ret;
}

// for mysql xa commit
int ObXaCommitExecutor::execute(ObExecContext &ctx, ObXaCommitStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa commit", K(ret));
  return ret;
}

// for mysql xa rollback
int ObXaRollbackExecutor::execute(ObExecContext &ctx, ObXaRollBackStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa rollback", K(ret));
  return ret;
}

int ObXaExecutorUtil::get_org_cluster_id(ObSQLSessionInfo *session, int64_t &org_cluster_id) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(session->get_ob_org_cluster_id(org_cluster_id))) {
    LOG_WARN("fail to get ob_org_cluster_id", K(ret));
  } else if (OB_INVALID_ORG_CLUSTER_ID == org_cluster_id ||
             OB_INVALID_CLUSTER_ID == org_cluster_id) {
    org_cluster_id = ObServerConfig::get_instance().cluster_id;
    // If ob_org_cluster_id is not set (0 is an invalid value, considered as not set), then set it to the cluster_id of the current cluster.
    // If the configuration item does not set cluster_id, then ObServerConfig::get_instance().cluster_id will get the default value -1.
    // If cluster_id is not set in the configuration, observer will not start, therefore org_cluster_id will not be -1.
    // For safety, here we set org_cluster_id to 0 or -1 as ObServerConfig::get_instance().cluster_id.
    if (org_cluster_id < OB_MIN_CLUSTER_ID
        || org_cluster_id > OB_MAX_CLUSTER_ID) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("org_cluster_id is set to cluster_id, but it is out of range",
                K(ret), K(org_cluster_id), K(OB_MIN_CLUSTER_ID), K(OB_MAX_CLUSTER_ID));
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
