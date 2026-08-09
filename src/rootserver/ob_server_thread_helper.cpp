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
#include "ob_server_thread_helper.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace lib;
namespace rootserver
{
int ObServerThreadHelper::create(
    const char *thread_name, int64_t thread_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_created_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", KR(ret));
  } else if (OB_ISNULL(thread_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("thread name is null", KR(ret));
  } else if (thread_cnt <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid thread count", KR(ret), K(thread_cnt));
  } else if (OB_FAIL(thread_cond_.init(ObWaitEventIds::REENTRANT_THREAD_COND_WAIT))) {
  } else {
    thread_name_ = thread_name;
    thread_cnt_ = thread_cnt;
    is_created_ = true;
    is_first_time_to_start_ = true;
  }
  return ret;
}

int ObServerThreadHelper::start()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_created_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (is_first_time_to_start_) {
    if (OB_FAIL(share::ObReentrantThread::create(thread_cnt_, thread_name_))) {
    } else if (OB_FAIL(share::ObReentrantThread::logical_start())) {
    } else {
      is_first_time_to_start_ = false;
    }
  } else if (OB_FAIL(share::ObReentrantThread::logical_start())) {
  }
  LOG_INFO("[SERVER THREAD] thread start", KR(ret), K(thread_cnt_), K(thread_name_));
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_SKIP_SERVER_THREAD_STOP);
void ObServerThreadHelper::stop()
{
  int ret = OB_SUCCESS;
  LOG_INFO("[SERVER THREAD] thread stop start", K(thread_cnt_), K(thread_name_));
  ret = ERRSIM_SKIP_SERVER_THREAD_STOP;
  if (OB_UNLIKELY(ERRSIM_SKIP_SERVER_THREAD_STOP)) {
    LOG_ERROR("[SERVER THREAD] skip server thread stop");
  } else if (!is_first_time_to_start_) {
    share::ObReentrantThread::logical_stop();
  }
  LOG_INFO("[SERVER THREAD] thread stop finish", K(thread_cnt_), K(thread_name_), KR(ret));
}

void ObServerThreadHelper::wait()
{
  LOG_INFO("[SERVER THREAD] thread wait start", K(thread_cnt_), K(thread_name_));
  if (!is_first_time_to_start_) {
    share::ObReentrantThread::logical_wait();
  }
  LOG_INFO("[SERVER THREAD] thread wait finish", K(thread_cnt_), K(thread_name_));
}

void ObServerThreadHelper::server_module_thread_stop()
{
  LOG_INFO("[SERVER THREAD] thread stop start", K(thread_cnt_), K(thread_name_));
  if (!is_first_time_to_start_) {
    share::ObReentrantThread::stop();
  }
  LOG_INFO("[SERVER THREAD] thread stop finish", K(thread_cnt_), K(thread_name_));
}

void ObServerThreadHelper::server_module_thread_wait()
{
  LOG_INFO("[SERVER THREAD] thread wait start", K(thread_cnt_), K(thread_name_));
  if (!is_first_time_to_start_) {
    {
      ObThreadCondGuard guard(thread_cond_);
      thread_cond_.broadcast();
    }
    share::ObReentrantThread::wait();
    share::ObReentrantThread::destroy();
    is_first_time_to_start_ = true;
  }
  LOG_INFO("[SERVER THREAD] thread wait finish", K(thread_cnt_), K(thread_name_));
}
void ObServerThreadHelper::destroy()
{
  LOG_INFO("[SERVER THREAD] thread destory start", K(thread_cnt_), K(thread_name_));
  if (is_created_) {
    if (!is_first_time_to_start_) {
      share::ObReentrantThread::stop();
    }
    {
      ObThreadCondGuard guard(thread_cond_);
      thread_cond_.broadcast();
    }
    if (!is_first_time_to_start_) {
      share::ObReentrantThread::wait();
      share::ObReentrantThread::destroy();
    }
    thread_cond_.destroy();
  }
  is_created_ = false;
  is_first_time_to_start_ = true;
  thread_cnt_ = 0;
  LOG_INFO("[SERVER THREAD] thread destory finish", K(thread_cnt_), K(thread_name_));
}

void ObServerThreadHelper::switch_to_follower_forcedly()
{
  stop();
}
int ObServerThreadHelper::switch_to_leader()
{
  int ret = OB_SUCCESS;
  LOG_INFO("[SERVER THREAD] thread start", K(thread_cnt_), K(thread_name_));
  if (OB_FAIL(start())) {
  } else {
    ObThreadCondGuard guard(thread_cond_);
    if (OB_FAIL(thread_cond_.broadcast())) {
    }
  }
  LOG_INFO("[SERVER THREAD] thread start finish", K(thread_cnt_), K(thread_name_));
  return ret;
}

void ObServerThreadHelper::run2() {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_created_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    lib::set_thread_name(thread_name_);
    LOG_INFO("thread run", K(thread_name_));
    do_work();
  }
}
void ObServerThreadHelper::idle(const int64_t idle_time_us)
{
  ObThreadCondGuard guard(thread_cond_);
  thread_cond_.wait_us(idle_time_us);
}

void ObServerThreadHelper::wakeup()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_created_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ObThreadCondGuard guard(thread_cond_);
    thread_cond_.broadcast();
  }
}

}//end of rootserver
}
