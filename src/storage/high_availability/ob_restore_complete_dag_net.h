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

#ifndef OCEANBASE_STORAGE_RESTORE_COMPLETE_DAG_NET_
#define OCEANBASE_STORAGE_RESTORE_COMPLETE_DAG_NET_

#include "ob_restore_dag_net.h"
#include "ob_restore_helper.h"
#include "ob_storage_ha_struct.h"
#include "ob_restore_handler.h"
#include "ob_restore_status.h"

namespace oceanbase
{
namespace restore
{
struct ObCompleteRestoreCtx : public storage::ObIHADagNetCtx
{
public:
  ObCompleteRestoreCtx();
  virtual ~ObCompleteRestoreCtx();
  void reset();
  void reuse();
  virtual int fill_comment(char *buf, const int64_t buf_len) const override;
  virtual bool is_valid() const override;
  virtual DagNetCtxType get_dag_net_ctx_type() override { return ObIHADagNetCtx::DagNetCtxType::RESTORE_COMPLETE; }

public:
  ObRestoreTask task_;
  int64_t start_ts_;
  int64_t finish_ts_;
  INHERIT_TO_STRING_KV("ObIHADagNetCtx", storage::ObIHADagNetCtx, K_(task), K_(start_ts), K_(finish_ts));
private:
  DISALLOW_COPY_AND_ASSIGN(ObCompleteRestoreCtx);
};

struct ObCompleteRestoreParam : public share::ObIDagInitParam
{
public:
  ObCompleteRestoreParam();
  virtual ~ObCompleteRestoreParam() {}
  virtual bool is_valid() const override;
  void reset();
  VIRTUAL_TO_STRING_KV(K_(task), K_(result));

public:
  ObRestoreTask task_;
  //hold and destory by ls
  ObRestoreHandler *handler_;
  int32_t result_;
};

class ObCompleteRestoreDagNet : public share::ObIDagNet
{
public:
  ObCompleteRestoreDagNet();
  virtual ~ObCompleteRestoreDagNet();
  virtual int init_by_param(const share::ObIDagInitParam *param) override;
  virtual bool is_valid() const override;
  virtual int start_running() override;
  virtual bool operator == (const share::ObIDagNet &other) const override;
  virtual uint64_t hash() const override;
  virtual int fill_comment(char *buf, const int64_t buf_len) const override;
  virtual int fill_dag_net_key(char *buf, const int64_t buf_len) const override;
  virtual int clear_dag_net_ctx() override;
  virtual int deal_with_cancel() override;
  ObCompleteRestoreCtx *get_complete_ctx() { return &ctx_; }
  INHERIT_TO_STRING_KV("ObIDagNet", share::ObIDagNet, K_(ctx));
private:
  int start_running_for_complete_();
  int update_restore_status_(ObLS *ls);
  int get_next_complete_status_(
      const ObRestoreStatus &current_complete_status,
      ObRestoreStatus &next_complete_status);
private:
  bool is_inited_;
  ObCompleteRestoreCtx ctx_;
  ObRestoreHandler *handler_;
  DISALLOW_COPY_AND_ASSIGN(ObCompleteRestoreDagNet);
};

class ObCompleteRestoreDag : public ObRestoreDag
{
public:
  explicit ObCompleteRestoreDag(const share::ObDagType::ObDagTypeEnum &dag_type);
  virtual ~ObCompleteRestoreDag();
  virtual bool operator == (const share::ObIDag &other) const override;
  virtual uint64_t hash() const override;
  virtual int fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const override;
  int prepare_ctx(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObRestoreDag", ObRestoreDag, KP(this));
protected:
  DISALLOW_COPY_AND_ASSIGN(ObCompleteRestoreDag);
};

class ObInitialCompleteRestoreDag : public ObCompleteRestoreDag
{
public:
  ObInitialCompleteRestoreDag();
  virtual ~ObInitialCompleteRestoreDag();
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual int create_first_task() override;
  int init(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObCompleteRestoreDag", ObCompleteRestoreDag, KP(this));
private:
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObInitialCompleteRestoreDag);
};

class ObInitialCompleteRestoreTask : public share::ObITask
{
public:
  ObInitialCompleteRestoreTask();
  virtual ~ObInitialCompleteRestoreTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObInitialCompleteRestoreTask"), KP(this), KPC_(ctx));
private:
  int generate_restore_dags_();
  int record_server_event_();
private:
  bool is_inited_;
  ObCompleteRestoreCtx *ctx_;
  share::ObIDagNet *dag_net_;
  DISALLOW_COPY_AND_ASSIGN(ObInitialCompleteRestoreTask);
};

class ObWaitDataReadyRestoreDag : public ObCompleteRestoreDag
{
public:
  ObWaitDataReadyRestoreDag();
  virtual ~ObWaitDataReadyRestoreDag();
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual int create_first_task() override;
  int init(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObCompleteRestoreDag", ObCompleteRestoreDag, KP(this));
private:
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObWaitDataReadyRestoreDag);
};

class ObWaitDataReadyRestoreTask : public share::ObITask
{
public:
  ObWaitDataReadyRestoreTask();
  virtual ~ObWaitDataReadyRestoreTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObWaitDataReadyRestoreTask"), KP(this), KPC(ctx_));
private:
  int get_wait_timeout_(int64_t &timeout);
  int wait_log_sync_();
  int wait_log_replay_sync_();
  int update_ls_restore_status_wait_();
  int check_all_tablet_ready_();
  int check_tablet_ready_(
      const common::ObTabletID &tablet_id,
      ObLS *ls,
      const int64_t timeout);
  int wait_log_replay_to_max_minor_end_scn_();
  int check_ls_and_task_status_(
      ObLS *ls);
  int record_server_event_();
  int init_timeout_ctx_(const int64_t timeout, ObTimeoutCtx &timeout_ctx);
private:
  static const int64_t IS_REPLAY_DONE_THRESHOLD_US = 3L * 1000 * 1000L;
  static const int64_t CHECK_CONDITION_INTERVAL = 200_ms;
  bool is_inited_;
  ObCompleteRestoreCtx *ctx_;
  ObLSHandle ls_handle_;
  palf::LSN log_sync_lsn_;
  share::SCN max_minor_end_scn_;
  DISALLOW_COPY_AND_ASSIGN(ObWaitDataReadyRestoreTask);
};

class ObFinishCompleteRestoreDag : public ObCompleteRestoreDag
{
public:
  ObFinishCompleteRestoreDag();
  virtual ~ObFinishCompleteRestoreDag();
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual int create_first_task() override;
  int init(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObCompleteRestoreDag", ObCompleteRestoreDag, KP(this));
private:
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObFinishCompleteRestoreDag);
};

class ObFinishCompleteRestoreTask : public share::ObITask
{
public:
  ObFinishCompleteRestoreTask();
  virtual ~ObFinishCompleteRestoreTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObFinishCompleteRestoreTask"), KP(this), KPC_(ctx));
private:
  int generate_initial_complete_restore_dag_();
  int record_server_event_();
private:
  bool is_inited_;
  ObCompleteRestoreCtx *ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObFinishCompleteRestoreTask);
};

} // namespace restore
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_RESTORE_COMPLETE_DAG_NET_
