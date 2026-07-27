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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_ddl_epoch.h"

#include "share/ob_global_stat_proxy.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_level.h"
#include "lib/oblog/ob_log_print_kv.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/utility.h"
#include "mysqlclient/ob_mysql_proxy.h"
#include "mysqlclient/ob_mysql_transaction.h"
#include "share/ob_errno.h"
#include "share/schema/ob_multi_version_schema_service.h"

namespace oceanbase {
namespace observer {
class ObInnerSQLConnection;
}  // namespace observer
}  // namespace oceanbase

namespace oceanbase
{
namespace share
{
namespace schema
{

int ObDDLEpochMgr::init(ObMySQLProxy *sql_proxy, share::schema::ObMultiVersionSchemaService *schema_service)
{
  int ret = OB_SUCCESS;
  if (inited_) {
  } else if (sql_proxy == NULL || schema_service == NULL) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ObDDLEpochMgr init", KR(ret), K(sql_proxy), K(schema_service));
  } else {
    sql_proxy_ = sql_proxy;
    schema_service_ = schema_service;
    inited_ = true;
  }
  return ret;
}

int ObDDLEpochMgr::get_ddl_epoch(int64_t &ddl_epoch)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLEpochMgr not init", KR(ret));
  } else {
    SpinRLockGuard guard(lock_);
    int find = false;
    for (int i = 0; i < ddl_epoch_stat_.count(); i++) {
      {
        ddl_epoch = ddl_epoch_stat_.at(i).ddl_epoch_;
        find = true;
        break;
      }
    }
    if (!find) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("not found ddl epoch", KR(ret));
    }
  }
  return ret;
}

int ObDDLEpochMgr::remove_ddl_epoch()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLEpochMgr not init", KR(ret));
  } else {
    SpinWLockGuard guard(lock_);
    LOG_INFO("remove_ddl_epoch", K(ddl_epoch_stat_));
    for (int i = 0; i < ddl_epoch_stat_.count(); i++) {
      {
        if (OB_FAIL(ddl_epoch_stat_.remove(i))) {
          LOG_WARN("remove_ddl_epoch", KR(ret));
        }
        break;
      }
    }
  }
  return ret;
}

int ObDDLEpochMgr::remove_all_ddl_epoch()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLEpochMgr not init", KR(ret));
  } else {
    SpinWLockGuard guard(lock_);
    LOG_INFO("remove_all_ddl_epoch", K(ddl_epoch_stat_));
    ddl_epoch_stat_.reuse();
  }
  return ret;
}

int ObDDLEpochMgr::update_ddl_epoch_(const int64_t ddl_epoch)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(lock_);
  int find = false;
  for (int i = 0; i < ddl_epoch_stat_.count(); i++) {
    {
      if (ddl_epoch > ddl_epoch_stat_.at(i).ddl_epoch_) {
        ddl_epoch_stat_.at(i).ddl_epoch_ = ddl_epoch;
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("update_ddl_epoch fail", KR(ret), K(ddl_epoch), K(ddl_epoch_stat_.at(i).ddl_epoch_));
      }
      find = true;
      break;
    }
  }
  if (OB_SUCC(ret) && !find) {
    if (OB_FAIL(ddl_epoch_stat_.push_back(ObDDLEpoch{ddl_epoch}))) {
      LOG_WARN("update_ddl_epoch", KR(ret), K(ddl_epoch));
    }
  }
  return ret;
}

