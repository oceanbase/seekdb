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

#ifndef OCEANBASE_DDL_TRANS_CONTROLLER_H
#define OCEANBASE_DDL_TRANS_CONTROLLER_H

#include "lib/container/ob_se_array.h"
#include "lib/lock/ob_thread_cond.h"
#include "lib/task/ob_timer.h"
#include "lib/hash/ob_hashset.h"
#include "common/ob_queue_thread.h"

namespace oceanbase
{

namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;

struct TaskDesc
{
  uint64_t tenant_id_;
  int64_t task_id_;
  bool task_end_;
  TO_STRING_KV(K_(tenant_id), K_(task_id), K_(task_end));
};

// impl for ddl schema change trans commit in order with schema_version
class ObDDLTransController : public lib::ThreadPool
{
public:
  ObDDLTransController() : inited_(false), schema_service_(NULL) {}
  ~ObDDLTransController();
  int init(share::schema::ObMultiVersionSchemaService *schema_service);
  void stop();
  void wait();
  void destroy();
  static const int DDL_TASK_COND_SLOT = 128;
  int create_task_and_assign_schema_version(
      const uint64_t tenant_id,
      const uint64_t schema_version_count,
      int64_t &task_id,
      ObIArray<int64_t> &schema_version_res);
  int wait_task_ready(const uint64_t tenant_id, const int64_t task_id, const int64_t wait_us);
  int remove_task(const uint64_t tenant_id, const int64_t task_id);
  int broadcast_consensus_version(const int64_t tenant_id,
                                  const int64_t schema_version,
                                  const ObArray<ObAddr> &server_list);
  int reserve_schema_version(const uint64_t tenant_id, const uint64_t schema_version_count);
private:
  virtual void run1() override;
  int check_task_ready_(const uint64_t tenant_id, const int64_t task_id, bool &ready);
private:
  bool inited_;
  common::ObThreadCond cond_slot_[DDL_TASK_COND_SLOT];
  ObSEArray<TaskDesc, 32> tasks_;
  common::SpinRWLock lock_;
  share::schema::ObMultiVersionSchemaService *schema_service_;

  common::hash::ObHashSet<uint64_t> tenants_;


  common::ObCond wait_cond_;
};

} // end schema
} // end share
} // end oceanbase

#endif
