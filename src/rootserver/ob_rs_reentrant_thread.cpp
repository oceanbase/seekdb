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

#define USING_LOG_PREFIX RS

#include "ob_rs_reentrant_thread.h"

namespace oceanbase
{
using namespace common;

namespace rootserver
{

ObRsReentrantThread::ObRsReentrantThread()
  :last_run_timestamp_(-1), thread_id_(-1)
{}

ObRsReentrantThread::ObRsReentrantThread(bool need_check)
  :last_run_timestamp_(need_check ? 0 : -1), thread_id_(-1)
{}

ObRsReentrantThread::~ObRsReentrantThread()
{}

void ObRsReentrantThread::update_last_run_timestamp() 
{
  int64_t time = ObTimeUtility::current_time();
  IGNORE_RETURN lib::Thread::update_loop_ts(time);
  if (ATOMIC_LOAD(&last_run_timestamp_) != -1) {
    ATOMIC_STORE(&last_run_timestamp_, time);
  }
}

bool ObRsReentrantThread::need_monitor_check() const 
{
  bool ret = false;
  int64_t last_run_timestamp = get_last_run_timestamp();
  int64_t schedule_interval = get_schedule_interval();
  if (schedule_interval >= 0 && last_run_timestamp > 0 
      && last_run_timestamp + schedule_interval + MAX_THREAD_SCHEDULE_OVERRUN_TIME 
      < ObTimeUtility::current_time()) {
    ret = true;
  }
  return ret;
}
int ObRsReentrantThread::start()
{
  return logical_start();
}
void ObRsReentrantThread::stop() 
{
  logical_stop();
}

void ObRsReentrantThread::wait() 
{
  logical_wait();
  if (get_last_run_timestamp() != -1) {
    ATOMIC_STORE(&last_run_timestamp_, 0);
  }
}

int ObRsReentrantThread::create(const int64_t thread_cnt, const char* thread_name, const int64_t wait_event_id)
{
  int ret = OB_SUCCESS;
  bool added = false;
  if (last_run_timestamp_ != -1) {
    added = true;
  }

  if (FAILEDx(share::ObReentrantThread::create(thread_cnt, thread_name, wait_event_id))) {
    LOG_WARN("fail to create reentraint thread", KR(ret), K(thread_name));
  } else if (last_run_timestamp_ != -1) {
    LOG_INFO("rs_monitor_check : reentrant thread check register success", K(thread_name));
  }

  return ret;
}

int ObRsReentrantThread::destroy()
{
  int ret = OB_SUCCESS;
  const char *thread_name = get_thread_name();
  if (OB_FAIL(share::ObReentrantThread::destroy())) {
    LOG_INFO("fail to destroy reentraint thread", KR(ret), K(thread_name));
  }  else if (last_run_timestamp_ != -1) {
    LOG_INFO("rs_monitor_check : reentrant thread check unregister success", 
        K(thread_name), K_(last_run_timestamp));
  }
  return ret;
}


int64_t ObRsReentrantThread::get_last_run_timestamp() const
{ 
  return ATOMIC_LOAD(&last_run_timestamp_); 
}

CheckThreadSet::CheckThreadSet() 
  : arr_(), rwlock_(ObLatchIds::THREAD_HANG_CHECKER_LOCK)
{
}

CheckThreadSet::~CheckThreadSet() 
{
  arr_.reset();
}






}
}
