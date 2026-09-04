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

#define USING_LOG_PREFIX STORAGE

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_server_checkpoint_slog_handler.h"
#include "storage/api/storage/runtime/ob_i_server_runtime.h"
#include "storage/blocksstable/ob_block_manager.h"
#include "storage/slog/ob_storage_log.h"
#include "storage/slog_ckpt/ob_server_checkpoint_reader.h"
#include "storage/slog_ckpt/ob_server_checkpoint_writer.h"
#include "share/ob_structured_event_logger.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"

namespace oceanbase
{
namespace storage
{

using namespace oceanbase::common;
using namespace oceanbase::blocksstable;

void ObServerCheckpointSlogHandler::ObWriteCheckpointTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  ObCurTraceId::init(GCONF.self_addr_);
  if (SERVER_STORAGE_META_SERVICE.is_started()) {
    if (OB_FAIL(handler_->write_checkpoint(false/*is_force*/))) {
    }
  } else {
    // Must wait for all slog replays to complete before doing ckpt, otherwise some macro blocks may not be marked
    LOG_INFO("slog replay not finish, do not write checkpoint");
  }
}

ObServerCheckpointSlogHandler::ObServerCheckpointSlogHandler()
  : is_inited_(false),
    is_writing_checkpoint_(false),
    server_slogger_(nullptr),
    server_runtime_(nullptr),
    lock_(common::ObLatchIds::SLOG_CKPT_LOCK),
    server_meta_block_handle_(),
    write_ckpt_task_(this),
    task_timer_(),
    runtime_meta_for_replay_(),
    runtime_meta_valid_for_replay_(false)
{
}

int ObServerCheckpointSlogHandler::init(
    ObStorageLogger *server_slogger, ObIServerRuntime &server_runtime)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObServerCheckpointSlogHandler has inited", K(ret));
  } else if (OB_FAIL(task_timer_.set_run_wrapper_with_ret(share::server_runtime()))) {
  } else if (OB_FAIL(task_timer_.init("ServerCkptSlogHandler"))) {
  } else if (OB_FAIL(task_timer_.schedule(write_ckpt_task_,
      ObWriteCheckpointTask::WRITE_CHECKPOINT_INTERVAL_US, true /*repeate*/))) {
  } else {
    server_slogger_ = server_slogger;
    server_runtime_ = &server_runtime;
    is_inited_ = true;
  }
  return ret;
}

int ObServerCheckpointSlogHandler::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(task_timer_.start())) {
  }
  return ret;
}

int ObServerCheckpointSlogHandler::start_replay()
{
  int ret = OB_SUCCESS;

  const ObServerSuperBlock &super_block = OB_STORAGE_OBJECT_MGR.get_server_super_block();
  ObLogCursor replay_finish_point;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!super_block.is_valid())) {
    ret = OB_ERR_SYS;
    LOG_WARN("super block is invalid", K(ret), K(super_block));
  } else {
    runtime_meta_for_replay_ = omt::ObServerRuntimeMeta();
    runtime_meta_valid_for_replay_ = false;

    if (OB_FAIL(read_checkpoint(super_block))) {
    } else if (OB_FAIL(replay_server_slog(super_block.body_.replay_start_point_, replay_finish_point))) {
    } else if (OB_FAIL(server_slogger_->start_log(replay_finish_point))) {
    }
  }
  return ret;
}

void ObServerCheckpointSlogHandler::get_replay_result(
    omt::ObServerRuntimeMeta &runtime_meta, bool &is_valid) const
{
  is_valid = runtime_meta_valid_for_replay_;
  if (runtime_meta_valid_for_replay_) {
    runtime_meta = runtime_meta_for_replay_;
  }
}

int ObServerCheckpointSlogHandler::get_replay_runtime_meta_(omt::ObServerRuntimeMeta &meta) const
{
  int ret = OB_SUCCESS;
  if (!runtime_meta_valid_for_replay_) {
    ret = OB_HASH_NOT_EXIST;
  } else {
    meta = runtime_meta_for_replay_;
  }
  return ret;
}

int ObServerCheckpointSlogHandler::set_replay_runtime_meta_(const omt::ObServerRuntimeMeta &meta)
{
  runtime_meta_for_replay_ = meta;
  runtime_meta_valid_for_replay_ = true;
  return OB_SUCCESS;
}

int ObServerCheckpointSlogHandler::do_post_replay_work()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(OB_SERVER_BLOCK_MGR.first_mark_device())) {
  } else {
    runtime_meta_valid_for_replay_ = false;
  }
  return ret;
}

void ObServerCheckpointSlogHandler::stop()
{
  task_timer_.stop();
}

void ObServerCheckpointSlogHandler::wait()
{
  task_timer_.wait();
}

void ObServerCheckpointSlogHandler::destroy()
{
  is_inited_ = false;
  task_timer_.destroy();
}

