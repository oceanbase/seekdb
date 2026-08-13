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

#ifndef OCEANBASE_STORAGE_STANDBY_RESTORE_TABLET_BUILDER_
#define OCEANBASE_STORAGE_STANDBY_RESTORE_TABLET_BUILDER_

#include "standby/restore/ob_standby_restore_rpc.h"
#include "ob_standby_restore_storage_struct.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "ob_storage_restore_struct.h"
#include "ob_standby_restore_reader.h"

namespace oceanbase
{
namespace restore
{
class ObStandbyRestoreHelper;
}

namespace share
{
class SCN;
}
namespace storage
{

class ObStandbyRestoreTableInfoMgr;
struct ObBuildMajorSSTablesParam final
{
  ObBuildMajorSSTablesParam(
      const ObStorageSchema &storage_schema,
      const bool has_truncate_info)
    : storage_schema_(storage_schema),
      has_truncate_info_(has_truncate_info)
  {}
  bool is_valid() const
  {
    return storage_schema_.is_valid();
  }
  TO_STRING_KV(K_(storage_schema), K_(has_truncate_info));
  const ObStorageSchema &storage_schema_;
  const bool has_truncate_info_;
};

class ObStandbyRestoreTableInfoMgr
{
public:
  ObStandbyRestoreTableInfoMgr();
  virtual ~ObStandbyRestoreTableInfoMgr();
  int init();
  int add_table_info(
      const common::ObTabletID &tablet_id,
      const obcall::ObCopyTabletSSTableInfo &sstable_info);
  int get_table_info(
      const common::ObTabletID &tablet_id,
      const ObITable::TableKey &table_key,
      const blocksstable::ObMigrationSSTableParam *&copy_table_info);
  int get_table_keys(
      const common::ObTabletID &tablet_id,
      common::ObIArray<ObITable::TableKey> &table_keys);
  int remove_tablet_table_info(const common::ObTabletID &tablet_id);
  int init_tablet_info(const obcall::ObCopyTabletSSTableHeader &copy_header);
  int check_copy_tablet_exist(const common::ObTabletID &tablet_id, bool &is_exist);
  int check_tablet_table_info_exist(
      const common::ObTabletID &tablet_id, bool &is_exist);
  int get_tablet_meta(
      const common::ObTabletID &tablet_id,
      const ObMigrationTabletParam *&tablet_meta);
  void reuse();

public:
  class ObStandbyRestoreTabletTableInfoMgr
  {
  public:
    ObStandbyRestoreTabletTableInfoMgr();
    virtual ~ObStandbyRestoreTabletTableInfoMgr();
    int init(const common::ObTabletID &tablet_id,
        const storage::ObCopyTabletStatus::STATUS &status,
        const ObMigrationTabletParam &tablet_meta);
    int add_copy_table_info(const blocksstable::ObMigrationSSTableParam &copy_table_info);
    int get_copy_table_info(
        const ObITable::TableKey &table_key,
        const blocksstable::ObMigrationSSTableParam *&copy_table_info);
    int get_table_keys(
        common::ObIArray<ObITable::TableKey> &table_keys);
    int check_copy_tablet_exist(bool &is_exist);
    int get_tablet_meta(const ObMigrationTabletParam *&tablet_meta);
  private:
    bool is_inited_;
    common::ObTabletID tablet_id_;
    storage::ObCopyTabletStatus::STATUS status_;
    common::ObArenaAllocator allocator_;
    common::ObArray<blocksstable::ObMigrationSSTableParam> copy_table_info_array_;
    ObMigrationTabletParam tablet_meta_;
    DISALLOW_COPY_AND_ASSIGN(ObStandbyRestoreTabletTableInfoMgr);
  };

private:
  static const int64_t MAX_BUCEKT_NUM = 4096;
  typedef hash::ObHashMap<common::ObTabletID, ObStandbyRestoreTabletTableInfoMgr *> TabletTableInfoMgr;
  bool is_inited_;
  common::SpinRWLock lock_;
  TabletTableInfoMgr table_info_mgr_map_;
  DISALLOW_COPY_AND_ASSIGN(ObStandbyRestoreTableInfoMgr);
};

struct ObStandbyRestoreCopySSTableParam final
{
  ObStandbyRestoreCopySSTableParam();
  ~ObStandbyRestoreCopySSTableParam() = default;
  bool is_valid() const;
  int assign(const ObStandbyRestoreCopySSTableParam &param);

