// Copyright (c) 2022 OceanBase
// OceanBase is licensed under Mulan PubL v2.
// You can use this software according to the terms and conditions of the Mulan PubL v2.
// You may obtain a copy of Mulan PubL v2 at:
//          http://license.coscl.org.cn/MulanPubL-2.0
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PubL v2 for more details.

#define USING_LOG_PREFIX STORAGE

#include "ob_storage_ha_utils.h"
#include "share/tablet/ob_tablet_table_operator.h"
#include "share/ob_global_merge_table_operator.h"
#include "share/ob_tablet_replica_checksum_operator.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "rootserver/ob_tenant_info_loader.h"
#include "observer/omt/ob_tenant.h"
#include "storage/tablet/ob_mds_schema_helper.h"
#include "share/ob_io_device_helper.h"
#include "storage/high_availability/ob_storage_ha_dag.h"

using namespace oceanbase::share;

namespace oceanbase
{
namespace storage
{

ERRSIM_POINT_DEF(EN_TRANSFER_ALLOW_RETRY);
ERRSIM_POINT_DEF(EN_CHECK_LOG_NEED_REBUILD);



int ObStorageHAUtils::get_server_version(uint64_t &server_version)
{
  int ret = OB_SUCCESS;
  server_version = CLUSTER_CURRENT_VERSION;
  return ret;
}

int ObStorageHAUtils::check_server_version(const uint64_t server_version)
{
  int ret = OB_SUCCESS;
  uint64_t cur_server_version = 0;
  if (OB_FAIL(get_server_version(cur_server_version))) {
    LOG_WARN("failed to get server version", K(ret));
  } else {
    bool can_migrate = cur_server_version >= server_version;
    if (!can_migrate) {
      ret = OB_MIGRATE_NOT_COMPATIBLE;
      LOG_WARN("migrate server not compatible", K(ret), K(server_version), K(cur_server_version));
    }
  }
  return ret;
}


int ObStorageHAUtils::check_merge_error_(const uint64_t tenant_id, common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  share::ObGlobalMergeInfo merge_info;
  if (OB_FAIL(ObGlobalMergeTableOperator::load_global_merge_info(sql_client, tenant_id, merge_info))) {
    LOG_WARN("failed to laod global merge info", K(ret), K(tenant_id));
  } else if (merge_info.is_merge_error()) {
    ret = OB_CHECKSUM_ERROR;
    LOG_ERROR("merge error, can not migrate", K(ret), K(tenant_id), K(merge_info));
  }
  return ret;
}

int ObStorageHAUtils::fetch_src_tablet_meta_info_(const uint64_t tenant_id, const common::ObTabletID &tablet_id,
    const share::ObLSID &ls_id, const common::ObAddr &src_addr, common::ObISQLClient &sql_client, SCN &compaction_scn)
{
  int ret = OB_SUCCESS;
  ObTabletTableOperator op;
  ObTabletReplica tablet_replica;
  if (OB_FAIL(op.init(share::OBCG_STORAGE, sql_client))) {
    LOG_WARN("failed to init operator", K(ret));
  } else if (OB_FAIL(op.get(tenant_id, tablet_id, ls_id, src_addr, tablet_replica))) {
    LOG_WARN("failed to get tablet meta info", K(ret), K(tenant_id), K(tablet_id), K(ls_id), K(src_addr));
  } else if (OB_FAIL(compaction_scn.convert_for_tx(tablet_replica.get_snapshot_version()))) {
    LOG_WARN("failed to get tablet meta info", K(ret), K(compaction_scn), K(tenant_id), K(tablet_id), K(ls_id), K(src_addr));
  } else {/*do nothing*/}
  return ret;
}

int ObStorageHAUtils::check_tablet_replica_checksum_(const uint64_t tenant_id, const common::ObTabletID &tablet_id,
    const share::ObLSID &ls_id, const SCN &compaction_scn, common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  ObReplicaCkmArray items;
  ObArray<ObTabletLSPair> pairs;
  ObTabletLSPair pair;
  if (OB_FAIL(pair.init(tablet_id, ls_id))) {
    LOG_WARN("failed to init pair", K(ret), K(tablet_id), K(ls_id));
  } else if (OB_FAIL(pairs.push_back(pair))) {
    LOG_WARN("failed to push back", K(ret), K(pair));
  } else if (OB_FAIL(items.init(tenant_id, 1/*expect_cnt*/))) {
    LOG_WARN("failed to init ckm array", KR(ret), K(items));
  } else if (OB_FAIL(ObTabletReplicaChecksumOperator::batch_get(tenant_id, pairs, compaction_scn,
      sql_client, items, false/*include_larger_than*/, share::OBCG_STORAGE/*group_id*/))) {
    LOG_WARN("failed to batch get replica checksum item", K(ret), K(tenant_id), K(pairs), K(compaction_scn));
  } else {
    ObArray<share::ObTabletReplicaChecksumItem> filter_items;
    ObTabletDataChecksumChecker data_checksum_checker;
    for (int64_t i = 0; OB_SUCC(ret) && i < items.count(); ++i) {
      const ObTabletReplicaChecksumItem &item = items.at(i);
      if (item.compaction_scn_ == compaction_scn) {
        if (OB_FAIL(filter_items.push_back(item))) {
          LOG_WARN("failed to push back", K(ret), K(item));
        }
      }
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < filter_items.count(); ++i) {
      const ObTabletReplicaChecksumItem &first_item = filter_items.at(0);
      const ObTabletReplicaChecksumItem &item = filter_items.at(i);
      if (OB_FAIL(data_checksum_checker.check_data_checksum(item))) {
        LOG_ERROR("failed to verify data checksum", K(ret), K(tenant_id), K(tablet_id),
            K(ls_id), K(compaction_scn), K(item), K(filter_items));
      } else if (OB_FAIL(item.verify_column_checksum(first_item))) {
        LOG_ERROR("failed to verify column checksum", K(ret), K(tenant_id), K(tablet_id),
            K(ls_id), K(compaction_scn), K(first_item), K(item), K(filter_items));
      }
    }
  }
  return ret;
}


int ObStorageHAUtils::check_tablet_is_deleted(
    const ObTabletHandle &tablet_handle,
    bool &is_deleted)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObTabletCreateDeleteMdsUserData data;

