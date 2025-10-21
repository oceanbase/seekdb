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

#define USING_LOG_PREFIX SHARE
#include "share/ob_leader_election_waiter.h"

#include "share/ls/ob_ls_table_operator.h"
namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace share;
namespace share
{

#define LOG_WAIT_RESULT(start_time, ...) \
    LOG_INFO("wait leader elect finish", K(ret), \
        "wait_time", ObTimeUtility::current_time() - start_time, ##__VA_ARGS__)


///////ExpectedLeader


ObLSLeaderElectionWaiter::ObLSLeaderElectionWaiter(
    ObLSTableOperator &lst_operator,
    volatile bool &stop)
  : stop_(stop),
    allocator_(ObModIds::OB_RS_PARTITION_TABLE_TEMP),
    lst_operator_(lst_operator)
{
}

ObLSLeaderElectionWaiter::~ObLSLeaderElectionWaiter()
{
}

int ObLSLeaderElectionWaiter::wait(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == tenant_id
                  || !ls_id.is_valid()
                  || timeout < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(ls_id), K(timeout));
  } else {
    common::ObAddr dummy_leader;
    if (OB_FAIL(wait(tenant_id, ls_id, timeout, dummy_leader))) {
      LOG_WARN("fail to wait leader", KR(ret), K(tenant_id), K(ls_id), K(timeout));
    }
  }
  return ret;
}

int ObLSLeaderElectionWaiter::wait(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const int64_t timeout,
    common::ObAddr &leader)
{
  int ret = OB_SUCCESS;
  const int64_t start_time = ObTimeUtility::current_time();
  const int64_t abs_timeout = start_time + timeout;
  if (OB_UNLIKELY(OB_INVALID_ID == tenant_id
                  || !ls_id.is_valid()
                  || timeout < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(ls_id), K(timeout));
  } else {
    if (OB_FAIL(wait_elect_leader(
            tenant_id, ls_id, CHECK_LEADER_ELECT_INTERVAL_US, abs_timeout, leader))) {
      LOG_WARN("fail to wait elect leader",
               KR(ret), K(tenant_id), K(ls_id), K(timeout), K(abs_timeout));
    }
  }
  LOG_WAIT_RESULT(start_time, K(timeout), K(tenant_id), K(ls_id));
  return ret;
}


int ObLSLeaderElectionWaiter::wait_elect_leader(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const int64_t check_interval,
    const int64_t abs_timeout,
    common::ObAddr &leader)
{
  int ret = OB_SUCCESS;
  ObLSInfo ls_info;
  const ObLSReplica *leader_replica = NULL;
  if (OB_UNLIKELY(OB_INVALID_ID == tenant_id
                  || !ls_id.is_valid())
                  || check_interval <= 0
                  || abs_timeout <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(ls_id),
             K(check_interval), K(abs_timeout));
  } else {
    int64_t sleep_interval = std::max(1l, check_interval / 100);
    while (!stop_) {
      const int64_t cluster_id = GCONF.cluster_id;
      if (OB_FAIL(lst_operator_.get(cluster_id, tenant_id,
          ls_id, share::ObLSTable::DEFAULT_MODE,ls_info))) {
        LOG_WARN("get partition info failed", K(tenant_id), K(ls_id), KR(ret));
      } else if (OB_FAIL(ls_info.find_leader(leader_replica))) {
        // failure is normal, since leader may have not taked over
      } else if (NULL == leader_replica) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL leader", KR(ret));
      } else if (!leader.is_valid()) {
        leader = leader_replica->get_server();
        break;
      } else if (leader == leader_replica->get_server()) {
        break;
      }
      if (OB_SUCCESS != ret || leader != leader_replica->get_server()) {
        const int64_t now = ObTimeUtility::current_time();
        if (now < abs_timeout) {
          if (OB_FAIL(check_sleep(std::min(sleep_interval, abs_timeout - now)))) {
            LOG_WARN("check sleep failed", KR(ret));
            break;
          }
        } else {
          ret = OB_WAIT_ELEC_LEADER_TIMEOUT;
          LOG_WARN("wait elect sys leader timeout", KR(ret),
                   K(abs_timeout), K(tenant_id), K(ls_id));
          break;
        }
      }
      sleep_interval = std::min(sleep_interval * 2, check_interval);
    }
    if (stop_ && OB_SUCC(ret)) {
      ret = OB_CANCELED;
      LOG_WARN("stop flag set, cancel task", KR(ret));
    }
  }
  return ret;
}

int ObLSLeaderElectionWaiter::check_sleep(
    const int64_t interval_us)
{
  int ret = OB_SUCCESS;
  int64_t escaped = 0;
  const static int64_t max_step = 10 * 1000; // 10ms;
  while (!stop_ && escaped < interval_us) {
    const int32_t step = static_cast<int32_t>(std::min(max_step, interval_us - escaped));
    ob_usleep(step);
    escaped += step;
  }
  if (stop_) {
    ret = OB_CANCELED;
    LOG_WARN("stop flag set, cancel task", K(ret));
  }
  return ret;
}

}//end namespace share
}//end namespace oceanbase
