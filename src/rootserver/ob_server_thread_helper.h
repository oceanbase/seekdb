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

#ifndef OCEANBASE_ROOTSERVER_OB_SERVER_THREAD_HELPER_H
#define OCEANBASE_ROOTSERVER_OB_SERVER_THREAD_HELPER_H
#include "lib/thread/ob_reentrant_thread.h"
#include "share/log/ob_log_base_type.h"
#include "lib/lock/ob_thread_cond.h"


namespace oceanbase
{
namespace rootserver
{
class ObServerThreadHelper : public share::ObReentrantThread,
  public logservice::ObILocalLogHandler
{
public:
  ObServerThreadHelper()
    : share::ObReentrantThread(),
      thread_cnt_(0),
      thread_cond_(),
      is_created_(false),
      is_first_time_to_start_(true),
      thread_name_("")
  {}
  virtual ~ObServerThreadHelper() {}
  virtual void do_work() = 0;
  virtual void run2() override;
  virtual int blocking_run() override { BLOCKING_RUN_IMPLEMENT(); }
  virtual void destroy();
  int start();
  void stop();
  void wait();
  void server_module_thread_stop();
  void server_module_thread_wait();
  int create(const char *thread_name, int64_t thread_cnt);
  void idle(const int64_t idle_time_us);
  void wakeup();
public:
  virtual void switch_to_follower_forcedly();

  virtual int switch_to_leader();
  virtual int switch_to_follower_gracefully()
  {
    stop();
    return OB_SUCCESS;
  }
  virtual int resume_leader()
  {
    return OB_SUCCESS;
  }
private:
  int64_t thread_cnt_;
  common::ObThreadCond thread_cond_;
  bool is_created_;
  bool is_first_time_to_start_;
  const char* thread_name_;
};


}
}


#endif /* !OCEANBASE_ROOTSERVER_OB_SERVER_THREAD_HELPER_H */