  is_deleted = false;

  if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet is null", K(ret));
  } else if (tablet->is_empty_shell()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet is empty shell", K(ret), KPC(tablet));
  } else if (OB_FAIL(tablet->ObITabletMdsInterface::get_tablet_status(share::SCN::max_scn(), data, ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US))) {
    if (OB_EMPTY_RESULT == ret || OB_ERR_SHARED_LOCK_CONFLICT == ret) {
      LOG_WARN("tablet_status is null or not committed", K(ret), KPC(tablet));
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get latest tablet status", K(ret), KPC(tablet));
    }
  } else if (ObTabletStatus::DELETED == data.tablet_status_
             || ObTabletStatus::TRANSFER_OUT_DELETED == data.tablet_status_
             || ObTabletStatus::SPLIT_SRC_DELETED == data.tablet_status_) {
    is_deleted = true;
  }
  return ret;
}

int ObStorageHAUtils::check_transfer_ls_can_rebuild(
    const share::SCN replay_scn,
    bool &need_rebuild)
{
  int ret = OB_SUCCESS;
  SCN readable_scn = SCN::base_scn();
  need_rebuild = false;
  if (!replay_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("argument invalid", K(ret), K(replay_scn));
  } else if (MTL_TENANT_ROLE_CACHE_IS_INVALID()) {
    ret = OB_NEED_RETRY;
    LOG_WARN("tenant role is invalid, need retry", KR(ret), K(replay_scn));
  } else if (MTL_TENANT_ROLE_CACHE_IS_PRIMARY()) {
    need_rebuild = true;
  } else if (OB_FAIL(get_readable_scn_(readable_scn))) {
    LOG_WARN("failed to get readable scn", K(ret), K(replay_scn));
  } else if (readable_scn >= replay_scn) {
    need_rebuild = true;
  } else {
    need_rebuild = false;
  }
  return ret;
}


