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

#include "ob_ddl_redo_log_replayer.h"
#include "storage/ddl/ob_ddl_replay_executor.h"

using namespace oceanbase::common;
using namespace oceanbase::lib;
using namespace oceanbase::blocksstable;
using namespace oceanbase::storage;
using namespace oceanbase::share;
using namespace oceanbase::transaction;

ObDDLRedoLogReplayer::ObDDLRedoLogReplayer()
  : is_inited_(false), ls_(nullptr), allocator_()
{
}

ObDDLRedoLogReplayer::~ObDDLRedoLogReplayer()
{
  destroy();
}

int ObDDLRedoLogReplayer::init(ObLS *ls)
{
  int ret = OB_SUCCESS;
  ObMemAttr attr("RedoLogBuckLock");
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDDLRedoLogReplayer has been inited twice", K(ret));
  } else if (OB_FAIL(allocator_.init(TOTAL_LIMIT, HOLD_LIMIT, OB_MALLOC_NORMAL_BLOCK_SIZE))) {
    LOG_WARN("fail to init allocator", K(ret));
  } else if (OB_FAIL(bucket_lock_.init(DEFAULT_HASH_BUCKET_COUNT, ObLatchIds::DEFAULT_BUCKET_LOCK, attr))) {
    LOG_WARN("fail to init bucket lock", K(ret));
  } else {
    ls_ = ls;
    is_inited_ = true;
  }
  return ret;
}

int ObDDLRedoLogReplayer::replay_start(const ObDDLStartLog &log, const SCN &scn)
{
  int ret = OB_SUCCESS;
  ObDDLStartReplayExecutor replay_executor;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_FAIL(replay_executor.init(ls_, log, scn))) {
    LOG_WARN("failed to init ddl start log replay executor", K(ret));
  } else if (OB_FAIL(replay_executor.execute(scn, log.get_table_key().tablet_id_))) {
    if (OB_NO_NEED_UPDATE == ret) {
      ret = OB_SUCCESS;
    } else if (OB_EAGAIN != ret) {
      LOG_ERROR("failed to replay", K(ret), K(log), K(scn));
    }
  }

  return ret;
}

int ObDDLRedoLogReplayer::replay_redo(const ObDDLRedoLog &log, const SCN &scn)
{
  int ret = OB_SUCCESS;
  ObDDLRedoReplayExecutor replay_executor;

  DEBUG_SYNC(BEFORE_REPLAY_DDL_MACRO_BLOCK);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_FAIL(replay_executor.init(ls_, log, scn))) {
    LOG_WARN("failed to init ddl redo log replay executor", K(ret));
  } else if (OB_FAIL(replay_executor.execute(scn, log.get_redo_info().table_key_.tablet_id_))) {
    if (OB_NO_NEED_UPDATE == ret) {
      ret = OB_SUCCESS;
    } else if (OB_EAGAIN != ret) {
      LOG_ERROR("failed to replay", K(ret), K(log), K(scn));
    }
  }

  return ret;
}

int ObDDLRedoLogReplayer::replay_commit(const ObDDLCommitLog &log, const SCN &scn)
{
  int ret = OB_SUCCESS;
  ObDDLCommitReplayExecutor replay_executor;

  DEBUG_SYNC(BEFORE_REPLAY_DDL_PREPRARE);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_FAIL(replay_executor.init(ls_, log, scn))) {
    LOG_WARN("init replay executor failed", K(ret));
  } else if (OB_FAIL(replay_executor.execute(scn, log.get_table_key().tablet_id_))) {
    LOG_WARN("execute replay execute failed", K(ret));
  }
  return ret;
}

int ObDDLRedoLogReplayer::replay_table_fork_freeze(const ObTableForkFreezeLog &log, const share::SCN &scn)
{
  int ret = OB_SUCCESS;
  ObTabletForkFreezeReplayExecutor replay_executor;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_FAIL(replay_executor.init(ls_, log, scn))) {
    LOG_WARN("failed to init table fork freeze log replay executor", K(ret));
  } else {
    const ObSArray<ObTabletID> &tablet_ids = log.tablet_ids_;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
      const ObTabletID &tablet_id = tablet_ids.at(i);
      if (OB_FAIL(replay_executor.execute(scn, tablet_id))) {
        if (OB_TABLET_NOT_EXIST == ret || OB_NO_NEED_UPDATE == ret || OB_TASK_EXPIRED == ret) {
          ret = OB_SUCCESS;
        } else if (OB_EAGAIN != ret) {
          LOG_ERROR("failed to replay table fork freeze log", K(ret), K(scn), K(log), K(tablet_id));
        }
      }
    }
  }

  return ret;
}

int ObDDLRedoLogReplayer::replay_table_fork_start(const ObTableForkStartLog &log, const share::SCN &scn)
{
  int ret = OB_SUCCESS;
  ObTabletForkStartReplayExecutor replay_executor;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_FAIL(replay_executor.init(ls_, log, scn))) {
    LOG_WARN("failed to init table fork start log replay executor", K(ret));
  } else {
    const ObSEArray<ObTabletID, 4> &source_tablet_ids = log.fork_info_.source_tablet_ids_;
    for (int64_t i = 0; OB_SUCC(ret) && i < source_tablet_ids.count(); ++i) {
      const ObTabletID &tablet_id = source_tablet_ids.at(i);
      if (OB_FAIL(replay_executor.execute(scn, tablet_id))) {
        if (OB_TABLET_NOT_EXIST == ret || OB_NO_NEED_UPDATE == ret || OB_TASK_EXPIRED == ret) {
          ret = OB_SUCCESS;
        } else if (OB_EAGAIN != ret) {
          LOG_ERROR("failed to replay table fork start log", K(ret), K(scn), K(log), K(tablet_id));
        }
      }
    }
  }
  return ret;
}

int ObDDLRedoLogReplayer::replay_table_fork_finish(const ObTableForkFinishLog &log, const share::SCN &scn)
{
  int ret = OB_SUCCESS;
  ObTabletForkFinishReplayExecutor replay_executor;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_FAIL(replay_executor.init(ls_, log, scn))) {
    LOG_WARN("failed to init table fork finish log replay executor", K(ret));
  } else {
    const ObSEArray<ObTabletID, 4> &source_tablet_ids = log.fork_info_.source_tablet_ids_;
    for (int64_t i = 0; OB_SUCC(ret) && i < source_tablet_ids.count(); ++i) {
      const ObTabletID &tablet_id = source_tablet_ids.at(i);
      if (OB_FAIL(replay_executor.execute(scn, tablet_id))) {
        if (OB_TABLET_NOT_EXIST == ret || OB_NO_NEED_UPDATE == ret || OB_TASK_EXPIRED == ret) {
          ret = OB_SUCCESS;
        } else if (OB_EAGAIN != ret) {
          LOG_ERROR("failed to replay fork table finish log", K(ret), K(scn), K(log), K(tablet_id));
        }
      }
    }
  }
  return ret;
}

void ObDDLRedoLogReplayer::destroy()
{
  is_inited_ = false;
  ls_ = nullptr;
  allocator_.reset();
}
