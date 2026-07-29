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

#define USING_LOG_PREFIX SQL_SESSION

#include "sql/session/ob_user_resource_mgr.h"
#include "ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
namespace oceanbase {
namespace sql {

static const char *MEMORY_LABEL = "UserResourceMgr";


void ObConnectResAlloc::free_value(ObConnectResource* tz_info)
{
  OB_DELETE(ObConnectResource, MEMORY_LABEL, tz_info);
  tz_info = NULL;
}

ObConnectResHashNode* ObConnectResAlloc::alloc_node(ObConnectResource* value)
{
  
  ObMemAttr attr(MEMORY_LABEL);
  return OB_NEW(ObConnectResHashNode, attr);
}

void ObConnectResAlloc::free_node(ObConnectResHashNode* node)
{
  if (NULL != node) {
    OB_DELETE(ObConnectResHashNode, MEMORY_LABEL, node);
    node = NULL;
  }
}

ObConnectResourceMgr::ObConnectResourceMgr()
: inited_(false), user_res_map_(), server_res_inited_(false), schema_service_(nullptr),
  timer_(nullptr),
  cleanup_task_(*this)
{
}

ObConnectResourceMgr::~ObConnectResourceMgr()
{}

int ObConnectResourceMgr::init(ObMultiVersionSchemaService &schema_service, common::ObTimer &timer)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_FAIL(user_res_map_.init("UserResCtrl"))) {
    LOG_WARN("fail to init user resource map", K(ret));
  } else {
    schema_service_ = &schema_service;
    timer_ = &timer;
    inited_ = true;
    const int64_t delay = ConnResourceCleanUpTask::SLEEP_USECONDS;
    const bool repeat = false;
    if (OB_FAIL(timer_->schedule(cleanup_task_, delay, repeat))) {
      LOG_WARN("schedual connect resource mgr failed", K(ret));
    }
  }
  return ret;
}

int ObConnectResourceMgr::apply_for_server_conn_resource(const ObPrivSet &priv,
                                                         const uint64_t max_connections)
{
  int ret = OB_SUCCESS;
  ObLatchWGuard wr_guard(server_res_.rwlock_, ObLatchIds::DEFAULT_MUTEX);
  server_res_inited_ = true;
  if (server_res_.cur_connections_ < max_connections
      || (max_connections == server_res_.cur_connections_
          && OB_PRIV_HAS_ANY(priv, OB_PRIV_SUPER))) {
    // A user with SUPER privilege may connect when the server reaches max_connections.
    server_res_.cur_connections_++;
  } else {
    ret = OB_ERR_CON_COUNT_ERROR;
    LOG_WARN("too many connections", K(ret), K(server_res_.cur_connections_),
      K(max_connections));
  }
  return ret;
}

void ObConnectResourceMgr::release_server_conn_resource()
{
  int ret = OB_SUCCESS;
  ObLatchWGuard wr_guard(server_res_.rwlock_, ObLatchIds::DEFAULT_MUTEX);
  if (OB_UNLIKELY(!server_res_inited_)) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("release server connection resource before any apply", K(ret));
  } else if (OB_UNLIKELY(0 == server_res_.cur_connections_)) {
    LOG_ERROR("server current connections is zero when releasing resource");
  } else {
    server_res_.cur_connections_--;
  }
}

// get user resource from hash map, insert if not exist.
int ObConnectResourceMgr::get_or_insert_user_resource(const uint64_t user_id,
      const uint64_t max_user_connections,
      const uint64_t max_connections_per_hour,
      ObConnectResource *&user_res)
{
  int ret = OB_SUCCESS;
  user_res = NULL;
  ObUserKey user_key(user_id);
  if (OB_FAIL(user_res_map_.get(user_key, user_res))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      // not exist, alloc and insert
      ObMemAttr attr(MEMORY_LABEL);
      if (OB_ISNULL(user_res = OB_NEW(ObConnectResource, attr))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate user resource failed", K(ret));
      } else {
        user_res->cur_connections_ = 0;
        user_res->history_connections_ = 0;
        user_res->start_time_ = 0;
        
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(user_res_map_.insert_and_get(user_key, user_res))) {
        LOG_WARN("insert and get failed", K(ret));
        OB_DELETE(ObConnectResource, MEMORY_LABEL, user_res);
        user_res = NULL;
        // 1. user resouce already exist because of concurrent insert, just get it.
        // 2. may also fail because of oom.
        if (OB_ENTRY_EXIST == ret && OB_FAIL(user_res_map_.get(user_key, user_res))) {
          // may happen with very very little probability: insert failed and then user is dropped
          // and value in the map is deleted by periodly task.
          LOG_WARN("user not exists", K(ret));
        }
      }
    } else {
      LOG_WARN("get user resource failed", K(ret));
    }
  }
  return ret;
}