  TO_STRING_KV(K_(copy_table_key_array), KP_(helper));

  common::ObArray<ObITable::TableKey> copy_table_key_array_;
  restore::ObStandbyRestoreHelper *helper_;
  DISALLOW_COPY_AND_ASSIGN(ObStandbyRestoreCopySSTableParam);
};

class ObStandbyRestoreCopySSTableInfoMgr
{
public:
  ObStandbyRestoreCopySSTableInfoMgr();
  virtual ~ObStandbyRestoreCopySSTableInfoMgr();
  int init(const ObStandbyRestoreCopySSTableParam &param);

  int get_copy_sstable_maro_range_info(
      const ObITable::TableKey &copy_table_key,
      ObCopySSTableMacroRangeInfo &copy_sstable_macro_range_info);
  int check_src_tablet_exist(bool &is_exist);
private:
  int build_sstable_macro_range_info_map_();

private:
  static const int64_t MACRO_RANGE_MAX_MACRO_COUNT = 128;
  typedef hash::ObHashMap<ObITable::TableKey, ObCopySSTableMacroRangeInfo *> CopySSTableMacroRangeInfoMap;
  bool is_inited_;
  ObStandbyRestoreCopySSTableParam param_;
  ObArenaAllocator allocator_;
  CopySSTableMacroRangeInfoMap macro_range_info_map_;
  storage::ObCopyTabletStatus::STATUS status_;
  DISALLOW_COPY_AND_ASSIGN(ObStandbyRestoreCopySSTableInfoMgr);
};

class ObStandbyRestoreTabletBuilderUtil
{
public:
  struct BatchBuildMinorSSTablesParam final
  {
    BatchBuildMinorSSTablesParam();
    ~BatchBuildMinorSSTablesParam() {}
    bool is_valid() const;
    int assign_sstables(
        ObTablesHandleArray &mds_tables,
        ObTablesHandleArray &minor_tables,
        ObTablesHandleArray &ddl_tables);

    ObLS *ls_;
    common::ObTabletID tablet_id_;
    const ObMigrationTabletParam *src_tablet_meta_;
    ObTablesHandleArray mds_tables_;
    ObTablesHandleArray minor_tables_;
    ObTablesHandleArray ddl_tables_;
    ObTabletRestoreAction::ACTION restore_action_;
    share::SCN release_mds_scn_;
    TO_STRING_KV(KP_(ls), K_(tablet_id), KP_(src_tablet_meta), K_(mds_tables),
        K_(minor_tables), K_(ddl_tables), K_(restore_action), K_(release_mds_scn));
    DISALLOW_COPY_AND_ASSIGN(BatchBuildMinorSSTablesParam);
  };

public:
  static int build_tablet_with_major_tables(
      ObLS *ls,
      const common::ObTabletID &tablet_id,
      const ObTablesHandleArray &major_tables,
      const ObBuildMajorSSTablesParam &major_sstables_param);
  static int build_table_with_minor_tables(
      const BatchBuildMinorSSTablesParam &param);
private:
  static int build_tablet_for_row_store_(
      ObLS *ls,
      const common::ObTabletID &tablet_id,
      const ObTablesHandleArray &major_tables,
      const ObBuildMajorSSTablesParam &major_sstables_param);
  static int get_tablet_(
      const common::ObTabletID &tablet_id,
      ObLS *ls,
      ObTabletHandle &tablet_handle);
  static int calc_multi_version_start_with_major_(
      const ObTablesHandleArray &major_tables,
      ObTablet *tablet,
      int64_t &multi_version_start);
  static int inner_update_tablet_table_store_with_major_(
      const int64_t multi_version_start,
      const ObTableHandleV2 &table_handle,
      ObLS *ls,
      ObTablet *tablet,
      const ObBuildMajorSSTablesParam &major_sstables_param);
  static int inner_update_tablet_table_store_with_minor_(
      const BatchBuildMinorSSTablesParam &param,
      ObTablet *tablet,
      const bool &need_tablet_meta_merge,
      const ObTablesHandleArray &tables_handle,
      const bool is_replace_remote);
  static int append_sstable_array_(ObTablesHandleArray &dest_array, const ObTablesHandleArray &src_array);
};


}
}
#endif // OCEANBASE_STORAGE_STANDBY_RESTORE_TABLET_BUILDER_