int ObStorageHAUtils::get_readable_scn_(share::SCN &readable_scn)
{
  int ret = OB_SUCCESS;
  readable_scn.set_base();
  rootserver::ObTenantInfoLoader *info = MTL(rootserver::ObTenantInfoLoader*);
  if (OB_ISNULL(info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant info is null", K(ret), KP(info));
  } else if (OB_FAIL(info->get_readable_scn(readable_scn))) {
    LOG_WARN("failed to get readable scn", K(ret), K(readable_scn));
  } else if (!readable_scn.is_valid()) {
    ret = OB_EAGAIN;
    LOG_WARN("readable_scn not valid", K(ret), K(readable_scn));
  }
  return ret;
}


int ObStorageHAUtils::check_disk_space()
{
  int ret = OB_SUCCESS;
  const int64_t required_size = 0;
  if (OB_FAIL(LOCAL_DEVICE_INSTANCE.check_space_full(required_size))) {
    LOG_WARN("failed to check is disk full, cannot transfer in", K(ret));
  }
  return ret;
}

int ObStorageHAUtils::calc_tablet_sstable_macro_block_cnt(
    const ObTabletHandle &tablet_handle, int64_t &data_macro_block_count)
{
  int ret = OB_SUCCESS;
  data_macro_block_count = 0;
  storage::ObTableStoreIterator table_store_iter;
  if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid tablet handle", K(ret), K(tablet_handle));
  } else if (OB_FAIL(tablet_handle.get_obj()->get_all_sstables(table_store_iter))) {
    LOG_WARN("failed to get all tables", K(ret), K(tablet_handle));
  } else if (0 == table_store_iter.count()) {
    // do nothing
  } else {
    ObITable *table_ptr = NULL;
    while (OB_SUCC(ret)) {
      table_ptr = NULL;
      if (OB_FAIL(table_store_iter.get_next(table_ptr))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to get next", K(ret));
        }
      } else if (OB_ISNULL(table_ptr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table should not be null", K(ret));
      } else if (!table_ptr->is_sstable()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table is not sstable", K(ret), KPC(table_ptr));
      } else {
        data_macro_block_count += static_cast<blocksstable::ObSSTable *>(table_ptr)->get_data_macro_block_count();
      }
    }
  }
  return ret;
}


int ObStorageHAUtils::check_tenant_will_be_deleted(
    bool &is_deleted)
{
  int ret = OB_SUCCESS;
  is_deleted = false;

  share::ObTenantBase *tenant_base = MTL_CTX();
  omt::ObTenant *tenant = nullptr;
  ObUnitInfoGetter::ObUnitStatus unit_status;
  if (OB_ISNULL(tenant_base)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant base should not be NULL", K(ret), KP(tenant_base));
  } else if (FALSE_IT(tenant = static_cast<omt::ObTenant *>(tenant_base))) {
  } else if (FALSE_IT(unit_status = tenant->get_unit_status())) {
  } else if (ObUnitInfoGetter::is_unit_will_be_deleted_in_observer(unit_status)) {
    is_deleted = true;
    FLOG_INFO("unit wait gc in observer, allow gc", K(tenant->id()), K(unit_status));
  }
  return ret;
}


//TODO(yangyi.yyy) put this interface into tablet

int ObStorageHAUtils::extract_macro_id_from_datum(
    const ObDatumRowkey &end_key,
    common::ObIArray<MacroBlockId> &macro_block_id_array)
{
  int ret = OB_SUCCESS;
  MacroBlockId macro_block_id;
  int64_t pos = 0;
  macro_block_id_array.reset();

  if (!end_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("extract macro id from datum get invalid argument", K(ret), K(end_key));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < end_key.get_datum_cnt(); ++i) {
      macro_block_id.reset();
      pos = 0;
      const ObString &macro_block_id_str = end_key.get_datum_ptr()[i].get_string();
      if (OB_FAIL(macro_block_id.deserialize(macro_block_id_str.ptr(), macro_block_id_str.length(), pos))) {
        LOG_WARN("failed to deserialize macro block id", K(ret), K(macro_block_id_str));
      } else if (!macro_block_id.is_valid() || !macro_block_id.is_id_mode_share()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("macro block id is invalid, unexpected", K(ret), K(macro_block_id));
      } else if (OB_FAIL(macro_block_id_array.push_back(macro_block_id))) {
        LOG_WARN("failed to push macro block id into array", K(ret), K(macro_block_id));
      }
    }
  }
  return ret;
}


