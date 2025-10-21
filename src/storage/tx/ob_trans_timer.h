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

#ifndef OCEANBASE_TRANSACTION_OB_TRANS_TIMER_
#define OCEANBASE_TRANSACTION_OB_TRANS_TIMER_

#include <stdint.h>
#include "ob_time_wheel.h"

namespace oceanbase
{

namespace common
{
class ObTimeWheel;
class ObTimeWheelTask;
}

namespace transaction
{
class ObTransService;
class ObTransCtx;
class ObTxDesc;
class ObTransService;

class ObITimeoutTask : public common::ObTimeWheelTask
{
public:
  ObITimeoutTask() : is_registered_(false), is_running_(false), delay_(0) {}
  virtual ~ObITimeoutTask() {}
  void reset()
  {
    is_registered_ = false;
    is_running_ = false;
    delay_ = 0;
    common::ObTimeWheelTask::reset();
  }
public:
  void set_registered(const bool is_registered) { is_registered_ = is_registered; }
  bool is_registered() const { return is_registered_; }
  void set_running(const bool is_running) { is_running_ = is_running; }
  bool is_running() const { return is_running_; }
  void set_delay(const int64_t delay) { delay_ = delay; }
  int64_t get_delay() const { return delay_; }
protected:
  bool is_registered_;
  bool is_running_;
  int64_t delay_;
};

class ObITransTimer
{
public:
  ObITransTimer() {}
  virtual ~ObITransTimer() {}
  virtual int init(const char *timer_name) = 0;
  virtual int start() = 0;
  virtual int stop() = 0;
  virtual int wait() = 0;
  virtual void destroy() = 0;
public:
  virtual int register_timeout_task(ObITimeoutTask &task, const int64_t delay) = 0;
  virtual int unregister_timeout_task(ObITimeoutTask &task) = 0;
};

class ObTransTimeoutTask : public ObITimeoutTask
{
public:
  ObTransTimeoutTask() :is_inited_(false), ctx_(NULL) {}
  virtual ~ObTransTimeoutTask() {}
  int init(ObTransCtx *ctx);
  void destroy();
  void reset();
public:
  void runTimerTask();
  uint64_t hash() const;
public:
  TO_STRING_KV(K_(is_inited), K_(is_registered), K_(is_running), K_(delay), KP_(ctx),
      K_(bucket_idx), K_(run_ticket), K_(is_scheduled), KP_(prev), KP_(next));
private:
  bool is_inited_;
  ObTransCtx *ctx_;
};

class ObTxTimeoutTask : public ObITimeoutTask
{
public:
  ObTxTimeoutTask() :is_inited_(false), tx_desc_(NULL), txs_(NULL) {}
  virtual ~ObTxTimeoutTask() {}
  int init(ObTxDesc *tx_desc, ObTransService* txs);
  void reset();
public:
  void runTimerTask();
  uint64_t hash() const;
public:
  TO_STRING_KV(K_(is_inited), K_(is_registered), K_(is_running), K_(delay), KP_(tx_desc),
      K_(bucket_idx), K_(run_ticket), K_(is_scheduled), KP_(prev), KP_(next));
private:
  bool is_inited_;
  ObTxDesc *tx_desc_;
  ObTransService *txs_;
};

class ObTransTimer : public ObITransTimer
{
public:
  ObTransTimer() : is_inited_(false), is_running_(false) {}
  virtual ~ObTransTimer() {}
  virtual int init(const char *timer_name);
  virtual int start();
  virtual int stop();
  virtual int wait();
  virtual void destroy();
public:
  virtual int register_timeout_task(ObITimeoutTask &task, const int64_t delay);
  virtual int unregister_timeout_task(ObITimeoutTask &task);
private:
  int64_t get_thread_num_() { return common::max(sysconf(_SC_NPROCESSORS_ONLN) / 24, 1); }
private:
  DISALLOW_COPY_AND_ASSIGN(ObTransTimer);
protected:
  // schedule timeout task precision. us
  static const int64_t TRANS_TIMEOUT_TASK_PRECISION_US = 100 * 1000L;

  bool is_inited_;
  bool is_running_;
  common::ObTimeWheel tw_;
};

class ObDupTableLeaseTimer : public ObTransTimer
{
public:
  ObDupTableLeaseTimer() {}
  virtual ~ObDupTableLeaseTimer() {}
private:
  static const int64_t DUP_TABLE_TIMEOUT_TASK_PRECISION_US = 3 * 1000 * 1000L;
  DISALLOW_COPY_AND_ASSIGN(ObDupTableLeaseTimer);
};

} // transaction
} // oceanbase

#endif
