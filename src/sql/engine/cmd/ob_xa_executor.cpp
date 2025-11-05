/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
