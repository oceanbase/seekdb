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

#pragma once

#include "lib/task/ob_timer.h"
#include "rootserver/mview/ob_mview_timer_task.h"
#include "storage/tx/ob_trans_part_ctx.h"

namespace oceanbase
{
namespace rootserver
{

class ObMViewMdsOpTask : public ObMViewTimerTask
{
public:
class CollectNeedDeleteMdsFunctor
{
public:
  CollectNeedDeleteMdsFunctor(hash::ObHashSet<transaction::ObTransID> &tx_set,
                              ObSEArray<transaction::ObTransID, 2> &del_tx_id):
                              tx_set_(tx_set), del_tx_id_(del_tx_id)
  {};
  int operator()(hash::HashMapPair<transaction::ObTransID, ObMViewOpArg> &mv_mds_kv);
  virtual ~CollectNeedDeleteMdsFunctor() {};
private:
  hash::ObHashSet<transaction::ObTransID> &tx_set_;
  ObSEArray<transaction::ObTransID, 2> &del_tx_id_;
};

public:
  ObMViewMdsOpTask();
  virtual ~ObMViewMdsOpTask();
  DISABLE_COPY_ASSIGN(ObMViewMdsOpTask);
  // for Service
  int init();
  int start();
  void stop();
  void wait();
  void destroy();
  // for TimerTask
  void runTimerTask() override;
  int update_mview_mds_op();
  static const int64_t MVIEW_MDS_OP_INTERVAL = 5 * 1000 * 1000; // 5s
private:
  bool is_inited_;
  bool in_sched_;
  bool is_stop_;
  uint64_t tenant_id_;
  int64_t last_sched_ts_;
};

} // namespace rootserver
} // namespace oceanbase
