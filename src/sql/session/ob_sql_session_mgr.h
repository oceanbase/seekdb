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

#ifndef _OCEABASE_SQL_SESSION_OB_SQL_SESSION_MGR_H_
#define _OCEABASE_SQL_SESSION_OB_SQL_SESSION_MGR_H_

#include "lib/container/ob_concurrent_bitset.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/ob_end_trans_callback.h"

namespace oceanbase
{
namespace observer
{
struct ObSMConnection;
}
namespace sql
{

class ObFreeSessionCtx
{
public:
  ObFreeSessionCtx() :
    has_inc_active_num_(false),
    sessid_(0)
  {
  }
  ~ObFreeSessionCtx() {}
  VIRTUAL_TO_STRING_KV(K_(has_inc_active_num), K_(sessid));
  bool has_inc_active_num_;
  uint32_t sessid_;
};

class ObSQLSessionMgr : public common::ObTimerTask
{
public:
  static const int64_t SCHEDULE_PERIOD = 1000*1000*5; //5s
  static const uint32_t MAX_VERSION = UINT8_MAX;//255
  static const int64_t BUCKET_COUNT = 1024;
  typedef SessionInfoKey Key;
  typedef common::hash::ObHashMap<uint64_t, ObSQLSessionInfo*> SessionMap;
  explicit ObSQLSessionMgr():
      //null_callback_(),
      sessinfo_map_(),
      next_sessid_(1)
  {
  }
  virtual ~ObSQLSessionMgr(){}

  int init();

  /**
   * @brief create a new ObSQLSessionInfo, and its session id is sessid
   * @param conn : connection information
   * @param sess_info : point to the ObSQLSessionInfo that the function created; used as value
   */
  void destroy();
  int create_session(observer::ObSMConnection *conn, ObSQLSessionInfo *&sess_info);
  // create session by session id.
  // need call revert_session if return success.
  int create_session(const uint32_t sessid, ObSQLSessionInfo *&session_info);

  /**
   * @brief get the ObSQLSessioninfo
   * @param sessid : session id; used as key
   * @param sess_info : point to the ObSQLSessionInfo that the function get; used as value
   */
  int get_session(uint32_t sessid, ObSQLSessionInfo *&sess_info);
  int inc_session_ref(const ObSQLSessionInfo *my_session);
  int free_session(const ObFreeSessionCtx &ctx);

  int get_session_count(int64_t &sess_cnt);

  /**
   * @brief if you create or get session successfully, you must call
   *        this function after using session
   *
   * @param sess_info : the session that you want to revert
   */
  void revert_session(ObSQLSessionInfo *sess_info);

  /**
   * @brief use the function to traverse all session
   * @param fn : it can be a pointer of the function or function object
   */
  template <typename Function>
  int for_each_session(Function &fn);

  template <typename Function>
  int for_each_hold_session(Function &fn);

  int kill_query(ObSQLSessionInfo &session);
  int set_query_deadlocked(ObSQLSessionInfo &session);
  static int kill_query(ObSQLSessionInfo &session,
      const ObSQLSessionState status);
  int kill_idle_timeout_tx(ObSQLSessionInfo *session);
  int kill_deadlock_tx(ObSQLSessionInfo *session);
  int kill_session(ObSQLSessionInfo &session);
  int disconnect_session(ObSQLSessionInfo &session);

  // kill all sessions from this tenant.
  int kill_tenant();

  /**
   * @brief timing clean time out session
   */
  virtual void runTimerTask();
  void try_check_session();

  // get min active snapshot version for all session
  int get_min_active_snapshot_version(share::SCN &snapshot_version);

  //used for guarantee the unique sessid when observer generates sessid
  int create_sessid(uint32_t &sessid);
  //inline ObNullEndTransCallback &get_null_callback() { return null_callback_; }
  SessionMap &get_sess_hold_map() { return sess_hold_map_; }
private:
  int check_session_leak();

  class ValueAlloc
  {
  public:
    ValueAlloc()
      : alloc_total_count_(0),
        free_total_count_(0)
    {}
    ~ValueAlloc() {}
    int clean_tenant();
    ObSQLSessionInfo* alloc_value();
    void free_value(ObSQLSessionInfo *sess);
    SessionInfoHashNode* alloc_node(ObSQLSessionInfo* value)
    {
      UNUSED(value);
      return op_alloc(SessionInfoHashNode);
    }
    void free_node(SessionInfoHashNode* node)
    {
      if (NULL != node) {
        op_free(node);
        node = NULL;
      }
    }
  private:
    volatile int64_t alloc_total_count_;
    volatile int64_t free_total_count_;
    static const int64_t MAX_REUSE_COUNT = 10000;
    static const int64_t MAX_SYS_VAR_MEM = 256 * 1024;
  };

  typedef common::ObTenantLinkHashMap<Key, ObSQLSessionInfo, ValueAlloc> HashMap;

  struct DumpHoldSession
  {
    DumpHoldSession() {}
    int operator()(common::hash::HashMapPair<uint64_t, ObSQLSessionInfo *> &entry);
  };

  class CheckSessionFunctor
  {
  public:
    CheckSessionFunctor() : sess_mgr_(NULL) {}
    explicit CheckSessionFunctor(ObSQLSessionMgr *sess_mgr): sess_mgr_(sess_mgr) {}
    virtual ~CheckSessionFunctor(){}
    bool operator()(sql::ObSQLSessionMgr::Key key, ObSQLSessionInfo *sess_info);
  private:
    ObSQLSessionMgr *sess_mgr_;
  };

