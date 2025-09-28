// Copyright (c) 2022 OceanBase
// OceanBase is licensed under Mulan PubL v2.
// You can use this software according to the terms and conditions of the Mulan PubL v2.
// You may obtain a copy of Mulan PubL v2 at:
//          http://license.coscl.org.cn/MulanPubL-2.0
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PubL v2 for more details.
#ifndef OCEABASE_STORAGE_HA_UTILS_H_
#define OCEABASE_STORAGE_HA_UTILS_H_

#include "share/ob_ls_id.h"
#include "common/ob_tablet_id.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "ob_storage_ha_struct.h"
#include "ob_transfer_struct.h"
#include "storage/ob_storage_rpc.h"

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace storage
{
class ObBackfillTXCtx;
class ObTransferHandler;
class ObStorageHAUtils
{
public:
  static int get_server_version(uint64_t &server_version);
  static int check_server_version(const uint64_t server_version);
  static int check_tablet_is_deleted(
      const ObTabletHandle &tablet_handle, 
      bool &is_deleted);

  // When the src_ls of the transfer does not exist, it is necessary to check whether the dest_ls can be rebuilt
  static int check_transfer_ls_can_rebuild(
      const share::SCN replay_scn,
      bool &need_rebuild);
  static int check_disk_space();

  static int calc_tablet_sstable_macro_block_cnt(
      const ObTabletHandle &tablet_handle, int64_t &data_macro_block_count);
  static int check_tenant_will_be_deleted(
      bool &is_deleted);
  static int extract_macro_id_from_datum(
      const ObDatumRowkey &end_key,
      common::ObIArray<MacroBlockId> &macro_block_id_array);

  static int check_log_status(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      int32_t &result);
  static int append_tablet_list(
      const common::ObIArray<ObLogicTabletID> &logic_tablet_id_array,
      common::ObIArray<ObTabletID> &tablet_id_array);
  static int build_major_sstable_reuse_info(
      const ObTabletHandle &tablet_handle,
      ObMacroBlockReuseMgr &macro_block_reuse_mgr,
      const bool &is_restore);
  static void sort_table_key_array_by_snapshot_version(common::ObArray<ObITable::TableKey> &table_key_array);
  static int get_tablet_backup_size_in_bytes(const ObLSID &ls_id, const ObTabletID &tablet_id, int64_t &backup_size);
  static int get_tablet_occupy_size_in_bytes(const ObLSID &ls_id, const ObTabletID &tablet_id, int64_t &occupy_size);
private:
  struct TableKeySnapshotVersionComparator final
  {
    bool operator()(const ObITable::TableKey &lhs, const ObITable::TableKey &rhs) {
      return lhs.get_snapshot_version() < rhs.get_snapshot_version();  
    }
  };
  static int check_merge_error_(const uint64_t tenant_id, common::ObISQLClient &sql_client);
  static int fetch_src_tablet_meta_info_(const uint64_t tenant_id, const common::ObTabletID &tablet_id,
      const share::ObLSID &ls_id, const common::ObAddr &src_addr, common::ObISQLClient &sql_client,
      share::SCN &compaction_scn);
  static int check_tablet_replica_checksum_(const uint64_t tenant_id, const common::ObTabletID &tablet_id,
      const share::ObLSID &ls_id, const share::SCN &compaction_scn, common::ObISQLClient &sql_client);
  static int get_readable_scn_(share::SCN &readable_scn);
  static int get_latest_major_sstable_array_(
      ObTableHandleV2 &latest_major, 
      common::ObArray<ObSSTableWrapper> &major_sstables);
  static int build_reuse_info_(
      const common::ObArray<ObSSTableWrapper> &major_sstabls, 
      const ObTabletHandle &tablet_handle,
      ObMacroBlockReuseMgr &macro_block_reuse_mgr);
  static int get_latest_available_major_(
      storage::ObTableStoreIterator &major_sstables_iter, 
      ObTableHandleV2 &latest_major);
};

struct ObTransferUtils
{
public:
  static int block_tx(const uint64_t tenant_id, const share::ObLSID &ls_id, const share::SCN &gts_scn);
  static int kill_tx(const uint64_t tenant_id, const share::ObLSID &ls_id, const share::SCN &gts_scn);
  static int unblock_tx(const uint64_t tenant_id, const share::ObLSID &ls_id, const share::SCN &gts_scn);
  static int get_gts(const uint64_t tenant_id, share::SCN &gts);
  static void set_transfer_module();
  static void clear_transfer_module();
  static void set_transfer_related_info(
      const share::ObLSID &dest_ls_id,
      const share::ObTransferTaskID &task_id, 
      const share::SCN &start_scn);
  static void reset_related_info(const share::ObLSID &dest_ls_id);

private:
  static int get_ls_(
      ObLSHandle &ls_handle,
      const share::ObLSID &dest_ls_id, 
      ObLS *&ls);
};

} // end namespace storage
} // end namespace oceanbase

#endif