int ObStorageHAUtils::append_tablet_list(
    const common::ObIArray<ObLogicTabletID> &logic_tablet_id_array,
    common::ObIArray<ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < logic_tablet_id_array.count(); ++i) {
    const ObLogicTabletID &logic_tablet_id = logic_tablet_id_array.at(i);
    if (OB_FAIL(tablet_id_array.push_back(logic_tablet_id.tablet_id_))) {
      LOG_WARN("failed to push back tablet id", K(ret), K(logic_tablet_id));
    }
  }
  return ret;
}

int ObStorageHAUtils::get_tablet_backup_size_in_bytes(
    const ObLSID &ls_id, const ObTabletID &tablet_id, int64_t &backup_size)
{
  int ret = OB_SUCCESS;
  backup_size = 0;
  ObTabletResidentInfo info;
  const ObTabletMapKey key(ls_id, tablet_id);
  ObTenantMetaMemMgr *t3m = MTL(ObTenantMetaMemMgr*);

  if (!ls_id.is_valid() || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(t3m->get_tablet_resident_info(key, info))) {
    LOG_WARN("fail to get tablet resident_info", K(ret), K(key));
  } else if (!info.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid resident_info", K(ret), K(key), K(info));
  } else {
    backup_size = info.get_backup_size();
  }
  return ret;
}

int ObStorageHAUtils::get_tablet_occupy_size_in_bytes(
    const ObLSID &ls_id, const ObTabletID &tablet_id, int64_t &occupy_size)
{
  int ret = OB_SUCCESS;
  occupy_size = 0;
  ObTabletResidentInfo info;
  const ObTabletMapKey key(ls_id, tablet_id);
  ObTenantMetaMemMgr *t3m = MTL(ObTenantMetaMemMgr*);

  if (!ls_id.is_valid() || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(t3m->get_tablet_resident_info(key, info))) {
    LOG_WARN("fail to get tablet resident_info", K(ret), K(key));
  } else if (!info.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid resident_info", K(ret), K(key), K(info));
  } else {
    occupy_size = info.get_occupy_size() + info.get_backup_size();
  }
  return ret;
}


int ObTransferUtils::get_gts(const uint64_t tenant_id, SCN &gts)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_TENANT_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant id is invalid", K(ret), K(tenant_id));
  } else {
    ret = OB_EAGAIN;
    const transaction::MonotonicTs stc = transaction::MonotonicTs::current_time();
    transaction::MonotonicTs unused_ts(0);
    const int64_t start_time = ObTimeUtility::fast_current_time();
    const int64_t TIMEOUT = 10 * 1000 * 1000; //10s
    while (OB_EAGAIN == ret) {
      if (ObTimeUtility::fast_current_time() - start_time > TIMEOUT) {
        ret = OB_TIMEOUT;
        LOG_WARN("get gts timeout", KR(ret), K(start_time), K(TIMEOUT));
      } else if (OB_FAIL(OB_TS_MGR.get_gts(tenant_id, stc, NULL, gts, unused_ts))) {
        if (OB_EAGAIN != ret) {
          LOG_WARN("failed to get gts", KR(ret), K(tenant_id));
        } else {
          // waiting 10ms
          ob_usleep(10L * 1000L);
        }
      }
    }
  }
  LOG_INFO("get tenant gts", KR(ret), K(tenant_id), K(gts));
  return ret;
}


