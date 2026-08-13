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

#ifndef OCEANBASE_STORAGE_OB_TABLET_AUTOINC_SEQ_SERVICE_H_
#define OCEANBASE_STORAGE_OB_TABLET_AUTOINC_SEQ_SERVICE_H_

#include "lib/lock/ob_bucket_lock.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "storage/tablet/ob_tablet_replay_executor.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{
struct BufferCtx;
}
class ObLS;

class ObSyncTabletSeqReplayExecutor final : public ObTabletReplayExecutor
{
public:
  ObSyncTabletSeqReplayExecutor();
  int init(const uint64_t autoinc_seq,
      const bool is_tablet_creating,
      const share::SCN &replay_scn);

  TO_STRING_KV(K_(seq),
               K_(is_tablet_creating),
               K_(scn));

protected:
  bool is_replay_update_tablet_status_() const override
  {
    return is_tablet_creating_;
  }

  // replay to the tablet
  // @return OB_SUCCESS, replay successfully, data has written to tablet.
  // @return OB_EAGAIN, failed to replay, need retry.
  // @return OB_NO_NEED_UPDATE, this log needs to be ignored.
  // @return other error codes, failed to replay.
  int do_replay_(ObTabletHandle &handle) override;

  virtual bool is_replay_update_mds_table_() const override
  {
    return true;
  }

private:
  uint64_t seq_;
  bool is_tablet_creating_;
  share::SCN scn_;
};

class ObTabletAutoincSeqReplayExecutor final : public ObTabletReplayExecutor
{
public:
  ObTabletAutoincSeqReplayExecutor()
    : ObTabletReplayExecutor(), user_ctx_(nullptr), scn_(), data_(nullptr) {}

  int init(
      mds::BufferCtx &user_ctx,
      const share::SCN &scn,
      const ObTabletAutoincSeq &data);

protected:
  bool is_replay_update_tablet_status_() const override
  {
    return true;
  }

  int do_replay_(ObTabletHandle &tablet_handle) override;

  virtual bool is_replay_update_mds_table_() const override
  {
    return true;
  }

private:
  mds::BufferCtx *user_ctx_;
  share::SCN scn_;
  const ObTabletAutoincSeq *data_;
};

class ObTabletAutoincSeqService final
{
public:
  static ObTabletAutoincSeqService &get_instance();
  int init();
  void destroy();
  int fetch_tablet_autoinc_seq_cache(
      const common::ObTabletID &tablet_id,
      const uint64_t cache_size,
      share::ObTabletAutoincInterval &interval);
  int batch_get_tablet_autoinc_seq(
      common::ObIArray<share::ObTabletAutoincSeqCopyParam> &params);
  int batch_set_tablet_autoinc_seq(
      common::ObIArray<share::ObTabletAutoincSeqCopyParam> &params,
      const bool is_tablet_creating = false);
  int replay_update_tablet_autoinc_seq(
      const ObLS *ls,
      const ObTabletID &tablet_id,
      const uint64_t autoinc_seq,
      const bool is_tablet_creating,
      const share::SCN &replay_scn);
  int batch_set_tablet_autoinc_seq_in_trans(
      ObLS &ls,
      const common::ObIArray<share::ObTabletAutoincSeqCopyParam> &params,
      const share::SCN &replay_scn,
      mds::BufferCtx &ctx);
private:
  ObTabletAutoincSeqService();
  ~ObTabletAutoincSeqService();
  int set_tablet_autoinc_seq_in_trans(
      ObLS &ls,
      const ObTabletID &tablet_id,
      const ObTabletAutoincSeq &data,
      const share::SCN &replay_scn,
      mds::BufferCtx &ctx);
private:
  static const int64_t BUCKET_LOCK_BUCKET_CNT = 10243L;
  bool is_inited_;
  common::ObBucketLock bucket_lock_;
};

} // end namespace storage
} // end namespace oceanbase
#endif  // OCEANBASE_STORAGE_OB_TABLET_AUTOINC_SEQ_SERVICE_H_