int ObDDLEpochMgr::promote_ddl_epoch(int64_t wait_us, int64_t &ddl_epoch_ret)
{
  int ret = OB_SUCCESS;

  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLEpochMgr not init", KR(ret));
  } else {
    bool locked = false;
    int64_t start_time = ObTimeUtility::current_time();
    while (OB_SUCC(ret) && ObTimeUtility::current_time() - start_time < wait_us) {
      if (lock_for_promote_.try_wrlock()) {
        locked = true;
        break;
      } else {
        ob_usleep(10 * 1000);
      }
    }
    if (locked) {
      int64_t ddl_epoch_tmp = 0;
      bool need_promote = false;
      if (OB_FAIL(get_ddl_epoch(ddl_epoch_tmp))) {
        if (ret == OB_ENTRY_NOT_EXIST) {
          ret = OB_SUCCESS;
          need_promote = true;
        } else {
          LOG_WARN("get_ddl_epoch fail", KR(ret));
        }
      } else {
        ddl_epoch_ret = ddl_epoch_tmp;
      }
      if (OB_SUCC(ret) && need_promote) {
        int64_t new_ddl_epoch = 0;
        // promote ddl_epoch in inner_table
        if (OB_FAIL(promote_ddl_epoch_inner_( new_ddl_epoch))) {
          LOG_WARN("promote_ddl_epoch_inner fail", KR(ret));
        }
        // refresh schema to promise schema_version newest
        else if (OB_FAIL(schema_service_->refresh_and_add_schema())) {
          LOG_WARN("refresh_runtime_schema fail", KR(ret));
        }
        // update ddl epoch
        else if (OB_FAIL(update_ddl_epoch_(new_ddl_epoch))) {
          LOG_WARN("update_ddl_epoch fail", KR(ret), K(new_ddl_epoch));
        } else {
          ddl_epoch_ret = new_ddl_epoch;
          LOG_INFO("promote ddl epoch", K(new_ddl_epoch));
        }
      }
      lock_for_promote_.unlock();
    } else {
      ret = OB_TIMEOUT;
      LOG_WARN("promote_epoch fail", KR(ret));
    }
  }
  return ret;
}

int ObDDLEpochMgr::promote_ddl_epoch_inner_(int64_t &new_ddl_epoch)
{
  int ret = OB_SUCCESS;
  // bugfix: 52360960
  // This function is called once after the management service restarts.
  // To simplify related logic and mangement of memory, single mutex lock will be used.
  lib::ObMutexGuard mutex_guard(mutex_for_promote_);
  ObMySQLTransaction trans;
  int64_t ddl_epoch_tmp = 0;
  observer::ObInnerSQLConnection *conn = NULL;
  ObGlobalStatProxy proxy(trans);
  int64_t timeout = 1 * 1000 * 1000;
  if (OB_FAIL(trans.start(sql_proxy_))) {
    LOG_WARN("trans start fail", KR(ret));
  } else if (OB_FAIL(ObGlobalStatProxy::select_ddl_epoch_for_update(trans, ddl_epoch_tmp))) {
    LOG_WARN("update ddl epoch", KR(ret));
    // Compatibility
    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
      if (OB_FAIL(proxy.set_ddl_epoch(1, false))) {
        LOG_WARN("set_ddl_epoch fail", KR(ret));
      } else {
        ddl_epoch_tmp = 1;
      }
    }
  } else if (FALSE_IT(ddl_epoch_tmp = ddl_epoch_tmp + 1)) {
  } else if (OB_FAIL(proxy.set_ddl_epoch(ddl_epoch_tmp))) {
    LOG_WARN("update ddl epoch", KR(ret));
  }
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      ret = ((OB_SUCC(ret)) ? tmp_ret : ret);
      LOG_WARN("fail to end trans", "is_commit", OB_SUCCESS == ret, KR(tmp_ret));
    }
  }
  if (OB_SUCC(ret)) {
    new_ddl_epoch = ddl_epoch_tmp;
  }
  return ret;
}

int ObDDLEpochMgr::check_and_lock_ddl_epoch(
    common::ObMySQLTransaction &trans,
    const int64_t ddl_epoch_local)
{
  int ret = OB_SUCCESS;
  int64_t ddl_epoch_core = 0;
  // bugfix: 52360960
  // Parallel ddl trans will commit serially controlled by wait_task_ready().
  // So, we don't protect parallel ddl trans commit by lock.
  ObGlobalStatProxy global_stat_proxy(trans);
  if (OB_FAIL(global_stat_proxy.select_ddl_epoch_for_update(trans, ddl_epoch_core))) {
    LOG_WARN("fail to get ddl epoch from inner table", KR(ret));
    if (OB_ERR_NULL_VALUE == ret) {
      // ignore ret
      (void) remove_ddl_epoch();
    }
  } else {
    if (ddl_epoch_local == ddl_epoch_core) {
    } else {
      ret = OB_RS_NOT_MASTER;
      LOG_WARN("ddl epoch changed", KR(ret), K(ddl_epoch_local), K(ddl_epoch_core));
      // ignore ret
      (void) remove_ddl_epoch();
    }
  }
  return ret;
}

} // end schema
} // end share
} // end oceanbase
