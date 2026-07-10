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

#ifndef _OCEABASE_OBSERVER_OB_SRV_DELIVER_H_
#define _OCEABASE_OBSERVER_OB_SRV_DELIVER_H_

#include "lib/thread/ob_thread_name.h"
#include "lib/thread/thread_mgr_interface.h"
#include "rpc/frame/ob_req_deliver.h"
#include "share/ob_thread_pool.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{

namespace omt
{
class ObTenant;
} // end of namespace omt
namespace rpc
{
namespace frame
{
class ObReqSessionHandler;
}
} // end of namespace rpc

namespace observer
{

using rpc::frame::ObReqQueue;
using rpc::frame::ObiReqQHandler;
using rpc::frame::ObReqSessionHandler;

class QueueThread
{
public:
  QueueThread(const char *thread_name = nullptr,
              uint64_t group_id = 0)
      : thread_(queue_, thread_name, group_id), tg_id_(-1), n_thread_(0) {}

  ~QueueThread() { destroy(); }

  int init() { return queue_.init(); }

public:
  int set_thread_count(int thread_cnt) {
    int ret = OB_SUCCESS;
    if (thread_cnt != n_thread_) {
      ret = TG_SET_THREAD_CNT(tg_id_, thread_cnt);
      n_thread_ = thread_cnt;
    }
    return ret;
  }
  void stop()
  {
    if (tg_id_ != -1) {
      TG_STOP(tg_id_);
    }
  }
  void wait()
  {
    if (tg_id_ != -1) {
      TG_WAIT(tg_id_);
    }
  }
  void destroy()
  {
    if (tg_id_ != -1) {
      TG_DESTROY(tg_id_);
      tg_id_ = -1;
    }
  }
  class Thread : public lib::TGRunnable {
  public:
    Thread(ObReqQueue &queue, const char *thread_name, const uint64_t group_id)
        : queue_(queue), thread_name_(thread_name), group_id_(group_id) {}
    void run1()
    {
      if (thread_name_ != nullptr) {
        lib::set_thread_name(thread_name_, get_thread_idx());
      }
      queue_.loop();
    }

  private:
    ObReqQueue &queue_;
    const char *thread_name_;
    
    const uint64_t group_id_;
  } thread_;
  ObReqQueue queue_;
  int tg_id_;

private:
  
  int n_thread_;
};

class ObSrvDeliver
    : public rpc::frame::ObReqQDeliver
{
public:
  ObSrvDeliver(ObiReqQHandler &qhandler,
               ObReqSessionHandler &session_handler,
               ObGlobalContext &gctx);

  int init();
  void stop();

  int repost(void* node);
  virtual int deliver(rpc::ObRequest &req);
  void set_host(const common::ObAddr &host) { host_ = host; }
  int create_queue_thread(int tg_id, const char *thread_name, QueueThread *&qthread);
private:
  int init_queue_threads();


  int deliver_mysql_request(rpc::ObRequest &req);

private:
  bool is_inited_;
  bool stop_;
  common::ObAddr host_;
  QueueThread *ddl_queue_;
  QueueThread *ddl_parallel_queue_;
  QueueThread *diagnose_queue_;
  ObReqSessionHandler &session_handler_;
  ObGlobalContext &gctx_;
  DISALLOW_COPY_AND_ASSIGN(ObSrvDeliver);

public:
  static const int64_t MAX_QUEUE_LEN = 10000;
  static const int LEASE_TASK_THREAD_CNT = 3;
  static const int MINI_MODE_LEASE_TASK_THREAD_CNT = 1;
  static const int DDL_TASK_THREAD_CNT = 1;
  static const int MYSQL_TASK_THREAD_CNT = 6;
  static const int MINI_MODE_MYSQL_TASK_THREAD_CNT = 1;
  static const int MYSQL_DIAG_TASK_THREAD_CNT = 2;
  static const int MINI_MODE_MYSQL_DIAG_TASK_THREAD_CNT = 1;
}; // end of class ObSrvDeliver

} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OCEABASE_OBSERVER_OB_SRV_DELIVER_H_ */
