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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_SESSION_PLAN_CACHE_UTILS_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_SESSION_PLAN_CACHE_UTILS_H_

#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/session/ob_sql_session_mgr.h"

namespace oceanbase
{
namespace observer
{

struct ObSessionPlanCacheEntry
{
  ObSessionPlanCacheEntry()
    : session_id_(0), object_id_(common::OB_INVALID_ID)
  {}
  ObSessionPlanCacheEntry(const uint32_t session_id,
                          const sql::ObCacheObjID object_id)
    : session_id_(session_id), object_id_(object_id)
  {}

  uint32_t session_id_;
  sql::ObCacheObjID object_id_;
  TO_STRING_KV(K_(session_id), K_(object_id));
};

class ObSessionPlanCacheLockGuard
{
public:
  explicit ObSessionPlanCacheLockGuard(sql::ObSQLSessionInfo &session)
    : mutex_(session.get_sql_plan_cache_mutex()),
      lock_ret_(mutex_.lock()),
      is_locked_(common::OB_SUCCESS == lock_ret_)
  {}

  ~ObSessionPlanCacheLockGuard()
  {
    if (is_locked_) {
      const int tmp_ret = mutex_.unlock();
      if (common::OB_SUCCESS != tmp_ret) {
        SERVER_LOG_RET(WARN, tmp_ret,
                       "failed to unlock session plan cache",
                       K(tmp_ret));
      }
    }
  }

  int get_lock_ret() const { return lock_ret_; }
  bool is_locked() const { return is_locked_; }

private:
  lib::ObMutex &mutex_;
  int lock_ret_;
  bool is_locked_;
  DISALLOW_COPY_AND_ASSIGN(ObSessionPlanCacheLockGuard);
};

template <typename Function>
class ObForEachSessionPlanCache
{
public:
  explicit ObForEachSessionPlanCache(Function &function)
    : function_(function), ret_(common::OB_SUCCESS)
  {}

  bool operator()(sql::ObSQLSessionMgr::Key key,
                  sql::ObSQLSessionInfo *session)
  {
    UNUSED(key);
    if (OB_ISNULL(session)) {
      ret_ = common::OB_ERR_UNEXPECTED;
      SERVER_LOG_RET(WARN, ret_, "unexpected null session", K_(ret));
    } else {
      // This dedicated lock protects only cache creation/destruction. It lets
      // diagnostics include busy sessions without creating the cross-session
      // query-lock cycle that a blocking query lock would introduce.
      ObSessionPlanCacheLockGuard lock_guard(*session);
      if (common::OB_SUCCESS != lock_guard.get_lock_ret()) {
        ret_ = lock_guard.get_lock_ret();
        SERVER_LOG_RET(WARN, ret_, "failed to lock session plan cache",
                       K_(ret), K(session->get_server_sid()));
      } else {
        sql::ObPlanCache *plan_cache = session->peek_sql_plan_cache();
        if (OB_NOT_NULL(plan_cache)) {
          ret_ = function_(*session, *plan_cache);
        }
      }
    }
    return common::OB_SUCCESS == ret_;
  }

  int get_ret() const { return ret_; }

private:
  Function &function_;
  int ret_;
};

template <typename Function>
int for_each_session_plan_cache(sql::ObSQLSessionMgr *session_mgr,
                                Function &function)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(session_mgr)) {
    ret = common::OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "sql session manager is null", K(ret));
  } else {
    ObForEachSessionPlanCache<Function> op(function);
    const int map_ret = session_mgr->for_each_session(op);
    if (common::OB_SUCCESS != op.get_ret()) {
      ret = op.get_ret();
    } else if (common::OB_SUCCESS != map_ret) {
      ret = map_ret;
      SERVER_LOG(WARN, "failed to traverse sql sessions", K(ret));
    }
  }
  return ret;
}

class ObCollectSessionPlanCacheEntries
{
public:
  explicit ObCollectSessionPlanCacheEntries(
      common::ObIArray<ObSessionPlanCacheEntry> &entries)
    : entries_(entries)
  {}

  int operator()(sql::ObSQLSessionInfo &session,
                 sql::ObPlanCache &plan_cache)
  {
    class CollectObjectIdOp
    {
    public:
      CollectObjectIdOp(const uint32_t session_id,
                        common::ObIArray<ObSessionPlanCacheEntry> &entries)
        : session_id_(session_id), entries_(entries)
      {}

      int operator()(common::hash::HashMapPair<
                     sql::ObCacheObjID,
                     sql::ObILibCacheObject *> &entry)
      {
        int ret = common::OB_SUCCESS;
        if (OB_ISNULL(entry.second)) {
          ret = common::OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "unexpected null cache object", K(ret));
        } else if (sql::ObLibCacheNameSpace::NS_CRSR == entry.second->get_ns()
                   && OB_FAIL(entries_.push_back(
                       ObSessionPlanCacheEntry(session_id_, entry.first)))) {
          SERVER_LOG(WARN, "failed to collect session plan cache entry",
                     K(ret), K_(session_id), K(entry.first));
        }
        return ret;
      }

    private:
      uint32_t session_id_;
      common::ObIArray<ObSessionPlanCacheEntry> &entries_;
    };

    CollectObjectIdOp op(session.get_server_sid(), entries_);
    return plan_cache.foreach_alloc_cache_obj(op);
  }

private:
  common::ObIArray<ObSessionPlanCacheEntry> &entries_;
};

inline int collect_session_plan_cache_entries(
    sql::ObSQLSessionMgr *session_mgr,
    common::ObIArray<ObSessionPlanCacheEntry> &entries)
{
  ObCollectSessionPlanCacheEntries op(entries);
  return for_each_session_plan_cache(session_mgr, op);
}

} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_SESSION_PLAN_CACHE_UTILS_H_