  class KillTenant
  {
  public:
    explicit KillTenant(ObSQLSessionMgr *mgr) :
      ret_(common::OB_SUCCESS), mgr_(mgr)
    {}
    bool operator()(sql::ObSQLSessionMgr::Key key, ObSQLSessionInfo *sess_info);
    int get_ret_code()
    {
      return ret_;
    }

  private:
    int ret_;
    ObSQLSessionMgr *mgr_;
  };

private:
  // Note: Must be defined before session_map_, depends on the order of destruction.
  //ObNullEndTransCallback null_callback_;
  // used for manage ObSQLSessionInfo
  HashMap sessinfo_map_;
  // Monotonically increasing session id allocator. Wraps around at UINT32_MAX, skips 0.
  uint32_t next_sessid_ CACHE_ALIGNED;
  SessionMap sess_hold_map_;
  DISALLOW_COPY_AND_ASSIGN(ObSQLSessionMgr);
}; // end of class ObSQLSessionMgr

template <typename Function>
int ObSQLSessionMgr::for_each_session(Function &fn)
{
  return sessinfo_map_.for_each(fn);
}

template <typename Function>
int ObSQLSessionMgr::for_each_hold_session(Function &fn)
{
  return get_sess_hold_map().foreach_refactored(fn);
}

inline int ObSQLSessionMgr::get_session(uint32_t sessid, ObSQLSessionInfo *&sess_info)
{
  int ret = sessinfo_map_.get(Key(sessid), sess_info);
  const bool v = GCONF._enable_trace_session_leak;
  if (OB_UNLIKELY(v)) {
    if (OB_LIKELY(sess_info)) {
      sess_info->on_get_session();
    }
  }
  return ret;
}

inline void ObSQLSessionMgr::revert_session(ObSQLSessionInfo *sess_info)
{
  const bool v = GCONF._enable_trace_session_leak;
  if (OB_UNLIKELY(v)) {
    if (OB_LIKELY(nullptr != sess_info)) {
      sess_info->on_revert_session();
    }
  }
  sessinfo_map_.revert(sess_info);
}

inline int ObSQLSessionMgr::get_session_count(int64_t &sess_cnt)
{
  sess_cnt = sessinfo_map_.count();
  return 0;
}


class ObSessionGetterGuard
{
public:
  explicit ObSessionGetterGuard(ObSQLSessionMgr &sess_mgr, uint32_t sessid);
  ~ObSessionGetterGuard();
  inline int get_session(ObSQLSessionInfo *&session)
  {
    session = session_;
    return ret_;
  }
private:
  int ret_;
  ObSQLSessionMgr &mgr_;
  ObSQLSessionInfo *session_;
};

class ObTenantSQLSessionMgr
{
public:
  explicit ObTenantSQLSessionMgr();
  ~ObTenantSQLSessionMgr();

  int init();
  void destroy();
  static int mtl_new(ObTenantSQLSessionMgr *&tenant_session_mgr);
  static int mtl_init(ObTenantSQLSessionMgr *&tenant_session_mgr);
  static void mtl_wait(ObTenantSQLSessionMgr *&tenant_session_mgr);
  static void mtl_destroy(ObTenantSQLSessionMgr *&tenant_session_mgr);
  ObSQLSessionInfo *alloc_session();
  void free_session(ObSQLSessionInfo *session);
  void clean_session_pool();
  int64_t count() const { return ATOMIC_LOAD(&count_); }
  uint64_t get_sql_plan_flush_epoch() const
  {
    return ATOMIC_LOAD(&sql_plan_flush_epoch_);
  }
  uint64_t inc_sql_plan_flush_epoch()
  {
    return ATOMIC_AAF(&sql_plan_flush_epoch_, 1);
  }
  ObCacheObjID alloc_sql_plan_id()
  {
    return ATOMIC_AAF(&next_sql_plan_id_, 1);
  }
  volatile ObCacheObjID *get_sql_plan_id_counter()
  {
    return &next_sql_plan_id_;
  }
private:
  class SessionPool
  {
  public:
    static const int64_t POOL_CAPACIPY = 32;
  public:
    SessionPool();
    int init(const int64_t capacity);
    int pop_session(ObSQLSessionInfo *&session);
    int push_session(ObSQLSessionInfo *&session);
    int64_t count() const;
    TO_STRING_KV(K(session_pool_.capacity()),
                 K(session_pool_.get_total()),
                 K(session_pool_.get_free()));
  private:
    ObSQLSessionInfo *session_array_[POOL_CAPACIPY];
    common::ObFixedQueue<ObSQLSessionInfo> session_pool_;
  };
private:
  
  SessionPool session_pool_;
  int64_t count_;
  volatile uint64_t sql_plan_flush_epoch_;
  volatile ObCacheObjID next_sql_plan_id_;
  ObFixedClassAllocator<ObSQLSessionInfo> session_allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObTenantSQLSessionMgr);
}; // end of class ObSQLSessionMgr

} // end of namespace sql
} // end of namespace oceanbase

#endif /* _OCEABASE_SQL_SESSION_OB_SQL_SESSION_MGR_H_ */
