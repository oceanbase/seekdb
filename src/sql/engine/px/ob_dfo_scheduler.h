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

#ifndef OCEANBASE_PX_OB_DFO_SCHEDULER_H_
#define OCEANBASE_PX_OB_DFO_SCHEDULER_H_

#include "sql/engine/px/ob_dfo_mgr.h"
#include "sql/engine/px/ob_px_util.h"

namespace oceanbase
{
namespace sql
{
class ObPxCoordInfo;
class ObPxRootDfoAction;
class ObPxMsgProc;

class ObDfoSchedulerBasic
{
public:
  ObDfoSchedulerBasic(ObPxCoordInfo &coord_info,
                      ObPxRootDfoAction &root_dfo_action,
                      ObIPxCoordEventListener &listener);
public:
  virtual int dispatch_dtl_data_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const = 0;
  virtual int try_schedule_next_dfo(ObExecContext &ctx) = 0;
  virtual int set_temp_table_ctx_for_sqc(ObExecContext &exec_ctx, ObDfo &child) const;
  virtual int dispatch_transmit_channel_info_via_sqc(ObExecContext &ctx,
                                                     ObDfo &child,
                                                     ObDfo &parent) const;
  virtual int dispatch_receive_channel_info_via_sqc(ObExecContext &ctx,
                                                    ObDfo &child,
                                                    ObDfo &parent,
                                                    bool is_parallel_scheduler = true) const;
  virtual void clean_dtl_interm_result(ObExecContext &ctx) = 0;
  int build_data_xchg_ch(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const;
  int build_data_mn_xchg_ch(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const;
  virtual int init_all_dfo_channel(ObExecContext &ctx) const;
  virtual int on_sqc_threads_inited(ObExecContext &ctx, ObDfo &dfo) const;
  virtual int dispatch_root_dfo_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObDfoSchedulerBasic);
protected:
  ObPxCoordInfo &coord_info_;
  ObPxRootDfoAction &root_dfo_action_;
  ObIPxCoordEventListener &listener_;
};

class ObSerialDfoScheduler : public ObDfoSchedulerBasic
{
public:
  using ObDfoSchedulerBasic::ObDfoSchedulerBasic;

  virtual int init_all_dfo_channel(ObExecContext &ctx) const;
  virtual int dispatch_dtl_data_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const;
  virtual int try_schedule_next_dfo(ObExecContext &ctx);
  virtual void clean_dtl_interm_result(ObExecContext &ctx) override;

private:
  int build_transmit_recieve_channel(ObExecContext &ctx, ObDfo *dfo) const;
  int init_dfo_channel(ObExecContext &ctx, ObDfo *child, ObDfo *parent) const;
  int init_data_xchg_ch(ObExecContext &ctx, ObDfo *dfo) const;
  int dispatch_sqcs(ObExecContext &exec_ctx, ObDfo &dfo, ObIArray<ObPxSqcMeta> &sqcs) const;
  int do_schedule_dfo(ObExecContext &ctx, ObDfo &dfo) const;
  // in-process DTL interm result cleanup (single-replica, self target)
  static int clean_dtl_interm_result_local(ObPxCleanDtlIntermResArgs &arg);
private:
  DISALLOW_COPY_AND_ASSIGN(ObSerialDfoScheduler);
};

class ObParallelDfoScheduler : public ObDfoSchedulerBasic
{
public:
    ObParallelDfoScheduler(ObPxCoordInfo &coord_info,
                           ObPxRootDfoAction &root_dfo_action,
                           ObIPxCoordEventListener &listener,
                           ObPxMsgProc &proc)
        : ObDfoSchedulerBasic(coord_info, root_dfo_action, listener), proc_(proc)
    {}
    virtual int dispatch_dtl_data_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const;
    virtual int try_schedule_next_dfo(ObExecContext &ctx);
    virtual void clean_dtl_interm_result(ObExecContext &ctx) override { UNUSED(ctx); }
private:
    int dispatch_transmit_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const;
    int dispatch_receive_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const;
    int do_schedule_dfo(ObExecContext &exec_ctx, ObDfo &dfo) const;
    // The SQC carries each task's transmit-channel data when interacting with the root DFO.
    int check_if_can_prealloc_xchg_ch(ObDfo &child, ObDfo &parent, bool &bret) const;
    int do_fast_schedule(ObExecContext &exec_ctx,
                         ObDfo &child,
                         ObDfo &root_dfo) const;
    int mock_on_sqc_init_msg(ObExecContext &ctx, ObDfo &dfo) const;
    int schedule_dfo(ObExecContext &exec_ctx, ObDfo &dfo) const;
    int on_root_dfo_scheduled(ObExecContext &ctx, ObDfo &root_dfo) const;
    int dispatch_sqc(ObExecContext &exec_ctx,
                     ObDfo &dfo,
                     ObIArray<ObPxSqcMeta> &sqcs) const;
    int schedule_pair(ObExecContext &exec_ctx,
                      ObDfo &child,
                      ObDfo &parent);
    int wait_for_dfo_finish(ObDfoMgr &dfo_mgr) const;
  private:
    ObPxMsgProc &proc_;
  private:
    DISALLOW_COPY_AND_ASSIGN(ObParallelDfoScheduler);
};


}
}

#endif // OCEANBASE_PX_OB_DFO_SCHEDULER_H_
