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

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_TIMER_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_TIMER_H_

#include "lib/task/ob_timer.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_thread_pool.h"           // ObThreadPool

namespace oceanbase
{
namespace archive
{
class ObLSMetaRecorder;
class ObArchiveRoundMgr;
class ObArchiveTimer : public share::ObThreadPool
{
  static const int64_t THREAD_RUN_INTERVAL = 1000 * 1000L; // 1s
public:
  ObArchiveTimer();
  ~ObArchiveTimer();

  int init(const uint64_t tenant_id, ObLSMetaRecorder *recorder, ObArchiveRoundMgr *round_mgr);
  void destroy();
  int start();
  void wait();
  void stop();

private:
  void run1();
  void do_thread_task_();

private:
  class LSMetaRecordTask
  {
  public:
    explicit LSMetaRecordTask(ObArchiveTimer *timer);
    ~LSMetaRecordTask();

    void handle();
    ObArchiveTimer *timer_;
  };

  friend class LSMetaRecordTask;
private:
  bool inited_;
  uint64_t tenant_id_;
  LSMetaRecordTask record_task_;

  ObLSMetaRecorder *recorder_;
  ObArchiveRoundMgr *round_mgr_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObArchiveTimer);
};

} // namespace archive
} // namespace oceanbase
#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_TIMER_H_ */
