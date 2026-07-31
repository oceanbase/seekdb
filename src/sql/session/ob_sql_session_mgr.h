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
  explicit ObSQLSessionMgr();
  virtual ~ObSQLSessionMgr();

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

  int kill_query(ObSQLSessionInfo &session);
  int set_query_deadlocked(ObSQLSessionInfo &session);
  static int kill_query(ObSQLSessionInfo &session,
      const ObSQLSessionState status);
  int kill_idle_timeout_tx(ObSQLSessionInfo *session);
  int kill_deadlock_tx(ObSQLSessionInfo *session);
  int kill_session(ObSQLSessionInfo &session);
  int disconnect_session(ObSQLSessionInfo &session);

  // Kill every active session during server shutdown.
  int kill_all_sessions(bool force_kill);
  void wait_sessions_drained();

  /**
   * @brief timing clean time out session
   */
  virtual void runTimerTask();
  void try_check_session();

  // get min active snapshot version for all session
  int get_min_active_snapshot_version(share::SCN &snapshot_version);

  //used for guarantee the unique sessid when observer generates sessid
  int create_sessid(uint32_t &sessid);
private:
  class ValueAlloc
  {
  public:
    ValueAlloc()
      : session_allocator_(lib::ObMemAttr("SQLSessionInfo"), common::get_cpu_count(), 4),
        active_count_(0),
        alloc_total_count_(0),
        free_total_count_(0)
    {}
    ~ValueAlloc() {}
    ObSQLSessionInfo* alloc_value();
    void free_value(ObSQLSessionInfo *sess);
    int64_t count() const { return ATOMIC_LOAD(&active_count_); }
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
    common::ObFixedClassAllocator<ObSQLSessionInfo> session_allocator_;
    int64_t active_count_;
    volatile int64_t alloc_total_count_;
    volatile int64_t free_total_count_;
  };

#ifdef OB_USE_ASAN
  typedef common::ObAllocatingLinkHashMap<Key, ObSQLSessionInfo, ValueAlloc,
                                          common::ZeroRefHandle> HashMap;
#else
  typedef common::ObAllocatingLinkHashMap<Key, ObSQLSessionInfo, ValueAlloc> HashMap;
#endif

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

  class KillAllSessions
  {
  public:
    KillAllSessions(ObSQLSessionMgr *mgr, bool force_kill) :
      ret_(common::OB_SUCCESS), mgr_(mgr), force_kill_(force_kill)
    {}
    bool operator()(sql::ObSQLSessionMgr::Key key, ObSQLSessionInfo *sess_info);
    int get_ret_code()
    {
      return ret_;
    }

  private:
    int ret_;
    ObSQLSessionMgr *mgr_;
    bool force_kill_;
  };

private:
  // used for manage ObSQLSessionInfo
  HashMap sessinfo_map_;
  // Monotonically increasing session id allocator. Wraps around at UINT32_MAX, skips 0.
  uint32_t next_sessid_ CACHE_ALIGNED;
  DISALLOW_COPY_AND_ASSIGN(ObSQLSessionMgr);
}; // end of class ObSQLSessionMgr

template <typename Function>
int ObSQLSessionMgr::for_each_session(Function &fn)
{
  return sessinfo_map_.for_each(fn);
}

inline int ObSQLSessionMgr::get_session(uint32_t sessid, ObSQLSessionInfo *&sess_info)
{
  return sessinfo_map_.get(Key(sessid), sess_info);
}

inline void ObSQLSessionMgr::revert_session(ObSQLSessionInfo *sess_info)
{
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

} // end of namespace sql
} // end of namespace oceanbase

#endif /* _OCEABASE_SQL_SESSION_OB_SQL_SESSION_MGR_H_ */