int ObConnectResourceMgr::increase_user_connections_count(
      const uint64_t max_user_connections,
      const uint64_t max_connections_per_hour,
      const ObString &user_name,
      ObConnectResource *user_res,
      bool &user_conn_increased)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(user_res)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("user resource is null", K(ret));
  } else {
    const static int64_t usec_per_hour = static_cast<int64_t>(1000000) * 3600;
    // check and update cur_connections and connections in one hour.
    ObLatchWGuard wr_guard(user_res->rwlock_, ObLatchIds::DEFAULT_MUTEX);
    if (0 != max_connections_per_hour) {
      int64_t cur_time = ObTimeUtility::current_time();
      if (cur_time - user_res->start_time_ > usec_per_hour) {
        user_res->start_time_ = cur_time;
        user_res->history_connections_ = 0;
      } else if (user_res->history_connections_ >= max_connections_per_hour) {
        ret = OB_ERR_USER_EXCEED_RESOURCE;
        LOG_WARN("user exceed max connections per hour", K(ret), KPC(user_res));
        LOG_USER_ERROR(OB_ERR_USER_EXCEED_RESOURCE, user_name.length(), user_name.ptr(),
                "max_connections_per_hour", user_res->history_connections_);
      }
    }
    if (OB_SUCC(ret) && 0 != max_user_connections) {
      if (user_res->cur_connections_ >= max_user_connections) {
        ret = OB_ERR_USER_EXCEED_RESOURCE;
        LOG_WARN("user exceed max user connections", K(ret), KPC(user_res));
        LOG_USER_ERROR(OB_ERR_USER_EXCEED_RESOURCE, user_name.length(), user_name.ptr(),
                "max_user_connections", user_res->cur_connections_);
      }
    }
    if (OB_SUCC(ret)) {
      user_res->history_connections_ += 0 == max_connections_per_hour ? 0 : 1;
      user_res->cur_connections_ += 0 == max_user_connections ? 0 : 1;
      user_conn_increased = 0 != max_user_connections;
    }
  }
  return ret;
}

// max_connections: max connections per hour.
// max_user_connections: max concurrent connections.
// 0 means no limit.
int ObConnectResourceMgr::on_user_connect(
      const uint64_t user_id,
      const ObPrivSet &priv,
      const ObString &user_name,
      const uint64_t max_connections_per_hour,
      const uint64_t max_user_connections,
      const uint64_t max_server_connections,
      ObSQLSessionInfo& session)
{
  int ret = OB_SUCCESS;
  if (!session.is_user_session()) {
    // do not limit connection count for inner sesion.
  } else {
    if (!session.has_got_server_conn_res()) {
      if (OB_FAIL(apply_for_server_conn_resource(priv, max_server_connections))) {
        LOG_WARN("server reached max_connections", K(ret));
      } else {
        session.set_got_server_conn_res(true);
      }
    }
    if (OB_FAIL(ret)) {
    } else if (session.has_got_user_conn_res()) {
    } else if (0 == max_connections_per_hour && 0 == max_user_connections) {
    } else {
      // According to document of MySQL:
      // "Resource-use counting takes place when any account has a nonzero limit placed on its use of any of the resources."
      // only increase cur_connections_ if max_user_connections is not zero
      // only record history_connections_ if max_connections_per_hour is not zero.
      ObConnectResource *user_res = NULL;
      bool user_conn_increased = false;
      if (OB_FAIL(get_or_insert_user_resource( user_id, max_user_connections,
                                              max_connections_per_hour, user_res))) {
        LOG_WARN("get or insert user resource failed", K(ret));
      } else if (OB_FAIL(increase_user_connections_count(max_user_connections, max_connections_per_hour,
            user_name, user_res, user_conn_increased))) {
        LOG_WARN("increase user connection count failed", K(ret));
      }
      if (user_conn_increased) {
        session.set_got_user_conn_res(true);
        session.set_conn_res_user_id(user_id);
      }
      if (OB_NOT_NULL(user_res)) {
        user_res_map_.revert(user_res);
        user_res = NULL;
      }
    }
  }
  return ret;
}

