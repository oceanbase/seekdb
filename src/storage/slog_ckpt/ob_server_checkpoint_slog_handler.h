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

#ifndef OB_STORAGE_CKPT_SERVER_CHECKPOINT_SLOG_HANDLER_H_
#define OB_STORAGE_CKPT_SERVER_CHECKPOINT_SLOG_HANDLER_H_

#include "common/log/ob_log_cursor.h"
#include "lib/atomic/ob_atomic.h"
#include "observer/omt/ob_tenant_meta.h"
#include "storage/slog_ckpt/ob_tenant_storage_checkpoint_reader.h"
#include "storage/ob_super_block_struct.h"
#include "storage/slog/ob_storage_log_replayer.h"

namespace oceanbase
{
namespace storage
{

struct ObMetaDiskAddr;

class ObRedoModuleReplayParam;

class ObStorageLogger;

class ObServerCheckpointSlogHandler : public ObIRedoModule
{
public:
  class ObWriteCheckpointTask : public common::ObTimerTask
  {
  public:
    static const int64_t FAIL_WRITE_CHECKPOINT_ALERT_INTERVAL = 1000L * 1000L * 3600LL;  // 6h
    static const int64_t WRITE_CHECKPOINT_INTERVAL_US = 1000L * 1000L * 60L;             // 1min
    static const int64_t RETRY_WRITE_CHECKPOINT_MIN_INTERVAL = 1000L * 1000L * 300L;     // 5min
    static const int64_t MIN_WRITE_CHECKPOINT_LOG_CNT = 50000; // TODO(fenggu)

    explicit ObWriteCheckpointTask(ObServerCheckpointSlogHandler *handler) : handler_(handler) {}
    virtual ~ObWriteCheckpointTask() = default;
    virtual void runTimerTask() override;

  private:
    ObServerCheckpointSlogHandler *handler_;
  };

  typedef common::hash::ObHashMap<uint64_t, omt::ObTenantMeta> TENANT_META_MAP;

  ObServerCheckpointSlogHandler();
  ~ObServerCheckpointSlogHandler() = default;
  ObServerCheckpointSlogHandler(const ObServerCheckpointSlogHandler &) = delete;
  ObServerCheckpointSlogHandler &operator=(const ObServerCheckpointSlogHandler &) = delete;

  int init(ObStorageLogger *server_slogger);
  int start();
  int start_replay(TENANT_META_MAP &tenant_meta_map);
  int do_post_replay_work();
  void stop();
  void wait();
  void destroy();
  int get_meta_block_list(common::ObIArray<blocksstable::MacroBlockId> &block_list);
  virtual int replay(const ObRedoModuleReplayParam &param) override;
  virtual int replay_over() override;

  static ObServerCheckpointSlogHandler &get_instance();

  int write_checkpoint(bool is_force);

private:
  virtual int parse(const int32_t cmd, const char *buf, const int64_t len, FILE *stream) override;

  int try_write_checkpoint_for_compat();
  int read_checkpoint(const ObServerSuperBlock &super_block);
  int replay_and_apply_server_slog(const common::ObLogCursor &replay_start_point);
  int replay_server_slog(const common::ObLogCursor &replay_start_point, common::ObLogCursor &replay_finish_point);

  int replay_create_tenant_prepare(const char *buf, const int64_t buf_len);
  int replay_create_tenant_commit(const char *buf, const int64_t buf_len);
  int replay_create_tenant_abort(const char *buf, const int64_t buf_len);

  int replay_delete_tenant_prepare(const char *buf, const int64_t buf_len);
  int replay_delete_tenant_commit(const char *buf, const int64_t buf_len);
  int replay_delete_tenant(const char *buf, const int64_t buf_len);
  int replay_update_tenant_unit(const char *buf, const int64_t buf_len);
  int replay_update_tenant_super_block(const char *buf, const int64_t buf_len);

  int set_meta_block_list(common::ObIArray<blocksstable::MacroBlockId> &meta_block_list);

private:
  bool is_inited_;
  bool is_writing_checkpoint_;
  ObStorageLogger *server_slogger_;
  common::TCRWLock lock_;  // protect block_handle
  ObMetaBlockListHandle server_meta_block_handle_;
  ObWriteCheckpointTask write_ckpt_task_;
  common::ObTimer task_timer_;
  TENANT_META_MAP *tenant_meta_map_for_replay_; // only used when replay
};

}  // end namespace storage
}  // namespace oceanbase

#endif  // OB_STORAGE_CKPT_SERVER_CHECKPOINT_SLOG_HANDLER_H_
