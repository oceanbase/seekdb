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
#include "rpc/frame/ob_req_deliver.h"
#include "share/ob_thread_pool.h"

namespace oceanbase
{

namespace omt
{
class ObTenant;
} // end of namespace omt

namespace observer
{

using rpc::frame::ObReqQueue;
using rpc::frame::ObiReqQHandler;

class ObDiagnoseQueueThread
{
private:
  class Thread : public lib::ThreadPool
  {
  public:
    explicit Thread(ObReqQueue &queue)
        : lib::ThreadPool(1), queue_(queue) {}
    void run1() override
    {
      lib::set_thread_name("DiagnoseQueueTh", get_thread_idx());
      queue_.loop();
    }

  private:
    ObReqQueue &queue_;
  };

public:
  ObDiagnoseQueueThread()
      : queue_(), thread_(queue_) {}

  ~ObDiagnoseQueueThread() { destroy(); }

  int init(const int64_t thread_cnt, ObiReqQHandler &qhandler)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(thread_.set_thread_count(thread_cnt))) {
    } else {
      queue_.set_qhandler(&qhandler);
    }
    return ret;
  }
  int start() { return thread_.start(); }
  bool push(rpc::ObRequest *req, const int max_queue_len)
  {
    return queue_.push(req, max_queue_len);
  }

  void stop()
  {
    thread_.stop();
  }
  void wait()
  {
    thread_.wait();
  }
  void destroy()
  {
    thread_.stop();
    thread_.wait();
    thread_.destroy();
  }

private:
  ObReqQueue queue_;
  Thread thread_;
};

class ObSrvDeliver
    : public rpc::frame::ObReqQDeliver
{
public:
  explicit ObSrvDeliver(ObiReqQHandler &qhandler);

  int init();
  void stop();

  int repost(void* node);
  virtual int deliver(rpc::ObRequest &req);
private:
  int deliver_mysql_request(rpc::ObRequest &req);

private:
  ObDiagnoseQueueThread diagnose_queue_;
  DISALLOW_COPY_AND_ASSIGN(ObSrvDeliver);

public:
  static const int64_t MAX_QUEUE_LEN = 10000;
  static const int MYSQL_DIAG_TASK_THREAD_CNT = 2;
  static const int MINI_MODE_MYSQL_DIAG_TASK_THREAD_CNT = 1;
}; // end of class ObSrvDeliver

} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OCEABASE_OBSERVER_OB_SRV_DELIVER_H_ */
