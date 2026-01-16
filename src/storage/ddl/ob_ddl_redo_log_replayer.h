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

#ifndef OCEANBASE_STORAGE_OB_DDL_REDO_LOG_REPLAYER_H
#define OCEANBASE_STORAGE_OB_DDL_REDO_LOG_REPLAYER_H

#include "storage/ddl/ob_ddl_clog.h"
#include "storage/ddl/ob_ddl_inc_clog.h"
#include "storage/ddl/ob_ddl_struct.h"
#include "storage/blocksstable/ob_block_sstable_struct.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
class ObTabletHandle;

class ObDDLRedoLogReplayer final
{
public:
  ObDDLRedoLogReplayer();
  ~ObDDLRedoLogReplayer();
  int init(ObLS *ls);
  void reset() { destroy(); }
  int replay_start(const ObDDLStartLog &log, const share::SCN &scn);
  int replay_redo(const ObDDLRedoLog &log, const share::SCN &scn);
  int replay_commit(const ObDDLCommitLog &log, const share::SCN &scn);
  int replay_split_start(const ObTabletSplitStartLog &log, const share::SCN &scn);
  int replay_split_finish(const ObTabletSplitFinishLog &log, const share::SCN &scn);
  int replay_tablet_freeze(const ObTabletFreezeLog &log, const share::SCN &scn);
  int replay_table_fork_freeze(const ObTableForkFreezeLog &log, const share::SCN &scn);
  int replay_table_fork_start(const ObTableForkStartLog &log, const share::SCN &scn);
  int replay_table_fork_finish(const ObTableForkFinishLog &log, const share::SCN &scn);
  #ifdef OB_BUILD_SHARED_STORAGE
  int replay_finish(const ObDDLFinishLog &log, const share::SCN &scn);
  #endif
  int replay_inc_start(const ObDDLIncStartLog &log, const share::SCN &scn);
  int replay_inc_commit(const ObDDLIncCommitLog &log, const share::SCN &scn);
private:
  void destroy();
  template <typename IncType, typename ...Args>
  int do_replay_inc_start(const common::ObTabletID &tablet_id, const SCN &scn, Args&&... args);
  int do_replay_inc_minor_commit(const common::ObTabletID &tablet_id, const SCN &scn);
private:
  static const int64_t TOTAL_LIMIT = 10 * 1024 * 1024 * 1024L;
  static const int64_t HOLD_LIMIT = 10 * 1024 * 1024 * 1024L;
  static const int64_t DEFAULT_HASH_BUCKET_COUNT = 100;
  static const int64_t DEFAULT_ID_MAP_HASH_BUCKET_COUNT = 1543;
  static const int64_t RETRY_INTERVAL = 100 * 1000L; // 100ms
  bool is_inited_;
  ObLS *ls_;
  common::ObConcurrentFIFOAllocator allocator_;
  common::ObBucketLock bucket_lock_;
};


}  // end namespace storage
}  // end namespace oceanbase

#endif  // OCEANBASE_STORAGE_OB_DDL_REDO_LOG_REPLAYER_H