int ObStorageHAUtils::check_log_status(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    int32_t &result)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObLSHandle ls_handle;
  bool is_log_sync = false;
  bool need_rebuild = false;
  bool has_fatal_error = false;
  result = OB_SUCCESS;

  if (OB_INVALID_TENANT_ID == tenant_id || !ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("argument is not valid", K(ret), K(tenant_id), K(ls_id));
  } else if (OB_FAIL(ObStorageHADagUtils::get_ls(ls_id, ls_handle))) {
    LOG_WARN("failed to get ls", K(ret), K(tenant_id), K(ls_id));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls), K(tenant_id), K(ls_id));
  } else if (OB_ISNULL(ls->get_log_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log handler should not be NULL", K(ret), K(tenant_id), K(ls_id));
  } else {
    if (OB_FAIL(ls->get_log_handler()->is_replay_fatal_error(has_fatal_error))) {
      if (OB_EAGAIN == ret) {
        has_fatal_error = false;
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to check replay fatal error", K(ret));
      }
    } else if (has_fatal_error) {
      result = OB_LOG_REPLAY_ERROR;
      LOG_WARN("log replay error", K(tenant_id), K(ls_id), K(result));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_SUCCESS != result) {
      //do nothing
    } else if (OB_FAIL(ls->get_log_handler()->is_in_sync(is_log_sync, need_rebuild))) {
      LOG_WARN("failed to get is_in_sync", K(ret), K(tenant_id), K(ls_id));
    } else if (need_rebuild) {
      result = OB_LS_NEED_REBUILD;
      LOG_WARN("ls need rebuild", K(tenant_id), K(ls_id), K(result));
    }
  }
#ifdef ERRSIM
  if (OB_SUCC(ret)) {
    int tmp_ret = OB_SUCCESS;
    tmp_ret = EN_CHECK_LOG_NEED_REBUILD ? : OB_SUCCESS;
    if (OB_TMP_FAIL(tmp_ret)) {
      result = OB_LS_NEED_REBUILD;
      SERVER_EVENT_ADD("storage_ha", "check_log_need_rebuild",
                      "tenant_id", tenant_id,
                      "ls_id", ls_id.id(),
                      "result", result);
      DEBUG_SYNC(AFTER_CHECK_LOG_NEED_REBUILD);
    }
  }
#endif
  return ret;
}

void ObTransferUtils::set_transfer_module()
{
#ifdef ERRSIM
  if (ObErrsimModuleType::ERRSIM_MODULE_NONE == THIS_WORKER.get_module_type().type_) {
    ObErrsimModuleType type(ObErrsimModuleType::ERRSIM_MODULE_TRANSFER);
    THIS_WORKER.set_module_type(type);
  }
#endif
}

void ObTransferUtils::clear_transfer_module()
{
#ifdef ERRSIM
  if (ObErrsimModuleType::ERRSIM_MODULE_TRANSFER == THIS_WORKER.get_module_type().type_) {
    ObErrsimModuleType type(ObErrsimModuleType::ERRSIM_MODULE_NONE);
    THIS_WORKER.set_module_type(type);
  }
#endif
}


void ObTransferUtils::set_transfer_related_info(
    const share::ObLSID &dest_ls_id,
    const share::ObTransferTaskID &task_id,
    const share::SCN &start_scn)
{
}

int ObTransferUtils::get_ls_(
    ObLSHandle &ls_handle,
    const share::ObLSID &dest_ls_id,
    ObLS *&ls)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = MTL(ObLSService*);
  ls = nullptr;
  if (!dest_ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument!", K(ret), K(dest_ls_id));
  } else if (OB_FAIL(ls_service->get_ls(dest_ls_id, ls_handle, ObLSGetMod::MDS_TABLE_MOD))) {
    LOG_WARN("failed to get ls", K(ret), K(ls_handle), K(dest_ls_id));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls is null", K(ret), K(dest_ls_id), K(ls_handle));
  }
  return ret;
}



