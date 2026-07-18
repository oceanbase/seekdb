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

#ifndef __OCEANBASE_SQL_ENGINE_PX_DFO_MGR_H__
#define __OCEANBASE_SQL_ENGINE_PX_DFO_MGR_H__

#include "lib/container/ob_se_array.h"
#include "sql/engine/px/ob_dfo.h"

namespace oceanbase
{
namespace sql
{

class ObTransmitSpec;
class ObPxCoordInfo;
class ObDfoMgr
{
public:
  explicit ObDfoMgr(common::ObIAllocator &allocator) :
      allocator_(allocator), inited_(false),
      root_dfo_(NULL)
  {}
  virtual ~ObDfoMgr() = default;
  void destroy();
  void reset();
  int init(ObExecContext &exec_ctx,
           const ObOpSpec &root_op_spec,
           const ObDfoInterruptIdGen &dfo_int_gen,
           ObPxCoordInfo &px_coord_info);
  ObDfo *get_root_dfo() { return root_dfo_; }
  
  virtual int get_ready_dfo(ObDfo *&dfo) const; // Only used for single-layer dfo scheduling
  // Can be selected for the upcoming scheduling queue DFO
  virtual int get_ready_dfos(common::ObIArray<ObDfo *> &dfos) const;
  // Already selected DFO to be scheduled in the queue
  virtual int get_active_dfos(common::ObIArray<ObDfo *> &dfos) const;
  // Already scheduled DFO
  virtual int get_scheduled_dfos(ObIArray<ObDfo*> &dfos) const;
  // Already scheduled, but not yet completed DFO
  virtual int get_running_dfos(ObIArray<ObDfo*> &dfos) const;

  int add_dfo_edge(ObDfo *edge);
  int find_dfo_edge(int64_t id, ObDfo *&edge);
  const ObIArray<ObDfo *> &get_all_dfos() { return edges_; }
  ObIArray<ObDfo *> &get_all_dfos_for_update() { return edges_; }

  DECLARE_TO_STRING;
private:
  int do_split(ObExecContext &exec_ctx,
               common::ObIAllocator &allocator,
               const ObOpSpec *phy_op,
               ObDfo *&parent_dfo,
               const ObDfoInterruptIdGen &dfo_id_gen,
               ObPxCoordInfo &px_coord_info) const;
  int create_dfo(common::ObIAllocator &allocator,
                 const ObOpSpec *dfo_root_op,
                 ObDfo *&dfo) const;
  int64_t get_adaptive_px_dop(const ObTransmitSpec &spec, ObExecContext &exec_ctx) const;
protected:
  common::ObIAllocator &allocator_;
  bool inited_;
  ObDfo *root_dfo_;
  common::ObSEArray<ObDfo *, 2> edges_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObDfoMgr);
};

class ObDfoSchedOrderGenerator
{
public:
  static int generate_sched_order(ObDfoMgr &dfo_mgr);
private:
  static int do_generate_sched_order(ObDfoMgr &dfo_mgr, ObDfo &root);
};

class ObDfoSchedDepthGenerator
{
public:
  static int generate_sched_depth(ObExecContext &ctx, ObDfoMgr &dfo_mgr);
private:
  static int do_generate_sched_depth(ObExecContext &ctx, ObDfoMgr &dfo_mgr, ObDfo &root);
  static int try_set_dfo_block(ObExecContext &exec_ctx, ObDfo &dfo, bool block = true);
  static int try_set_dfo_unblock(ObExecContext &exec_ctx, ObDfo &dfo);
  static bool check_if_need_do_earlier_sched(ObDfo &child);
};

class ObDfoWorkerAssignment
{
public:
  static int assign_worker(ObDfoMgr &dfo_mgr,
                           int64_t expected_worker_count,
                           int64_t minimal_worker_count,
                           int64_t admited_worker_count,
                           bool use_adaptive_px_dop);
  static int get_dfos_worker_count(const ObIArray<ObDfo*> &dfos,
                                   const bool get_minimal,
                                   int64_t &total_assigned);
  static int calc_admited_worker_count(const ObIArray<ObDfo*> &dfos,
                                       ObExecContext &exec_ctx,
                                       const ObOpSpec &root_op_spec,
                                       int64_t &px_expected,
                                       int64_t &px_minimal,
                                       int64_t &px_admited);
};

}
}
#endif /* __OCEANBASE_SQL_ENGINE_PX_DFO_MGR_H__ */