// Whether need decrease cur_connections_, that's a question.
// It depends on whether cur_connections_ is increased when create the connection.
// Since max_user_connections is only allowed to be modified globally, which means it remains
// unchanged from connection to disconnection, we can use it decide whether decrease cur_connections_.
int ObConnectResourceMgr::on_user_disconnect(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  if (!session.is_user_session()) {
    // do not limit connection count for inner sesion.
    if (OB_UNLIKELY(session.has_got_server_conn_res() || session.has_got_user_conn_res())) {
      LOG_ERROR("inner session expect no connection resource", K(session.get_conn_res_user_id()),
                K(session.has_got_server_conn_res()));
    }
  } else {
    
    if (session.has_got_server_conn_res()) {
      release_server_conn_resource();
      session.set_got_server_conn_res(false);
    }
    if (session.has_got_user_conn_res()) {
      uint64_t user_id = session.get_conn_res_user_id();
      ObConnectResource *user_res = NULL;
      ObUserKey user_key(user_id);
      if (OB_FAIL(user_res_map_.get(user_key, user_res))) {
        // maybe already dropped.
        ret = OB_SUCCESS;
      } else if (OB_ISNULL(user_res)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("user resource is null", K(ret));
      } else {
        ObLatchWGuard wr_guard(user_res->rwlock_, ObLatchIds::DEFAULT_MUTEX);
        if (OB_UNLIKELY(0 == user_res->cur_connections_)) {
          LOG_ERROR("current connections is zero when disconnect", K(user_id));
        } else {
          user_res->cur_connections_--;
        }
        user_res_map_.revert(user_res);
      }
      session.set_got_user_conn_res(false);
    }
  }
  return ret;
}

bool ObConnectResourceMgr::CleanUpConnResourceFunc::operator() (
    ObUserKey key, ObConnectResource *conn_res)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn_res)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("user res is NULL", K(ret), K(conn_res));
  } else {
    const ObUserInfo *user_info = NULL;
    if (OB_FAIL(schema_guard_.get_user_info(key.user_id_, user_info))) {
      if (OB_RUNTIME_SCHEMA_NOT_READY != ret) {
        LOG_ERROR("get user info failed", K(ret), K(key.user_id_));
      } else {
        ret = OB_SUCCESS;
        conn_res_map_.del(key);
      }
    } else if (OB_ISNULL(user_info)) {
      conn_res_map_.del(key);
    }
  }
  return OB_SUCCESS == ret;
}

// task for cleanup periodly. Remove dropped user from user_res_map_.
void ObConnectResourceMgr::ConnResourceCleanUpTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(conn_res_mgr_.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("schema service is null", K(ret));
  } else if (OB_FAIL(conn_res_mgr_.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get runtime schema guard failed", K(ret));
  } else {
    LOG_INFO("clean up connection resource",
              K(conn_res_mgr_.user_res_map_.size()), K(conn_res_mgr_.server_res_inited_));
    CleanUpConnResourceFunc user_func(schema_guard, conn_res_mgr_.user_res_map_);
    if (OB_FAIL(conn_res_mgr_.user_res_map_.for_each(user_func))) {
      LOG_WARN("cleanup dropped user failed", K(ret));
    }
  }
  const int64_t delay = SLEEP_USECONDS;
  const bool repeat = false;
  if (OB_SUCC(ret) && OB_ISNULL(conn_res_mgr_.timer_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("connect resource cleanup timer is null", K(ret));
  } else if (OB_SUCC(ret) && OB_FAIL(conn_res_mgr_.timer_->schedule(*this, delay, repeat))) {
    LOG_ERROR("schedule connect resource cleanup task failed", K(ret));
  }
}

}  // namespace sql
}  // namespace oceanbase