int ObServerCheckpointSlogHandler::read_checkpoint(const ObServerSuperBlock &super_block)
{
  int ret = OB_SUCCESS;
  ObServerCheckpointReader server_ckpt_reader;

  if (OB_FAIL(server_ckpt_reader.read_checkpoint(super_block))) {
  } else if (OB_FAIL(set_meta_block_list(server_ckpt_reader.get_meta_block_list()))) {
  } else if (OB_FAIL(server_ckpt_reader.get_runtime_meta(
                 runtime_meta_for_replay_, runtime_meta_valid_for_replay_))) {
  }
  return ret;
}

int ObServerCheckpointSlogHandler::set_meta_block_list(ObIArray<MacroBlockId> &meta_block_list)
{
  int ret = OB_SUCCESS;
  TCWLockGuard guard(lock_);
  if (OB_FAIL(server_meta_block_handle_.add_macro_blocks(meta_block_list))) {
  }
  return ret;
}

int ObServerCheckpointSlogHandler::get_meta_block_list(ObIArray<MacroBlockId> &meta_block_list)
{
  int ret = OB_SUCCESS;
  TCRLockGuard guard(lock_);
  meta_block_list.reset();
  const ObIArray<blocksstable::MacroBlockId> &block_list = server_meta_block_handle_.get_meta_block_list();

  for (int64_t i = 0; OB_SUCC(ret) && i < block_list.count(); ++i) {
    if (OB_FAIL(meta_block_list.push_back(block_list.at(i)))) {
    }
  }
  return ret;
}

int ObServerCheckpointSlogHandler::replay_server_slog(const ObLogCursor &replay_start_point,
                                                      ObLogCursor &replay_finish_point)
{
  int ret = OB_SUCCESS;
  ObStorageLogReplayer replayer;
  blocksstable::ObLogFileSpec log_file_spec;
  log_file_spec.retry_write_policy_ = "normal";
  log_file_spec.log_create_policy_ = "normal";
  log_file_spec.log_write_policy_ = "truncate";

  if (OB_FAIL(replayer.init(server_slogger_->get_dir(), log_file_spec))) {
  } else if (OB_FAIL(replayer.register_redo_module(
    ObRedoLogMainType::OB_REDO_LOG_SERVER_RUNTIME, this))) {
  } else if (OB_FAIL(replayer.replay(replay_start_point, replay_finish_point))) {
  } else if (OB_FAIL(replayer.replay_over())) {
  }
  return ret;
}

int ObServerCheckpointSlogHandler::replay(const ObRedoModuleReplayParam &param)
{
  int ret = OB_SUCCESS;
  const char *buf = param.buf_;
  const int64_t len = param.disk_addr_.size();
  ObRedoLogMainType main_type = ObRedoLogMainType::OB_REDO_LOG_MAX;
  enum ObRedoLogSubType sub_type;
  ObIRedoModule::parse_cmd(param.cmd_, main_type, sub_type);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param));
  } else if (ObRedoLogMainType::OB_REDO_LOG_SERVER_RUNTIME != main_type) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong redo log type.", K(ret), K(main_type), K(sub_type));
  } else {
    switch (sub_type) {
      case ObRedoLogSubType::OB_REDO_LOG_UPDATE_SERVER_RESOURCES: {
        if (OB_FAIL(replay_update_server_resources(buf, len))) {
        }
        break;
      }
      case ObRedoLogSubType::OB_REDO_LOG_UPDATE_RUNTIME_SUPER_BLOCK: {
        if (OB_FAIL(replay_update_runtime_super_block(buf, len))) {
        }
        break;
      }
      default: {
        ret = OB_ERR_SYS;
        LOG_ERROR("unknown subtype", K(ret), K(sub_type), K(param));
      }
    }
  }

  return ret;
}

int ObServerCheckpointSlogHandler::parse(
  const int32_t cmd, const char *buf, const int64_t len, FILE *stream)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  ObRedoLogMainType main_type = ObRedoLogMainType::OB_REDO_LOG_SERVER_RUNTIME;
  ObRedoLogSubType sub_type = ObRedoLogSubType::OB_REDO_LOG_INVALID;
  char slog_name[ObStorageLogReplayer::MAX_SLOG_NAME_LEN];

  ObIRedoModule::parse_cmd(cmd, main_type, sub_type);
  if (OB_ISNULL(buf) || OB_ISNULL(stream) || OB_UNLIKELY(len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), KP(stream), K(len));
  } else if (OB_UNLIKELY(ObRedoLogMainType::OB_REDO_LOG_SERVER_RUNTIME != main_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("slog type does not match", K(ret), K(main_type), K(sub_type));
  } else if (OB_UNLIKELY(0 > fprintf(stream, "\nserver slog: "))) {
    ret = OB_IO_ERROR;
    LOG_WARN("fail to write server slog to stream", K(ret));
  } else {
    switch (sub_type) {
      case ObRedoLogSubType::OB_REDO_LOG_UPDATE_SERVER_RESOURCES: {
        share::ObServerRuntimeConfig runtime_config;
        ObUpdateServerResourcesLog slog_entry(runtime_config);
        snprintf(slog_name, ObStorageLogReplayer::MAX_SLOG_NAME_LEN, "update server resources slog: ");
        if (OB_FAIL(ObStorageLogReplayer::print_slog(buf, len, slog_name, slog_entry, stream))) {
        }
        break;
      }
      case ObRedoLogSubType::OB_REDO_LOG_UPDATE_RUNTIME_SUPER_BLOCK: {
        ObServerRuntimeSuperBlock super_block(false /*is_hidden*/);
        ObUpdateRuntimeSuperBlockLog slog_entry(super_block);
        snprintf(slog_name, ObStorageLogReplayer::MAX_SLOG_NAME_LEN, "update runtime super block slog: ");
        if (OB_FAIL(ObStorageLogReplayer::print_slog(buf, len, slog_name, slog_entry, stream))) {
        }
        break;
      }
      default: {
        ret = OB_ERR_SYS;
        LOG_ERROR("unknown subtype", K(ret), K(sub_type));
      }
    }
  }

  return ret;
}