int ObStorageHAUtils::build_major_sstable_reuse_info(
      const ObTabletHandle &tablet_handle,
      ObMacroBlockReuseMgr &macro_block_reuse_mgr,
      const bool &is_restore)
{
  // 1. get local max major sstable snapshot version (and related sstable)
  // 2. iterate these major sstables' macro blocks (if not co, there is only one major sstable), update reuse map
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObTabletMemberWrapper<ObTabletTableStore> wrapper;
  ObTableHandleV2 latest_major;

  if (!tablet_handle.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_handle));
  } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
  } else {
    if (macro_block_reuse_mgr.is_inited()) {
      LOG_INFO("reuse info mgr has been inited before (maybe retry), won't init again", K(macro_block_reuse_mgr.is_inited()));
    } else if (OB_FAIL(macro_block_reuse_mgr.init())) {
      LOG_WARN("failed to init reuse info mgr", K(ret));
    }

    // 1. for shared storage mode, we won't build reuse info
    // 2. when restore, we won't build reuse info for lastest major sstable, because restore always read all sstable from backup media
    // (i.e. will scan all sstables' macro block again when tablet restore dag retry)
    // when migrate, we will keep the major sstable already been copied to dest server, so we need to build reuse info for lastest major sstable
    // that already been copied to dest server
    if (OB_SUCC(ret) && !GCTX.is_shared_storage_mode() && !is_restore) {
      common::ObArray<ObSSTableWrapper> major_sstables;
      int64_t reuse_info_count = 0;
      ObTableStoreIterator major_sstable_iter;

      if (OB_FAIL(tablet->fetch_table_store(wrapper))) {
        LOG_WARN("failed to fetch table store", K(ret), KPC(tablet));
      } else if (!wrapper.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("table store wrapper is invalid", K(ret), K(wrapper), KPC(tablet));
      } else if (OB_FAIL(wrapper.get_member()->get_major_sstables(major_sstable_iter, false /*unpack_co_table*/))) {
        LOG_WARN("failed to get major sstables", K(ret), K(wrapper), KPC(tablet));
      } else if (0 == major_sstable_iter.count()) {
        LOG_INFO("no major sstable, skip build reuse info", K(ret), KPC(tablet));
      } else if (OB_FAIL(get_latest_available_major_(major_sstable_iter, latest_major))) {
        // get_major_sstables return major sstables ordered by snapshot version in ascending order
        LOG_WARN("failed to get latest available major sstable", K(ret), K(wrapper), KPC(tablet));
      } else if (!latest_major.is_valid()) {
        // skip, first major sstable has backup data, no need to build reuse info
        LOG_INFO("first major sstable has backup data, no need to build reuse info", K(ret), K(wrapper), KPC(tablet));
      } else if (OB_FAIL(get_latest_major_sstable_array_(latest_major, major_sstables))){
        LOG_WARN("failed to get latest major sstable array", K(ret), K(latest_major));
      } else {
        if (OB_FAIL(build_reuse_info_(major_sstables, tablet_handle, macro_block_reuse_mgr))) {
          LOG_WARN("failed to build reuse info", K(ret), K(major_sstables), KPC(tablet), K(latest_major));
        } else if (OB_FAIL(macro_block_reuse_mgr.count(reuse_info_count))) {
          LOG_WARN("failed to count reuse info", K(ret), K(major_sstables), KPC(tablet), K(latest_major));
        } else {
          LOG_INFO("succeed to build reuse info", K(ret), K(major_sstables), KPC(tablet), K(latest_major), K(reuse_info_count));
        }

        // if build reuse info failed, reset reuse mgr
        if (OB_FAIL(ret)) {
          macro_block_reuse_mgr.reset();
        }
      }
    }
  }

  return ret;
}

