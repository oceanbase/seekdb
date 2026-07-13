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

#ifndef OCEANBASE_STORAGE_OB_TABLET_CREATE_DELETE_HELPER
#define OCEANBASE_STORAGE_OB_TABLET_CREATE_DELETE_HELPER

#include <stdint.h>
#include "lib/container/ob_array.h"
#include "lib/container/ob_array_serialization.h"
#include "lib/hash/ob_hashset.h"
#include "common/ob_tablet_id.h"
#include "storage/memtable/ob_memtable.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "storage/meta_mem/ob_tenant_meta_mem_mgr.h"
#include "share/scn.h"
#include "storage/tablet/ob_tablet_status.h"
#include "storage/tablet/ob_tablet_common.h"
#include "storage/tablet/ob_tablet_mds_data_cache.h"

namespace oceanbase
{
namespace blocksstable
{
class ObSSTable;
}

namespace transaction
{
class ObTransID;
}

namespace storage
{
class ObTabletMapKey;
class ObLS;
class ObTabletCreateSSTableParam;
class ObTableHandleV2;
class ObTablet;
class ObTabletCreateDeleteMdsUserData;

class ObTabletCreateDeleteHelper
{
public:
  static int replay_mds_get_tablet( const ObTabletMapKey &key, ObLS *ls, ObTabletHandle &handle);
  static int get_tablet(
      const ObTabletMapKey &key,
      ObTabletHandle &handle,
      const int64_t timeout_us = ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US);

  // snapshot version is used for multi source data reading,
  // tablet's multi source data will infect its visibility.
  // if snapshot version is MAX_TRANS_VERSION, it means we'll ignore
  // tablet creation/deletion transaction commit version,
  // and the tablet is fully visible as long as it really exists.
  static int check_and_get_tablet(
      const ObTabletMapKey &key,
      ObTabletHandle &handle,
      const int64_t timeout_us,
      const ObMDSGetTabletMode mode,
      const int64_t snapshot_version);
  static int check_status_for_new_mds(
      const ObTablet &tablet,
      const int64_t snapshot_version,
      ObTabletStatusCache &tablet_status_cache);
  static int check_read_snapshot_by_commit_version(
      const ObTablet &tablet,
      const int64_t create_commit_version,
      const int64_t delete_commit_version,
      const int64_t snapshot_version,
      const ObTabletStatus &tablet_status);
  static int check_read_snapshot_for_normal_or_split_dst(
      const ObTablet &tablet,
      const int64_t snapshot_version,
      const ObTabletCreateDeleteMdsUserData &user_data,
      const mds::MdsWriter &writer,
      const mds::TwoPhaseCommitState &trans_state,
      const share::SCN &trans_version);
  static int check_read_snapshot_for_deleted(
      const ObTablet &tablet,
      const int64_t snapshot_version,
      const ObTabletCreateDeleteMdsUserData &user_data,
      const mds::MdsWriter &writer,
      const mds::TwoPhaseCommitState &trans_state,
      const share::SCN &trans_version);
  static int check_read_snapshot_for_split_src(
      const ObTablet &tablet,
      const int64_t snapshot_version,
      const ObTabletCreateDeleteMdsUserData &user_data,
      const mds::TwoPhaseCommitState &trans_state);
  static int check_read_snapshot_for_split_src_deleted(
      const ObTablet &tablet,
      const ObTabletCreateDeleteMdsUserData &user_data,
      const mds::TwoPhaseCommitState &trans_state);
  static int check_read_snapshot_by_commit_version(
      const int64_t snapshot_version,
      const ObTabletCreateDeleteMdsUserData &user_data);
  static int check_read_snapshot_for_committed_create_tx(
      const ObTablet &tablet,
      const int64_t snapshot_version,
      const ObTabletCreateDeleteMdsUserData &user_data);
  static int check_read_snapshot_for_create_tx(
      const ObTablet &tablet,
      const int64_t snapshot_version,
      const ObTabletCreateDeleteMdsUserData &user_data,
      const mds::MdsWriter &writer,
      const mds::TwoPhaseCommitState &trans_state,
      const share::SCN &trans_version);
public:
  static int create_tmp_tablet(
      const ObTabletMapKey &key,
      common::ObArenaAllocator &allocator,
      ObLS &ls,
      ObTabletHandle &handle);
  static int prepare_create_msd_tablet();
  static int create_msd_tablet(
      const ObTabletMapKey &key,
      ObTabletHandle &handle);
  static int acquire_tmp_tablet(
      const ObTabletMapKey &key,
      common::ObArenaAllocator &allocator,
      ObTabletHandle &handle);
  static int acquire_tablet_from_pool(
      const ObTabletPoolType &type,
      const ObTabletMapKey &key,
      ObTabletHandle &handle);
  // Attention !!! only used when first creating tablet
  static int create_empty_sstable(
      common::ObArenaAllocator &allocator,
      const ObStorageSchema &storage_schema,
      const common::ObTabletID &tablet_id,
      const int64_t snapshot_version,
      ObTableHandleV2 &table_handle);

  template <typename T = blocksstable::ObSSTable>
  static int create_sstable(
      const ObTabletCreateSSTableParam &param,
      common::ObArenaAllocator &allocator,
      ObTableHandleV2 &table_handle);
  template<typename T = blocksstable::ObSSTable>
  static int create_sstable(
      const ObTabletCreateSSTableParam &param,
      common::ObArenaAllocator &allocator,
      T &sstable);
  static bool is_pure_hidden_tablets(const obcall::ObCreateTabletInfo &info);

private:
  class ReadMdsFunctor
  {
  public:
    ReadMdsFunctor(ObTabletCreateDeleteMdsUserData &user_data);
  private:
    ObTabletCreateDeleteMdsUserData &user_data_;
  };
  class DummyReadMdsFunctor
  {
  public:
    int operator()(const ObTabletCreateDeleteMdsUserData &) { return common::OB_SUCCESS; }
  };
};

template <typename T>
int ObTabletCreateDeleteHelper::create_sstable(
    const ObTabletCreateSSTableParam &param,
    common::ObArenaAllocator &allocator,
    ObTableHandleV2 &table_handle)
{
  int ret = common::OB_SUCCESS;
  void *buf = allocator.alloc(sizeof(T));
  T *sstable = nullptr;
  if (OB_ISNULL(buf)) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "fail to allocate sstable memory", K(ret));
  } else if (FALSE_IT(sstable = new (buf) T())) {
  } else if (OB_FAIL(create_sstable(param, allocator, *sstable))) {
    STORAGE_LOG(WARN, "fail to create sstable", K(ret));
  } else if (OB_FAIL(table_handle.set_sstable(sstable, &allocator))) {
    STORAGE_LOG(WARN, "fail to set table handle", K(ret), KPC(sstable));
  }
  return ret;
}

template <typename T>
int ObTabletCreateDeleteHelper::create_sstable(
    const ObTabletCreateSSTableParam &param,
    common::ObArenaAllocator &allocator,
    T &sstable)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = common::OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid args", K(ret), K(param));
  } else if (OB_FAIL(sstable.init(param, &allocator))) {
    STORAGE_LOG(WARN, "fail to init sstable", K(ret), K(param));
  }
  return ret;
}
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_CREATE_DELETE_HELPER