int ObServerCheckpointSlogHandler::replay_update_server_resources(const char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  share::ObServerRuntimeConfig runtime_config;
  ObUpdateServerResourcesLog log_entry(runtime_config);
  SMART_VAR(omt::ObServerRuntimeMeta, runtime_meta) {
    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("ObServerCheckpointSlogHandler is not initialized", K(ret));
    } else if (OB_ISNULL(buf) || buf_len <= 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), KP(buf), K(buf_len));
    } else if (OB_FAIL(log_entry.deserialize(buf, buf_len, pos))) {
    } else if (OB_FAIL(get_replay_runtime_meta_(runtime_meta))) {
    } else if (FALSE_IT(runtime_meta.runtime_config_ = runtime_config)) {
    } else if (OB_FAIL(set_replay_runtime_meta_(runtime_meta))) {
    }
  }

  return ret;
}

int ObServerCheckpointSlogHandler::replay_update_runtime_super_block(const char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  ObServerRuntimeSuperBlock super_block(false /*is_hidden*/);
  ObUpdateRuntimeSuperBlockLog log_entry(super_block);

  HEAP_VAR(omt::ObServerRuntimeMeta, runtime_meta) {
    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("ObServerCheckpointSlogHandler is not initialized", K(ret));
    } else if (OB_ISNULL(buf) || buf_len <= 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), KP(buf), K(buf_len));
    } else if (OB_FAIL(log_entry.deserialize(buf, buf_len, pos))) {
    } else if (OB_FAIL(get_replay_runtime_meta_(runtime_meta))) {
    } else if (FALSE_IT(runtime_meta.super_block_ = super_block)) {
    } else if (OB_FAIL(set_replay_runtime_meta_(runtime_meta))) {
    }
  }
  return ret;
}

int ObServerCheckpointSlogHandler::replay_over()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObServerCheckpointSlogHandler::write_checkpoint(bool is_force)
{
  int ret = OB_SUCCESS;

  static int64_t last_write_time_ = 0;
  static ObLogCursor last_slog_cursor_;

  ObLogCursor cur_cursor;
  int64_t alert_interval = ObWriteCheckpointTask::FAIL_WRITE_CHECKPOINT_ALERT_INTERVAL;
  int64_t min_interval = ObWriteCheckpointTask::RETRY_WRITE_CHECKPOINT_MIN_INTERVAL;
  bool is_writing_checkpoint_set = false;
  const int64_t start_time = ObTimeUtility::current_time();
  int64_t cost_time = 0;


  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if(!ATOMIC_BCAS(&is_writing_checkpoint_, false, true)) {
    ret = OB_NEED_WAIT;
    LOG_WARN("is writing checkpoint, need wait", K(ret));
  } else {
    is_writing_checkpoint_set = true;
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(server_slogger_->get_active_cursor(cur_cursor))) {
  } else if (OB_UNLIKELY(!cur_cursor.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur_cursor is invalid", K(ret));
  } else if (((start_time > last_write_time_ + min_interval) && cur_cursor.newer_than(last_slog_cursor_)
      && (cur_cursor.log_id_ - last_slog_cursor_.log_id_ >= ObWriteCheckpointTask::MIN_WRITE_CHECKPOINT_LOG_CNT))
      || is_force
      || (cur_cursor.file_id_ > last_slog_cursor_.file_id_)) {
    ObServerCheckpointWriter server_ckpt_writer;
    if (OB_FAIL(server_ckpt_writer.init(server_slogger_, *server_runtime_))) {
    } else if (OB_FAIL(server_ckpt_writer.write_checkpoint(cur_cursor))) {
    } else if (OB_FAIL(set_meta_block_list(server_ckpt_writer.get_meta_block_list()))) {
    } else {
      last_write_time_ = start_time;
      last_slog_cursor_ = cur_cursor;
      cost_time = ObTimeUtility::current_time() - start_time;
    }
    SERVER_EVENT_ADD("storage", "write slog checkpoint",
        "ret", ret, "cursor", cur_cursor, "cost_time(us)", cost_time);

    LOG_INFO("finish write server checkpoint", K(ret), K(last_slog_cursor_), K(cur_cursor),
        K_(last_write_time), K(start_time), K(is_force), K(cost_time));
  }

  if (is_writing_checkpoint_set) {
    ATOMIC_STORE(&is_writing_checkpoint_, false);
  }

  return ret;
}

}  // end namespace storage
}  // namespace oceanbase