int ObStorageHAUtils::get_latest_available_major_(
  storage::ObTableStoreIterator &major_sstables_iter,
  ObTableHandleV2 &latest_major)
{
  int ret = OB_SUCCESS;
  latest_major.reset();
  ObTableHandleV2 cur_major_handle;
  ObSSTableMetaHandle sst_meta_hdl;

  // major sstables must be sorted by snapshot version in ascending order
  // get the latest major sstable that has no backup data and all previous major sstables have backup data
  while (OB_SUCC(ret)) {
    cur_major_handle.reset();
    sst_meta_hdl.reset();
    const ObSSTable *sstable = nullptr;
    if (OB_FAIL(major_sstables_iter.get_next(cur_major_handle))) {
      if (OB_ITER_END == ret) {
        // no more major sstables
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("failed to get next major sstable handle", K(ret), K(cur_major_handle));
      }
    } else if (OB_FAIL(cur_major_handle.get_sstable(sstable))) {
      LOG_WARN("failed to get sstable from handle", K(ret), K(cur_major_handle));
    } else if (OB_ISNULL(sstable) || !ObITable::is_major_sstable(sstable->get_key().table_type_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid major sstable", K(ret), KPC(sstable));
    } else if (OB_FAIL(sstable->get_meta(sst_meta_hdl))) {
      LOG_WARN("failed to get sstable meta", K(ret), KPC(sstable));
    } else if (sst_meta_hdl.get_sstable_meta().get_basic_meta().table_backup_flag_.has_backup()) {
      // stop at the first major sstable that has backup data
      break;
    } else {
      latest_major = cur_major_handle;
    }
  }

  return ret;
}

int ObStorageHAUtils::get_latest_major_sstable_array_(
    ObTableHandleV2 &latest_major,
    common::ObArray<ObSSTableWrapper> &major_sstables)
{
  int ret = OB_SUCCESS;
  major_sstables.reset();
  ObITable *latest_major_table = nullptr;
  ObITable::TableKey table_key;
  ObITable::TableType table_type = ObITable::MAX_TABLE_TYPE;

  if (OB_ISNULL(latest_major_table = latest_major.get_table())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("latest major sstable should not be null", K(ret), K(latest_major));
  } else if (FALSE_IT(table_key = latest_major_table->get_key())) {
  } else if (FALSE_IT(table_type = table_key.table_type_)) {
  }
  // table type of the local major sstable which has max snapshot version
  // could be normal major sstable (row store) or co sstable (column store)
  else if (table_type != ObITable::COLUMN_ORIENTED_SSTABLE && table_type != ObITable::MAJOR_SSTABLE) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid table type", K(ret), K(table_key));
  } else if (table_type == ObITable::MAJOR_SSTABLE) {
    ObSSTable *sstable = static_cast<ObSSTable *> (latest_major_table);
    ObSSTableWrapper major_sstable_wrapper;

    if (OB_ISNULL(sstable) || !sstable->is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sstable should not be null or invalid", K(ret), K(latest_major), KPC(sstable));
    } else if (!ObITable::is_major_sstable(sstable->get_key().table_type_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid sstable type, not major sstable", K(ret), KPC(sstable));
    }
    // won't set meta hdl here, because meta hdl is already hold by outer table handle (latest_major)
    else if (OB_FAIL(major_sstable_wrapper.set_sstable(sstable))) {
      LOG_WARN("failed to set sstable for major sstable wrapper", K(ret), KPC(sstable));
    } else if (OB_FAIL(major_sstables.push_back(major_sstable_wrapper))) {
      LOG_WARN("failed to push back major sstable wrapper", K(ret), K(major_sstable_wrapper));
    }
  } else if (table_type == ObITable::COLUMN_ORIENTED_SSTABLE) {
    const ObCOSSTableV2 *co_sstable = static_cast<const ObCOSSTableV2 *> (latest_major_table);

    if (OB_ISNULL(co_sstable) || !co_sstable->is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("co sstable should not be null or invalid", K(ret), K(latest_major), KPC(co_sstable));
    } else if (OB_FAIL(co_sstable->get_all_tables(major_sstables))) {
      LOG_WARN("failed to get all co & cg tables", K(ret), K(table_key), KPC(co_sstable));
    }
  }

  return ret;
}

int ObStorageHAUtils::build_reuse_info_(
    const common::ObArray<ObSSTableWrapper> &major_sstables,
    const ObTabletHandle &tablet_handle,
    ObMacroBlockReuseMgr &macro_block_reuse_mgr)
{
  int ret = OB_SUCCESS;

  for (int64_t i = 0; OB_SUCC(ret) && i < major_sstables.count(); ++i) {
    const ObSSTable *sstable = major_sstables.at(i).get_sstable();
    if (OB_ISNULL(sstable) || !ObITable::is_major_sstable(sstable->get_key().table_type_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("sstable should not be NULL and should be major" , K(ret), KPC(sstable));
    } else if (OB_FAIL(macro_block_reuse_mgr.update_single_reuse_map(sstable->get_key(), tablet_handle, *sstable))) {
      LOG_WARN("failed to update reuse map", K(ret), K(tablet_handle), KPC(sstable));
    }
  }

  return ret;
}

void ObStorageHAUtils::sort_table_key_array_by_snapshot_version(common::ObArray<ObITable::TableKey> &table_key_array)
{
  TableKeySnapshotVersionComparator cmp;
  lib::ob_sort(table_key_array.begin(), table_key_array.end(), cmp);
}

} // end namespace storage
} // end namespace oceanbase
