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

#include "ob_ls_tablet_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "share/rc/ob_server_runtime.h"
#include "share/schema/ob_schema_runtime_service.h"
#include "storage/blocksstable/ob_datum_row_store.h"
#include "storage/blocksstable/ob_datum_row_utils.h"
#include "storage/ob_dml_running_ctx.h"
#include "storage/ob_table_dml_param.h"
#include "storage/ob_partition_range_spliter.h"
#include "storage/ob_query_iterator_factory.h"
#include "storage/ob_value_row_iterator.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "storage/access/ob_rows_info.h"
#include "storage/access/ob_rows_info.h"
#include "storage/access/ob_table_estimator.h"
#include "storage/access/ob_index_sstable_estimator.h"
#include "storage/blocksstable/ob_sstable.h"
#include "storage/ddl/ob_direct_insert_sstable_ctx.h"
#include "storage/retrieval/ob_block_stat_iter.h"
#include "storage/tablet/ob_mds_schema_helper.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/tablet/ob_tablet_service_clog_replay_executor.h"
#include "storage/slog_ckpt/ob_tablet_replay_create_handler.h"
#include "storage/tablet/ob_tablet_mds_table_mini_merger.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"
#include "data_plane/report/ob_tablet_report.h"
#include "query/vector/ob_vector_index_util.h"
#include "share/vector/ob_vector_index_mode.h"
#include "storage/api/storage/vector/ob_i_vector_index_runtime.h"
#include "storage/meta_mem/ob_tablet_pointer.h"
#include "storage/truncate_info/ob_truncate_partition_filter.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"

using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::transaction;
using namespace oceanbase::blocksstable;

namespace oceanbase
{
namespace storage
{
using namespace mds;

ERRSIM_POINT_DEF(EN_CREATE_EMPTY_SHELL_TABLET_ERROR);

ObLSTabletService::ObLSTabletService()
  : ls_(nullptr),
    tx_data_memtable_mgr_(),
    tx_ctx_memtable_mgr_(),
    lock_memtable_mgr_(),
    mds_table_mgr_(),
    tablet_id_set_(),
    bucket_lock_(),
    is_inited_(false),
    is_stopped_(false)
{
}

ObLSTabletService::~ObLSTabletService()
{
}

int ObLSTabletService::init(
    ObLS *ls)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ls));
  } else if (OB_FAIL(tablet_id_set_.init(ObTabletCommon::BUCKET_LOCK_BUCKET_CNT))) {
  } else if (OB_FAIL(bucket_lock_.init(ObTabletCommon::BUCKET_LOCK_BUCKET_CNT,
      ObLatchIds::TABLET_BUCKET_LOCK, "TabletSvrBucket"))) {
  } else if (OB_FAIL(mds_table_mgr_.init(ls))) {
  } else {
    ls_ = ls;
    is_stopped_ = false;
    is_inited_ = true;
  }

  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }

  return ret;
}

void ObLSTabletService::destroy()
{
  delete_all_tablets();
  tablet_id_set_.destroy();
  tx_data_memtable_mgr_.destroy();
  tx_ctx_memtable_mgr_.destroy();
  lock_memtable_mgr_.destroy();
  mds_table_mgr_.destroy();
  bucket_lock_.destroy();
  ls_= nullptr;
  is_stopped_ = false;
  is_inited_ = false;
}

int ObLSTabletService::stop()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    is_stopped_ = true;
  }
  return ret;
}

int ObLSTabletService::offline()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(offline_build_tablet_without_memtable_())) {
  } else if (OB_FAIL(offline_gc_uncommitted_tablets_())) {
  } else if (OB_FAIL(offline_destroy_memtable_and_mds_table_())) {
  } else {
    mds_table_mgr_.offline();
  }
  return ret;
}

int ObLSTabletService::online()
{
  return OB_SUCCESS;
}

int ObLSTabletService::replay(
    const void *buffer,
    const int64_t nbytes,
    const palf::LSN &lsn,
    const SCN &scn)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  logservice::ObLogBaseHeader base_header;
  common::ObTabletID tablet_id;
  const char *log_buf = static_cast<const char *>(buffer);
  ObTabletServiceClogReplayExecutor replayer_executor;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(base_header.deserialize(log_buf, nbytes, pos))) {
  } else if (logservice::ObLogBaseType::STORAGE_SCHEMA_LOG_BASE_TYPE != base_header.get_log_type()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("log type not supported", K(ret), "log_type", base_header.get_log_type());
  } else if (OB_FAIL(tablet_id.deserialize(log_buf, nbytes, pos))) {
  } else if (OB_FAIL(replayer_executor.init(log_buf, nbytes, pos, scn))) {
  } else if (OB_FAIL(replayer_executor.execute(scn, tablet_id))) {
    if (OB_TABLET_NOT_EXIST == ret) {
      ret = OB_SUCCESS; // TODO (gaishun.gs): unify multi data replay logic
      LOG_INFO("tablet does not exist, skip", K(ret), K(replayer_executor));
    } else if (OB_TIMEOUT == ret) {
      LOG_INFO("replace timeout errno", KR(ret), K(replayer_executor));
      ret = OB_EAGAIN;
    } else {
      LOG_ERROR("failed to replay", K(ret), K(replayer_executor));
    }
  }

  return ret;
}

void ObLSTabletService::deactivate()
{
  // TODO
}

int ObLSTabletService::activate()
{
  int ret = OB_SUCCESS;
  //TODO
  return ret;
}

int ObLSTabletService::flush(SCN &recycle_scn)
{
  UNUSED(recycle_scn);
  return OB_SUCCESS;
}

SCN ObLSTabletService::get_rec_scn()
{
  return SCN::max_scn();
}

int ObLSTabletService::prepare_for_safe_destroy()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(delete_all_tablets())) {
  }
  return ret;
}

int ObLSTabletService::delete_all_tablets()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(ls_)) {
    ObSArray<ObTabletID> tablet_id_array;
    GetAllTabletIDOperator op(tablet_id_array);

    ObTimeGuard time_guard("ObLSTabletService::delete_all_tablets", 1_s);
    common::ObBucketWLockAllGuard lock_guard(bucket_lock_);
    time_guard.click("Lock");
    if (OB_FAIL(tablet_id_set_.foreach(op))) {
    } else if (tablet_id_array.empty()) {
      // tablet id array is empty, do nothing
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < tablet_id_array.count(); ++i) {
        const ObTabletID &tablet_id = tablet_id_array.at(i);
        if (OB_FAIL(inner_remove_tablet(tablet_id))) {
          LOG_ERROR("failed to do remove tablet", K(ret), K(tablet_id));
          ob_usleep(1_s);
          ob_abort();
        }
      }
      time_guard.click("RemoveTablet");

      if (OB_SUCC(ret)) {
        report_tablet_to_rs(tablet_id_array);
        time_guard.click("ReportToRS");
      }
    }
  }
  return ret;
}

int ObLSTabletService::remove_tablet(const ObTabletHandle& tablet_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet", K(ret), K(tablet_handle));
  } else {
    const ObTablet &target_tablet = *(tablet_handle.get_obj());
    const ObTabletID tablet_id = target_tablet.get_tablet_meta().tablet_id_;
    const ObTabletMapKey key(tablet_id);
    ObTabletHandle cur_tablet_handle;
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());

    if (OB_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, cur_tablet_handle))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        LOG_INFO("tablet does not exist, maybe already deleted", K(ret), K(key));
      } else {
        LOG_WARN("failed to get tablet", K(ret), K(key));
      }
    } else if (&target_tablet != cur_tablet_handle.get_obj()) {
      ret = OB_EAGAIN;
      LOG_INFO("tablet object has been changed, need retry", K(ret), K(key), K(target_tablet), KPC(cur_tablet_handle.get_obj()));
    } else {
      if (OB_FAIL(LOCAL_STORAGE_META_PERSISTER.remove_tablet(tablet_handle))) {
      } else if (OB_FAIL(tablet_handle.get_obj()->wait_release_memtables())) {
      } else if (OB_FAIL(inner_remove_tablet(tablet_id))) {
        LOG_ERROR("failed to do remove tablet", K(ret), K(tablet_id));
        ob_usleep(1_s);
        ob_abort();
      } else {
        report_tablet_to_rs(tablet_id);
      }
    }
  }
  return ret;
}

int ObLSTabletService::remove_tablets(const common::ObIArray<common::ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  const int64_t tablet_cnt = tablet_id_array.count();
  ObSArray<uint64_t> all_tablet_id_hash_array;
  ObSArray<ObTabletID> tablet_ids;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(0 == tablet_cnt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args, tablet id array is empty", K(ret), K(tablet_id_array));
  } else if (OB_FAIL(all_tablet_id_hash_array.reserve(tablet_cnt))) {
  } else if (OB_FAIL(tablet_ids.reserve(tablet_cnt))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_cnt; ++i) {
      const ObTabletID &tablet_id = tablet_id_array.at(i);
      if (OB_FAIL(all_tablet_id_hash_array.push_back(tablet_id.hash()))) {
      }
    }
  }

  if (OB_SUCC(ret)) {
    ObMetaDiskAddr tablet_addr;
    ObTimeGuard time_guard("ObLSTabletService::remove_tablets", 1_s);
    ObMultiBucketLockGuard lock_guard(bucket_lock_, true/*is_write_lock*/);
    if (OB_FAIL(lock_guard.lock_multi_buckets(all_tablet_id_hash_array))) {
    } else {
      time_guard.click("Lock");
      ObTabletHandle tablet_handle;
      ObTabletMapKey key;

      // check tablet existence
      for (int64_t i = 0; OB_SUCC(ret) && i < tablet_cnt; ++i) {
        const ObTabletID &tablet_id = tablet_id_array.at(i);
        key.tablet_id_ = tablet_id;
        tablet_addr.reset();
        if (OB_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, tablet_handle))) {
          if (OB_TABLET_NOT_EXIST == ret) {
            ret = OB_SUCCESS;
            LOG_INFO("tablet does not exist, maybe already deleted", K(ret), K(key));
          } else {
            LOG_WARN("failed to get tablet", K(ret), K(key));
          }
        } else if (OB_FAIL(tablet_handle.get_obj()->wait_release_memtables())) {
        } else if (OB_FAIL(tablet_handle.get_obj()->get_meta_disk_addr(tablet_addr))) {
        } else if (!tablet_addr.is_disked()) {
          if (OB_FAIL(inner_remove_tablet(tablet_id))) {
          } else {
            FLOG_INFO("succeeded to remove non disked tablet from memory", K(ret), K(key));
          }
        } else if (OB_FAIL(tablet_ids.push_back(tablet_id))) {
        }
      }

      // write slog and do remove tablet
      if (OB_FAIL(ret)) {
      } else if (tablet_ids.empty()) {
        LOG_INFO("all tablets already deleted, do nothing", K(ret), K(tablet_id_array));
      } else if (OB_FAIL(LOCAL_STORAGE_META_PERSISTER.remove_tablets(tablet_ids))) {
      } else {
        time_guard.click("WrSlog");
        for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
          const ObTabletID &tablet_id = tablet_ids.at(i);
          if (OB_FAIL(inner_remove_tablet(tablet_id))) {
            LOG_ERROR("failed to do remove tablet", K(ret), K(tablet_id));
            ob_usleep(1_s);
            ob_abort();
          }
        }

        if (OB_SUCC(ret)) {
          report_tablet_to_rs(tablet_ids);
          time_guard.click("ReportToRS");
        }
      }
    }
  }

  return ret;
}

int ObLSTabletService::do_remove_tablet(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObTimeGuard time_guard("RmTabletLock", 1_s);
  ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
  time_guard.click("Lock");
  if (OB_FAIL(inner_remove_tablet(tablet_id))) {
  }
  return ret;
}

// TODO(yunshan.tys) cope with failure of deleting tablet (tablet hasn't been loaded from disk)
int ObLSTabletService::inner_remove_tablet(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  const ObTabletMapKey key(tablet_id);
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  ObDirectLoadMgr *direct_load_mgr = ::oceanbase::share::server_service<::oceanbase::storage::ObDirectLoadMgr>();

  if (OB_FAIL(tablet_id_set_.erase(tablet_id))) {
    if (OB_HASH_NOT_EXIST == ret) {
      // tablet id is already erased
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to erase tablet id from set", K(ret), K(tablet_id));
    }
  }

  if (OB_SUCC(ret)) {
    // loop retry to delete tablet from t3m
    while (OB_FAIL(t3m->del_tablet(key))) {
      if (REACH_TIME_INTERVAL(10_s)) {
        LOG_ERROR("failed to delete tablet from t3m", K(ret), K(tablet_id));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(direct_load_mgr->remove_tablet_direct_load(
        ObTabletDirectLoadMgrKey(tablet_id, ObDirectLoadType::DIRECT_LOAD_DDL)))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_ERROR("remove tablet direct load failed", K(ret), K(tablet_id));
      }
    }
  }

  if (OB_SUCC(ret)) {
    FLOG_INFO("succeeded to remove tablet", K(ret), K(tablet_id));
  }

  return ret;
}

int ObLSTabletService::get_tablet(
    const ObTabletID &tablet_id,
    ObTabletHandle &handle,
    const int64_t timeout_us,
    const ObMDSGetTabletMode mode)
{
  int ret = OB_SUCCESS;
  const ObTabletMapKey key(tablet_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid()
      || mode < ObMDSGetTabletMode::READ_ALL_COMMITED)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(mode));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::check_and_get_tablet(key, handle, timeout_us, mode,
      ObTransVersion::MAX_TRANS_VERSION))) {
    if (OB_TABLET_NOT_EXIST == ret) {
    } else {
      LOG_WARN("failed to check and get tablet", K(ret), K(key), K(timeout_us), K(mode));
    }
  }

  return ret;
}

int ObLSTabletService::get_tablet_addr(const ObTabletMapKey &key, ObMetaDiskAddr &addr)
{
  int ret = OB_SUCCESS;
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(t3m->get_tablet_addr(key, addr))) {
  }

  return ret;
}

void ObLSTabletService::report_tablet_to_rs(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;

  if (tablet_id.is_ls_inner_tablet()) {
    // no need to report for ls inner tablet
  } else if (OB_FAIL(data_plane::submit_tablet_update(tablet_id))) {
  }
}

void ObLSTabletService::report_tablet_to_rs(
    const common::ObIArray<common::ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;

  // ignore ret on purpose
  for (int64_t i = 0; i < tablet_id_array.count(); ++i) {
    const common::ObTabletID &tablet_id = tablet_id_array.at(i);
    if (tablet_id.is_ls_inner_tablet()) {
      // no need to report for ls inner tablet
      continue;
    } else if (OB_FAIL(data_plane::submit_tablet_update(tablet_id))) {
    }
  }
}

int ObLSTabletService::table_scan(ObTabletHandle &tablet_handle, ObTableScanIterator &iter, ObTableScanParam &param)
{
  int ret = OB_SUCCESS;
  NG_TRACE(S_table_scan_begin);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(prepare_scan_table_param(param, *(::oceanbase::share::server_service<::oceanbase::share::schema::ObSchemaRuntimeService>()->get_schema_service())))) {
  } else if (OB_FAIL(inner_table_scan(tablet_handle, iter, param))) {
  }
  NG_TRACE(S_table_scan_end);

  return ret;
}

int ObLSTabletService::table_rescan(ObTabletHandle &tablet_handle, ObTableScanParam &param, ObNewRowIterator *result)
{
  int ret = OB_SUCCESS;
  NG_TRACE(S_table_rescan_begin);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K(result), K_(is_inited));
  } else if (OB_ISNULL(result)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(prepare_scan_table_param(param, *(::oceanbase::share::server_service<::oceanbase::share::schema::ObSchemaRuntimeService>()->get_schema_service())))) {
  } else {
    ObTableScanIterator *iter = static_cast<ObTableScanIterator*>(result);
    if (OB_FAIL(inner_table_scan(tablet_handle, *iter, param))) {
    }
  }
  NG_TRACE(S_table_rescan_end);
  return ret;
}

int ObLSTabletService::refresh_tablet_addr(
    const common::ObTabletID &tablet_id,
    const ObUpdateTabletPointerParam &param,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  const ObTabletMapKey key(tablet_id);
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();

  while (OB_SUCC(ret)) {
    ret = tablet_id_set_.set(tablet_id);
    if (OB_SUCC(ret)) {
      break;
    } else if (OB_ALLOCATE_MEMORY_FAILED == ret) {
      usleep(100 * 1000);
      if (REACH_COUNT_INTERVAL(100)) {
        LOG_ERROR("no memory for tablet id set, retry", K(ret), K(tablet_id));
      }
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to set tablet id set", K(ret), K(tablet_id));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(t3m->compare_and_swap_tablet(key, tablet_handle, tablet_handle, param))) {
  }

  return ret;
}

int ObLSTabletService::refresh_memtable_for_ckpt(
    const ObMetaDiskAddr &old_addr,
    const ObMetaDiskAddr &cur_addr,
    ObTabletHandle &new_tablet_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!old_addr.is_equal_for_persistence(cur_addr))) {
    ret = OB_NOT_THE_OBJECT;
    LOG_WARN("the old tablet has been replaced", K(ret), K(old_addr), K(cur_addr));
  } else if (OB_UNLIKELY(old_addr != cur_addr)) {
    // memtables were updated
    if (OB_FAIL(new_tablet_handle.get_obj()->refresh_memtable_and_update_seq(cur_addr.seq()))) {
    }
  }
  return ret;
}

int ObLSTabletService::update_tablet_checkpoint(
    const ObTabletMapKey &key,
    const ObMetaDiskAddr &old_addr,
    const ObMetaDiskAddr &new_addr,
    ObTabletHandle &new_handle)
{
  int ret = OB_SUCCESS;

  ObTimeGuard time_guard("UpdateTabletCKPT", 3_s);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls tablet svr hasn't been inited", K(ret));
  } else if (OB_UNLIKELY(!key.is_valid()
                      || !old_addr.is_valid()
                      || !new_addr.is_valid()
                      || !new_addr.is_block()
                      || !new_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key), K(new_addr), K(new_handle));
  } else {
    common::ObArenaAllocator allocator(common::ObMemAttr("CKPTUpdate"));
    ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
    ObTabletHandle tablet_handle;
    ObMetaDiskAddr addr;
    ObBucketHashWLockGuard lock_guard(bucket_lock_, key.tablet_id_.hash());
    time_guard.click("Lock");
    if (OB_FAIL(t3m->get_tablet_addr(key, addr))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_TABLET_NOT_EXIST;
      }
      LOG_WARN("fail to get old tablet addr", K(ret), K(key));
    } else {
      ObUpdateTabletPointerParam param;
      if (OB_FAIL(t3m->get_tablet(WashTabletPriority::WTP_LOW, key, tablet_handle))) {
      } else if (FALSE_IT(time_guard.click("GetOld"))) {
      } else if (OB_FAIL(refresh_memtable_for_ckpt(old_addr, addr, new_handle))) {
      } else if (FALSE_IT(time_guard.click("UpdateTablet"))) {
      } else if (OB_FAIL(new_handle.get_obj()->get_updating_tablet_pointer_param(param))) {
      } else if (OB_FAIL(t3m->compare_and_swap_tablet(key, tablet_handle, new_handle, param))) {
      }
    }

    if (OB_SUCC(ret)) {
      time_guard.click("CASwap");
      FLOG_INFO("succeeded to update tablet ckpt", K(key), K(old_addr), K(new_addr));
    }
  }
  return ret;
}

int ObLSTabletService::update_tablet_table_store(
    const ObTabletHandle &old_tablet_handle,
    const ObIArray<storage::ObITable *> &tables)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator(common::ObMemAttr("UpTabStore"));
  ObTabletHandle new_tablet_hdl;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls tablet svr hasn't been inited", K(ret));
  } else if (OB_UNLIKELY(!old_tablet_handle.is_valid() || 0 == tables.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("old tablet handle is invalid", K(ret), K(old_tablet_handle), K(tables.count()));
  } else {
    ObTablet *old_tablet = old_tablet_handle.get_obj();
    const common::ObTabletID &tablet_id = old_tablet->get_tablet_meta().tablet_id_;

    ObTimeGuard time_guard("ObLSTabletService::ReplaceSSTable", 1_s);
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    time_guard.click("Lock");

    ObTabletHandle tablet_handle;
    if (OB_FAIL(direct_get_tablet(tablet_id, tablet_handle))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        ret = OB_EAGAIN;
        LOG_WARN("this tablet has been deleted, skip it", K(ret), K(tablet_id));
      } else {
        LOG_WARN("fail to get tablet", K(ret));
      }
    } else if (tablet_handle.get_obj() != old_tablet) {
      ret = OB_EAGAIN;
      LOG_WARN("tablet has changed, skip it", K(ret), K(tablet_handle), K(old_tablet_handle));
    } else if (old_tablet->is_empty_shell()) {
      LOG_INFO("old tablet is empty shell tablet, should skip this operation", K(ret), "old_tablet", old_tablet);
    } else {
      time_guard.click("GetTablet");
      ObTabletHandle tmp_tablet_hdl;
      ObTablet *tmp_tablet = nullptr;
      const ObTabletMapKey key(tablet_id);
      ObMetaDiskAddr disk_addr;
      const ObTabletPersisterParam param(ls_->get_ls_epoch(), tablet_id);

      if (OB_FAIL(ObTabletCreateDeleteHelper::acquire_tmp_tablet(key, allocator, tmp_tablet_hdl))) {
      } else if (FALSE_IT(tmp_tablet = tmp_tablet_hdl.get_obj())) {
      } else if (OB_FAIL(tmp_tablet->init_for_defragment(allocator, tables, *old_tablet))) {
      } else if (FALSE_IT(time_guard.click("InitTablet"))) {
      } else if (OB_FAIL(ObTabletPersister::persist_and_transform_tablet(param, *tmp_tablet, new_tablet_hdl))) {
      } else if (FALSE_IT(disk_addr = new_tablet_hdl.get_obj()->tablet_addr_)) {
      } else if (OB_FAIL(safe_update_cas_tablet(key, disk_addr, old_tablet_handle, new_tablet_hdl, time_guard))) {
      } else {
        LOG_INFO("succeeded to build new tablet", K(ret), K(disk_addr),
            K(new_tablet_hdl), KPC(new_tablet_hdl.get_obj()));
      }
    }
  }
  return ret;
}

int ObLSTabletService::update_tablet_table_store(
    const common::ObTabletID &tablet_id,
    const ObUpdateTableStoreParam &param,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator("UpdateTmpTablet", OB_MALLOC_NORMAL_BLOCK_SIZE, ObCtxIds::DEFAULT_CTX_ID);
  if (share::is_reserve_mode()) {
    // TODO(@DanLing) use LocalArena later
    allocator.set_ctx_id(ObCtxIds::MERGE_RESERVE_CTX_ID);
  }
  const ObTabletMapKey key(tablet_id);
  ObTabletHandle old_tablet_hdl;
  ObTabletHandle tmp_tablet_hdl;
  ObTabletHandle new_tablet_hdl;
  ObTimeGuard time_guard("ObLSTabletService::UpdateTableStore", 1_s);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || !param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(param));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::acquire_tmp_tablet(key, allocator, tmp_tablet_hdl))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TABLET_NOT_EXIST;
    } else {
      LOG_WARN("fail to acquire temporary tablet", K(ret), K(key));
    }
  } else {
    ObTablet *tmp_tablet = tmp_tablet_hdl.get_obj();
    time_guard.click("Acquire");
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    time_guard.click("Lock");
    if (OB_FAIL(direct_get_tablet(tablet_id, old_tablet_hdl))) {
    } else if (old_tablet_hdl.get_obj()->is_empty_shell()) {
      handle = old_tablet_hdl;
      LOG_INFO("old tablet is empty shell tablet, should skip this operation", K(ret), "old_tablet", old_tablet_hdl.get_obj());
    } else {
      time_guard.click("GetTablet");
      ObTablet *old_tablet = old_tablet_hdl.get_obj();
      ObMetaDiskAddr disk_addr;
      const ObTabletPersisterParam persist_param(ls_->get_ls_epoch(), tablet_id);
      share::SCN not_used_scn;
      if (!is_mds_merge(param.compaction_info_.merge_type_) && OB_FAIL(tmp_tablet->init_for_merge(allocator, param, *old_tablet))) {
        LOG_WARN("failed to init tablet", K(ret), K(param), KPC(old_tablet));
      } else if (is_mds_merge(param.compaction_info_.merge_type_) && OB_FAIL(tmp_tablet->init_with_mds_sstable(allocator, *old_tablet, not_used_scn, param))) {
        LOG_WARN("failed to init tablet with mds", K(ret), K(param), KPC(old_tablet));
      } else if (FALSE_IT(time_guard.click("InitNew"))) {
      } else if (OB_FAIL(ObTabletPersister::persist_and_transform_tablet(persist_param, *tmp_tablet, new_tablet_hdl))) {
      } else if (FALSE_IT(disk_addr = new_tablet_hdl.get_obj()->tablet_addr_)) {
      } else if (OB_FAIL(safe_update_cas_tablet(key, disk_addr, old_tablet_hdl, new_tablet_hdl, time_guard))) {
      } else {
        handle = new_tablet_hdl;
        LOG_INFO("succeeded to build new tablet", K(ret), K(key), K(disk_addr), K(param), K(handle));
      }
    }
  }
  return ret;
}

int ObLSTabletService::update_tablet_to_empty_shell(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  const ObTabletMapKey key(tablet_id);
  common::ObArenaAllocator allocator(common::ObMemAttr("UpdEmptySh"));
  ObTabletHandle new_tablet_handle;
  ObTabletHandle tmp_tablet_handle;
  ObTabletHandle old_tablet_handle;
  ObTimeGuard time_guard("UpdateTabletToEmptyShell", 3_s);
  ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());

  time_guard.click("Lock");
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls tablet svr hasn't been inited", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id));
  } else if (OB_FAIL(direct_get_tablet(tablet_id, old_tablet_handle))) {
  } else if (old_tablet_handle.get_obj()->is_empty_shell()) {
    LOG_INFO("old tablet is empty shell tablet, should skip this operation", K(ret), "old_tablet", old_tablet_handle.get_obj());
  } else if (FALSE_IT(time_guard.click("GetOld"))) {
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::acquire_tmp_tablet(key, allocator, tmp_tablet_handle))) {
  } else {
    time_guard.click("Acquire");
    ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
    ObTablet *old_tablet = old_tablet_handle.get_obj();
    ObTablet *tmp_tablet = tmp_tablet_handle.get_obj();
    ObTablet *new_tablet = nullptr;
    ObMetaDiskAddr disk_addr;
    const ObTabletPersisterParam param(ls_->get_ls_epoch(), tablet_id);
    if (OB_FAIL(tmp_tablet->init_empty_shell(*tmp_tablet_handle.get_allocator(), *old_tablet))) {
    } else if (FALSE_IT(time_guard.click("InitNew"))) {
    } else if (OB_FAIL(ObTabletPersister::transform_empty_shell(param, *tmp_tablet, new_tablet_handle))) {
    } else if (FALSE_IT(time_guard.click("Transform"))) {
    } else {
      if (OB_FAIL(safe_update_cas_empty_shell(key, old_tablet_handle, new_tablet_handle, time_guard))) {
      }
    }
    if (OB_SUCC(ret)) {
      ls_->get_tablet_gc_handler()->set_tablet_gc_trigger();
      LOG_INFO("succeeded to build empty shell tablet", K(ret), K(key), K(disk_addr));
    }
  }
  return ret;
}

int ObLSTabletService::update_medium_compaction_info(
    const common::ObTabletID &tablet_id,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator(common::ObMemAttr("UpMeidumCom"));
  ObTabletHandle old_tablet_handle;
  ObTimeGuard time_guard("ObLSTabletService::update_medium_compaction_info", 1_s);
  ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
  time_guard.click("Lock");

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id));
  } else if (OB_FAIL(direct_get_tablet(tablet_id, old_tablet_handle))) {
  } else if (old_tablet_handle.get_obj()->is_empty_shell()) {
    handle = old_tablet_handle;
    LOG_INFO("old tablet is empty shell tablet, should skip this operation", K(ret), "old_tablet", old_tablet_handle.get_obj());
  } else {
    time_guard.click("GetTablet");
    ObTabletHandle tmp_tablet_hdl;
    ObTabletHandle new_tablet_hdl;
    ObTablet *tmp_tablet = nullptr;
    ObTablet *old_tablet = old_tablet_handle.get_obj();
    const ObTabletMapKey key(tablet_id);
    ObMetaDiskAddr disk_addr;
    const ObTabletPersisterParam param(ls_->get_ls_epoch(), tablet_id);

    if (OB_FAIL(ObTabletCreateDeleteHelper::acquire_tmp_tablet(key, allocator, tmp_tablet_hdl))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_TABLET_NOT_EXIST;
      } else {
        LOG_WARN("failed to acquire tablet", K(ret), K(key));
      }
    } else if (FALSE_IT(tmp_tablet = tmp_tablet_hdl.get_obj())) {
    } else if (OB_FAIL(tmp_tablet->init_with_update_medium_info(allocator, *old_tablet, true/*clear_wait_check_flag*/))) {
    } else if (FALSE_IT(time_guard.click("InitNew"))) {
    } else if (OB_FAIL(ObTabletPersister::persist_and_transform_tablet(param, *tmp_tablet, new_tablet_hdl))) {
    } else if (FALSE_IT(disk_addr = new_tablet_hdl.get_obj()->tablet_addr_)) {
    } else if (OB_FAIL(safe_update_cas_tablet(key, disk_addr, old_tablet_handle, new_tablet_hdl, time_guard))) {
    } else {
      handle = new_tablet_hdl;
    }
  }
  return ret;
}

int ObLSTabletService::build_new_tablet_from_mds_table(
    compaction::ObTabletMergeCtx &ctx,
    const common::ObTabletID &tablet_id,
    const ObTableHandleV2 &mds_mini_sstable_handle,
    const share::SCN &flush_scn,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator(common::ObMemAttr("BuildMSD"));
  const ObTabletMapKey key(tablet_id);
  ObTabletHandle old_tablet_hdl;
  ObTabletHandle tablet_for_mds_dump_handle;
  ObTabletHandle tmp_tablet_hdl;
  ObTabletHandle new_tablet_handle;
  const blocksstable::ObSSTable *mds_sstable = nullptr;
  ObTimeGuard time_guard("ObLSTabletService::build_new_tablet_from_mds_table_with_mini", 30_ms);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::acquire_tmp_tablet(key, allocator, tmp_tablet_hdl))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TABLET_NOT_EXIST;
    } else {
      LOG_WARN("failed to acquire tablet", K(ret), K(key));
    }
  } else {
    time_guard.click("Acquire");
    if (OB_FAIL(direct_get_tablet(tablet_id, tablet_for_mds_dump_handle))) {
    } else if (OB_ISNULL(tablet_for_mds_dump_handle.get_obj())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
    } else if (tablet_for_mds_dump_handle.get_obj()->is_empty_shell()) {
      handle = tablet_for_mds_dump_handle;
      LOG_INFO("mds tablet is empty shell tablet, should skip mds table dump operation", K(ret),
          "mds tablet", *tablet_for_mds_dump_handle.get_obj());
    } else if (OB_FAIL(mds_mini_sstable_handle.get_sstable(mds_sstable))) {
    } else {
      ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
      time_guard.click("Lock");
      if (OB_FAIL(direct_get_tablet(tablet_id, old_tablet_hdl))) {
      } else if (OB_ISNULL(old_tablet_hdl.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
      } else if (old_tablet_hdl.get_obj()->is_empty_shell()) {
        handle = old_tablet_hdl;
        LOG_INFO("old tablet is empty shell tablet, should skip mds table dump operation", K(ret),
            "old tablet", *old_tablet_hdl.get_obj());
      } else {
        time_guard.click("GetOldTablet");
        ObTablet *old_tablet = old_tablet_hdl.get_obj();
        ObTablet *tmp_tablet = tmp_tablet_hdl.get_obj();
        ObMetaDiskAddr disk_addr;
        const ObTabletPersisterParam param(ls_->get_ls_epoch(), tablet_id);
        ObUpdateTableStoreParam mds_param(ctx.static_param_.version_range_.snapshot_version_,
                                          1/*multi_version_start*/,
                                          ObMdsSchemaHelper::get_instance().get_storage_schema(),
                                          mds_sstable,
                                          false/*allow_duplicate_sstable*/);
        if (OB_FAIL(mds_param.init_with_compaction_info(
          ObCompactionTableStoreParam(ctx.get_merge_type(), mds_sstable->get_end_scn()/*clog_checkpoint_scn*/, false/*need_report*/, false/*has_truncate_info*/)))) {
        } else if (OB_FAIL(tmp_tablet->init_with_mds_sstable(allocator, *old_tablet, flush_scn, mds_param))) {
        } else if (FALSE_IT(time_guard.click("InitTablet"))) {
        } else if (OB_FAIL(ObTabletPersister::persist_and_transform_tablet(param, *tmp_tablet, new_tablet_handle))) {
        } else if (FALSE_IT(time_guard.click("Persist"))) {
        } else if (FALSE_IT(disk_addr = new_tablet_handle.get_obj()->tablet_addr_)) {
        } else if (OB_FAIL(safe_update_cas_tablet(key, disk_addr, old_tablet_hdl, new_tablet_handle, time_guard))) {
        } else {
          time_guard.click("SafeCAS");
          handle = new_tablet_handle;
          LOG_INFO("succeeded to build new tablet with mds mini sstable",
              K(ret), K(key), K(disk_addr), K(new_tablet_handle), K(flush_scn), KP(mds_sstable));
        }
      }
    }
  }

  return ret;
}

int ObLSTabletService::update_tablet_release_memtable_for_offline(
    const common::ObTabletID &tablet_id,
    const SCN scn)
{
  int ret = OB_SUCCESS;
  const ObTabletMapKey key(tablet_id);
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObTimeGuard time_guard("ObLSTabletService::update_tablet_release_memtable", 1_s);
  ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
  time_guard.click("Lock");
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || !scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(scn));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, tablet_handle))) {
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(key));
  } else if (tablet->is_empty_shell()) {
    //do nothing
  } else {
    time_guard.click("get_tablet");
    ObITable *table = nullptr;
    ObTableStoreIterator iter;
    const bool is_from_buf_pool = nullptr == tablet_handle.get_obj()->get_allocator();
    if (is_from_buf_pool) {
      ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
      ObTabletHandle new_tablet_handle;
      ObUpdateTabletPointerParam param;
      const ObTabletPersisterParam persist_param(ls_->get_ls_epoch(), tablet_id);
      if (OB_FAIL(ObTabletPersister::copy_from_old_tablet(persist_param, *tablet, new_tablet_handle))) {
      } else if (FALSE_IT(time_guard.click("CpTablet"))) {
      } else if (OB_FAIL(new_tablet_handle.get_obj()->rebuild_memtables(scn))) {
      } else if (OB_FAIL(new_tablet_handle.get_obj()->get_updating_tablet_pointer_param(param,
              false/*update tablet attr*/))) {
      } else if (OB_FAIL(t3m->compare_and_swap_tablet(key, tablet_handle, new_tablet_handle, param))) {
      } else {
        time_guard.click("CASwap");
        LOG_INFO("succeeded to copy tablet to release memtable", K(ret), K(key), K(tablet_handle), K(new_tablet_handle));
      }
    } else if (OB_UNLIKELY(!tablet->get_tablet_addr().is_memory())) {
      ret = OB_NOT_SUPPORTED;
      LOG_ERROR("This tablet is full tablet, but its addr isn't memory", K(ret), KPC(tablet));
    } else if (OB_FAIL(tablet->get_all_sstables(iter))) {
    } else if (1 == iter.count() && OB_FAIL(iter.get_next(table))) {
      LOG_WARN("fail to get next table", K(ret), K(iter));
    } else if (OB_UNLIKELY(iter.count() > 1)
               || (OB_NOT_NULL(table) && (!table->is_sstable()
                                          || static_cast<ObSSTable *>(table)->get_data_macro_block_count() != 0))) {
      ret = OB_NOT_SUPPORTED;
      LOG_ERROR("This tablet is full tablet, but all of its sstables isn't only one empty major",
          K(ret), K(iter), KPC(table));
    } else if (OB_FAIL(tablet_handle.get_obj()->wait_release_memtables())) {
    } else if (OB_FAIL(inner_remove_tablet(tablet_id))) {
    } else {
      time_guard.click("RmTablet");
    }
  }
  return ret;
}

int ObLSTabletService::ObUpdateDDLCommitSCN::modify_tablet_meta(ObTabletMeta &meta)
{
  int ret = OB_SUCCESS;
  if (!ddl_commit_scn_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_commit_scn_));
  } else if (meta.ddl_commit_scn_.is_valid_and_not_min() && ddl_commit_scn_ != meta.ddl_commit_scn_) {
    ret = OB_ERR_SYS;
    LOG_WARN("ddl commit scn already set", K(ret), K(meta), K(ddl_commit_scn_));
  } else {
    meta.ddl_commit_scn_ = ddl_commit_scn_;
  }
  return ret;
}

int ObLSTabletService::update_tablet_ddl_commit_scn(
    const common::ObTabletID &tablet_id,
    const SCN ddl_commit_scn)
{
  int ret = OB_SUCCESS;
  const ObTabletMapKey key(tablet_id);
  ObTabletHandle old_handle;
  ObTimeGuard time_guard("ObLSTabletService::update_tablet_ddl_commit_scn", 1_s);
  ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
  time_guard.click("Lock");
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || !ddl_commit_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(ddl_commit_scn));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, old_handle))) {
  } else {
    time_guard.click("get_tablet");
    ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
    ObMetaDiskAddr disk_addr;
    ObUpdateDDLCommitSCN modifier(ddl_commit_scn);
    ObUpdateTabletPointerParam param;
    ObTabletHandle new_handle;
    const ObTablet &old_tablet = *old_handle.get_obj();
    const ObTabletPersisterParam persist_param(ls_->get_ls_epoch(), tablet_id);

    if (OB_FAIL(ObTabletPersister::persist_and_transform_only_tablet_meta(persist_param, old_tablet, modifier, new_handle))) {
    } else if (FALSE_IT(time_guard.click("Persist"))) {
    } else if (FALSE_IT(disk_addr = new_handle.get_obj()->tablet_addr_)) {
    } else if (OB_FAIL(safe_update_cas_tablet(key, disk_addr, old_handle, new_handle, time_guard))) {
    } else {
      LOG_INFO("succeeded to update tablet ddl commit scn", K(ret), K(key), K(disk_addr), K(old_handle),
          K(new_handle), K(ddl_commit_scn), K(time_guard));
    }
  }
  return ret;
}

int ObLSTabletService::update_tablet_report_status(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTimeGuard time_guard("ObLSTabletService::update_tablet_report_status", 1_s);
  ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
  time_guard.click("Lock");

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id));
  } else if (OB_FAIL(direct_get_tablet(tablet_id, tablet_handle))) {
  } else if (tablet_handle.get_obj()->is_empty_shell()) {
    LOG_INFO("old tablet is empty shell tablet, should skip this operation", K(ret), "old_tablet", tablet_handle.get_obj());
  } else {
    time_guard.click("GetTablet");
    ObMetaDiskAddr disk_addr;
    const ObTabletMapKey key(tablet_id);
    ObTablet *tablet = tablet_handle.get_obj();
    ObTabletHandle new_tablet_handle;
    bool need_report = true;

    if (tablet->tablet_meta_.report_status_.need_report()) {
      tablet->tablet_meta_.report_status_.cur_report_version_ = tablet->tablet_meta_.report_status_.merge_snapshot_version_;
    } else {
      need_report = false;
      FLOG_INFO("tablet doesn't need to report", K(ret), K(tablet_id));
    }

    if (need_report) {
      const ObTabletPersisterParam param(ls_->get_ls_epoch(), tablet_id);
      if (OB_FAIL(ObTabletPersister::persist_and_transform_tablet(param, *tablet, new_tablet_handle))) {
      } else if (FALSE_IT(time_guard.click("Persist"))) {
      } else if (FALSE_IT(disk_addr = new_tablet_handle.get_obj()->tablet_addr_)) {
      } else if (OB_FAIL(safe_update_cas_tablet(key, disk_addr, tablet_handle, new_tablet_handle, time_guard))) {
      } else {
        LOG_INFO("succeeded to build new tablet", K(ret), K(key), K(disk_addr), K(tablet_handle));
      }
    }
  }
  return ret;
}

int ObLSTabletService::update_tablet_snapshot_version(
    const common::ObTabletID &tablet_id,
    const int64_t snapshot_version)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator(common::ObMemAttr("UTabletSnapVer"));
  ObTabletHandle old_tablet_handle;
  ObTimeGuard time_guard("ObLSTabletService::update_tablet_snapshot_version", 1_s);
  ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
  time_guard.click("Lock");

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || 0 >= snapshot_version)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(snapshot_version));
  } else if (OB_FAIL(direct_get_tablet(tablet_id, old_tablet_handle))) {
  } else {
    time_guard.click("GetTablet");

    ObTabletHandle tmp_tablet_hdl;
    ObTabletHandle new_tablet_hdl;
    ObTablet *tmp_tablet = nullptr;
    ObTablet *old_tablet = old_tablet_handle.get_obj();
    const ObTabletMapKey key(tablet_id);
    ObMetaDiskAddr disk_addr;
    const ObTabletPersisterParam param(ls_->get_ls_epoch(), tablet_id);
    if (OB_FAIL(ObTabletCreateDeleteHelper::acquire_tmp_tablet(key, allocator, tmp_tablet_hdl))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_TABLET_NOT_EXIST;
      } else {
        LOG_WARN("failed to acquire tablet", K(ret), K(key));
      }
    } else if (FALSE_IT(tmp_tablet = tmp_tablet_hdl.get_obj())) {
    } else if (OB_FAIL(tmp_tablet->init_with_updated_members(allocator, *old_tablet, snapshot_version))) {
    } else if (FALSE_IT(time_guard.click("InitNew"))) {
    } else if (OB_FAIL(ObTabletPersister::persist_and_transform_tablet(param, *tmp_tablet, new_tablet_hdl))) {
    } else if (FALSE_IT(disk_addr = new_tablet_hdl.get_obj()->tablet_addr_)) {
    } else if (OB_FAIL(safe_update_cas_tablet(key, disk_addr, old_tablet_handle, new_tablet_hdl, time_guard))) {
    }
  }
  return ret;
}

int ObLSTabletService::update_tablet_restore_status(
    const common::ObTabletID &tablet_id,
    const ObTabletRestoreStatus::STATUS &restore_status)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTabletRestoreStatus::STATUS current_status = ObTabletRestoreStatus::RESTORE_STATUS_MAX;
  bool can_change = false;

  ObTimeGuard time_guard("ObLSTabletService::update_tablet_restore_status", 1_s);
  ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
  time_guard.click("Lock");
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!ObTabletRestoreStatus::is_valid(restore_status))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(restore_status));
  } else if (OB_FAIL(direct_get_tablet(tablet_id, tablet_handle))) {
  } else if (tablet_handle.get_obj()->is_empty_shell()) {
    LOG_INFO("old tablet is empty shell tablet, should skip this operation", K(ret), "old_tablet", tablet_handle.get_obj());
  } else {
    time_guard.click("GetTablet");
    ObMetaDiskAddr disk_addr;
    const ObTabletMapKey key(tablet_id);
    ObTablet *tablet = tablet_handle.get_obj();
    ObTabletHandle new_tablet_handle;
    if (OB_FAIL(tablet->tablet_meta_.local_status_.get_restore_status(current_status))) {
    } else if (OB_FAIL(ObTabletRestoreStatus::check_can_change_status(current_status, restore_status, can_change))) {
    } else if (!can_change) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("can not change restore status", K(ret), K(current_status), K(restore_status), KPC(tablet));
    } else if (OB_FAIL(tablet->tablet_meta_.local_status_.set_restore_status(restore_status))) {
    } else {
      // TODO(jiahua.cjh) move check valid to tablet init after generate new version tablet.
      const ObTabletPersisterParam param(ls_->get_ls_epoch(), tablet_id);
      if (OB_FAIL(tablet->check_valid())) {
      } else if (OB_FAIL(ObTabletPersister::persist_and_transform_tablet(param, *tablet, new_tablet_handle))) {
      } else if (FALSE_IT(time_guard.click("Persist"))) {
      } else if (FALSE_IT(disk_addr = new_tablet_handle.get_obj()->tablet_addr_)) {
      } else if (OB_FAIL(safe_update_cas_tablet(key, disk_addr, tablet_handle, new_tablet_handle, time_guard))) {
      } else {
        LOG_INFO("succeeded to build new tablet", K(ret), K(key), K(disk_addr), K(restore_status), K(tablet_handle));
#ifdef ERRSIM
        SERVER_EVENT_SYNC_ADD("physical_restore", "update_tablet_restore_status",
                              "tablet_id", tablet_id.id(),
                              "old_restore_status", current_status,
                              "new_restore_status", restore_status);
#endif
      }

      if (OB_FAIL(ret)) {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = tablet->tablet_meta_.local_status_.set_restore_status(current_status))) {
          LOG_ERROR("failed to set restore status", K(tmp_ret), K(current_status), KPC(tablet));
          ob_abort();
        }
      }
    }
  }
  return ret;
}

int ObLSTabletService::replay_create_inner_tablet(
    common::ObArenaAllocator &allocator,
    const ObMetaDiskAddr &disk_addr,
    const ObTabletMapKey &key,
    const int64_t ls_epoch,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t buf_len = 0;
  int64_t pos = 0;
  ObTablet *tablet = tablet_handle.get_obj();
  tablet->tablet_addr_ = disk_addr;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->read_from_disk(disk_addr, allocator, buf, buf_len))) {
  } else if (OB_FAIL(tablet->deserialize_for_replay(allocator, buf, buf_len, pos))) {
  } else if (OB_FAIL(tablet->init_shared_params(key.tablet_id_))) {
  }
  return ret;
}


int ObLSTabletService::replay_create_tablet(
    const ObMetaDiskAddr &disk_addr,
    const char *buf,
    const int64_t buf_len,
    const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  bool b_exist = false;
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  ObFreezer *freezer = ls_->get_freezer();
  common::ObArenaAllocator allocator(common::ObMemAttr("ReplayCreate"));
  ObTabletHandle tablet_hdl;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(has_tablet(tablet_id, b_exist))) {
  } else if (b_exist) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("restart replay tablet should not exist", K(ret), K(tablet_id));
  } else {
    ObTimeGuard time_guard("ObLSTabletService::replay_create_tablet", 1_s);
    const ObTabletMapKey key(tablet_id);
    ObTablet *tablet = nullptr;
    int64_t pos = 0;
    ObMetaDiskAddr old_addr;
    ObTabletPoolType pool_type(ObTabletPoolType::TP_MAX);
    int64_t try_cache_size = 0;
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    time_guard.click("Lock");
    if (OB_FAIL(ObTabletCreateDeleteHelper::create_tmp_tablet(key, allocator, *ls_, tablet_hdl))) {
    } else if (FALSE_IT(tablet = tablet_hdl.get_obj())) {
    } else if (FALSE_IT(tablet->tablet_addr_ = disk_addr)) {
    } else if (OB_FAIL(t3m->get_tablet_addr(key, old_addr))) {
    } else if (OB_FAIL(tablet->deserialize_for_replay(allocator, buf, buf_len, pos))) {
    } else if (FALSE_IT(time_guard.click("Deserialize"))) {
    } else if (OB_FAIL(tablet->init_shared_params(tablet_id))) {
    } else if (OB_FAIL(tablet_id_set_.set(tablet_id))) {
    } else {
      if (tablet->is_empty_shell()) {
        pool_type = ObTabletPoolType::TP_NORMAL;
      } else {
        try_cache_size = tablet->get_try_cache_size();
        if (try_cache_size > ObStorageMetaMemMgr::NORMAL_TABLET_POOL_SIZE) {
          pool_type = ObTabletPoolType::TP_LARGE;
        } else {
          pool_type = ObTabletPoolType::TP_NORMAL;
        }
      }
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(t3m->compare_and_swap_tablet(
        key, old_addr,
        disk_addr,
        pool_type,
        true /* whether to set tablet pool */))) {
    } else if (FALSE_IT(time_guard.click("CASwap"))) {
    } else if (OB_FAIL(tablet->check_and_set_initial_state())) {
    } else if (OB_FAIL(tablet->start_direct_load_task_if_need())) {
    } else if (OB_FAIL(tablet->inc_macro_ref_cnt())) {
    }

    if (OB_SUCC(ret)) {
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(rollback_remove_tablet_without_lock(tablet_id))) {
      }
    }
  }
  return ret;
}

int ObLSTabletService::get_tablet_with_timeout(
    const common::ObTabletID &tablet_id,
    ObTabletHandle &handle,
    const int64_t retry_timeout_us,
    const ObMDSGetTabletMode mode,
    const share::SCN &snapshot)
{
  int ret = OB_SUCCESS;
  const ObTabletMapKey key(tablet_id);
  const int64_t timeout_step_us = 10_s;
  const int64_t snapshot_version = snapshot.get_val_for_tx();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid()
      || mode < ObMDSGetTabletMode::READ_ALL_COMMITED)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(mode));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::check_and_get_tablet(key, handle, timeout_step_us, mode, snapshot_version))) {
    while (OB_ALLOCATE_MEMORY_FAILED == ret && ObClockGenerator::getClock() < retry_timeout_us) {
      ret = ObTabletCreateDeleteHelper::check_and_get_tablet(key, handle, timeout_step_us, mode, snapshot_version);
    }
    if (OB_ALLOCATE_MEMORY_FAILED == ret) {
      ret = OB_TIMEOUT;
      LOG_WARN("get tablet timeout", K(ret), K(retry_timeout_us), K(ObTimeUtil::current_time()), K(mode));
    }
  }
  return ret;
}

int ObLSTabletService::direct_get_tablet(const common::ObTabletID &tablet_id, ObTabletHandle &handle)
{
#ifdef ENABLE_DEBUG_LOG
  ObTimeGuard tg("direct_get_tablet", 10_ms);
#endif
  int ret = OB_SUCCESS;
  const ObTabletMapKey key(tablet_id);

  if (OB_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, handle))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("failed to get tablet from t3m", K(ret), K(key));
    }
  }

  return ret;
}

int ObLSTabletService::inner_table_scan(
    ObTabletHandle &tablet_handle,
    ObTableScanIterator &iter,
    ObTableScanParam &param)
{
  // NOTICE: ObTableScanParam for_update_ param is ignored here,
  // upper layer will handle it, so here for_update_ is always false
  int ret = OB_SUCCESS;
  ObStoreCtx &store_ctx = iter.get_ctx_guard().get_store_ctx();
  int64_t data_max_schema_version = 0;
  bool is_bounded_staleness_read = (NULL == param.trans_desc_)
                                   ? false
                                   : param.snapshot_.is_weak_read();
  if (OB_UNLIKELY(!tablet_handle.is_valid()) || OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_handle), K(param));
  } else if (is_bounded_staleness_read
      && OB_FAIL(tablet_handle.get_obj()->get_max_schema_version(data_max_schema_version))) {
    LOG_WARN("failed to get max schema version", K(ret), K(param));
  } else if (is_bounded_staleness_read
      && OB_FAIL(tablet_handle.get_obj()->check_schema_version_for_bounded_staleness_read(
          param.schema_version_, data_max_schema_version, param.index_id_))) {
    //check schema_version with ref_table_id, because schema_version of scan_param is from ref table
    LOG_WARN("check schema version for bounded staleness read fail", K(ret), K(param));
    //need to get store ctx of PG, cur_key_ saves the real partition
  } else if (param.fb_snapshot_.is_min()) {
    ret = OB_SNAPSHOT_DISCARDED;
  } else {
    const int64_t snapshot_version = store_ctx.mvcc_acc_ctx_.get_snapshot_version().get_val_for_tx();
    const int64_t current_time = ObClockGenerator::getClock();
    const int64_t timeout = param.timeout_ - current_time;
    if (OB_UNLIKELY(timeout <= 0)) {
      ret = OB_TIMEOUT;
      LOG_WARN("table scan timeout", K(ret), K(current_time), "table_scan_param_timeout", param.timeout_, K(lbt()));
    } else if (OB_FAIL(tablet_handle.get_obj()->check_snapshot_readable_with_cache(snapshot_version, param.schema_version_, timeout))) {
    } else if (param.need_switch_param_) {
      if (OB_FAIL(iter.switch_param(param, tablet_handle))) {
      }
    } else if (OB_FAIL(iter.init(param, tablet_handle))) {
    }
  }

  if (OB_FAIL(ret)) {
  }

  return ret;
}

int ObLSTabletService::has_tablet(
    const common::ObTabletID &tablet_id,
    bool &b_exist)
{
  int ret = OB_SUCCESS;
  b_exist = false;
  const ObTabletMapKey key(tablet_id);
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();

  if (OB_FAIL(t3m->has_tablet(key, b_exist))) {
  }

  return ret;
}

int ObLSTabletService::create_tablet(
    const common::ObTabletID &tablet_id,
    const common::ObTabletID &data_tablet_id,
    const share::SCN &create_scn,
    const int64_t snapshot_version,
    const ObCreateTabletSchema &create_tablet_schema,
    const bool need_create_empty_major_sstable,
    const share::SCN &clog_checkpoint_scn,
    const share::SCN &mds_checkpoint_scn,
    const storage::ObTabletMdsUserDataType &create_type,
    const bool micro_index_clustered,
    const uint64_t data_format_version,
    ObTabletHandle &tablet_handle,
    const share::ObForkTabletInfo &fork_info)
{
  int ret = OB_SUCCESS;
  UNUSED(data_format_version);
  common::ObArenaAllocator tmp_allocator(common::ObMemAttr("CreateTab"));
  common::ObArenaAllocator *allocator = nullptr;
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  const ObTabletMapKey key(tablet_id);
  ObTablet *tablet = nullptr;
  ObFreezer *freezer = ls_->get_freezer();
  tablet_handle.reset();

  if (OB_FAIL(ObTabletCreateDeleteHelper::prepare_create_msd_tablet())) {
  } else {
    ObUpdateTabletPointerParam param;
    ObBucketHashWLockGuard lock_guard(bucket_lock_, key.tablet_id_.hash());
    if (OB_FAIL(ObTabletCreateDeleteHelper::create_msd_tablet(key, tablet_handle))) {
    } else if (OB_ISNULL(tablet = tablet_handle.get_obj())
        || OB_ISNULL(allocator = tablet_handle.get_allocator())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("new tablet is null", K(ret), KP(tablet), KP(allocator), K(tablet_handle));
    } else if (OB_FAIL(tablet->init_for_first_time_creation(*allocator, tablet_id, data_tablet_id,
        create_scn, snapshot_version, create_tablet_schema, need_create_empty_major_sstable, clog_checkpoint_scn, mds_checkpoint_scn,
        micro_index_clustered, freezer, fork_info))) {
    } else if (OB_FAIL(tablet->get_updating_tablet_pointer_param(param))) {
    } else if (OB_FAIL(t3m->compare_and_swap_tablet(key, tablet_handle, tablet_handle, param))) {
    } else if (OB_FAIL(tablet_id_set_.set(tablet_id))) {
    } else {
      report_tablet_to_rs(tablet_id);
    }

    if (OB_SUCC(ret)) {
      LOG_INFO("succeed to create tablet", K(ret), K(tablet_id));
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(rollback_remove_tablet_without_lock(tablet_id))) {
      }
    }
  }

  return ret;
}

int ObLSTabletService::create_inner_tablet(
    const common::ObTabletID &tablet_id,
    const common::ObTabletID &data_tablet_id,
    const share::SCN &create_scn,
    const int64_t snapshot_version,
    const ObCreateTabletSchema &create_tablet_schema,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  uint64_t compat_version = 0;
  
  bool need_create_empty_major_old_version = true;
  common::ObArenaAllocator allocator(common::ObMemAttr("LSCreateTab"));
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  const ObTabletMapKey key(tablet_id);
  ObTablet *tmp_tablet = nullptr;
  ObFreezer *freezer = ls_->get_freezer();
  ObTabletHandle tmp_tablet_hdl;
  ObMetaDiskAddr disk_addr;
  const ObTabletPersisterParam param(ls_->get_ls_epoch(), tablet_id);
  ObTimeGuard time_guard("ObLSTabletService::create_inner_tablet", 10_ms);
  const share::SCN clog_checkpoint_scn = ObTabletMeta::INIT_CLOG_CHECKPOINT_SCN;
  const share::SCN mds_checkpoint_scn = ObTabletMeta::INIT_CLOG_CHECKPOINT_SCN;

  ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
  if (OB_FAIL(ObTabletCreateDeleteHelper::create_tmp_tablet(key, allocator, *ls_, tmp_tablet_hdl))) {
  } else if (OB_ISNULL(tmp_tablet = tmp_tablet_hdl.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("new tablet is null", K(ret), KPC(tmp_tablet), K(tmp_tablet_hdl));
  } else if (FALSE_IT(time_guard.click("CreateTablet"))) {
  } else if (OB_FAIL(tmp_tablet->init_for_first_time_creation(allocator, tablet_id, data_tablet_id,
      create_scn, snapshot_version, create_tablet_schema, true/*need_create_empty_major_sstable*/, clog_checkpoint_scn, mds_checkpoint_scn,
      false/*micro_index_clustered*/, freezer))) {
  } else if (FALSE_IT(time_guard.click("InitTablet"))) {
  } else if (OB_FAIL(ObTabletPersister::persist_and_transform_tablet(param, *tmp_tablet, tablet_handle))) {
  } else if (FALSE_IT(time_guard.click("Persist"))) {
  } else if (FALSE_IT(disk_addr = tablet_handle.get_obj()->get_tablet_addr())) {
  } else if (OB_FAIL(safe_create_cas_tablet(tablet_id, disk_addr, tablet_handle, time_guard))) {
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("create ls inner tablet success", K(ret), K(key), K(disk_addr));
  } else {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(rollback_remove_tablet_without_lock(tablet_id))) {
    }
  }
  return ret;
}

int ObLSTabletService::rollback_remove_tablet(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls tablet service do not init", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    if (OB_FAIL(rollback_remove_tablet_without_lock(tablet_id))) {
    }
  }
  return ret;
}

int ObLSTabletService::rollback_remove_tablet_without_lock(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  const ObTabletMapKey key(tablet_id);

  if (OB_FAIL(tablet_id_set_.erase(tablet_id))) {
    if (OB_HASH_NOT_EXIST == ret) {
      // tablet id is already erased
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to erase tablet id from set", K(ret), K(tablet_id));
    }
  }

  if (OB_SUCC(ret)) {
    // loop retry to delete tablet from t3m
    while (OB_FAIL(t3m->del_tablet(key))) {
      if (REACH_TIME_INTERVAL(10_s)) {
        LOG_ERROR("failed to delete tablet from t3m", K(ret), K(tablet_id));
      }
    }
  }

  return ret;
}

int ObLSTabletService::create_memtable(const common::ObTabletID &tablet_id, CreateMemtableArg &arg)
{
  int ret = OB_SUCCESS;
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  ObTabletHandle old_tablet_handle;
  const ObTabletMapKey key(tablet_id);
  ObTabletHandle new_tablet_handle;

  ObTimeGuard time_guard("ObLSTabletService::create_memtable", 10_ms);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || arg.schema_version_ < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(arg));
  } else {
    // we need bucket lock here to protect multi version tablet creation
    // during tablet creating new memtable and put it into table store.
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    time_guard.click("Lock");
    if (OB_FAIL(direct_get_tablet(tablet_id, old_tablet_handle))) {
    } else if (old_tablet_handle.get_obj()->is_empty_shell()) {
      LOG_INFO("old tablet is empty shell tablet, should skip this operation", K(ret), "old_tablet", old_tablet_handle.get_obj());
    } else {
      time_guard.click("get tablet");
      ObTabletCreateDeleteMdsUserData user_data;
      ObUpdateTabletPointerParam param;
      mds::MdsWriter writer;
      mds::TwoPhaseCommitState trans_stat;
      share::SCN trans_version;
      ObTablet &old_tablet = *(old_tablet_handle.get_obj());
      bool is_committed = false;
      // Do not create a new memtable while tablet status is being changed.
      if (arg.for_replay_) {
      } else if (OB_FAIL(old_tablet.ObITabletMdsInterface::get_latest_tablet_status(user_data, writer, trans_stat, trans_version))) {
      } else if (FALSE_IT(is_committed = mds::TwoPhaseCommitState::ON_COMMIT == trans_stat)) {
      } else if (!is_committed || !user_data.tablet_status_.is_writable_for_dml()) {
        ret = OB_EAGAIN;
        if (REACH_TIME_INTERVAL(10000)) {
          LOG_WARN("tablet status not allow create new memtable", K(ret), K(is_committed), K(user_data));
        }
      }
      if (FAILEDx(old_tablet.create_memtable(arg))) {
        if (OB_MINOR_FREEZE_NOT_ALLOW != ret) {
          LOG_WARN("fail to create memtable", K(ret), K(new_tablet_handle), K(tablet_id), K(arg));
        }
      } else if (FALSE_IT(time_guard.click("create memtable"))) {
      } else if (OB_FAIL(old_tablet.get_updating_tablet_pointer_param(param, false /*update tablet attr*/))) {
      } else if (OB_FAIL(t3m->compare_and_swap_tablet(key, old_tablet_handle, old_tablet_handle, param))) {
      }
    }
  }

  return ret;
}

// ATTENTION!
// here we pass VALUE rather than REF for tablet id,
// because tablet id may be from iter, which will be reset in function,
// thus tablet id will be invalid
int ObLSTabletService::get_read_tables(
    const common::ObTabletID tablet_id,
    const int64_t timeout_us,
    // snapshot used for get tablet for mds
    const int64_t snapshot_version_for_tablet,
    // snapshot used for filter tables in table_store
    const int64_t snapshot_version_for_tables,
    ObTabletTableIterator &iter,
    const bool allow_no_ready_read)
{
  return inner_get_read_tables(tablet_id, timeout_us, snapshot_version_for_tablet,
      snapshot_version_for_tables, iter, allow_no_ready_read,
      ObMDSGetTabletMode::READ_READABLE_COMMITED);
}

int ObLSTabletService::inner_get_read_tables(
    const common::ObTabletID tablet_id,
    const int64_t timeout_us,
    const int64_t snapshot_version_for_tablet,
    const int64_t snapshot_version_for_tables,
    ObTabletTableIterator &iter,
    const bool allow_no_ready_read,
    const ObMDSGetTabletMode mode)
{
  int ret = OB_SUCCESS;
  ObTabletHandle &handle = iter.tablet_handle_;
  iter.reset();
  ObTabletMapKey key;
  key.tablet_id_ = tablet_id;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() ||
                         snapshot_version_for_tables < 0 ||
                         snapshot_version_for_tablet < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(snapshot_version_for_tablet),
             K(snapshot_version_for_tables));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::check_and_get_tablet(key, handle,
      timeout_us,
      mode,
      snapshot_version_for_tablet))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("fail to check and get tablet", K(ret), K(key), K(timeout_us),
               K(snapshot_version_for_tablet), K(snapshot_version_for_tables));
    }
  } else if (OB_UNLIKELY(!handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, invalid tablet handle", K(ret), K(handle));
  } else if (OB_FAIL(handle.get_obj()->get_read_tables(snapshot_version_for_tables, iter, allow_no_ready_read))) {
  }
  return ret;
}

int ObLSTabletService::set_tablet_status(
    const common::ObTabletID &tablet_id,
    const ObTabletCreateDeleteMdsUserData &tablet_status,
    mds::MdsCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || !tablet_status.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(tablet_status));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    ObTabletHandle tablet_handle;
    if (OB_FAIL(direct_get_tablet(tablet_id, tablet_handle))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        ret = OB_EAGAIN;
        LOG_WARN("this tablet has been deleted, skip it", K(ret), K(tablet_id));
      } else {
        LOG_WARN("fail to get tablet", K(ret));
      }
    } else if (OB_FAIL(tablet_handle.get_obj()->set_tablet_status(tablet_status, ctx))) {
    } else {
      LOG_INFO("succeeded to set tablet status", K(ret), K(tablet_id), K(tablet_status));
    }
  }
  return ret;
}

int ObLSTabletService::replay_set_tablet_status(
    const common::ObTabletID &tablet_id,
    const share::SCN &scn,
    const ObTabletCreateDeleteMdsUserData &tablet_status,
    mds::MdsCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || !tablet_status.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(tablet_status));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    ObTabletHandle tablet_handle;
    if (OB_FAIL(direct_get_tablet(tablet_id, tablet_handle))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        ret = OB_EAGAIN;
        LOG_WARN("this tablet has been deleted, skip it", K(ret), K(tablet_id));
      } else {
        LOG_WARN("fail to get tablet", K(ret));
      }
    } else if (OB_FAIL(tablet_handle.get_obj()->replay_set_tablet_status(scn, tablet_status, ctx))) {
    } else {
      LOG_INFO("succeeded to replay set tablet status", K(ret), K(tablet_id), K(scn), K(tablet_status));
    }
  }
  return ret;
}

int ObLSTabletService::set_ddl_complete(
  const common::ObTabletID &tablet_id,
  const mds::DummyKey &key,
  const ObTabletDDLCompleteMdsUserData &ddl_complete,
  mds::MdsCtx &ctx,
  const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K(is_inited_));
  } else if (tablet_id.is_inner_tablet()) {
    /* skip */
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    ObTabletHandle tablet_handle;
    if (OB_FAIL(direct_get_tablet(tablet_id, tablet_handle))) {
    } else if (OB_FAIL(tablet_handle.get_obj()->set_ddl_complete(key, ddl_complete, ctx, timeout_us))) {
    } else {
      LOG_INFO("succeeded to set ddl info", K(ret), K(tablet_id), K(key), K(ddl_complete), K(timeout_us));
    }
  }
  return ret;
}

int ObLSTabletService::set_ddl_info(
    const common::ObTabletID &tablet_id,
    const ObTabletBindingMdsUserData &ddl_data,
    mds::MdsCtx &ctx,
    const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(ddl_data));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    ObTabletHandle tablet_handle;
    if (OB_FAIL(direct_get_tablet(tablet_id, tablet_handle))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        ret = OB_EAGAIN;
        LOG_WARN("this tablet has been deleted, skip it", K(ret), K(tablet_id));
      } else {
        LOG_WARN("fail to get tablet", K(ret));
      }
    } else if (OB_FAIL(tablet_handle.get_obj()->set_ddl_info(ddl_data, ctx, timeout_us))) {
    } else {
      LOG_INFO("succeeded to set ddl info", K(ret), K(tablet_id), K(ddl_data), K(timeout_us));
    }
  }
  return ret;
}

int ObLSTabletService::replay_set_ddl_info(
    const common::ObTabletID &tablet_id,
    const share::SCN &scn,
    const ObTabletBindingMdsUserData &ddl_data,
    mds::MdsCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(ddl_data));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    ObTabletHandle tablet_handle;
    if (OB_FAIL(direct_get_tablet(tablet_id, tablet_handle))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        ret = OB_EAGAIN;
        LOG_WARN("this tablet has been deleted, skip it", K(ret), K(tablet_id));
      } else {
        LOG_WARN("fail to get tablet", K(ret));
      }
    } else if (OB_FAIL(tablet_handle.get_obj()->replay_set_ddl_info(scn, ddl_data, ctx))) {
    } else {
      LOG_INFO("succeeded to set ddl info", K(ret), K(tablet_id), K(ddl_data), K(scn));
    }
  }
  return ret;
}

int ObLSTabletService::replay_set_ddl_complete(
    const common::ObTabletID &tablet_id,
    const share::SCN &scn,
    const mds::DummyKey &key,
    const ObTabletDDLCompleteMdsUserData &ddl_data,
    mds::MdsCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(ddl_data));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());
    ObTabletHandle tablet_handle;
    if (OB_FAIL(direct_get_tablet(tablet_id, tablet_handle))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        ret = OB_EAGAIN;
        LOG_WARN("this tablet has been deleted, skip it", K(ret), K(tablet_id));
      } else {
        LOG_WARN("fail to get tablet", K(ret));
      }
    } else if (OB_FAIL(tablet_handle.get_obj()->replay_set_ddl_complete(scn, key, ddl_data, ctx))) {
    } else {
      LOG_INFO("succeeded to replay set ddl info", K(ret), K(tablet_id), K(key), K(ddl_data), K(scn));
    }
  }
  return ret;
}

int ObLSTabletService::insert_rows(
    ObTabletHandle &tablet_handle,
    ObStoreCtx &ctx,
    const ObDMLBaseParam &dml_param,
    const common::ObIArray<uint64_t> &column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;

  NG_TRACE(S_insert_rows_begin);
  int64_t afct_num = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!ctx.is_valid())
      || !ctx.is_write()
      || OB_UNLIKELY(!dml_param.is_valid())
      || OB_UNLIKELY(column_ids.count() <= 0)
      || OB_ISNULL(row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ctx), K(dml_param), K(column_ids), KP(row_iter));
  } else {
    HEAP_VAR(ObDMLRunningCtx, run_ctx, ctx,
                            dml_param,
                            ctx.mvcc_acc_ctx_.mem_ctx_->get_query_allocator(),
                            ObDmlFlag::DF_INSERT) {
    int64_t row_count = 0;
    int64_t batch_idx = 0;
    ObDatumRow *rows = nullptr;
    if (OB_FAIL(prepare_dml_running_ctx(&column_ids, nullptr, tablet_handle, run_ctx))) {
    } else {
      tablet_handle.reset();
      ObTabletHandle tmp_handle;
      HEAP_VAR(ObRowsInfo, rows_info) {
        ObRelativeTable &relative_table = run_ctx.relative_table_;
        const ObColDescIArray &col_descs = *(run_ctx.col_descs_);
        blocksstable::ObDatumRowIterator *unused_dup_row_iter = nullptr;
        while (OB_SUCC(ret) && OB_SUCC(get_next_rows(row_iter, rows, row_count))) {
          // need to be called just after get_next_row to ensure that previous row's LOB memoroy is valid if get_next_row accesses it
          dml_param.lob_allocator_.reuse();
          // Let ObStorageTableGuard refresh retired memtable, should not hold origin tablet handle
          // outside the while loop.
          if (tmp_handle.get_obj() != relative_table.tablet_iter_.get_tablet_handle().get_obj()) {
            tmp_handle = run_ctx.relative_table_.tablet_iter_.get_tablet_handle();
            rows_info.reset();
            if (OB_FAIL(rows_info.init(
                col_descs, relative_table, ctx, tmp_handle.get_obj()->get_rowkey_read_info()))) {
            }
          }
          if (OB_FAIL(ret)) {
          } else if (row_count <= 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("row_count should be greater than 0", K(ret));
          } else {
            for (int64_t i = 0; i < row_count; i++) {
              rows[i].row_flag_.set_flag(ObDmlFlag::DF_INSERT);
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(rows_info.assign_rows(row_count, rows))) {
          } else if (OB_FAIL(insert_rows_to_tablet(tmp_handle, run_ctx, rows_info))) {
          } else {
            afct_num += row_count;
          }
        } // end of while
      } else {
        LOG_WARN("Failed to allocate ObRowsInfo", K(ret));
      }
    }

    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
    if (OB_SUCC(ret) && !run_ctx.lob_dml_ctx_.is_all_task_done()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("lob data may not be insert", K(ret), K(run_ctx.lob_dml_ctx_));
    }

      }
}

  if (OB_SUCC(ret)) {
    affected_rows = afct_num;
  }
  NG_TRACE(S_insert_rows_end);

  return ret;
}

int ObLSTabletService::get_storage_row(
    const ObDatumRow &sql_row,
    const ObIArray<uint64_t> &column_ids,
    const ObColDescIArray &column_descs,
    ObRowGetter &row_getter,
    ObRelativeTable &data_table,
    ObStoreCtx &store_ctx,
    const ObDMLBaseParam &dml_param,
    ObDatumRow *&out_row,
    bool use_fuse_row_cache)
{
  int ret = OB_SUCCESS;
  ObDatumRowkey datum_rowkey;
  ObDatumRowkeyHelper rowkey_helper;
  if (OB_FAIL(rowkey_helper.prepare_datum_rowkey(sql_row, data_table.get_rowkey_column_num(), column_descs, datum_rowkey))) {
  } else if (OB_FAIL(init_row_getter(row_getter, store_ctx, dml_param, column_ids, data_table, false/*is_multi_get*/, true))) {
  } else if (OB_FAIL(row_getter.open(datum_rowkey, use_fuse_row_cache))) {
  } else if (OB_FAIL(row_getter.get_next_row(out_row))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("failed to get single storage row", K(ret), K(sql_row));
    }
  }
  return ret;
}

int ObLSTabletService::mock_duplicated_rows_(blocksstable::ObDatumRowIterator *&duplicated_rows)
{
  int ret = OB_SUCCESS;
  ObValueRowIterator *dup_iter = NULL;

  if (OB_ISNULL(dup_iter = ObQueryIteratorFactory::get_insert_dup_iter())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("no memory to alloc ObValueRowIterator", K(ret));
  } else {
    duplicated_rows = dup_iter;
    if (OB_FAIL(dup_iter->init())) {
      LOG_WARN("failed to initialize ObValueRowIterator", K(ret));
      ObQueryIteratorFactory::free_insert_dup_iter(duplicated_rows);
      duplicated_rows = nullptr;
    }
  }

  return ret;
}

int ObLSTabletService::insert_rows_with_fetch_dup(
    ObTabletHandle &tablet_handle,
    ObStoreCtx &ctx,
    const ObDMLBaseParam &dml_param,
    const common::ObIArray<uint64_t> &column_ids,
    const common::ObIArray<uint64_t> &duplicated_column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    const ObInsertFlag flag,
    int64_t &affected_rows,
    blocksstable::ObDatumRowIterator *&duplicated_rows)
{
  int ret = OB_SUCCESS;
  int64_t afct_num = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!ctx.is_valid()
             || !ctx.is_write()
             || !dml_param.is_valid()
             || column_ids.count() <= 0
             || duplicated_column_ids.count() <= 0
             || nullptr == row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ctx), K(dml_param),
        K(column_ids), K(duplicated_column_ids), KP(row_iter), K(flag));
  } else {
    HEAP_VAR(ObDMLRunningCtx, run_ctx, ctx,
                            dml_param,
                            ctx.mvcc_acc_ctx_.mem_ctx_->get_query_allocator(),
                            ObDmlFlag::DF_INSERT) {
    int64_t row_count = 0;
    ObDatumRow *rows = nullptr;
    if (OB_FAIL(prepare_dml_running_ctx(&column_ids, nullptr, tablet_handle, run_ctx))) {
    } else {
      tablet_handle.reset();
      ObTabletHandle tmp_handle;
      HEAP_VAR(ObRowsInfo, rows_info) {
        int64_t dup_row_count = 0;
        bool has_ignore_dup_error = false;
        ObRelativeTable &relative_table = run_ctx.relative_table_;
        const ObColDescIArray &col_descs = *(run_ctx.col_descs_);
        while (OB_SUCC(ret) && OB_SUCC(get_next_rows(row_iter, rows, row_count))) {
          // need to be called just after get_next_row to ensure that previous row's LOB memoroy is valid if get_next_row accesses it
          dml_param.lob_allocator_.reuse();
          // Let ObStorageTableGuard refresh retired memtable, should not hold origin tablet handle
          // outside the while loop.
          if (tmp_handle.get_obj() != relative_table.tablet_iter_.get_tablet_handle().get_obj()) {
            tmp_handle = relative_table.tablet_iter_.get_tablet_handle();
            rows_info.reset();
            if (OB_FAIL(rows_info.init(col_descs,
                                       relative_table,
                                       ctx,
                                       tmp_handle.get_obj()->get_rowkey_read_info()))) {
            }
          }
          if (OB_FAIL(ret)) {
          } else if (row_count <= 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("row_count should be greater than 0", K(ret));
          } else {
            for (int64_t i = 0; i < row_count; i++) {
              rows[i].row_flag_.set_flag(ObDmlFlag::DF_INSERT);
            }
            if (OB_FAIL(rows_info.assign_rows(row_count, rows))) {
            } else if (OB_FAIL(rows_info.set_need_find_all_duplicate_rows(true, /*need_find_all_duplicate_key*/
                                                                          &duplicated_column_ids,
                                                                          &duplicated_rows))) {
            } else if (OB_FAIL(insert_rows_to_tablet(tmp_handle, run_ctx, rows_info))) {
              // the dup error is ignored here, so need clean up lob tasks that were successfully created but not executed
              run_ctx.lob_dml_ctx_.reuse();
              if (OB_ERR_PRIMARY_KEY_DUPLICATE == ret) {
                for (int64_t i = 0; i < row_count; i++) {
                  if (rows_info.is_row_duplicate(i)) {
                    dup_row_count++;
                  }
                }
                has_ignore_dup_error = true;
                ret = OB_SUCCESS; // ignore this error to continue to find all duplicate rows
              } else if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
                LOG_WARN("failed to write row", K(ret));
              }
            } else {
              afct_num += row_count;
            }
          }
        } // end of while

        if (OB_ITER_END == ret) {
          if (has_ignore_dup_error) {
            ret = OB_ERR_PRIMARY_KEY_DUPLICATE; // recover the duplicate key error
            if (nullptr == duplicated_rows) {
              // For primary key conflicts caused by concurrent insertions within
              // a statement, we need to return the corresponding duplicated_rows.
              // However, under circumstances where an exception may unexpectedly
              // prevent us from reading the conflicting rows within statements,
              // at such times, it becomes necessary for us to mock the rows.
              int tmp_ret = OB_SUCCESS;
              if (OB_TMP_FAIL(mock_duplicated_rows_(duplicated_rows))) {
                LOG_WARN("failed to mock duplicated rows", K(tmp_ret));
                ret = tmp_ret;
              }
            }
          } else {
            ret = OB_SUCCESS;
          }
        }

        if (OB_SUCC(ret) && !run_ctx.lob_dml_ctx_.is_all_task_done()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("lob data may not be insert", K(ret), K(run_ctx.lob_dml_ctx_));
        }
      } else {
        LOG_WARN("Failed to allocate ObRowsInfo", K(ret));
      }
    }
  }
  } // end HEAP_VAR(run_ctx)

  if (OB_SUCC(ret)) {
    affected_rows = afct_num;
  }
  return ret;
}

static inline
bool is_lob_update(ObDMLRunningCtx &run_ctx, const ObIArray<int64_t> &update_idx)
{
  bool bool_ret = false;
  if (run_ctx.relative_table_.is_storage_index_table() &&
      run_ctx.relative_table_.is_index_local_storage() &&
      run_ctx.relative_table_.is_vector_index()) {
    // bool_ret = false
  } else {
    for (int64_t i = 0; i < update_idx.count() && !bool_ret; ++i) {
      int64_t idx = update_idx.at(i);
      if (run_ctx.col_descs_->at(idx).col_type_.is_lob_storage()) {
        bool_ret = true;
      }
    }
  }
  return bool_ret;
}

int ObLSTabletService::update_rows(
    ObTabletHandle &tablet_handle,
    ObStoreCtx &ctx,
    const ObDMLBaseParam &dml_param,
    const ObIArray<uint64_t> &column_ids,
    const ObIArray< uint64_t> &updated_column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  NG_TRACE(S_update_rows_begin);
  const ObTabletID &data_tablet_id = ctx.tablet_id_;
  int64_t afct_num = 0;
  int64_t dup_num = 0;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!ctx.is_valid()
             || !ctx.is_write()
             || !dml_param.is_valid()
             || column_ids.count() <= 0
             || updated_column_ids.count() <= 0
             || nullptr == row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ctx), K(dml_param),
        K(column_ids), K(updated_column_ids), KP(row_iter));
  } else {
    HEAP_VAR(ObDMLRunningCtx, run_ctx, ctx,
                            dml_param,
                            ctx.mvcc_acc_ctx_.mem_ctx_->get_query_allocator(),
                            ObDmlFlag::DF_UPDATE,
                            true /* is_need_check_old_row_ */) {
    ObIAllocator &work_allocator = run_ctx.allocator_;
    bool rowkey_change = false;
    UpdateIndexArray update_idx;
    ObDatumRowStore row_store;
    bool lob_update = false;
    ObRelativeTable &relative_table = run_ctx.relative_table_;

    if (OB_FAIL(prepare_dml_running_ctx(&column_ids, &updated_column_ids, tablet_handle, run_ctx))) {
    } else if (FALSE_IT(tablet_handle.reset())) {
    } else if (OB_UNLIKELY(!relative_table.is_valid())) {
      ret = OB_ERR_SYS;
      LOG_ERROR("data table is not prepared", K(ret));
    } else if (OB_FAIL(construct_update_idx(relative_table.get_rowkey_column_num(),
        run_ctx.col_map_, updated_column_ids, update_idx))) {
    } else if (OB_FAIL(check_rowkey_change(updated_column_ids, relative_table, rowkey_change))) {
    } else {
      int64_t cur_time = 0;
      lob_update = is_lob_update(run_ctx, update_idx);
      ObDatumRow *old_rows = nullptr;
      ObDatumRow *new_rows = nullptr;
      ObDatumRow *tmp_rows = nullptr;
      int64_t old_rows_count = 0;
      int64_t new_rows_count = 0;
      ObTabletHandle tmp_handle;
      ObRowsInfo *rows_infos = nullptr;
      int64_t max_tmp_row_cnt = 0;
      /**
      * When _ob_immediate_row_conflict_check is true, indicates MySQL compatibility mode requiring:
      * - Row-by-row UPDATE execution
      * - Immediate conflict row checking
      *
      * Normally conflict row checking not needed for non-unique indexes, it is required in these special cases:
      * 1. Partitioned table PK updates causing row movement:
      *    - DAS layer splits into DELETE+INSERT
      *    - Global indexes may use UPDATE directly
      *    - Different execution paths may cause inconsistent conflict handling
      *    between main table and index table
      *
      * 2. PDML (Parallel DML) PK updates:
      *    - Different threads updating different rows
      *    - Update order mismatch between main table and index table
      *    - May lead to inconsistent conflict resolution
      *
      * For these cases, non-unique indexes MUST still perform conflict checking
      * (through duplicate key error reporting) to prevent data inconsistency, but this checking
      * can keep using batch interfaces without row-by-row updates.
      */
      const bool use_row_by_row_update = ctx.mvcc_acc_ctx_.write_flag_.is_immediate_row_check() &&
        rowkey_change && (!relative_table.is_storage_index_table() || relative_table.is_unique_index());
      // The batch interface keeps delayed-new-row handling disabled for performance.
      const bool delay_new = false;
      const ObColDescIArray &col_descs = *(run_ctx.col_descs_);

      while (OB_SUCC(ret)
          && OB_SUCC(row_iter->get_next_rows(old_rows, old_rows_count))
          && OB_SUCC(row_iter->get_next_rows(new_rows, new_rows_count))) {
        // need to be called just after get_next_row to ensure that previous row's LOB memoroy is valid if get_next_row accesses it
        dml_param.lob_allocator_.reuse();
        // Let ObStorageTableGuard refresh retired memtable, should not hold origin tablet handle
        // outside the while loop.
        if (tmp_handle.get_obj() != run_ctx.relative_table_.tablet_iter_.get_tablet_handle().get_obj()) {
          tmp_handle = run_ctx.relative_table_.tablet_iter_.get_tablet_handle();
          if (nullptr != rows_infos) {
            rows_infos[0].reset();
            rows_infos[1].reset();
            if (OB_FAIL(rows_infos[0].init(col_descs, relative_table, ctx,
                tmp_handle.get_obj()->get_rowkey_read_info()))) {
            } else if (OB_FAIL(rows_infos[1].init(col_descs, relative_table, ctx,
                tmp_handle.get_obj()->get_rowkey_read_info()))) {
            }
          }
        }
        cur_time = ObClockGenerator::getClock();
        if (OB_UNLIKELY(cur_time > dml_param.timeout_)) {
          ret = OB_TIMEOUT;
          LOG_WARN("query timeout", K(cur_time), K(dml_param), K(ret));
        } else if (OB_UNLIKELY(old_rows_count != new_rows_count)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("row count is not equal", K(ret), K(old_rows_count), K(new_rows_count));
        } else if (1 == new_rows_count) {
          old_rows[0].row_flag_.set_flag(ObDmlFlag::DF_UPDATE);
          new_rows[0].row_flag_.set_flag(ObDmlFlag::DF_UPDATE);
        } else if (nullptr == rows_infos) { // is first batch
          if (OB_ISNULL(rows_infos = static_cast<ObRowsInfo*>(work_allocator.alloc(2 * sizeof(ObRowsInfo) )))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("fail to allocate memory", K(ret));
          } else {
            new (rows_infos) ObRowsInfo[2];
            if (OB_FAIL(rows_infos[0].init(
                col_descs, relative_table, ctx, tmp_handle.get_obj()->get_rowkey_read_info()))) {
            } else if (OB_FAIL(rows_infos[1].init(
                col_descs, relative_table, ctx, tmp_handle.get_obj()->get_rowkey_read_info()))) {
            }
          }
        }

        if (OB_SUCC(ret) && nullptr != rows_infos && 1 != new_rows_count) {
          for (int64_t i = 0; i < new_rows_count; i++) {
            old_rows[i].row_flag_.set_flag(ObDmlFlag::DF_UPDATE);
            new_rows[i].row_flag_.set_flag(ObDmlFlag::DF_UPDATE);
          }
          if (OB_FAIL(rows_infos[0].assign_rows(old_rows_count, old_rows))) {
          } else if (OB_FAIL(rows_infos[1].assign_rows(new_rows_count, new_rows))) {
          }
          // the tmp_tbl_rows is used to delete the old row if the rowkey change
          if (OB_SUCC(ret) && rowkey_change && (tmp_rows == nullptr || new_rows_count > max_tmp_row_cnt)) {
            max_tmp_row_cnt = new_rows_count;
            if (tmp_rows != nullptr) {
              work_allocator.free(tmp_rows);
              tmp_rows = nullptr;
            }
            if (OB_ISNULL(tmp_rows = static_cast<ObDatumRow*>(work_allocator.alloc(new_rows_count * sizeof(ObDatumRow))))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("fail to allocate memory", K(ret));
            } else {
              new (tmp_rows) ObDatumRow[new_rows_count];
            }
          }
        }

        if (OB_SUCC(ret)) {
          if (1 == new_rows_count) {
            if (OB_FAIL(update_row_to_tablet(tmp_handle,
                                             run_ctx,
                                             rowkey_change,
                                             update_idx,
                                             delay_new,
                                             lob_update,
                                             old_rows[0],
                                             new_rows[0],
                                             row_store))) {
            }
          } else {
            if (use_row_by_row_update) {
              for (int64_t i = 0; OB_SUCC(ret) && i < new_rows_count; i++) {
                if (OB_FAIL(update_row_to_tablet(tmp_handle,
                                                 run_ctx,
                                                 rowkey_change,
                                                 update_idx,
                                                 delay_new,
                                                 lob_update,
                                                 old_rows[i],
                                                 new_rows[i],
                                                 row_store))) {
                }
              }
            } else {
              if (OB_FAIL(update_rows_to_tablet(tmp_handle,
                                                run_ctx,
                                                rowkey_change,
                                                update_idx,
                                                delay_new,
                                                lob_update,
                                                tmp_rows,
                                                rows_infos[0],
                                                rows_infos[1],
                                                row_store))) {
              }
            }
          }
          if (OB_SUCC(ret)) {
            afct_num += new_rows_count;
          }
        }
      } // end of while

      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
      if (OB_SUCC(ret) && !run_ctx.lob_dml_ctx_.is_all_task_done()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("lob data may not be insert", K(ret), K(run_ctx.lob_dml_ctx_));
      }
      if (OB_SUCC(ret) && row_store.get_row_count() > 0) {
        ObDatumRow &old_row_for_delay = old_rows[0];
        ObDatumRow &new_row_for_delay = new_rows[0];
        if (OB_FAIL(delay_process_new_rows(run_ctx,
                                           update_idx,
                                           rowkey_change,
                                           old_row_for_delay,
                                           new_row_for_delay,
                                           row_store))) {
        }
      }
      if (nullptr != rows_infos) {
        rows_infos[0].~ObRowsInfo();
        rows_infos[1].~ObRowsInfo();
        work_allocator.free(rows_infos);
      }
      if (nullptr != tmp_rows) {
        work_allocator.free(tmp_rows);
      }
    }

    if (OB_SUCC(ret)) {
      affected_rows = afct_num;
    }
      }
}
  NG_TRACE(S_update_rows_end);
  return ret;
}

// Process rows stored for delayed new-row handling.
int ObLSTabletService::delay_process_new_rows(
    ObDMLRunningCtx &run_ctx,
    const common::ObIArray<int64_t> &update_idx,
    const bool rowkey_change,
    ObDatumRow &old_row,
    ObDatumRow &new_row,
    ObDatumRowStore &row_store)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tmp_handle;
  ObDatumRowStore::Iterator row_iter = row_store.begin();
  while (OB_SUCC(ret) && OB_SUCC(row_iter.get_next_row(new_row))) {
    // Let ObStorageTableGuard refresh retired memtable, should not hold origin tablet handle
    // outside the while loop.
    if (tmp_handle.get_obj() != run_ctx.relative_table_.tablet_iter_.get_tablet_handle().get_obj()) {
      tmp_handle = run_ctx.relative_table_.tablet_iter_.get_tablet_handle();
    }
    if (OB_FAIL(row_iter.get_next_row(old_row))) {
    } else if (OB_FAIL(process_lob_before_update(tmp_handle,
                                                 run_ctx,
                                                 update_idx,
                                                 rowkey_change,
                                                 1,
                                                 &old_row,
                                                 &new_row))) {
    } else if (OB_FAIL(process_new_row(tmp_handle,
                                       run_ctx,
                                       update_idx,
                                       rowkey_change,
                                       old_row,
                                       new_row))) {
    } else if (OB_FAIL(process_lob_after_update(tmp_handle,
                                                run_ctx,
                                                update_idx,
                                                rowkey_change,
                                                1,
                                                &old_row,
                                                &new_row))) {
    }
  }
  if (OB_ITER_END == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObLSTabletService::put_rows(
    ObTabletHandle &tablet_handle,
    ObStoreCtx &ctx,
    const ObDMLBaseParam &dml_param,
    const ObIArray<uint64_t> &column_ids,
    ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  NG_TRACE(S_update_rows_begin);
  const ObTabletID &data_tablet_id = ctx.tablet_id_;
  int64_t afct_num = 0;
  ObTimeGuard timeguard(__func__, 3_s);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!ctx.is_valid())
      || OB_UNLIKELY(!ctx.is_write())
      || OB_UNLIKELY(!dml_param.is_valid())
      || OB_UNLIKELY(column_ids.count() <= 0)
      || OB_ISNULL(row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ctx), K(dml_param), K(column_ids), KP(row_iter));
  } else {
    HEAP_VAR(ObDMLRunningCtx, run_ctx, ctx,
                            dml_param,
                            ctx.mvcc_acc_ctx_.mem_ctx_->get_query_allocator(),
                            ObDmlFlag::DF_UPDATE) {
    ObDatumRow *rows = nullptr;
    int64_t row_count = 0;
    const ObRelativeTable &data_table = run_ctx.relative_table_;

    if (OB_FAIL(prepare_dml_running_ctx(&column_ids, nullptr, tablet_handle, run_ctx))) {
    } else {
      ObTabletHandle tmp_handle;
      HEAP_VAR(ObRowsInfo, rows_info) {
      const ObRelativeTable &data_table = run_ctx.relative_table_;
      const ObColDescIArray &col_descs = *(run_ctx.col_descs_);
        while (OB_SUCC(ret) && OB_SUCC(get_next_rows(row_iter, rows, row_count))) {
          ObStoreRow reserved_row;
          // Let ObStorageTableGuard refresh retired memtable, should not hold origin tablet handle
          // outside the while loop.
          if (tmp_handle.get_obj() != run_ctx.relative_table_.tablet_iter_.get_tablet_handle().get_obj()) {
            tmp_handle = run_ctx.relative_table_.tablet_iter_.get_tablet_handle();
            rows_info.reset();
            if (OB_FAIL(rows_info.init(
                col_descs, data_table, ctx, tmp_handle.get_obj()->get_rowkey_read_info()))) {
            }
          }
          if (OB_FAIL(ret)) {
            // do nothing
          } else if (row_count <= 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("row_count should be greater than 0", K(ret));
          } else {
            for (int64_t i = 0; i < row_count; i++) {
              rows[i].row_flag_.set_flag(ObDmlFlag::DF_UPDATE);
            }
            if (OB_FAIL(rows_info.assign_rows(row_count, rows))) {
            }
          }

          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(put_rows_to_tablet(tmp_handle, run_ctx, rows_info, afct_num))) {
          }
        }  // end of while

        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
      } else {
        LOG_WARN("Failed to allocate ObRowsInfo", K(ret));
      }
    }

    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
    if (OB_SUCC(ret) && !run_ctx.lob_dml_ctx_.is_all_task_done()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("lob data may not be insert", K(ret), K(run_ctx.lob_dml_ctx_));
    }
      }
}

  if (OB_SUCC(ret)) {
    affected_rows = afct_num;
  }
  NG_TRACE(S_update_row_end);

  return ret;
}

int ObLSTabletService::delete_rows(
    ObTabletHandle &tablet_handle,
    ObStoreCtx &ctx,
    const ObDMLBaseParam &dml_param,
    const ObIArray<uint64_t> &column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  NG_TRACE(S_delete_rows_begin);
  const ObTabletID &data_tablet_id = ctx.tablet_id_;
  ObRowReshape *row_reshape = nullptr;
  int64_t afct_num = 0;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_ISNULL(row_iter) || !ctx.is_valid() || !ctx.is_write()
             || column_ids.count() <= 0 || OB_ISNULL(row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(dml_param), K(column_ids),
        KP(row_iter), K(ctx));
  } else {
    HEAP_VAR(ObDMLRunningCtx, run_ctx, ctx,
                            dml_param,
                            ctx.mvcc_acc_ctx_.mem_ctx_->get_query_allocator(),
                            ObDmlFlag::DF_DELETE,
                            true /* is_need_check_old_row_ */) {
    int64_t row_count = 0;
    ObDatumRow *rows = nullptr;
    ObDatumRow *tmp_rows = nullptr;
    char *tmp_rows_buf = nullptr;
    int64_t max_tmp_row_cnt = 0;

    if (OB_FAIL(prepare_dml_running_ctx(&column_ids, nullptr, tablet_handle, run_ctx))) {
    } else {
      tablet_handle.reset();
      HEAP_VAR(ObRowsInfo, rows_info) {
        ObRelativeTable &relative_table = run_ctx.relative_table_;
        const ObColDescIArray &col_descs = *(run_ctx.col_descs_);
         ObIAllocator &work_allocator = run_ctx.allocator_;
        int64_t cur_time = 0;
        ObTabletHandle tmp_handle;
        int64_t max_row_cnt = 0;

        while (OB_SUCC(ret) && OB_SUCC(get_next_rows(row_iter, rows, row_count))) {
          // need to be called just after get_next_row to ensure that previous row's LOB memoroy is valid if get_next_row accesses it
          dml_param.lob_allocator_.reuse();
          // Let ObStorageTableGuard refresh retired memtable, should not hold origin tablet handle
          // outside the while loop.
          if (tmp_handle.get_obj() != relative_table.tablet_iter_.get_tablet_handle().get_obj()) {
            tmp_handle = relative_table.tablet_iter_.get_tablet_handle();
            rows_info.reset();
            if (OB_FAIL(rows_info.init(
                col_descs, relative_table, ctx, tmp_handle.get_obj()->get_rowkey_read_info()))) {
            }
          }
          cur_time = ObClockGenerator::getClock();
          if (OB_FAIL(ret)) {
          } else if (cur_time > run_ctx.dml_param_.timeout_) {
            ret = OB_TIMEOUT;
            LOG_WARN("query timeout", K(cur_time), K(run_ctx.dml_param_), K(ret));
          } else if (row_count <= 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("row_count should be greater than 0", K(ret));
          } else if (row_count == 1) {
            tmp_rows = &run_ctx.datum_row_;
          } else if (tmp_rows_buf == nullptr || row_count > max_tmp_row_cnt) {
            max_tmp_row_cnt = row_count;
            if (tmp_rows_buf != nullptr) {
              work_allocator.free(tmp_rows_buf);
              tmp_rows_buf = nullptr;
            }
            if (OB_ISNULL(tmp_rows_buf = static_cast<char*>(work_allocator.alloc(sizeof(ObDatumRow) * row_count)))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("fail to allocate memory", K(ret), K(row_count));
            } else {
              tmp_rows = new (tmp_rows_buf) ObDatumRow[row_count];
            }
          } else {
            tmp_rows = reinterpret_cast<ObDatumRow*>(tmp_rows_buf);
          }

          if (OB_SUCC(ret)) {
            for (int64_t i = 0; i < row_count; i++) {
              rows[i].row_flag_.set_flag(ObDmlFlag::DF_DELETE);
            }
            if (OB_FAIL(rows_info.assign_rows(row_count, rows))) {
            } else if (OB_FAIL(delete_rows_in_tablet(tmp_handle, run_ctx, tmp_rows, rows_info))) {
            } else {
              afct_num += row_count;
            }
          }
        } // end of while

        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        if (tmp_rows_buf != nullptr) {
          work_allocator.free(tmp_rows_buf);
        }
      }
    }
    if (OB_SUCC(ret)) {
      affected_rows = afct_num;
    }
      }
}
  NG_TRACE(S_delete_rows_end);
  return ret;
}

int ObLSTabletService::lock_rows(
    ObTabletHandle &tablet_handle,
    ObStoreCtx &ctx,
    const ObDMLBaseParam &dml_param,
    const ObLockFlag lock_flag,
    const bool is_sfu,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  UNUSEDx(lock_flag, is_sfu);
  NG_TRACE(S_lock_rows_begin);
  int ret = OB_SUCCESS;
  const ObTabletID &data_tablet_id = ctx.tablet_id_;
  ObTimeGuard timeguard(__func__, 3_s);
  int64_t afct_num = 0;
  ObColDescArray col_desc;
  common::ObSEArray<uint64_t, 1> column_ids;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet service is not initialized", K(ret));
  } else if (OB_UNLIKELY(!ctx.is_valid()
             || !ctx.is_write()
             || !dml_param.is_valid()
             || OB_ISNULL(row_iter))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ctx), K(dml_param), KPC(row_iter));
  } else {
    timeguard.click("Get");
    HEAP_VAR(ObDMLRunningCtx, run_ctx, ctx,
                            dml_param,
                            ctx.mvcc_acc_ctx_.mem_ctx_->get_query_allocator(),
                            ObDmlFlag::DF_LOCK) {
    ObDatumRow *row = nullptr;
    if (OB_FAIL(prepare_dml_running_ctx(nullptr, nullptr, tablet_handle, run_ctx))) {
    } else if (FALSE_IT(tablet_handle.reset())) {
    } else if (FALSE_IT(timeguard.click("Prepare"))) {
    } else if (OB_FAIL(run_ctx.relative_table_.get_rowkey_column_ids(col_desc))) {
    } else if (OB_FAIL(run_ctx.relative_table_.get_rowkey_column_ids(column_ids))) {
    } else {
      timeguard.click("GetIds");
      run_ctx.column_ids_ = &column_ids;
      run_ctx.col_descs_ = &col_desc;
      ObTabletHandle tmp_handle;
      int64_t error_row_idx = 0;
      while (OB_SUCCESS == ret && OB_SUCC(row_iter->get_next_row(row))) {
        // Let ObStorageTableGuard refresh retired memtable, should not hold origin tablet handle
        // outside the while loop.
        if (tmp_handle.get_obj() != run_ctx.relative_table_.tablet_iter_.get_tablet_handle().get_obj()) {
          tmp_handle = run_ctx.relative_table_.tablet_iter_.get_tablet_handle();
        }
        ObRelativeTable &relative_table = run_ctx.relative_table_;
        const ObStorageDatumUtils &datum_utils = dml_param.table_param_->get_data_table().get_read_info().get_datum_utils();
        bool is_exists = true;
        if (ObTimeUtility::current_time() > dml_param.timeout_) {
          ret = OB_TIMEOUT;
          int64_t cur_time = ObClockGenerator::getClock();
          LOG_WARN("query timeout", K(cur_time), K(dml_param), K(ret));
        } else if (GCONF.enable_defensive_check()
            && OB_FAIL(check_old_row_legitimacy_wrap(datum_utils.get_cmp_funcs(), tmp_handle, run_ctx, 1, row, error_row_idx))) {
          LOG_WARN("check row legitimacy failed", K(ret), KPC(row));
        } else if (GCONF.enable_defensive_check()
            && OB_FAIL(check_datum_row_nullable_value(col_desc, relative_table, *row))) {
          LOG_WARN("check lock row nullable failed", K(ret));
        } else if (FALSE_IT(timeguard.click("Check"))) {
        } else if (OB_FAIL(lock_row_wrap(tmp_handle, run_ctx.relative_table_, ctx, col_desc, *row))) {
          if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
            LOG_WARN("failed to lock row", K(*row), K(ret));
          }
        } else {
          ++afct_num;
        }
        timeguard.click("Lock");
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        affected_rows = afct_num;
      }
    }
      }
}
  NG_TRACE(S_lock_rows_end);
  return ret;
}

int ObLSTabletService::lock_row(
    ObTabletHandle &tablet_handle,
    ObStoreCtx &ctx,
    const ObDMLBaseParam &dml_param,
    blocksstable::ObDatumRow &row,
    const ObLockFlag lock_flag,
    const bool is_sfu)
{
  UNUSEDx(lock_flag, is_sfu);
  int ret = OB_SUCCESS;
  const ObTabletID &data_tablet_id = ctx.tablet_id_;
  ObTimeGuard timeguard(__func__, 3_s);
  int64_t afct_num = 0;
  ObColDescArray col_desc;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet service is not initialized", K(ret));
  } else if (OB_UNLIKELY(!ctx.is_valid() || !dml_param.is_valid() || !row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ctx), K(dml_param), K(row));
  } else {
    HEAP_VAR(ObDMLRunningCtx, run_ctx, ctx,
                            dml_param,
                            ctx.mvcc_acc_ctx_.mem_ctx_->get_query_allocator(),
                            ObDmlFlag::DF_LOCK) {
    if (OB_FAIL(prepare_dml_running_ctx(nullptr, nullptr, tablet_handle, run_ctx))) {
    } else if (OB_FAIL(run_ctx.relative_table_.get_rowkey_column_ids(col_desc))) {
    } else {
      if (ObTimeUtility::current_time() > dml_param.timeout_) {
        ret = OB_TIMEOUT;
        int64_t cur_time = ObClockGenerator::getClock();
        LOG_WARN("query timeout", K(cur_time), K(dml_param), K(ret));
      } else if (OB_FAIL(lock_row_wrap(tablet_handle, run_ctx.relative_table_, ctx, col_desc, row))) {
        if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
          LOG_WARN("failed to lock row", K(row), K(ret));
        }
      } else {
        ++afct_num;
      }
    }
      }
}

  return ret;
}

int ObLSTabletService::build_tablet_with_batch_tables(
    const ObTabletID &tablet_id,
  const ObBatchUpdateTableStoreParam &param)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(common::ObMemAttr("BuildBatchTab"));
  ObMetaDiskAddr disk_addr;
  ObTimeGuard time_guard("ObLSTabletService::build_tablet_with_batch_tables", 1_s);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(is_stopped_)) {
    ret = OB_NOT_RUNNING;
    LOG_WARN("tablet service stopped", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || !param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(param));
  } else {
    ObTabletHandle old_tablet_handle;
    ObTabletHandle tmp_tablet_handle;
    ObTabletHandle new_tablet_handle;

    ObBucketHashWLockGuard lock_guard(bucket_lock_, tablet_id.hash());

    time_guard.click("Lock");

    if (OB_FAIL(direct_get_tablet(tablet_id, old_tablet_handle))) {
    } else if (old_tablet_handle.get_obj()->is_empty_shell()) {
      LOG_INFO("old tablet is empty shell tablet, should skip this operation", K(ret), "old_tablet", old_tablet_handle.get_obj());
    } else {
      ObTablet *old_tablet = old_tablet_handle.get_obj();
      ObTablet *tmp_tablet = nullptr;
      const ObTabletMapKey key(tablet_id);
      const ObTabletPersisterParam persist_param(ls_->get_ls_epoch(), tablet_id);

      if (OB_FAIL(ObTabletCreateDeleteHelper::acquire_tmp_tablet(key, allocator, tmp_tablet_handle))) {
      } else if (FALSE_IT(tmp_tablet = tmp_tablet_handle.get_obj())) {
      } else if (OB_FAIL(tmp_tablet->init_for_sstable_replace(allocator, param, *old_tablet))) {
      } else if (FALSE_IT(time_guard.click("InitTablet"))) {
      } else if (OB_FAIL(ObTabletPersister::persist_and_transform_tablet(persist_param, *tmp_tablet, new_tablet_handle))) {
      } else if (FALSE_IT(time_guard.click("Persist"))) {
      } else if (FALSE_IT(disk_addr = new_tablet_handle.get_obj()->tablet_addr_)) {
      } else if (OB_FAIL(safe_update_cas_tablet(key, disk_addr, old_tablet_handle, new_tablet_handle, time_guard))) {
      } else {
        LOG_INFO("succeed to build tablet with batch tables", K(ret), K(key), K(disk_addr), K(param));
      }
    }
  }
  return ret;
}

int ObLSTabletService::safe_update_cas_tablet(
    const ObTabletMapKey &key,
    const ObMetaDiskAddr &addr,
    const ObTabletHandle &old_handle,
    ObTabletHandle &new_handle,
    ObTimeGuard &time_guard)
{
  int ret = OB_SUCCESS;
  ObUpdateTabletPointerParam param;
  ObLocalStorageCheckpointSlogHandler::ObCkptSlogROptLockGuard guard(
      ::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->get_ckpt_slog_hdl());
  if (OB_FAIL(guard.get_ret())) {
  } else if (OB_FAIL(new_handle.get_obj()->get_updating_tablet_pointer_param(param))) {
  } else if (OB_FAIL(LOCAL_STORAGE_META_PERSISTER.update_tablet(key.tablet_id_, addr))) {
  } else if (FALSE_IT(time_guard.click("WrSlog"))) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>()->compare_and_swap_tablet(key, old_handle, new_handle, param))) {
    LOG_ERROR("failed to compare and swap tablet", K(ret), K(key), K(addr), K(param));
    ob_usleep(1_s);
    ob_abort();
  } else {
    time_guard.click("CASwap");
  }
  return ret;
}

int ObLSTabletService::safe_update_cas_empty_shell(
    const ObTabletMapKey &key,
    const ObTabletHandle &old_handle,
    ObTabletHandle &new_handle,
    ObTimeGuard &time_guard)
{
  int ret = OB_SUCCESS;
  ObMetaDiskAddr addr;
  ObUpdateTabletPointerParam param;
  ObTablet *tablet = new_handle.get_obj();
  ObLocalStorageCheckpointSlogHandler::ObCkptSlogROptLockGuard guard(::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->get_ckpt_slog_hdl());
  if (OB_FAIL(guard.get_ret())) {
  } else if (OB_FAIL(new_handle.get_obj()->get_updating_tablet_pointer_param(param))) {
  } else if (OB_FAIL(LOCAL_STORAGE_META_PERSISTER.write_empty_shell_tablet(tablet, addr))) {
  } else if (FALSE_IT(tablet->tablet_addr_ = addr)) {
  } else if (FALSE_IT(param.tablet_addr_ = addr)) {
  } else if (FALSE_IT(time_guard.click("WrSlog"))) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>()->compare_and_swap_tablet(key, old_handle, new_handle, param))) {
    LOG_ERROR("failed to compare and swap tablet", K(ret), K(key), K(old_handle), K(new_handle), K(param));
    ob_usleep(1_s);
    ob_abort();
  } else {
    time_guard.click("CASwap");
  }
  return ret;
}

int ObLSTabletService::safe_create_cas_tablet(
    const ObTabletID &tablet_id,
    const ObMetaDiskAddr &addr,
    ObTabletHandle &tablet_handle,
    ObTimeGuard &time_guard)
{
  int ret = OB_SUCCESS;
  ObUpdateTabletPointerParam param;
  ObLocalStorageCheckpointSlogHandler::ObCkptSlogROptLockGuard guard(
      ::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->get_ckpt_slog_hdl());
  if (OB_FAIL(guard.get_ret())) {
  } else if (OB_FAIL(tablet_handle.get_obj()->get_updating_tablet_pointer_param(param))) {
  } else if (OB_FAIL(LOCAL_STORAGE_META_PERSISTER.update_tablet(tablet_id, addr))) {
  } else if (FALSE_IT(time_guard.click("WrSlog"))) {
  } else if (OB_FAIL(refresh_tablet_addr(tablet_id, param, tablet_handle))) {
    LOG_ERROR("failed to refresh tablet addr", K(ret), K(tablet_id), K(param), K(lbt()));
    ob_usleep(1_s);
    ob_abort();
  } else {
    time_guard.click("RefreshAddr");
  }
  return ret;
}

int ObLSTabletService::check_old_row_legitimacy(
    const ObStoreCmpFuncs &cmp_funcs,
    ObTabletHandle &data_tablet_handle,
    ObRelativeTable &data_table,
    ObStoreCtx &store_ctx,
    const ObDMLBaseParam &dml_param,
    const ObIArray<uint64_t> *column_ids_ptr,
    const ObColDescIArray *col_descs_ptr,
    const bool is_need_check_old_row,
    const bool is_udf,
    const blocksstable::ObDmlFlag &dml_flag,
    const blocksstable::ObDatumRow &old_row)
{
  int ret = OB_SUCCESS;
  // EN_9 may inject OB_ERR_DEFENSIVE_CHECK for the matching session id.
  const int inject_err = OB_E(EventTable::EN_9, store_ctx.mvcc_acc_ctx_.tx_desc_->get_session_id()) OB_SUCCESS;
  if (OB_ERR_DEFENSIVE_CHECK == inject_err) {
    ret = OB_ERR_DEFENSIVE_CHECK;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(data_table.get_rowkey_column_num() > old_row.count_) || OB_ISNULL(column_ids_ptr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("old row is invalid", K(ret), K(old_row), K(data_table.get_rowkey_column_num()), KP(column_ids_ptr));
  } else if (is_need_check_old_row) {
    ObArenaAllocator scan_allocator((common::ObMemAttr(ObModIds::OB_TABLE_SCAN_ITER)));
    ObRowGetter storage_row_getter(scan_allocator, *data_tablet_handle.get_obj());
    ObDatumRow *storage_old_row = nullptr;
    const ObIArray<uint64_t> &column_ids = *column_ids_ptr;
    const ObColDescIArray &column_descs =  *col_descs_ptr;
    uint64_t err_col_id = OB_INVALID_ID;
    if (OB_FAIL(get_storage_row(old_row, column_ids, column_descs, storage_row_getter,
                                data_table, store_ctx, dml_param, storage_old_row, true))) {
      if (OB_ITER_END == ret) {
        ret = OB_ERR_DEFENSIVE_CHECK;
        FLOG_WARN("old row in storage is not exists", K(ret), K(old_row));
      } else {
        LOG_WARN("get next row from old_row_getter failed", K(ret), K(column_ids), K(old_row));
      }
    } else if (OB_ISNULL(storage_old_row)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, storage old row is NULL", K(ret));
    } else if (storage_old_row->count_ != old_row.count_) {
      ret = OB_ERR_DEFENSIVE_CHECK;
      FLOG_WARN("storage old row is not matched with sql old row", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < old_row.count_; ++i) {
        const ObStorageDatum &storage_val = storage_old_row->storage_datums_[i];
        const ObStorageDatum &sql_val = old_row.storage_datums_[i];
        const ObObjMeta &sql_meta = column_descs.at(i).col_type_;
        int cmp_ret = 0;
        if (sql_meta.is_lob_storage()) {
          // skip all text and lob
        } else if (OB_UNLIKELY(storage_val.is_nop_value())) {
          bool is_nop = false;
          if (OB_FAIL(data_table.is_nop_default_value(column_ids.at(i), is_nop))) {
          } else if (!is_nop) {
            err_col_id = column_ids.at(i);
            ret = OB_ERR_DEFENSIVE_CHECK;
            err_col_id = column_ids.at(i);
            FLOG_WARN("storage_val is not equal with sql_val, maybe catch a bug", K(ret),
                 K(i), K(column_ids.at(i)), K(storage_val), K(sql_val));
          }
        } else if (sql_val.is_nop_value()) {
          //this column is nop val, means that this column does not be touched by DML
          //just ignore it
        } else if (OB_FAIL(cmp_funcs.at(i).compare(storage_val, sql_val, cmp_ret)) || 0 != cmp_ret) {
          ret = OB_ERR_DEFENSIVE_CHECK;
          err_col_id = column_ids.at(i);
          FLOG_WARN("storage_val is not equal with sql_val, maybe catch a bug", K(ret),
                  K(storage_val), K(sql_val), K(column_ids.at(i)), K(cmp_ret));
        }
      }
    }

    if (OB_ERR_DEFENSIVE_CHECK == ret && dml_param.is_batch_stmt_) {
      // When performing batch deletion, the index table deletion may occur before the main table deletion, so all tables may encounter error 4377 during batch deletion, which could be due to duplicate deletions.
      ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
    }
    if (OB_ERR_DEFENSIVE_CHECK == ret) {
      int tmp_ret = OB_SUCCESS;
      bool is_virtual_gen_col = false;
      if (is_udf) {
        ret = OB_ERR_INDEX_KEY_NOT_FOUND;
        LOG_WARN("index key not found on udf column", K(ret), K(old_row));
      } else if (data_table.is_index_table() && OB_TMP_FAIL(check_is_gencol_check_failed(data_table, err_col_id, is_virtual_gen_col))) {
        //don't change ret if gencol check failed
        LOG_WARN("check is functional index failed", K(ret), K(tmp_ret), K(data_table));
      } else if (is_virtual_gen_col) {
        ret = OB_ERR_GENCOL_LEGIT_CHECK_FAILED;
        LOG_WARN("Legitimacy check failed for functional index.", K(ret), K(old_row), KPC(storage_old_row));
      }
      if (OB_ERR_DEFENSIVE_CHECK == ret) {
        ObString func_name = ObString::make_string("check_old_row_legitimacy");
        LOG_USER_ERROR(OB_ERR_DEFENSIVE_CHECK, func_name.length(), func_name.ptr());
        LOG_ERROR_RET(OB_ERR_DEFENSIVE_CHECK,
                      "Fatal Error!!! Catch a defensive error!",
                      K(ret),
                      "column_id", column_ids,
                      KPC(storage_old_row),
                      "sql_old_row", old_row,
                      K(dml_param),
                      K(dml_flag),
                      K(store_ctx),
                      "relative_table", data_table);
        LOG_DBA_ERROR_V2(OB_STORAGE_DEFENSIVE_CHECK_FAIL,
                         OB_ERR_DEFENSIVE_CHECK,
                         "Fatal Error!!! Catch a defensive error!");
        LOG_ERROR("Dump data table info", K(ret), K(data_table));
        store_ctx.force_print_trace_log();
      }
    }
  }

  return ret;
}

int ObLSTabletService::check_is_gencol_check_failed(const ObRelativeTable &data_table, uint64_t error_col_id, bool &is_virtual_gen_col)
{
  int ret = OB_SUCCESS;
  is_virtual_gen_col = false;
  if (data_table.is_index_table()) {
    const ObColumnParam *param = nullptr;
    
    uint64_t index_table_id = data_table.get_table_id();
    const ObTableSchema *index_table_schema = NULL;
    const ObTableSchema *data_table_schema = NULL;
    ObMultiVersionSchemaService *schema_service = ::oceanbase::share::server_service<::oceanbase::share::schema::ObSchemaRuntimeService>()->get_schema_service();
    ObSchemaGetterGuard schema_guard;
    if (OB_ISNULL(schema_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret), KP(schema_service));
    } else if (OB_FAIL(schema_service->get_runtime_schema_guard(schema_guard))) {
    }  else if (OB_FAIL(schema_guard.get_table_schema( index_table_id, index_table_schema))) {
    } else if (OB_ISNULL(index_table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("index table schema is unexpected null", K(ret));
    } else if (OB_FAIL(schema_guard.get_table_schema( index_table_schema->get_data_table_id(), data_table_schema))) {
    } else if (OB_ISNULL(data_table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("data table schema is unexpected null", K(ret));
    } else if (OB_INVALID_ID != error_col_id) {
      //check specified column
      const ObColumnSchemaV2 *column = NULL;
      if (is_shadow_column(error_col_id)) {
        //shadow column does not exists in basic table, do nothing
      } else if (OB_ISNULL(column = data_table_schema->get_column_schema(error_col_id))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret), KP(column));
      } else if (column->is_virtual_generated_column()) {
        is_virtual_gen_col = true;
      }
    } else {
      //check all columns
      for (ObTableSchema::const_column_iterator iter = index_table_schema->column_begin();
          OB_SUCC(ret) && iter != index_table_schema->column_end() && !is_virtual_gen_col; iter++) {
        const ObColumnSchemaV2 *column = *iter;
        //the column id in the data table is the same with that in the index table
        if (OB_ISNULL(column)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null", K(ret), KP(column));
        } else if (is_shadow_column(column->get_column_id())) {
          //shadow column does not exists in basic table, do nothing
        } else if (OB_ISNULL(column = data_table_schema->get_column_schema(column->get_column_id()))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null", K(ret), KP(column));
        } else if (column->is_virtual_generated_column()) {
          is_virtual_gen_col = true;
        }
      }
    }
  }
  return ret;
}

int ObLSTabletService::check_new_row_legitimacy(
    ObDMLRunningCtx &run_ctx,
    const int64_t row_count,
    const ObDatumRow *datum_rows)
{
  int ret = OB_SUCCESS;
  ObRelativeTable &data_table = run_ctx.relative_table_;
  int64_t data_table_cnt = data_table.get_column_count();
  if (OB_ISNULL(run_ctx.column_ids_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("column ids is nullptr", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
    if (OB_FAIL(check_datum_row_nullable_value(*run_ctx.col_descs_, data_table, datum_rows[i]))) {
    } else if (OB_FAIL(check_datum_row_shadow_pk(*run_ctx.column_ids_, data_table, datum_rows[i],
        run_ctx.dml_param_.table_param_->get_data_table().get_read_info().get_datum_utils()))) {
    }
  }
  return ret;
}

int ObLSTabletService::insert_rows_to_tablet(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    ObRowsInfo &rows_info)
{
  int ret = OB_SUCCESS;
  ObStoreCtx &ctx = run_ctx.store_ctx_;
  const ObDMLBaseParam &dml_param = run_ctx.dml_param_;
  ObRelativeTable &data_table = run_ctx.relative_table_;
  const int64_t row_count = rows_info.get_rowkey_cnt();
  if (OB_FAIL(ret)) {
  } else if (ObClockGenerator::getClock() > dml_param.timeout_) {
    ret = OB_TIMEOUT;
    int64_t cur_time = ObClockGenerator::getClock();
    LOG_WARN("query timeout", K(cur_time), K(dml_param), K(ret));
  } else if (OB_FAIL(insert_vector_index_rows(tablet_handle, run_ctx, rows_info.rows_, row_count))) {
  } else if (OB_FAIL(process_lob_before_insert(tablet_handle, run_ctx, rows_info.rows_, row_count))) {
  } else if (OB_FAIL(insert_tablet_rows(tablet_handle, run_ctx, rows_info))) {
  } else if (OB_FAIL(process_lob_after_insert(tablet_handle, run_ctx, rows_info.rows_, row_count))) {
  }
  return ret;
}

int ObLSTabletService::insert_tablet_rows(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    ObRowsInfo &rows_info)
{
  int ret = OB_SUCCESS;
  ObRelativeTable &table = run_ctx.relative_table_;
  const int64_t row_count = rows_info.get_rowkey_cnt();
  const bool check_exists = !table.is_storage_index_table() || table.is_unique_index() ||
      run_ctx.store_ctx_.mvcc_acc_ctx_.write_flag_.is_update_pk_dop();

  // 1. Defensive checking of new rows.
  if (GCONF.enable_defensive_check()) {
    if (OB_FAIL(check_new_row_legitimacy(run_ctx, row_count, rows_info.rows_))) {
    }
  }
  // 2. Insert rows with uniqueness constraint and write conflict checking.
  if (OB_SUCC(ret)) {
    if (OB_FAIL(insert_rows_wrap(tablet_handle,
                                 table,
                                 run_ctx.store_ctx_,
                                 run_ctx.dml_param_,
                                 check_exists,
                                 *run_ctx.col_descs_,
                                 rows_info))) {
      if (OB_ERR_PRIMARY_KEY_DUPLICATE == ret) {
        blocksstable::ObDatumRowkey &duplicate_rowkey = rows_info.get_conflict_rowkey();
        LOG_WARN("Rowkey already exist", K(ret), K(duplicate_rowkey), K(row_count),
            K(rows_info.get_conflict_idx()), "need_find_all_duplicate_key", rows_info.need_find_all_duplicate_key());
#ifndef OB_BUILD_PACKAGE
        if (table.is_fts_index()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("unexpected error, duplicated row", K(ret), K(table));
        }
#endif
      } else if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
        LOG_ERROR("Failed to insert rows to tablet", K(ret), K(rows_info));
      }
    }
  }
  // 3. Log user error message if rowkey is duplicate.
  if (OB_ERR_PRIMARY_KEY_DUPLICATE == ret && !run_ctx.dml_param_.is_ignore_ && !rows_info.need_find_all_duplicate_key()) {
    int tmp_ret = OB_SUCCESS;
    char rowkey_buffer[OB_TMP_BUF_SIZE_256];
    ObString index_name = "PRIMARY";
    if (OB_TMP_FAIL(extract_rowkey(table, rows_info.get_conflict_rowkey(),
            rowkey_buffer, OB_TMP_BUF_SIZE_256, run_ctx.dml_param_.tz_info_))) {
    }
    if (table.is_index_table()) {
      if (OB_TMP_FAIL(table.get_index_name(index_name))) {
      }
    }
    LOG_USER_ERROR(OB_ERR_PRIMARY_KEY_DUPLICATE, rowkey_buffer, index_name.length(), index_name.ptr());
  }
  return ret;
}

int ObLSTabletService::put_rows_to_tablet(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    ObRowsInfo &rows_info,
    int64_t &afct_num)
{
  int ret = OB_SUCCESS;
  ObStoreCtx &ctx = run_ctx.store_ctx_;
  const ObDMLBaseParam &dml_param = run_ctx.dml_param_;
  ObRelativeTable &data_table = run_ctx.relative_table_;
  const int64_t row_count = rows_info.get_rowkey_cnt();

  for (int64_t i = 0; i < run_ctx.col_descs_->count() && OB_SUCC(ret); ++i) {
    const ObColDesc &column = run_ctx.col_descs_->at(i);
    if (column.col_type_.is_lob_storage()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "Lob column uses put_rows interface");
      LOG_WARN("put_rows not support lob", K(ret), K(column));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (ObClockGenerator::getClock() > dml_param.timeout_) {
    ret = OB_TIMEOUT;
    int64_t cur_time = ObClockGenerator::getClock();
    LOG_WARN("query timeout", K(cur_time), K(dml_param), K(ret));
  } else if (OB_FAIL(put_tablet_rows(tablet_handle, run_ctx, rows_info))) {
  } else {
    afct_num = afct_num + row_count;
  }
  return ret;
}

int ObLSTabletService::put_tablet_rows(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    ObRowsInfo &rows_info)
{
  int ret = OB_SUCCESS;
  ObRelativeTable &table = run_ctx.relative_table_;
  const int64_t row_count = rows_info.get_rowkey_cnt();
  // 1. Defensive checking of new rows.
  if (GCONF.enable_defensive_check()) {
    if (OB_FAIL(check_new_row_legitimacy(run_ctx, row_count, rows_info.rows_))) {
    }
  }
  // 2. Insert rows with write conflict checking.
  // Check write conflict in memtable + sstable.
  if (OB_SUCC(ret)) {
    if (OB_FAIL(insert_rows_wrap(tablet_handle,
                                 table,
                                 run_ctx.store_ctx_,
                                 run_ctx.dml_param_,
                                 false /* check_exists */,
                                 *run_ctx.col_descs_,
                                 rows_info))) {
      if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
        LOG_ERROR("Failed to insert rows to tablet", K(ret), K(rows_info));
      }
    }
  }
  return ret;
}

OB_INLINE int ObLSTabletService::check_rowkey_length(const ObDMLRunningCtx &run_ctx, const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  int64_t rowkey_length = 0;
  const int64_t rowkey_column_num = run_ctx.relative_table_.get_rowkey_column_num();
  if (run_ctx.has_lob_rowkey_) {
    for (int64_t i = 0; i < rowkey_column_num; ++i) {
      rowkey_length += datum_row.storage_datums_[i].len_;
    }
    if (rowkey_length > OB_MAX_VARCHAR_LENGTH_KEY) {
      ret = OB_ERR_TOO_LONG_KEY_LENGTH;
      LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_VARCHAR_LENGTH_KEY);
      STORAGE_LOG(WARN, "rowkey is too long", K(ret), K(rowkey_length), K(rowkey_column_num), K(datum_row));
    }
  }
  return ret;
}

int ObLSTabletService::process_lob_before_insert(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    blocksstable::ObDatumRow &datum_row,
    const int16_t row_idx)
{
  int ret = OB_SUCCESS;
  int64_t col_cnt = run_ctx.col_descs_->count();
  ObLobManager *lob_mngr = ::oceanbase::share::server_service<::oceanbase::storage::ObLobManager>();
  const ObTableSchemaParam &table_param = run_ctx.dml_param_.table_param_->get_data_table();
  if (OB_ISNULL(lob_mngr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[STORAGE_LOB]failed to get lob manager handle.", K(ret));
  } else if (datum_row.count_ != col_cnt) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[STORAGE_LOB]column count invalid", K(ret), K(col_cnt), K(datum_row.count_), KPC(run_ctx.col_descs_));
  } else if (table_param.is_vector_index_snapshot()) {
    // dml insert to 5 table skip insert lob locator;
  } else {
    const int64_t cur_time = ObClockGenerator::getClock();
    const int64_t relative_timeout = run_ctx.dml_param_.timeout_ - cur_time;
    if (OB_UNLIKELY(relative_timeout <= 0)) {
      ret = OB_TIMEOUT;
      LOG_WARN("timeout has reached", K(ret), "timeout", run_ctx.dml_param_.timeout_, K(cur_time));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < col_cnt; ++i) {
      const ObColDesc &column = run_ctx.col_descs_->at(i);
      ObStorageDatum &datum = datum_row.storage_datums_[i];
      if (datum.is_null() || datum.is_nop_value()) {
        // do nothing
      } else if (column.col_type_.is_lob_storage()) {
        if (OB_FAIL(ObLobTabletDmlHelper::process_lob_column_before_insert(tablet_handle, run_ctx, datum_row, row_idx, i, datum))) {
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_rowkey_length(run_ctx, datum_row))) {
      LOG_WARN("failed to check rowkey length", K(ret), K(datum_row));
    }
  }
  return ret;
}

int update_lob_meta_table_seq_no(ObDMLRunningCtx &run_ctx, int64_t row_count)
{
  int ret = OB_SUCCESS;
  const ObDMLBaseParam &dml_param = run_ctx.dml_param_;
  const ObTableDMLParam *table_param = dml_param.table_param_;
  if (OB_ISNULL(table_param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table_param is null", K(ret));
  } else if (! table_param->get_data_table().is_lob_meta_table()) {
    // skip if not lob meta table
  } else if (row_count != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob meta table row_count incorrect", K(ret), K(row_count));
  } else if (! dml_param.spec_seq_no_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("spec_seq_no_ is invalid", K(ret), K(row_count), K(dml_param));
  } else if (! run_ctx.store_ctx_.mvcc_acc_ctx_.tx_scn_.is_valid()
      || run_ctx.store_ctx_.mvcc_acc_ctx_.tx_scn_ > dml_param.spec_seq_no_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("seq_no unexpected", K(run_ctx.store_ctx_.mvcc_acc_ctx_.tx_scn_), K(dml_param.spec_seq_no_));
  } else {
    run_ctx.store_ctx_.mvcc_acc_ctx_.tx_scn_ = dml_param.spec_seq_no_;
  }
  return ret;
}

int ObLSTabletService::process_lob_before_insert(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    blocksstable::ObDatumRow *rows,
    int64_t row_count)
{
  int ret = OB_SUCCESS;
  // DEBUG_SYNC(DELAY_INDEX_WRITE);
  ObLobManager *lob_mngr = ::oceanbase::share::server_service<::oceanbase::storage::ObLobManager>();
  if (OB_ISNULL(lob_mngr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[STORAGE_LOB]failed to get lob manager handle.", K(ret));
  } else if (OB_FAIL(update_lob_meta_table_seq_no(run_ctx, row_count))) {
  } else {
    int64_t col_cnt = run_ctx.col_descs_->count();
    for (int64_t k = 0; OB_SUCC(ret) && k < row_count; k++) {
      if (OB_FAIL(process_lob_before_insert(tablet_handle, run_ctx, rows[k], k))) {
      }
    }
  }
  return ret;
}

int ObLSTabletService::insert_vector_index_rows(
      ObTabletHandle &data_tablet,
      ObDMLRunningCtx &run_ctx,
      blocksstable::ObDatumRow *rows,
      int64_t row_count)
{
  int ret = OB_SUCCESS;
  const ObTableSchemaParam &table_param = run_ctx.dml_param_.table_param_->get_data_table();
  if (table_param.is_vector_delta_buffer()) {
    ObString vec_idx_param = run_ctx.dml_param_.table_param_->get_data_table().get_vec_index_param();
    int64_t vec_dim = run_ctx.dml_param_.table_param_->get_data_table().get_vec_dim();
    const uint64_t vec_id_col_id = run_ctx.dml_param_.table_param_->get_data_table().get_vec_id_col_id();
    const uint64_t vec_vector_col_id = run_ctx.dml_param_.table_param_->get_data_table().get_vec_vector_col_id();
    const uint64_t vec_type_col_id = vec_vector_col_id - 1;
    // get vector col idx
    int64_t vec_id_idx = OB_INVALID_INDEX;
    int64_t type_idx = OB_INVALID_INDEX;
    int64_t vector_idx = OB_INVALID_INDEX;
    int64_t extra_info_actual_size = 0;
    // get extra info col idx
    // delta_buffer table columns def is: <vid, type, vector, extra_infos>
    ObIVectorIndexRuntime *vec_index_service = ::oceanbase::share::server_service<::oceanbase::storage::ObIVectorIndexRuntime>();
    ObPluginVectorIndexAdapterGuard adaptor_guard;
    if (OB_FAIL(vec_index_service->acquire_adapter_guard(run_ctx.relative_table_.get_tablet_id(),
                                                        ObIndexType::INDEX_TYPE_VEC_DELTA_BUFFER_LOCAL,
                                                        adaptor_guard,
                                                        &vec_idx_param,
                                                        vec_dim))) {
    } else if (OB_FAIL(adaptor_guard.get_adatper()->get_extra_info_actual_size(extra_info_actual_size))) {
    }
    ObArray<share::ObExtraIdxType> extra_info_id_types;
    for (int64_t i = 0; OB_SUCC(ret) && i < run_ctx.dml_param_.table_param_->get_col_descs().count(); i++) {
      uint64_t col_id = run_ctx.dml_param_.table_param_->get_col_descs().at(i).col_id_;
      if (col_id == vec_id_col_id) {
        vec_id_idx = i;
      } else if (col_id == vec_type_col_id) {
        type_idx = i;
      } else if (col_id == vec_vector_col_id) {
        vector_idx = i;
      } else if (extra_info_actual_size > 0){
        // has extra_info
        ObExtraIdxType extra_idx_type;
        extra_idx_type.idx_ = i;
        extra_idx_type.type_= run_ctx.dml_param_.table_param_->get_col_descs().at(i).col_type_;
        if (OB_FAIL(extra_info_id_types.push_back(extra_idx_type))) {
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(vec_id_idx == OB_INVALID_INDEX || type_idx == OB_INVALID_INDEX || vector_idx == OB_INVALID_INDEX)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get vec index column idxs", K(ret), K(vec_id_col_id), K(vec_type_col_id), K(vec_vector_col_id),
          K(vec_id_idx), K(type_idx), K(vector_idx));
    } else {
      if (OB_FAIL(adaptor_guard.get_adatper()->insert_rows(rows, vec_id_idx, type_idx, vector_idx, extra_info_id_types, row_count))) {
      } else {
        for (int64_t k = 0; OB_SUCC(ret) && k < row_count; k++) {
          // process for each row or call batch
          // set vector null for not to storage
          rows[k].storage_datums_[vector_idx].set_null();
        }
        adaptor_guard.get_adatper()->update_can_skip(NOT_SKIP);
      }
    }
  } else if (table_param.is_hybrid_vector_index_log()) {
    const blocksstable::ObDmlFlag dml_flag = run_ctx.dml_flag_;
    ObString vec_idx_param = run_ctx.dml_param_.table_param_->get_data_table().get_vec_index_param();
    int64_t vec_dim = run_ctx.dml_param_.table_param_->get_data_table().get_vec_dim();
    ObPluginVectorIndexAdapterGuard adaptor_guard;
    ObIVectorIndexRuntime *vec_index_service = ::oceanbase::share::server_service<::oceanbase::storage::ObIVectorIndexRuntime>();
    if (OB_FAIL(vec_index_service->acquire_adapter_guard(run_ctx.relative_table_.get_tablet_id(),
                                                        ObIndexType::INDEX_TYPE_HYBRID_INDEX_LOG_LOCAL,
                                                        adaptor_guard,
                                                        &vec_idx_param,
                                                        vec_dim))) {
    } else {
      if (dml_flag == ObDmlFlag::DF_DELETE) {
        const uint64_t vec_id_col_id = run_ctx.dml_param_.table_param_->get_data_table().get_vec_id_col_id();
        const uint64_t vec_vector_col_id = run_ctx.dml_param_.table_param_->get_data_table().get_vec_vector_col_id();
        // get vector col idx
        int64_t vec_id_idx = OB_INVALID_INDEX;
        int64_t vector_idx = OB_INVALID_INDEX;
        for (int64_t i = 0; OB_SUCC(ret) && i < run_ctx.dml_param_.table_param_->get_col_descs().count(); i++) {
          uint64_t col_id = run_ctx.dml_param_.table_param_->get_col_descs().at(i).col_id_;
          if (col_id == vec_id_col_id) {
            vec_id_idx = i;
          } else if (col_id == vec_vector_col_id) {
            vector_idx = i;
          }
        }
        if (vec_id_idx == OB_INVALID_INDEX || vector_idx == OB_INVALID_INDEX) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("fail to get vec index column idxs", K(ret), K(vec_id_col_id), K(vec_vector_col_id),
              K(vec_id_idx), K(vector_idx));
        } else if (OB_FAIL(adaptor_guard.get_adatper()->handle_insert_incr_table_rows(rows, vec_id_idx, vector_idx, row_count))) {
        }
      }
      if (OB_SUCC(ret)) {
        adaptor_guard.get_adatper()->update_can_skip(NOT_SKIP);
      }
    }
  } else if (table_param.is_hybrid_vector_index_embedded()) {
    ObString vec_idx_param = run_ctx.dml_param_.table_param_->get_data_table().get_vec_index_param();
    int64_t vec_dim = run_ctx.dml_param_.table_param_->get_data_table().get_vec_dim();
    const uint64_t vec_id_col_id = run_ctx.dml_param_.table_param_->get_data_table().get_vec_id_col_id();
    const uint64_t vec_vector_col_id = run_ctx.dml_param_.table_param_->get_data_table().get_embedded_vec_col_id();
    // get vector col idx
    int64_t vec_id_idx = OB_INVALID_INDEX;
    int64_t embedded_vec_idx = OB_INVALID_INDEX;
    int64_t extra_info_actual_size = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < run_ctx.dml_param_.table_param_->get_col_descs().count(); i++) {
      uint64_t col_id = run_ctx.dml_param_.table_param_->get_col_descs().at(i).col_id_;
      if (col_id == vec_id_col_id) {
        vec_id_idx = i;
      } else if (col_id == vec_vector_col_id) {
        embedded_vec_idx = i;
      }
    }
    if (vec_id_idx == OB_INVALID_INDEX || embedded_vec_idx == OB_INVALID_INDEX) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get vec index column idxs", K(ret), K(vec_id_col_id), K(vec_vector_col_id),
          K(vec_id_idx), K(embedded_vec_idx));
    } else {
      // get extra info col idx
      // hybrid vec embedded table columns def is: <vid, embedded_vector>
      ObIVectorIndexRuntime *vec_index_service = ::oceanbase::share::server_service<::oceanbase::storage::ObIVectorIndexRuntime>();
      ObPluginVectorIndexAdapterGuard adaptor_guard;
      if (OB_FAIL(vec_index_service->acquire_adapter_guard(run_ctx.relative_table_.get_tablet_id(),
                                                          ObIndexType::INDEX_TYPE_HYBRID_INDEX_EMBEDDED_LOCAL,
                                                          adaptor_guard,
                                                          &vec_idx_param,
                                                          vec_dim))) {
      } else {
        if (OB_FAIL(adaptor_guard.get_adatper()->get_extra_info_actual_size(extra_info_actual_size))) {
        } else {
          ObArray<share::ObExtraIdxType> extra_info_id_types;
          if (extra_info_actual_size > 0) {
            for (int64_t i = 0; OB_SUCC(ret) && i < run_ctx.dml_param_.table_param_->get_col_descs().count(); i++) {
              uint64_t col_id = run_ctx.dml_param_.table_param_->get_col_descs().at(i).col_id_;
              if (col_id != vec_id_col_id && col_id != vec_vector_col_id) {
                // has extra_info
                ObExtraIdxType extra_idx_type;
                extra_idx_type.idx_ = i;
                extra_idx_type.type_= run_ctx.dml_param_.table_param_->get_col_descs().at(i).col_type_;
                if (OB_FAIL(extra_info_id_types.push_back(extra_idx_type))) {
                }
              }
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(adaptor_guard.get_adatper()->handle_insert_embedded_table_rows(rows, vec_id_idx, embedded_vec_idx, extra_info_id_types, row_count))) {
            }
          }
        }
      }
    }
  } else if (table_param.is_ivf_vector_index()) { // check outrow
    ObLobManager *lob_mngr = ::oceanbase::share::server_service<::oceanbase::storage::ObLobManager>();
    for (int64_t k = 0; OB_SUCC(ret) && k < row_count; k++) {
      blocksstable::ObDatumRow &datum_row = rows[k];
      int64_t col_cnt = run_ctx.col_descs_->count();
      for (int64_t i = 0; OB_SUCC(ret) && i < col_cnt; ++i) {
        const ObColDesc &column = run_ctx.col_descs_->at(i);
        ObStorageDatum &datum = datum_row.storage_datums_[i];
        if (datum.is_null() || datum.is_nop_value()) {
          // do nothing
        } else if (column.col_type_.is_lob_storage()) {
          ObString raw_data = datum.get_string();
          bool has_lob_header = datum.has_lob_header() && raw_data.length() > 0;
          ObLobLocatorV2 src_data_locator(raw_data, has_lob_header);
          int64_t new_byte_len = 0;
          if (OB_FAIL(src_data_locator.get_lob_data_byte_len(new_byte_len))) {
          } else if (new_byte_len > table_param.get_lob_inrow_threshold()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected outrow datum in ivf vector index", K(ret), K(new_byte_len),
                K(table_param.get_lob_inrow_threshold()));
          }
        }
      }
    }
  } else if (OB_UNLIKELY(run_ctx.dml_param_.table_param_->get_data_table().is_vector_index_id())) {
    ObIVectorIndexRuntime *vec_index_service = ::oceanbase::share::server_service<::oceanbase::storage::ObIVectorIndexRuntime>();
    ObPluginVectorIndexAdapterGuard adaptor_guard;
    ObString vec_idx_param = run_ctx.dml_param_.table_param_->get_data_table().get_vec_index_param();
    const int64_t vec_dim = run_ctx.dml_param_.table_param_->get_data_table().get_vec_dim();
    int tmp_ret = vec_index_service->acquire_adapter_guard(run_ctx.relative_table_.get_tablet_id(),
                                                           ObIndexType::INDEX_TYPE_VEC_INDEX_ID_LOCAL,
                                                           adaptor_guard,
                                                           &vec_idx_param,
                                                           vec_dim);
    if (OB_SUCCESS == tmp_ret) {
      const bool is_async_index =
          share::is_vector_index_sync_mode_async(vec_idx_param);
      if (!is_async_index) {
        adaptor_guard.get_adatper()->update_index_id_dml_scn(run_ctx.store_ctx_.mvcc_acc_ctx_.snapshot_.version_);
        adaptor_guard.get_adatper()->update_can_skip(NOT_SKIP);
      }
    } else {
      LOG_WARN("acquire_adapter_guard for index_id table failed, skip adapter update",
               K(tmp_ret), K(run_ctx.relative_table_.get_tablet_id()));
    }
  }
  return ret;
}

int ObLSTabletService::extract_rowkey(
    const ObRelativeTable &table,
    const blocksstable::ObDatumRowkey &rowkey,
    char *buffer,
    const int64_t buffer_len,
    const ObTimeZoneInfo *tz_info)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<share::schema::ObColDesc, common::OB_MAX_ROWKEY_COLUMN_NUMBER> rowkey_cols;
  ObStoreRowkey store_rowkey;
  ObDatumRowkeyHelper rowkey_helper;
  if (OB_FAIL(table.get_rowkey_column_ids(rowkey_cols))) {
  } else if (OB_FAIL(rowkey_helper.convert_store_rowkey(rowkey, rowkey_cols, store_rowkey))) {
  } else {
    ret = extract_rowkey(table, store_rowkey, buffer, buffer_len, tz_info);
  }
  return ret;
}

int ObLSTabletService::extract_rowkey(
    const ObRelativeTable &table,
    const common::ObStoreRowkey &rowkey,
    char *buffer,
    const int64_t buffer_len,
    const ObTimeZoneInfo *tz_info)
{
  int ret = OB_SUCCESS;

  if (!table.is_valid() || !rowkey.is_valid() || OB_ISNULL(buffer) || buffer_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table), K(rowkey), K(buffer), K(buffer_len), K(tz_info));
  } else {
    const int64_t rowkey_size = table.get_rowkey_column_num();
    int64_t pos = 0;
    int64_t valid_rowkey_size = 0;
    uint64_t column_id = OB_INVALID_ID;

    for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_size; i++) {
      if (OB_FAIL(table.get_rowkey_col_id_by_idx(i, column_id))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to get rowkey column description", K(i), K(ret));
      } else if (!is_shadow_column(column_id)) {
        valid_rowkey_size ++;
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < valid_rowkey_size; ++i) {
      const ObObj &obj  = rowkey.get_obj_ptr()[i];
      if (OB_FAIL(obj.print_plain_str_literal(buffer, buffer_len - 1, pos, tz_info))) {
      } else if (i < valid_rowkey_size - 1) {
        if (OB_FAIL(databuff_printf(buffer,  buffer_len - 1, pos, "-"))) {
        }
      }
    }
    if (buffer != nullptr) {
      buffer[pos++] = '\0';
    }
  }

  return ret;
}

int ObLSTabletService::get_next_rows(
    blocksstable::ObDatumRowIterator *row_iter,
    blocksstable::ObDatumRow *&rows,
    int64_t &row_count)
{
  return row_iter->get_next_rows(rows, row_count);
}

int ObLSTabletService::construct_update_idx(
    const int64_t schema_rowkey_cnt,
    const share::schema::ColumnMap *col_map,
    const common::ObIArray<uint64_t> &upd_col_ids,
    UpdateIndexArray &update_idx)
{
  int ret = OB_SUCCESS;
  int err = OB_SUCCESS;

  if (OB_ISNULL(col_map) || upd_col_ids.count() <= 0 || update_idx.count() > 0 || schema_rowkey_cnt <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(col_map), K(upd_col_ids), K(upd_col_ids.count()), K(schema_rowkey_cnt));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < upd_col_ids.count(); ++i) {
      int32_t idx = -1;
      const uint64_t &col_id = upd_col_ids.at(i);
      if (OB_SUCCESS != (err = col_map->get(col_id, idx)) || idx < 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column id doesn't exist", K(ret), K(col_id), K(err));
      } else if (idx < schema_rowkey_cnt) {
        // update_idx should not contain rowkey
      } else if (OB_FAIL(update_idx.push_back(idx))) {
      }
    }
    if (OB_SUCC(ret) && update_idx.count() > 1) {
      lib::ob_sort(update_idx.begin(), update_idx.end());
    }
  }

  return ret;
}

int ObLSTabletService::check_rowkey_change(
    const ObIArray<uint64_t> &update_ids,
    const ObRelativeTable &relative_table,
    bool &rowkey_change)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(update_ids.count() <= 0 || !relative_table.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(update_ids), K(ret));
  } else {
    const int64_t count = update_ids.count();
    bool is_rowkey = false;
    rowkey_change = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < count && !rowkey_change; ++i) {
      if (OB_FAIL(relative_table.is_rowkey_column_id(update_ids.at(i), is_rowkey))) {
      } else {
        rowkey_change = is_rowkey;
      }
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (relative_table.is_unique_index() && !rowkey_change) {
      uint64_t cid = OB_INVALID_ID;
      bool innullable = true;
      for (int64_t j = 0; OB_SUCC(ret) && j < relative_table.get_rowkey_column_num() && !rowkey_change; ++j) {
        if (OB_FAIL(relative_table.get_rowkey_col_id_by_idx(j, cid))) {
        } else if (is_shadow_column(cid)) {
          if (innullable) {
            break; // other_change
          } else {
            cid -= OB_MIN_SHADOW_COLUMN_ID;
            for (int64_t k = 0; OB_SUCC(ret) && k < count; ++k) {
              if (cid == update_ids.at(k)) {
                rowkey_change = true;
                break;
              }
            }
          }
        } else {
          bool is_nullable = false;
          if (OB_FAIL(relative_table.is_column_nullable_for_write(cid, is_nullable))) {
          } else if (is_nullable) {
            innullable = false;
          }
        }
      }
    }

  }

  return ret;
}

int ObLSTabletService::process_lob_before_update(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const ObIArray<int64_t> &update_idx,
    const bool rowkey_change,
    const int64_t row_count,
    blocksstable::ObDatumRow *old_rows,
    blocksstable::ObDatumRow *new_rows)
{
  int ret = OB_SUCCESS;
  const int64_t col_cnt = run_ctx.col_descs_->count();
  const ObTableSchemaParam &table_param = run_ctx.dml_param_.table_param_->get_data_table();

  if (table_param.is_vector_index_snapshot()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected process vec table update in process_lob_before_update", K(ret));
  } else if (OB_FAIL(update_lob_meta_table_seq_no(run_ctx, 1/*row_count*/))) {
  } else {
    const int64_t cur_time = ObClockGenerator::getClock();
    const int64_t relative_timeout = run_ctx.dml_param_.timeout_ - cur_time;
    if (OB_UNLIKELY(relative_timeout <= 0)) {
      ret = OB_TIMEOUT;
      LOG_WARN("timeout has reached", K(ret), "timeout", run_ctx.dml_param_.timeout_, K(cur_time));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < col_cnt; ++i) {
      const ObColDesc &column = run_ctx.col_descs_->at(i);
      if (column.col_type_.is_lob_storage()) {
        bool is_col_update = false;
        const bool is_rowkey_col = i < run_ctx.relative_table_.get_rowkey_column_num();
        for (int64_t j = 0; !is_rowkey_col && !is_col_update && j < update_idx.count(); ++j) {
          if (update_idx.at(j) == i) {
            is_col_update = true;
          }
        }

        for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_count; row_idx++) {
          ObStorageDatum &old_datum = old_rows[row_idx].storage_datums_[i];
          ObStorageDatum &new_datum = new_rows[row_idx].storage_datums_[i];
          if (is_rowkey_col || is_col_update) {
            // get new lob locator
            ObString new_lob_str = (new_datum.is_null() || new_datum.is_nop_value())
                                   ? ObString(0, nullptr) : new_datum.get_string();
            // for not strict sql mode, will insert empty string without lob header
            bool has_lob_header = new_datum.has_lob_header() && new_lob_str.length() > 0;
            ObLobLocatorV2 new_lob(new_lob_str, has_lob_header);
            if (OB_FAIL(ret)) {
            } else if (new_datum.is_null() ||
                       new_datum.is_nop_value() ||
                       new_lob.is_full_temp_lob() ||
                       new_lob.is_persist_lob() ||
                       (new_lob.is_lob_disk_locator() && new_lob.has_inrow_data())) {
              if (OB_FAIL(ObLobTabletDmlHelper::process_lob_column_before_update(
                  run_ctx, old_rows[row_idx], new_rows[row_idx], rowkey_change, row_idx, i, old_datum, new_datum))) {
              }
            } else if (new_lob.is_delta_temp_lob()) {
              if (OB_FAIL(ObLobTabletDmlHelper::process_delta_lob(run_ctx, old_rows[row_idx], i, old_datum, new_lob, new_datum))) {
              }
            } else {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected obj for new lob", K(ret), K(i), K(row_idx), K(new_datum), K(new_lob));
            }
          } else {
            if (old_datum.is_null()) {
              new_datum.set_null();
            } else if (old_datum.is_nop_value()) {
              new_datum.set_nop();
            } else if (new_datum.is_nop_value() || new_datum.is_null()) {
              // do nothing
            } else {
              ObString val_str = old_datum.get_string();
              ObLobCommon *lob_common = reinterpret_cast<ObLobCommon*>(val_str.ptr());
              if (!lob_common->in_row_ && rowkey_change) {
                if (val_str.length() < ObLobManager::LOB_WITH_OUTROW_CTX_SIZE) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("not enough space for lob header", K(ret), K(val_str), K(i));
                } else if (OB_FAIL(ObLobTabletDmlHelper::process_lob_column_before_update(
                      run_ctx, old_rows[row_idx], new_rows[row_idx], rowkey_change, row_idx, i, old_datum, new_datum))) {
                }
              } else {
                new_datum.reuse();
                new_datum.set_string(val_str.ptr(), val_str.length());
                if (old_datum.has_lob_header()) {
                  new_datum.set_has_lob_header();
                }
              }
            }
          }
        } // end of for row
      }
    } // end of for column
    for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_count; row_idx++) {
      if (OB_FAIL(check_rowkey_length(run_ctx, new_rows[row_idx]))) {
      }
    }
  }
  return ret;
}

int ObLSTabletService::update_rows_to_tablet(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const bool rowkey_change,
    const ObIArray<int64_t> &update_idx,
    const bool delay_new,
    const bool lob_update,
    ObDatumRow *tmp_rows,
    ObRowsInfo &old_rows_info,
    ObRowsInfo &new_rows_info,
    ObDatumRowStore &row_store)
{
  int ret = OB_SUCCESS;
  const ObDMLBaseParam &dml_param = run_ctx.dml_param_;
  const ObColDescIArray &col_descs = *run_ctx.col_descs_;
  const int64_t row_count = new_rows_info.get_rowkey_cnt();

  if (OB_UNLIKELY(!old_rows_info.is_valid() || !new_rows_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rows info", K(ret), K(old_rows_info), K(new_rows_info));
  } else if (OB_FAIL(process_old_rows(tablet_handle,
                                      run_ctx,
                                      rowkey_change,
                                      lob_update,
                                      tmp_rows,
                                      old_rows_info))) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
      LOG_WARN("fail to process old rows", K(ret), K(old_rows_info),
        K(col_descs), K(rowkey_change), K(lob_update), K(row_count));
    }
  } else if (OB_FAIL(insert_vector_index_rows(tablet_handle, run_ctx, new_rows_info.rows_, row_count))) {
  } else if (delay_new) {
    if (OB_FAIL(cache_rows_to_row_store(row_count,
                                        old_rows_info.rows_,
                                        new_rows_info.rows_,
                                        row_store))) {
    }
  } else if (OB_FAIL(process_lob_before_update(tablet_handle,
                                               run_ctx,
                                               update_idx,
                                               rowkey_change,
                                               row_count,
                                               old_rows_info.rows_,
                                               new_rows_info.rows_))) {
  } else if (OB_FAIL(process_new_rows(tablet_handle,
                                      run_ctx,
                                      update_idx,
                                      rowkey_change,
                                      old_rows_info,
                                      new_rows_info))) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
      LOG_WARN("fail to process new row", K(ret), K(old_rows_info), K(new_rows_info));
    }
  } else if (OB_FAIL(process_lob_after_update(tablet_handle,
                                              run_ctx,
                                              update_idx,
                                              rowkey_change,
                                              row_count,
                                              old_rows_info.rows_,
                                              new_rows_info.rows_))) {
  }
  return ret;
}

int ObLSTabletService::cache_rows_to_row_store(const int64_t row_count,
                                               ObDatumRow *old_rows,
                                               ObDatumRow *new_rows,
                                               ObDatumRowStore &row_store)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
    if (OB_FAIL(row_store.add_row(new_rows[i]))) {
    } else if (OB_FAIL(row_store.add_row(old_rows[i]))) {
    }
  }
  return ret;
}

int ObLSTabletService::update_row_to_tablet(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const bool rowkey_change,
    const ObIArray<int64_t> &update_idx,
    const bool delay_new,
    const bool lob_update,
    ObDatumRow &old_datum_row,
    ObDatumRow &new_datum_row,
    ObDatumRowStore &row_store)
{
  int ret = OB_SUCCESS;
  const ObDMLBaseParam &dml_param = run_ctx.dml_param_;
  const ObColDescIArray &col_descs = *run_ctx.col_descs_;

  if (OB_UNLIKELY(col_descs.count() != old_datum_row.count_ || col_descs.count() != new_datum_row.count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(col_descs.count()), K(old_datum_row.count_), K(new_datum_row.count_));
  } else if (OB_FAIL(process_old_row(tablet_handle,
                                     run_ctx,
                                     rowkey_change,
                                     lob_update,
                                     old_datum_row))) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
      LOG_WARN("fail to process old row", K(ret), K(*run_ctx.col_descs_), K(old_datum_row), K(rowkey_change));
    }
  } else if (OB_FAIL(insert_vector_index_rows(tablet_handle, run_ctx, &new_datum_row, 1))) {
  } else if (delay_new) {
    if (OB_FAIL(cache_rows_to_row_store(1, &old_datum_row, &new_datum_row, row_store))) {
    }
  } else if (OB_FAIL(process_lob_before_update(tablet_handle,
                                               run_ctx,
                                               update_idx,
                                               rowkey_change,
                                               1,
                                               &old_datum_row,
                                               &new_datum_row))) {
  } else if (OB_FAIL(process_new_row(tablet_handle,
                                     run_ctx,
                                     update_idx,
                                     rowkey_change,
                                     old_datum_row,
                                     new_datum_row))) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
      LOG_WARN("fail to process new row", K(new_datum_row), K(ret));
    }
  } else if (OB_FAIL(process_lob_after_update(tablet_handle,
                                              run_ctx,
                                              update_idx,
                                              rowkey_change,
                                              1,
                                              &old_datum_row,
                                              &new_datum_row))) {
  }

  return ret;
}

int ObLSTabletService::process_old_rows(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const bool rowkey_change,
    const bool lob_update,
    ObDatumRow *tmp_rows,
    ObRowsInfo &old_rows_info)
{
  int ret = OB_SUCCESS;
  ObStoreCtx &store_ctx = run_ctx.store_ctx_;
  ObRelativeTable &relative_table = run_ctx.relative_table_;
  bool is_delete_total_quantity_log = run_ctx.dml_param_.is_total_quantity_log_;
  int64_t error_row_idx = 0;
  ObDatumRow *old_rows = old_rows_info.rows_;
  const int64_t row_count = old_rows_info.get_rowkey_cnt();

  if (OB_UNLIKELY(!relative_table.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid relative tables", K(ret), K(relative_table));
  } else if (OB_UNLIKELY(!store_ctx.is_valid()
      || nullptr == run_ctx.col_descs_
      || run_ctx.col_descs_->count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(store_ctx), KP(run_ctx.col_descs_), K(is_delete_total_quantity_log));
  } else if (OB_FAIL(check_old_row_legitimacy_wrap(run_ctx.cmp_funcs_,
      tablet_handle, run_ctx, row_count, old_rows, error_row_idx))) {
    if (OB_ERR_DEFENSIVE_CHECK == ret) {
      dump_diag_info_for_old_row_loss(run_ctx, old_rows[error_row_idx]);
    }
    LOG_WARN("check old row legitimacy failed", K(error_row_idx), K(old_rows[error_row_idx]));
  } else if (OB_FAIL(process_old_rows_lob_col(tablet_handle, run_ctx, row_count, old_rows))){
  } else {
    ObColDescIArray &col_descs = const_cast<ObColDescIArray&>(*run_ctx.col_descs_);
    const uint64_t &table_id = relative_table.get_table_id();
    if (OB_UNLIKELY(run_ctx.dml_param_.prelock_)) {
      bool locked = false;
      for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
        const ObDatumRowkey &datum_rowkey = old_rows_info.get_rowkey(i);
        if (OB_FAIL(check_row_locked_by_myself_wrap(
            tablet_handle, relative_table, store_ctx, datum_rowkey, locked))) {
        } else if (!locked) {
          ret = OB_ERR_ROW_NOT_LOCKED;
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (rowkey_change) {
      if (OB_ISNULL(tmp_rows)) { // tmp_rows must be not null if rowkey changed
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid tmp rows", K(ret));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
        old_rows[i].row_flag_.set_flag(ObDmlFlag::DF_DELETE);
        if (OB_FAIL(tmp_rows[i].shallow_copy(old_rows[i]))) {
        } else {
          tmp_rows[i].row_flag_.set_flag(ObDmlFlag::DF_UPDATE);
        }
      }
      if (OB_SUCC(ret)) {
        ObSEArray<int64_t, 8> update_idx;
        if (OB_FAIL(update_rows_wrap(tablet_handle,
                                     relative_table,
                                     run_ctx.store_ctx_,
                                     col_descs,
                                     update_idx,
                                     tmp_rows,
                                     old_rows_info))) {
          if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
            LOG_WARN("failed to update rows", K(ret), K(old_rows_info));
          }
        }
      }
    } else if (lob_update) {
      // need to lock main table rows that don't need to be deleted
      for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
        const ObDatumRowkey &datum_rowkey = old_rows_info.get_rowkey(i);
        if (OB_FAIL(lock_row_wrap(tablet_handle, relative_table, store_ctx, datum_rowkey))) {
          if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
            LOG_WARN("lock row failed", K(ret), K(table_id), K(i), K(datum_rowkey));
          }
        }
      }
    }
  }
  return ret;
}

int ObLSTabletService::process_old_row(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const bool rowkey_change,
    const bool lob_update,
    ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  ObStoreCtx &store_ctx = run_ctx.store_ctx_;
  ObRelativeTable &relative_table = run_ctx.relative_table_;
  int64_t error_row_idx = 0;
  bool is_delete_total_quantity_log = run_ctx.dml_param_.is_total_quantity_log_;
  if (OB_UNLIKELY(!relative_table.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid relative tables", K(ret), K(relative_table));
  } else if (OB_UNLIKELY(!store_ctx.is_valid()
      || nullptr == run_ctx.col_descs_
      || run_ctx.col_descs_->count() <= 0
      || !datum_row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(store_ctx), KP(run_ctx.col_descs_), K(datum_row), K(is_delete_total_quantity_log));
  } else if (OB_FAIL(check_old_row_legitimacy_wrap(run_ctx.cmp_funcs_, tablet_handle, run_ctx, 1, &datum_row, error_row_idx))) {
    if (OB_ERR_DEFENSIVE_CHECK == ret) {
      dump_diag_info_for_old_row_loss(run_ctx, datum_row);
    }
    LOG_WARN("check old row legitimacy failed", K(ret), K(datum_row));
  } else if (OB_FAIL(process_old_row_lob_col(tablet_handle, run_ctx, datum_row))){
  } else {
    ObColDescIArray &col_descs = const_cast<ObColDescIArray&>(*run_ctx.col_descs_);
    const uint64_t &table_id = relative_table.get_table_id();
    int64_t rowkey_size = relative_table.get_rowkey_column_num();
    ObDatumRowkey datum_rowkey;
    ObDatumRowkeyHelper rowkey_helper(run_ctx.allocator_);
    if (OB_UNLIKELY(run_ctx.dml_param_.prelock_)) {
      bool locked = false;
      if (OB_FAIL(rowkey_helper.prepare_datum_rowkey(datum_row, rowkey_size, col_descs, datum_rowkey))) {
      } else if (OB_FAIL(check_row_locked_by_myself_wrap(tablet_handle, relative_table, store_ctx, datum_rowkey, locked))) {
      } else if (!locked) {
        ret = OB_ERR_ROW_NOT_LOCKED;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (rowkey_change) {
      ObDatumRow del_row;
      ObDatumRow new_row;

      ObSEArray<int64_t, 8> update_idx;
      if (OB_FAIL(del_row.shallow_copy(datum_row))) {
      } else if (FALSE_IT(del_row.row_flag_.set_flag(ObDmlFlag::DF_UPDATE))) {
      } else if (OB_FAIL(new_row.shallow_copy(datum_row))) {
      } else if (FALSE_IT(new_row.row_flag_.set_flag(ObDmlFlag::DF_DELETE))) {
      } else if (OB_FAIL(update_row_wrap(tablet_handle,
                                         relative_table,
                                         run_ctx.store_ctx_,
                                         col_descs,
                                         update_idx,
                                         del_row,
                                         new_row))) {
        if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
          LOG_WARN("failed to write data tablet row", K(ret), K(del_row), K(new_row));
        }
      }
    } else if (lob_update) {
      // need to lock main table rows that don't need to be deleted
      if (OB_FAIL(rowkey_helper.prepare_datum_rowkey(datum_row, rowkey_size, col_descs, datum_rowkey))) {
      } else if (OB_FAIL(lock_row_wrap(tablet_handle, relative_table, store_ctx, datum_rowkey))) {
        if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
          LOG_WARN("lock row failed", K(ret), K(table_id), K(datum_row), K(rowkey_size), K(datum_rowkey));
        }
      }
    }
  }
  return ret;
}
int ObLSTabletService::process_new_rows(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const common::ObIArray<int64_t> &update_idx,
    const bool rowkey_change,
    ObRowsInfo &old_rows_info,
    ObRowsInfo &new_rows_info)
{
  int ret = OB_SUCCESS;
  const int64_t row_count = new_rows_info.get_rowkey_cnt();

  if (OB_UNLIKELY(update_idx.count() < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(update_idx));
  } else if (GCONF.enable_defensive_check()
      && OB_FAIL(check_new_row_legitimacy(run_ctx, row_count, new_rows_info.rows_))) {
    LOG_WARN("check new row legitimacy failed", K(ret), K(new_rows_info));
  } else {
    const ObColDescIArray &col_descs = *run_ctx.col_descs_;
    ObRelativeTable &relative_table = run_ctx.relative_table_;
    bool is_update_total_quantity_log = run_ctx.dml_param_.is_total_quantity_log_;
    const common::ObTimeZoneInfo *tz_info = run_ctx.dml_param_.tz_info_;
    const int64_t row_count = new_rows_info.get_rowkey_cnt();

    if (!rowkey_change) {
      if (!is_update_total_quantity_log) {
        // For minimal mode, set pk columns of old_row to nop value, because
        // they are already stored in new_row.
        const int64_t rowkey_col_cnt = relative_table.get_rowkey_column_num();
        for (int64_t i = 0; i < row_count; i++) {
          for (int64_t j = 0; j < rowkey_col_cnt; ++j) {
            (old_rows_info.rows_[i].storage_datums_[j]).set_nop();
          }
        }
      }
      if (OB_FAIL(update_rows_wrap(tablet_handle,
                                   relative_table,
                                   run_ctx.store_ctx_,
                                   col_descs,
                                   update_idx,
                                   old_rows_info.rows_,
                                   new_rows_info))) {
        if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
          LOG_WARN("failed to update to row", K(ret), K(new_rows_info));
        }
      }
    } else {
      for (int64_t i = 0; i < row_count; i++) {
        new_rows_info.rows_[i].row_flag_.set_flag(ObDmlFlag::DF_INSERT);
      }
      const bool check_exist = !relative_table.is_storage_index_table() || relative_table.is_unique_index() ||
                               run_ctx.store_ctx_.mvcc_acc_ctx_.write_flag_.is_update_pk_dop() ||
                               run_ctx.store_ctx_.mvcc_acc_ctx_.write_flag_.is_immediate_row_check();

      if (OB_FAIL(insert_rows_wrap(tablet_handle,
                                   relative_table,
                                   run_ctx.store_ctx_,
                                   run_ctx.dml_param_,
                                   check_exist,
                                   col_descs,
                                   new_rows_info))) {
        if (OB_ERR_PRIMARY_KEY_DUPLICATE == ret) {
          int tmp_ret = OB_SUCCESS;
          char rowkey_buffer[OB_TMP_BUF_SIZE_256];
          ObString index_name = "PRIMARY";
          if (OB_TMP_FAIL(extract_rowkey(relative_table, new_rows_info.get_conflict_rowkey(),
               rowkey_buffer, OB_TMP_BUF_SIZE_256, run_ctx.dml_param_.tz_info_))) {
          }
          if (relative_table.is_index_table()) {
            if (OB_TMP_FAIL(relative_table.get_index_name(index_name))) {
            }
          }
          LOG_USER_ERROR(OB_ERR_PRIMARY_KEY_DUPLICATE, rowkey_buffer, index_name.length(), index_name.ptr());
        } else if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
          LOG_WARN("failed to update to row", K(ret), K(new_rows_info));
        }
      }
    }
  }
  return ret;
}

int ObLSTabletService::process_new_row(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const ObIArray<int64_t> &update_idx,
    const bool rowkey_change,
    const ObDatumRow &old_datum_row,
    ObDatumRow &new_datum_row)
{
  int ret = OB_SUCCESS;
  ObStoreCtx &ctx = run_ctx.store_ctx_;
  ObRelativeTable &relative_table = run_ctx.relative_table_;
  const bool is_update_total_quantity_log = run_ctx.dml_param_.is_total_quantity_log_;
  const common::ObTimeZoneInfo *tz_info = run_ctx.dml_param_.tz_info_;
  if (OB_UNLIKELY(!ctx.is_valid()
      || !relative_table.is_valid()
      || nullptr == run_ctx.col_descs_
      || run_ctx.col_descs_->count() <= 0
      || update_idx.count() < 0
      || (is_update_total_quantity_log && !old_datum_row.is_valid())
      || !new_datum_row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ctx),
        KP(run_ctx.col_descs_), K(update_idx), K(old_datum_row), K(new_datum_row),
        K(is_update_total_quantity_log), K(rowkey_change));
  } else {
    const ObColDescIArray &col_descs = *run_ctx.col_descs_;
    new_datum_row.row_flag_.set_flag(rowkey_change ? ObDmlFlag::DF_INSERT : ObDmlFlag::DF_UPDATE);
    if (!rowkey_change) {
      ObDatumRow old_row;
      if (OB_FAIL(old_row.shallow_copy(old_datum_row))) {
      } else {
        old_row.row_flag_.set_flag(ObDmlFlag::DF_UPDATE);
        if (!is_update_total_quantity_log) {
          // For minimal mode, set pk columns of old_row to nop value, because
          // they are already stored in new_row.
          const int64_t rowkey_col_cnt = relative_table.get_rowkey_column_num();
          for (int64_t i = 0; i < rowkey_col_cnt; ++i) {
            (old_row.storage_datums_[i]).set_nop();
          }
        }
        if (OB_FAIL(update_row_wrap(tablet_handle, relative_table,
            ctx, col_descs, update_idx, old_row, new_datum_row))) {
          if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
            LOG_WARN("failed to update to row", K(ret), K(old_row), K(new_datum_row));
          }
        }
      }
    } else {
      const bool check_exist = !relative_table.is_storage_index_table() ||
                               relative_table.is_unique_index() ||
                               ctx.mvcc_acc_ctx_.write_flag_.is_update_pk_dop() ||
                               ctx.mvcc_acc_ctx_.write_flag_.is_immediate_row_check();
      if (OB_FAIL(insert_row_wrap(tablet_handle,
                                  relative_table,
                                  ctx,
                                  check_exist,
                                  col_descs,
                                  new_datum_row))) {
        if (OB_ERR_PRIMARY_KEY_DUPLICATE == ret) {
          char buffer[OB_TMP_BUF_SIZE_256];
          ObDatumRowkey rowkey;
          if (OB_SUCCESS != rowkey.assign(new_datum_row.storage_datums_, relative_table.get_rowkey_column_num())) {
          } else if (OB_SUCCESS != extract_rowkey(relative_table, rowkey, buffer, OB_TMP_BUF_SIZE_256, tz_info)) {
          } else {
            ObString index_name = "PRIMARY";
            if (relative_table.is_index_table()) {
              relative_table.get_index_name(index_name);
            }
            LOG_USER_ERROR(OB_ERR_PRIMARY_KEY_DUPLICATE, buffer, index_name.length(), index_name.ptr());
          }
          LOG_WARN("rowkey already exists", K(ret), K(new_datum_row));
        } else if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
          LOG_WARN("failed to update to row", K(ret), K(new_datum_row));
        }
      }
    }
  }
  return ret;
}

int ObLSTabletService::check_datum_row_nullable_value(const ObIArray<ObColDesc> &col_descs,
                                                    ObRelativeTable &relative_table,
                                                    const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(col_descs.count() > datum_row.get_column_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new row is invalid", K(ret), K(datum_row.get_column_count()), K(col_descs.count()));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < col_descs.count(); ++i) {
    uint64_t column_id = col_descs.at(i).col_id_;
    bool is_nullable = false;
    if (datum_row.storage_datums_[i].is_nop()) {
      //nothing
    } else if (OB_UNLIKELY(is_shadow_column(column_id))) {
      //the shadow pk is generated internally,
      //and the nullable attribute check for it is skipped
    } else if (OB_FAIL(relative_table.is_column_nullable_for_write(column_id, is_nullable))) {
    } else if (datum_row.storage_datums_[i].is_null() && !is_nullable) {
      bool is_hidden = false;
      bool is_gen_col = false;
      bool is_nullable_for_read = false;
      if (OB_FAIL(relative_table.is_column_nullable_for_read(column_id, is_nullable_for_read))) {
      } else if (is_nullable_for_read) {
        //this column is not null novalidate, maybe the null column come from the old data
        //so output trace log and ignore it
      } else if (OB_FAIL(relative_table.is_hidden_column(column_id, is_hidden))) {
      } else if (OB_FAIL(relative_table.is_gen_column(column_id, is_gen_col))) {
      } else if (is_hidden && !is_gen_col) {
        ret = OB_BAD_NULL_ERROR;
        LOG_WARN("Catch a defensive nullable error, "
                 "maybe cause by add column not null default null ONLINE", K(ret),
                 K(column_id), K(col_descs), K(datum_row), K(relative_table));
      } else {
        ret = OB_ERR_DEFENSIVE_CHECK;
        ObString func_name = ObString::make_string("check_datum_row_nullable_value");
        LOG_USER_ERROR(OB_ERR_DEFENSIVE_CHECK, func_name.length(), func_name.ptr());
        LOG_ERROR_RET(OB_ERR_DEFENSIVE_CHECK,
                      "Fatal Error!!! Catch a defensive error!", K(ret),
                      K(column_id), K(col_descs), K(datum_row), K(relative_table));
        LOG_DBA_ERROR_V2(OB_STORAGE_DEFENSIVE_CHECK_FAIL,
                         OB_ERR_DEFENSIVE_CHECK,
                         "Fatal Error!!! Catch a defensive error!");
      }
    } else if (!datum_row.storage_datums_[i].is_null() && col_descs.at(i).col_type_.is_number()) {
      number::ObNumber num(datum_row.storage_datums_[i].get_number());
      if (OB_FAIL(num.sanity_check())) {
      }
      if (OB_SUCCESS != ret) {
        ret = OB_ERR_DEFENSIVE_CHECK;
        ObString func_name = ObString::make_string("check_datum_row_nullable_value");
        LOG_USER_ERROR(OB_ERR_DEFENSIVE_CHECK, func_name.length(), func_name.ptr());
        LOG_ERROR_RET(OB_ERR_DEFENSIVE_CHECK,
                      "Fatal Error!!! Catch a defensive error!", K(ret),
                      K(column_id), K(col_descs), K(datum_row), K(relative_table));
        LOG_DBA_ERROR_V2(OB_STORAGE_DEFENSIVE_CHECK_FAIL,
                         OB_ERR_DEFENSIVE_CHECK,
                         "Fatal Error!!! Catch a defensive error!");
      }
    }
  }
  return ret;
}

int ObLSTabletService::check_datum_row_shadow_pk(
    const ObIArray<uint64_t> &column_ids,
    ObRelativeTable &data_table,
    const blocksstable::ObDatumRow &datum_row,
    const blocksstable::ObStorageDatumUtils &rowkey_datum_utils)
{
  int ret = OB_SUCCESS;
  if (data_table.get_shadow_rowkey_column_num() > 0) {
    //check shadow pk
    int64_t rowkey_cnt = data_table.get_rowkey_column_num();
    int64_t spk_cnt = data_table.get_shadow_rowkey_column_num();
    int64_t index_col_cnt = rowkey_cnt - spk_cnt;
    bool need_spk = false;
    if (OB_UNLIKELY(index_col_cnt <= 0) || OB_UNLIKELY(column_ids.count() < rowkey_cnt)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("index column count is invalid", K(ret),
               K(index_col_cnt), K(rowkey_cnt), K(spk_cnt), K(column_ids.count()));
    } else {
      // mysql compatibility: as long as there is a null column in the unique index key, the shadow column needs to be filled
      bool rowkey_has_null = false;
      for (int64_t i = 0; !rowkey_has_null && i < index_col_cnt; i++) {
        rowkey_has_null = datum_row.storage_datums_[i].is_null();
      }
      need_spk = rowkey_has_null;
    }
    for (int64_t i = index_col_cnt; OB_SUCC(ret) && i < rowkey_cnt; ++i) {
      uint64_t spk_column_id = column_ids.at(i);
      uint64_t real_pk_id = spk_column_id - OB_MIN_SHADOW_COLUMN_ID;
      const ObStorageDatum &spk_value = datum_row.storage_datums_[i];
      int64_t pk_idx = OB_INVALID_INDEX;
      int cmp = 0;
      if (OB_LIKELY(!need_spk)) {
        if (!spk_value.is_null()) {
          ret = OB_ERR_DEFENSIVE_CHECK;
          ObString func_name = ObString::make_string("check_datum_row_shadow_pk");
          LOG_USER_ERROR(OB_ERR_DEFENSIVE_CHECK, func_name.length(), func_name.ptr());
          LOG_ERROR("Fatal Error!!! Catch a defensive error!", K(ret),
                    "column_id", column_ids, K(datum_row), K(data_table),
                    K(spk_value), K(i), K(spk_column_id), K(real_pk_id));
        }
      } else if (OB_UNLIKELY(!has_exist_in_array(column_ids, real_pk_id, &pk_idx))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("real pk column not exists in column_ids", K(ret), K(column_ids), K(real_pk_id));
      } else if (OB_FAIL(rowkey_datum_utils.get_cmp_funcs().at(i).compare(datum_row.storage_datums_[pk_idx], spk_value, cmp)) || 0 != cmp) {
        ret = OB_ERR_DEFENSIVE_CHECK;
        ObString func_name = ObString::make_string("check_datum_row_shadow_pk");
        LOG_USER_ERROR(OB_ERR_DEFENSIVE_CHECK, func_name.length(), func_name.ptr());
        LOG_ERROR_RET(OB_ERR_DEFENSIVE_CHECK,
                      "Fatal Error!!! Catch a defensive error!", K(ret),
                      "column_id", column_ids, K(datum_row), K(data_table),
                      K(spk_value), "pk_value", datum_row.storage_datums_[pk_idx],
                      K(pk_idx), K(i), K(spk_column_id), K(real_pk_id));
        LOG_DBA_ERROR_V2(OB_STORAGE_DEFENSIVE_CHECK_FAIL,
                         OB_ERR_DEFENSIVE_CHECK,
                         "Fatal Error!!! Catch a defensive error!");
      }
    }
  }
  return ret;
}

int ObLSTabletService::check_row_locked_by_myself(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const ObDatumRowkey &rowkey,
    bool &locked)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = tablet_handle.get_obj();

  if (OB_UNLIKELY(nullptr == tablet
      || !relative_table.is_valid()
      || !store_ctx.is_valid()
      || !rowkey.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_handle),
        K(relative_table), K(store_ctx), K(rowkey));
  } else {
    ObStorageTableGuard guard(tablet, store_ctx, true);
    if (OB_FAIL(guard.refresh_and_protect_memtable_for_write(relative_table))) {
    } else if (OB_FAIL(tablet->check_row_locked_by_myself(relative_table, store_ctx, rowkey, locked))) {
    }
  }

  return ret;
}

int ObLSTabletService::process_old_rows_lob_col(
    ObTabletHandle &data_tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const int64_t row_count,
    blocksstable::ObDatumRow *old_rows)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
    if (OB_FAIL(process_old_row_lob_col(data_tablet_handle, run_ctx, old_rows[i]))) {
    }
  }
  return ret;
}

int ObLSTabletService::process_old_row_lob_col(
    ObTabletHandle &data_tablet_handle,
    ObDMLRunningCtx &run_ctx,
    blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  bool has_lob_col = false;
  bool need_reread = is_sys_table(run_ctx.relative_table_.get_table_id());
  int64_t col_cnt = run_ctx.col_descs_->count();
  if (datum_row.count_ != col_cnt) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[STORAGE_LOB]Invliad row col cnt", K(ret), K(col_cnt), K(datum_row));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_cnt; ++i) {
      const ObColDesc &column = run_ctx.col_descs_->at(i);
      if (is_lob_storage(column.col_type_.get_type())) {
        has_lob_col = true;
        ObStorageDatum &datum = datum_row.storage_datums_[i];
        need_reread = need_reread || (!datum.is_null() && !datum.is_nop_value() && !datum.has_lob_header());
        break;
      }
    }
  }
  if (OB_SUCC(ret) && has_lob_col) {
    if (!need_reread) {
      for (int64_t i = 0; OB_SUCC(ret) && i < col_cnt; ++i) {
        const ObColDesc &column = run_ctx.col_descs_->at(i);
        if (is_lob_storage(column.col_type_.get_type())) {
          ObStorageDatum &datum = datum_row.storage_datums_[i];
          bool has_lob_header = datum.has_lob_header();
          if (datum.is_null() || datum.is_nop_value()) {
            // do nothing
          } else if (!has_lob_header) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("lob should have lob locator here.", K(ret), K(i), K(datum));
          } else {
            ObLobLocatorV2 lob(datum.get_string(), has_lob_header);
            ObString disk_loc;
            if (!lob.is_valid()) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("invalid lob locator.", K(ret), K(lob));
            } else if (lob.is_simple()) {
              // do nothing
            } else if (OB_FAIL(lob.get_disk_locator(disk_loc))) {
            } else {
              datum.set_string(disk_loc.ptr(), disk_loc.length());
              if (has_lob_header) {
                datum.set_has_lob_header();
              }
            }
          }
        }
      }
    } else {
      if (OB_FAIL(table_refresh_row_wrap(data_tablet_handle, run_ctx, datum_row))) {
      }
    }
  }
  return ret;
}

int ObLSTabletService::table_refresh_row(
    ObTabletHandle &data_tablet_handle,
    ObRelativeTable &data_table,
    ObStoreCtx &store_ctx,
    const ObDMLBaseParam &dml_param,
    const ObColDescIArray &col_descs,
    ObIAllocator &lob_allocator,
    blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator scan_allocator((common::ObMemAttr(ObModIds::OB_LOB_ACCESS_BUFFER)));
  ObRowGetter storage_row_getter(scan_allocator, *data_tablet_handle.get_obj());

  int64_t col_cnt = col_descs.count();
  ObSEArray<uint64_t, 8> out_col_ids;
  for (int i = 0; OB_SUCC(ret) && i < col_cnt; ++i) {
    if (OB_FAIL(out_col_ids.push_back(col_descs.at(i).col_id_))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else {
    ObDatumRow *new_row = nullptr;
    if (OB_FAIL(get_storage_row(datum_row, out_col_ids, col_descs, storage_row_getter,
                                data_table, store_ctx, dml_param, new_row))) {
      if (ret == OB_ITER_END) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("get next row from single row getter failed", K(ret));
      }
    } else if (OB_ISNULL(new_row)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("get next row from single row null", K(ret));
    } else if (new_row->count_ != datum_row.count_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get row from single row col count not equal.", K(ret), K(datum_row.count_), K(new_row->count_));
    } else {
      // only write cells, not write row
      for (int64_t i = 0; OB_SUCC(ret) && i < new_row->count_; ++i) {
        if (OB_FAIL(datum_row.storage_datums_[i].deep_copy(new_row->storage_datums_[i], lob_allocator))) {
        }
      }
    }
  }
  return ret;
}

int ObLSTabletService::delete_rows_in_tablet(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    ObDatumRow *tmp_rows,
    ObRowsInfo &rows_info)
{
  int ret = OB_SUCCESS;
  const ObDMLBaseParam &dml_param = run_ctx.dml_param_;
  ObStoreCtx &ctx = run_ctx.store_ctx_;
  ObRelativeTable &relative_table = run_ctx.relative_table_;
  const int64_t row_count = rows_info.get_rowkey_cnt();
  ObDatumRow *rows = rows_info.rows_;
  int64_t error_row_idx = 0;

  if (OB_FAIL(check_old_row_legitimacy_wrap(
      run_ctx.cmp_funcs_, tablet_handle, run_ctx, row_count, rows, error_row_idx))) {
    if (OB_ERR_DEFENSIVE_CHECK == ret) {
      dump_diag_info_for_old_row_loss(run_ctx, rows[error_row_idx]);
    }
    LOG_WARN("check old row legitimacy failed", K(rows_info));
  } else if (OB_FAIL(process_old_rows_lob_col(tablet_handle, run_ctx, row_count, rows))){
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
      if (OB_FAIL(delete_lob_tablet_rows(tablet_handle, run_ctx, rows[i]))) {
      }
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
    if (OB_FAIL(tmp_rows[i].shallow_copy(rows[i]))) {
    } else {
      tmp_rows[i].row_flag_.set_flag(ObDmlFlag::DF_UPDATE);
    }
  }
  if (OB_SUCC(ret)) {
    ObSEArray<int64_t, 8> update_idx; // update_idx is a dummy param here
    if (OB_FAIL(update_rows_wrap(tablet_handle,
                                 relative_table,
                                 ctx,
                                 *run_ctx.col_descs_,
                                 update_idx,
                                 tmp_rows,
                                 rows_info))) {
      if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
        LOG_WARN("failed to set row", K(ret), K(*run_ctx.col_descs_), K(rows_info));
      }
    }
  }
  return ret;
}

int ObLSTabletService::delete_lob_tablet_rows(
    ObTabletHandle &data_tablet,
    ObDMLRunningCtx &run_ctx,
    blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  int64_t col_cnt = run_ctx.col_descs_->count();
  const ObTableSchemaParam &table_param = run_ctx.dml_param_.table_param_->get_data_table();
  if (table_param.is_vector_index_snapshot()) {
    LOG_INFO("vector index skip dml delete lob tablet", K(ret));
  } else if (datum_row.count_ != col_cnt) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[STORAGE_LOB]Invliad row col cnt", K(col_cnt), K(datum_row));
  } else if (OB_FAIL(update_lob_meta_table_seq_no(run_ctx, 1/*row_count*/))) {
  } else {
    ObLobCommon *lob_common = nullptr;
    for (int64_t i = 0; OB_SUCC(ret) && i < col_cnt; ++i) {
      const ObColDesc &column = run_ctx.col_descs_->at(i);
      if (column.col_type_.is_lob_storage()) {
        blocksstable::ObStorageDatum &datum = datum_row.storage_datums_[i];
        ObLobAccessParam lob_param;
        if (OB_FAIL(ObLobTabletDmlHelper::delete_lob_col(run_ctx, datum_row, i, datum, lob_common, lob_param))) {
        }
      }
    }
  }
  return ret;
}
// revert end

int ObLSTabletService::prepare_scan_table_param(
    ObTableScanParam &param,
    share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret =  OB_SUCCESS;
  if (NULL == param.table_param_ || OB_INVALID_ID == param.table_param_->get_table_id()) {
    void *buf = NULL;
    ObTableParam *table_param = NULL;
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *table_schema = NULL;
    
    const bool check_formal = param.index_id_ > OB_MAX_CORE_TABLE_ID;
    if (OB_FAIL(schema_service.get_runtime_schema_guard(schema_guard))) {
    } else if (check_formal && OB_FAIL(schema_guard.check_formal_guard())) {
      LOG_WARN("Fail to check formal schema, ", K(param.index_id_), K(ret));
    } else  if (OB_FAIL(schema_guard.get_table_schema(
                param.index_id_, table_schema))) {
    } else if (NULL == table_schema) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("table not exist", K(param.index_id_), K(ret));
    } else {
       if (NULL == (buf = param.allocator_->alloc(sizeof(ObTableParam)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Fail to allocate memory, ", K(ret));
       } else {
         //TODO table param should not generate twice!!!!
         table_param = new (buf) ObTableParam(*param.allocator_);
         table_param->get_enable_lob_locator_v2() = true;
         if (OB_FAIL(table_param->convert(*table_schema, param.column_ids_, param.pd_storage_flag_))) {
         } else {
           param.table_param_ = table_param;
         }
       }
    }
  }
  return ret;
}

void ObLSTabletService::dump_diag_info_for_old_row_loss(
    ObDMLRunningCtx &run_ctx,
    const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  ObStoreCtx &store_ctx = run_ctx.store_ctx_;
  ObRelativeTable &data_table = run_ctx.relative_table_;
  ObColDescIArray &col_descs = const_cast<ObColDescIArray&>(*run_ctx.col_descs_);
  ObArenaAllocator allocator(common::ObMemAttr("DumpDIAGInfo"));
  ObTableAccessParam access_param;
  ObTableAccessContext access_ctx;
  ObSEArray<int32_t, 16> out_col_pros;
  ObDatumRowkey datum_rowkey;
  ObDatumRowkeyHelper rowkey_helper(allocator);
  const int64_t schema_rowkey_cnt = data_table.get_rowkey_column_num();
  ObTableStoreIterator &table_iter = *data_table.tablet_iter_.table_iter();
  ObQueryFlag query_flag(ObQueryFlag::Forward,
      false, /*is daily merge scan*/
      false, /*is read multiple macro block*/
      false, /*sys task scan, read one macro block in single io*/
      false /*is full row scan?*/,
      false,
      false);
  query_flag.read_latest_ = ObQueryFlag::OBSF_MASK_READ_LATEST;
  common::ObVersionRange trans_version_rang;
  trans_version_rang.base_version_ = 0;
  trans_version_rang.multi_version_start_ = 0;
  trans_version_rang.snapshot_version_ = store_ctx.mvcc_acc_ctx_.get_snapshot_version().get_val_for_tx();

  const share::schema::ObTableSchemaParam *schema_param = data_table.get_schema_param();
  const ObITableReadInfo *read_info = &schema_param->get_read_info();
  for (int64_t i = 0; OB_SUCC(ret) && i < read_info->get_request_count(); i++) {
    if (OB_FAIL(out_col_pros.push_back(i))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(rowkey_helper.prepare_datum_rowkey(datum_row, schema_rowkey_cnt, col_descs, datum_rowkey))) {
  } else if (OB_FAIL(access_ctx.init(query_flag, store_ctx, allocator, trans_version_rang))) {
  } else {
    access_param.is_inited_ = true;
    access_param.iter_param_.table_id_ = data_table.get_table_id();
    access_param.iter_param_.tablet_id_ = data_table.tablet_iter_.get_tablet()->get_tablet_meta().tablet_id_;
    if (nullptr != data_table.tablet_iter_.get_tablet()) {
    }
    access_param.iter_param_.read_info_ = read_info;
    access_param.iter_param_.out_cols_project_ = &out_col_pros;
    access_param.iter_param_.set_tablet_handle(data_table.get_tablet_handle());
    access_param.iter_param_.need_trans_info_ = true;

    ObStoreRowIterator *getter = nullptr;
    ObITable *table = nullptr;
    const ObDatumRow *row = nullptr;

    FLOG_INFO("Try to find the specified rowkey within all the sstable", K(datum_row), K(table_iter));
    FLOG_INFO("Prepare the diag env to dump the rows", K(store_ctx), K(datum_rowkey),
        K(access_ctx.trans_version_range_));

    table_iter.resume();
    while (OB_SUCC(ret)) {
      if (OB_FAIL(table_iter.get_next(table))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to get next tables", K(ret));
        }
      } else if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table must not be null", K(ret), K(table_iter));
      } else if (OB_FAIL(table->get(access_param.iter_param_, access_ctx, datum_rowkey, getter))) {
      } else if (OB_FAIL(getter->get_next_row(row))) {
      } else if (row->row_flag_.is_not_exist() || row->row_flag_.is_delete()){
        FLOG_INFO("Cannot found rowkey in the table", KPC(row), KPC(table));
      } else if (table->is_sstable()) {
        FLOG_INFO("Found rowkey in the sstable",
            KPC(row), KPC(reinterpret_cast<ObSSTable*>(table)));
      } else if (table->is_data_memtable()) {
        FLOG_INFO("Found rowkey in the memtable",
            KPC(row), KPC(static_cast<memtable::ObMemtable*>(table)));
      }
      if (OB_SUCC(ret) && table->is_sstable()) {
        FLOG_INFO("Dump rowkey from sstable without row cache", KPC(row), KPC(reinterpret_cast<ObSSTable*>(table)));
        access_ctx.query_flag_.set_not_use_row_cache();
        getter->reuse();
        if (OB_FAIL(getter->init(access_param.iter_param_, access_ctx, table, &datum_rowkey))) {
        } else if (OB_FAIL(getter->get_next_row(row))) {
        } else if (row->row_flag_.is_not_exist() || row->row_flag_.is_delete()){
          FLOG_INFO("Cannot found rowkey in the table without row cache", KPC(row), KPC(table));
        } else {
          FLOG_INFO("Found rowkey in the sstable without row cache",
              KPC(row), KPC(reinterpret_cast<ObSSTable*>(table)));
        }
        access_ctx.query_flag_.set_use_row_cache();
      }

      // ignore error in the loop
      if (OB_FAIL(ret) && OB_ITER_END != ret) {
        ret = OB_SUCCESS;
      }
      if (OB_NOT_NULL(getter)) {
        getter->~ObStoreRowIterator();
        getter = nullptr;
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }

    if (OB_SUCC(ret)) {
      FLOG_INFO("prepare to use single merge to find row", K(datum_rowkey), K(access_param));
      ObSingleMerge *get_merge = nullptr;
      ObGetTableParam get_table_param;
      ObDatumRow *row = nullptr;
      void *buf = nullptr;
      if (OB_FAIL(get_table_param.tablet_iter_.assign(data_table.tablet_iter_))) {
      } else if (OB_ISNULL(buf = allocator.alloc(sizeof(ObSingleMerge)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Failed to alloc memory for single merge", K(ret));
      } else if (FALSE_IT(get_merge = new(buf)ObSingleMerge())) {
      } else if (OB_FAIL(get_merge->init(access_param, access_ctx, get_table_param))) {
      } else if (OB_FAIL(get_merge->open(datum_rowkey))) {
      } else if (FALSE_IT(get_merge->disable_fill_default())) {
      } else {
        while (OB_SUCC(get_merge->get_next_row(row))) {
          FLOG_INFO("Found one row for the rowkey", KPC(row));
        }
        FLOG_INFO("Finish to find rowkey with single merge", K(ret), K(datum_rowkey));
      }
      if (OB_NOT_NULL(get_merge)) {
        get_merge->~ObSingleMerge();
        get_merge = nullptr;
      }
    }
#ifdef ENABLE_DEBUG_LOG
    // print single row check info
    if (store_ctx.mvcc_acc_ctx_.tx_id_.is_valid()) {
      transaction::ObTransService *trx = ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();
      if (OB_NOT_NULL(trx)
          && NULL != trx->get_defensive_check_mgr()) {
        (void)trx->get_defensive_check_mgr()->dump(store_ctx.mvcc_acc_ctx_.tx_id_);
      }
    }
#endif
  }
}

int ObLSTabletService::prepare_dml_running_ctx(
    const common::ObIArray<uint64_t> *column_ids,
    const common::ObIArray<uint64_t> *upd_col_ids,
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(run_ctx.init(
      column_ids,
      upd_col_ids,
      ::oceanbase::share::server_service<::oceanbase::share::schema::ObSchemaRuntimeService>()->get_schema_service(),
      tablet_handle))) {
  }

  return ret;
}

int ObLSTabletService::get_ls_min_end_scn(
    SCN &min_end_scn_from_latest_tablets, SCN &min_end_scn_from_old_tablets)
{
  int ret = OB_SUCCESS;
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  ObSArray<ObTabletID> tablet_ids;
  GetAllTabletIDOperator op(tablet_ids, true/*except_ls_inner_tablet*/);
  min_end_scn_from_latest_tablets.set_max();
  min_end_scn_from_old_tablets.set_max();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(tablet_id_set_.foreach(op))) {
  } else {
    SCN ls_checkpoint = ls_->get_clog_checkpoint_scn();
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
      ObTabletMapKey key(tablet_ids.at(i));
      SCN min_end_scn_from_latest = SCN::max_scn();
      SCN min_end_scn_from_old = SCN::max_scn();
      if (OB_FAIL(t3m->get_min_end_scn_for_ls(key,
                                              ls_checkpoint,
                                              min_end_scn_from_latest,
                                              min_end_scn_from_old))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("fail to get min end scn", K(ret), K(key));
        } else {
          ret = OB_SUCCESS;
        }
      } else {
        if (min_end_scn_from_latest < min_end_scn_from_latest_tablets) {
          min_end_scn_from_latest_tablets = min_end_scn_from_latest;
        }

        if (min_end_scn_from_old < min_end_scn_from_old_tablets) {
          min_end_scn_from_old_tablets = min_end_scn_from_old;
        }
      }
    }
    // Tx data contains MDS tx_ops, so wait for the LS checkpoint before recycling it.
    if (ls_checkpoint < min_end_scn_from_latest_tablets) {
      min_end_scn_from_latest_tablets = ls_checkpoint;
    }
    LOG_INFO("get ls min end scn finish", K(ls_checkpoint));
  }
  return ret;
}

int ObLSTabletService::get_multi_ranges_cost(
    const common::ObTabletID &tablet_id,
    const int64_t timeout_us,
    const common::ObIArray<common::ObStoreRange> &ranges,
    int64_t &total_size)
{
  int ret = OB_SUCCESS;
  ObTabletTableIterator iter;
  const int64_t max_snapshot_version = INT64_MAX;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(get_read_tables(tablet_id, timeout_us, max_snapshot_version, max_snapshot_version, iter, false/*allow_no_ready_read*/))) {
  } else {
    ObPartitionMultiRangeSpliter spliter;
    if (OB_FAIL(spliter.get_multi_range_size(ranges,
                                             iter.get_tablet()->get_rowkey_read_info(),
                                             *iter.table_iter(),
                                             total_size))) {
    }
  }
  return ret;
}

int ObLSTabletService::split_multi_ranges(
    const common::ObTabletID &tablet_id,
    const int64_t timeout_us,
    const ObIArray<ObStoreRange> &ranges,
    const int64_t expected_task_count,
    common::ObIAllocator &allocator,
    ObArrayArray<ObStoreRange> &multi_range_split_array)
{
  int ret = OB_SUCCESS;

  ObTabletTableIterator iter;
  const int64_t max_snapshot_version = INT64_MAX;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(get_read_tables(tablet_id,
                                     timeout_us,
                                     max_snapshot_version,
                                     max_snapshot_version,
                                     iter,
                                     false /*allow_no_ready_read*/))) {
  } else {
    ObPartitionMultiRangeSpliter spliter;
    if (OB_FAIL(spliter.get_split_multi_ranges(ranges,
                                               expected_task_count,
                                               iter.get_tablet()->get_rowkey_read_info(),
                                               *iter.table_iter(),
                                               allocator,
                                               multi_range_split_array,
                                               /* for compaction */ false))) {
    }
  }

  return ret;
}

int ObLSTabletService::estimate_row_count(
    const ObTableScanParam &param,
    const ObTableScanRange &scan_range,
    const int64_t timeout_us,
    common::ObIArray<ObEstRowCountRecord> &est_records,
    int64_t &logical_row_count,
    int64_t &physical_row_count)
{
  int ret = OB_SUCCESS;
  ObPartitionEst batch_est;
  ObTabletTableIterator tablet_iter;
  common::ObSEArray<ObITable*, 4> tables;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!param.is_estimate_valid() ||
                         !scan_range.is_valid() ||
                         param.frozen_version_ == -1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param), K(scan_range), K(param.frozen_version_));
  } else if (scan_range.is_empty()) {
  } else {
    const int64_t snapshot_version = -1 == param.frozen_version_ ?
        GET_BATCH_ROWS_READ_SNAPSHOT_VERSION : param.frozen_version_;
    if (OB_FAIL(get_read_tables(param.tablet_id_, timeout_us, snapshot_version, snapshot_version, tablet_iter, false/*allow_no_ready_read*/))) {
      if (OB_TABLET_NOT_EXIST != ret) {
        LOG_WARN("failed to get tablet_iter", K(ret), K(snapshot_version), K(param));
      }
    } else {
      int64_t major_version = -1;
      while(OB_SUCC(ret)) {
        ObITable *table = nullptr;
        if (OB_FAIL(tablet_iter.table_iter()->get_next(table))) {
          if (OB_ITER_END != ret) {
            LOG_WARN("failed to get next table", K(ret), K(tablet_iter.table_iter()));
          } else {
            ret = OB_SUCCESS;
            break;
          }
        } else if (OB_ISNULL(table)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table shoud not be null", K(ret), K(tablet_iter.table_iter()));
        } else if (table->is_sstable()) {
          const ObSSTable *sstable = static_cast<const ObSSTable*>(table);
          if (sstable->is_major_sstable()) {
            major_version = sstable->get_data_version();
          } else if (table->get_upper_trans_version() <= major_version) {
            continue;
          }
        }
        if (OB_FAIL(ret)) {
        } else if (table->no_data_to_read()) {
          continue;
        } else if (OB_FAIL(tables.push_back(table))) {
        }
      }
    }
    if (OB_SUCC(ret) && tables.count() > 0) {
      ObTableEstimateBaseInput base_input(param.scan_flag_, param.index_id_, param.tx_id_, tables, tablet_iter.get_tablet_handle());
      if (scan_range.is_get()) {
        if (OB_FAIL(ObTableEstimator::estimate_row_count_for_get(base_input, scan_range.get_rowkeys(), batch_est))) {
        }
      } else if (OB_FAIL(ObTableEstimator::estimate_row_count_for_scan(base_input, scan_range.get_ranges(), batch_est, est_records))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    logical_row_count = batch_est.logical_row_count_;
    physical_row_count = batch_est.physical_row_count_;
  }
  return ret;
}

int ObLSTabletService::inner_estimate_block_count_and_row_count(
    ObTabletTableIterator &tablet_iter,
    int64_t &macro_block_count,
    int64_t &micro_block_count,
    int64_t &sstable_row_count,
    int64_t &memtable_row_count)
{
  int ret = OB_SUCCESS;
  ObITable *table = nullptr;
  ObSSTable *sstable = nullptr;
  macro_block_count = 0;
  micro_block_count = 0;
  sstable_row_count = 0;
  memtable_row_count = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  }

  while (OB_SUCC(ret)) {
    ObSSTableMetaHandle sst_meta_hdl;
    if (OB_FAIL(tablet_iter.table_iter()->get_next(table))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("failed to get next tables", K(ret));
      } else {
        ret = OB_SUCCESS;
        break;
      }
    } else if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null table", K(ret), K(tablet_iter.table_iter()));
    } else if (table->is_data_memtable()) {
      memtable_row_count += static_cast<memtable::ObMemtable *>(table)->get_physical_row_cnt();
    } else if (table->is_sstable()) {
      sstable = static_cast<ObSSTable *>(table);
      if (OB_FAIL(sstable->get_meta(sst_meta_hdl))) {
      } else {
        sstable = static_cast<ObSSTable *>(table);
        macro_block_count += sstable->get_data_macro_block_count();
        micro_block_count += sst_meta_hdl.get_sstable_meta().get_data_micro_block_count();
        sstable_row_count += sst_meta_hdl.get_sstable_meta().get_row_count();
      }
    }
  }
  return ret;
}

int ObLSTabletService::estimate_block_count_and_row_count(
    const common::ObTabletID &tablet_id,
    const int64_t timeout_us,
    int64_t &macro_block_count,
    int64_t &micro_block_count,
    int64_t &sstable_row_count,
    int64_t &memtable_row_count)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTabletTableIterator tablet_iter;
  share::SCN max_readable_scn;
  int64_t snapshot_version_for_tablet = 0;
  int64_t snapshot_version_for_tables = 0;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(OB_TS_MGR.get_gts(max_readable_scn))) {
  } else if (FALSE_IT(snapshot_version_for_tablet = static_cast<int64_t>(max_readable_scn.get_val_for_sql()))) {
  } else if (FALSE_IT(snapshot_version_for_tables = static_cast<int64_t>(max_readable_scn.get_val_for_sql()))) {
  } else if (OB_FAIL(inner_get_read_tables(
          tablet_id,
          timeout_us,
          snapshot_version_for_tablet,
          snapshot_version_for_tables,
          tablet_iter,
          false/*allow_no_ready_read*/,
          ObMDSGetTabletMode::READ_READABLE_COMMITED))) {
  } else if (OB_FAIL(inner_estimate_block_count_and_row_count(
          tablet_iter,
          macro_block_count,
          micro_block_count,
          sstable_row_count,
          memtable_row_count))) {
  }
  return ret;
}

int ObLSTabletService::get_tx_data_memtable_mgr(ObMemtableMgrHandle &mgr_handle)
{
  mgr_handle.reset();
  return mgr_handle.set_memtable_mgr(&tx_data_memtable_mgr_);
}

int ObLSTabletService::get_tx_ctx_memtable_mgr(ObMemtableMgrHandle &mgr_handle)
{
  mgr_handle.reset();
  return mgr_handle.set_memtable_mgr(&tx_ctx_memtable_mgr_);
}

int ObLSTabletService::get_lock_memtable_mgr(ObMemtableMgrHandle &mgr_handle)
{
  mgr_handle.reset();
  return mgr_handle.set_memtable_mgr(&lock_memtable_mgr_);
}

int ObLSTabletService::get_mds_table_mgr(mds::MdsTableMgrHandle &mgr_handle)
{
  mgr_handle.reset();
  return mgr_handle.set_mds_table_mgr(&mds_table_mgr_);
}

int ObLSTabletService::create_ls_inner_tablet(
    const common::ObTabletID &tablet_id,
    const SCN &major_frozen_scn,
    const ObCreateTabletSchema &create_tablet_schema,
    const SCN &create_scn)
{
  int ret = OB_SUCCESS;
  bool b_exist = false;
  ObTabletHandle tablet_handle;
  common::ObTabletID empty_tablet_id;
  ObMetaDiskAddr disk_addr;
  const int64_t snapshot_version = major_frozen_scn.get_val_for_tx();

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!major_frozen_scn.is_valid())
      || OB_UNLIKELY(!create_tablet_schema.is_valid())
      || OB_UNLIKELY(!create_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(major_frozen_scn),
        K(create_tablet_schema), K(create_scn));
  } else if (OB_UNLIKELY(!tablet_id.is_ls_inner_tablet())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet id is not ls inner tablet", K(ret), K(tablet_id));
  } else if (OB_FAIL(has_tablet(tablet_id, b_exist))) {
  } else if (OB_UNLIKELY(b_exist)) {
    ret = OB_TABLET_EXIST;
    LOG_WARN("tablet already exists", K(ret), K(tablet_id));
  } else if (OB_FAIL(create_inner_tablet(tablet_id, tablet_id/*data_tablet_id*/,
        create_scn, snapshot_version, create_tablet_schema, tablet_handle))) {
  }

  return ret;
}

int ObLSTabletService::remove_ls_inner_tablet(
    const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<common::ObTabletID, 1> tablet_id_array;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id));
  } else if (OB_FAIL(do_remove_tablet(tablet_id))) {
  }

  return ret;
}

int ObLSTabletService::build_tablet_iter(ObLSTabletAddrIterator &iter)
{
  int ret = common::OB_SUCCESS;
  GetAllTabletIDOperator op(iter.tablet_ids_, false /*except_ls_inner_tablet*/);
  iter.ls_tablet_service_ = this;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(tablet_id_set_.foreach(op))) {
  } else if (OB_UNLIKELY(!iter.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is invalid", K(ret), K(iter));
  }
  if (OB_FAIL(ret)) {
    iter.reset();
  }
  return ret;
}

int ObLSTabletService::build_tablet_iter(ObLSTabletIterator &iter, const bool except_ls_inner_tablet)
{
  int ret = common::OB_SUCCESS;
  GetAllTabletIDOperator op(iter.tablet_ids_, except_ls_inner_tablet);
  iter.ls_tablet_service_ = this;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret), K_(is_inited));
  } else if (OB_FAIL(tablet_id_set_.foreach(op))) {
  } else if (OB_UNLIKELY(!iter.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is invalid", K(ret), K(iter));
  }
  if (OB_FAIL(ret)) {
    iter.reset();
  }
  return ret;
}

int ObLSTabletService::is_tablet_exist(const common::ObTabletID &tablet_id, bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", KR(ret));
  } else if (OB_FAIL(tablet_id_set_.exist(tablet_id))) {
    if (OB_HASH_EXIST == ret) {
      ret = OB_SUCCESS; // ignore ret
      is_exist = true;
    } else if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS; // ignore ret
    } else {
      STORAGE_LOG(WARN, "fail to check is tablet exist", KR(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObLSTabletService::GetAllTabletIDOperator::operator()(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id));
  } else if (except_ls_inner_tablet_ && tablet_id.is_ls_inner_tablet()) {
    // do nothing
  } else if (OB_FAIL(tablet_ids_.push_back(tablet_id))) {
  }
  return ret;
}

int ObLSTabletService::DestroyMemtableAndMemberAndMdsTableOperator::operator()(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  cur_tablet_id_ = tablet_id;
  if (OB_UNLIKELY(!tablet_id.is_valid()) || OB_ISNULL(tablet_svr_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(tablet_svr_));
  } else if (OB_ISNULL(tablet_svr_->ls_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls is null", K(ret));
  } else {
    const ObTabletMapKey key(tablet_id);
    if (OB_FAIL(t3m->release_memtable_and_mds_table_for_ls_offline(key))) {
    }
  }
  return ret;
}

int ObLSTabletService::SetMemtableFrozenOperator::operator()(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObTabletHandle handle;
  cur_tablet_id_ = tablet_id;
  if (OB_UNLIKELY(!tablet_id.is_valid()) || OB_ISNULL(tablet_svr_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(tablet_svr_));
  } else if (OB_FAIL(tablet_svr_->get_tablet(tablet_id,
                                             handle,
                                             ObTabletCommon::DEFAULT_GET_TABLET_NO_WAIT,
                                             ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    if (OB_TABLET_NOT_EXIST == ret) {
      LOG_WARN("failed to get tablet, skip set memtable frozen", K(ret), K(tablet_id));
      ret = OB_SUCCESS;
    } else {
      LOG_ERROR("failed to get tablet", K(ret), K(tablet_id));
    }
  } else if (OB_FAIL(handle.get_obj()->set_frozen_for_all_memtables())) {
  }
  return ret;
}

int ObLSTabletService::get_all_tablet_ids(
    const bool except_ls_inner_tablet,
    common::ObIArray<ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  GetAllTabletIDOperator op(tablet_id_array, except_ls_inner_tablet);
  if (OB_FAIL(tablet_id_set_.foreach(op))) {
  }
  return ret;
}

int ObLSTabletService::flush_mds_table(int64_t recycle_scn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ls tablet service is not init", KR(ret), KPC(this));
  } else if (OB_FAIL(mds_table_mgr_.flush(SCN::max_scn(), true))) {
  }
  LOG_INFO("finish flush mds table", KR(ret), K(recycle_scn));
  return ret;
}

int ObLSTabletService::set_frozen_for_all_memtables()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    SetMemtableFrozenOperator set_mem_frozen_op(this);
    if (OB_FAIL(tablet_id_set_.foreach(set_mem_frozen_op))) {
    }
  }
  return ret;
}

int ObLSTabletService::get_tablet_without_memtables(
    const WashTabletPriority &priority,
    const ObTabletMapKey &key,
    common::ObArenaAllocator &allocator,
    ObTabletHandle &handle)
{
  TIMEGUARD_INIT(GetStaticTablet, 1_s);
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  const bool force_alloc_new = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_ISNULL(t3m)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("storage metadata memory manager should not be null", K(ret), KP(t3m));
  } else if (CLICK_FAIL(t3m->get_tablet_with_allocator(
      priority, key, allocator, handle, force_alloc_new))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TABLET_NOT_EXIST;
    } else {
      LOG_WARN("failed to get tablet with allocator", K(ret), K(priority), K(key));
    }
  } else if (CLICK_FAIL(handle.get_obj()->clear_memtables_on_table_store())) {
    LOG_WARN("failed to clear memtables on table store", K(ret), K(key));
  }
  return ret;
}


int ObLSTabletService::offline_build_tablet_without_memtable_()
{
  int ret = OB_SUCCESS;
  ObArray<ObTabletID> tablet_id_array;
  const bool except_ls_inner_tablet = false;
  const SCN scn(SCN::max_scn());

  if (OB_FAIL(get_all_tablet_ids(except_ls_inner_tablet, tablet_id_array))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_id_array.count(); ++i) {
      const ObTabletID &tablet_id = tablet_id_array.at(i);
      if (OB_FAIL(update_tablet_release_memtable_for_offline(tablet_id, scn))) {
      }
    }
  }
  return ret;
}

int ObLSTabletService::offline_destroy_memtable_and_mds_table_()
{
  int ret = OB_SUCCESS;
  DestroyMemtableAndMemberAndMdsTableOperator clean_mem_op(this);
  if (OB_FAIL(tablet_id_set_.foreach(clean_mem_op))) {
  }
  return ret;
}

int ObLSTabletService::check_tablet_no_active_memtable(const ObIArray<ObTabletID> &tablet_list, bool &has)
{
  int ret = OB_SUCCESS;
  has = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    for (int64_t idx = 0; !has && OB_SUCC(ret) && idx < tablet_list.count(); idx++) {
      ObTabletID tablet_id = tablet_list.at(idx);
      ObTabletHandle handle;
      ObTablet *tablet = NULL;
      ObTableHandleV2 table_handle;
      if (OB_FAIL(direct_get_tablet(tablet_id, handle))) {
      } else if (FALSE_IT(tablet = handle.get_obj())) {
      } else if (OB_FAIL(tablet->get_active_memtable(table_handle))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to get active memtable", K(ret), K(tablet_id));
        }
      } else if (OB_ISNULL(table_handle.get_table())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null table", K(ret), K(tablet_id));
      } else if (table_handle.get_table()->is_active_memtable()) {
        LOG_WARN("tablet has active memtable", K(tablet_id), K(table_handle));
        has = true;
      }
    }
  }
  return ret;
}

int ObLSTabletService::offline_gc_uncommitted_tablets_()
{
  int ret = OB_SUCCESS;
  LOG_INFO("start offline gc uncommitted tablets", K(ret));
  ObTabletIDArray deleted_tablets;
  ObLSTabletIterator tablet_iter(ObMDSGetTabletMode::READ_WITHOUT_CHECK);
  bool tablet_status_is_written = false;
  ObTabletCreateDeleteMdsUserData data;
  bool is_finish = false;
  // get deleted_tablets
  if (OB_FAIL(build_tablet_iter(tablet_iter))) {
  } else {
    ObTabletHandle tablet_handle;
    ObTablet *tablet = NULL;
    mds::MdsWriter writer;// will be removed later
    mds::TwoPhaseCommitState trans_stat;// will be removed later
    share::SCN trans_version;// will be removed later
    while (OB_SUCC(ret)) {
      if (OB_FAIL(tablet_iter.get_next_tablet(tablet_handle))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to get tablet", KR(ret), KPC(this), K(tablet_handle));
        }
      } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid tablet handle", KR(ret), KPC(this), K(tablet_handle));
      } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet is NULL", KR(ret));
      } else if (tablet->is_ls_inner_tablet()) {
        // skip ls inner tablet
      } else if (tablet->is_empty_shell()) {
        // skip empty shell
      } else if (OB_FAIL(tablet->check_tablet_status_written(tablet_status_is_written))) {
      } else if (OB_FAIL(tablet->ObITabletMdsInterface::get_latest(data, writer, trans_stat, trans_version))) {
        if (OB_EMPTY_RESULT == ret) {
          ret = OB_SUCCESS;
          if (tablet_status_is_written) {
            if (OB_FAIL(deleted_tablets.push_back(tablet->get_tablet_id()))) {
            } else {
              LOG_INFO("tablet need be gc", KPC(tablet));
            }
          }
        }
      }
    }
  }

  // gc deleted_tablets
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < deleted_tablets.count(); ++i) {
      const common::ObTabletID &tablet_id = deleted_tablets.at(i);
      if (OB_FAIL(do_remove_tablet(tablet_id))) {
      } else {
        LOG_INFO("gc tablet finish", K(ret), K(tablet_id));
      }
    }
  }
  return ret;
}

int ObLSTabletService::lock_row_wrap(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const ObDatumRowkey &rowkey)
{
  return tablet_handle.get_obj()->lock_row(relative_table, store_ctx, rowkey);
}

int ObLSTabletService::lock_row_wrap(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    ObColDescArray &col_descs,
    blocksstable::ObDatumRow &row)
{
  return tablet_handle.get_obj()->lock_row(relative_table, store_ctx, col_descs, row);
}

int ObLSTabletService::update_row_wrap(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const ObIArray<share::schema::ObColDesc> &col_descs,
    const ObIArray<int64_t> &update_idx,
    const blocksstable::ObDatumRow &old_row,
    blocksstable::ObDatumRow &new_row)
{
  return tablet_handle.get_obj()->update_row(
      relative_table, store_ctx, col_descs, update_idx, old_row, new_row);
}

int ObLSTabletService::insert_rows_wrap(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const ObDMLBaseParam &dml_param,
    const bool check_exist,
    const ObColDescIArray &col_descs,
    ObRowsInfo &rows_info)
{
  int tmp_ret = OB_SUCCESS;
  int ret = tablet_handle.get_obj()->insert_rows(
      relative_table, store_ctx, check_exist, col_descs, rows_info);
  if (rows_info.need_find_all_duplicate_key() && OB_ERR_PRIMARY_KEY_DUPLICATE == ret) {
    if (OB_TMP_FAIL(get_conflict_rows(tablet_handle,
                                      relative_table,
                                      store_ctx,
                                      dml_param,
                                      rows_info))) {
      LOG_WARN("failed to get conflict row(s)", K(ret), K(rows_info));
      ret = tmp_ret;
    }
  }
  return ret;
}

int ObLSTabletService::update_rows_wrap(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const ObColDescIArray &col_descs,
    const ObIArray<int64_t> &update_idx,
    const blocksstable::ObDatumRow *old_rows,
    ObRowsInfo &rows_info)
{
  return tablet_handle.get_obj()->update_rows(
      relative_table, store_ctx, col_descs, update_idx, old_rows, rows_info);
}

int ObLSTabletService::insert_row_wrap(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const bool check_exists,
    const ObIArray<share::schema::ObColDesc> &col_descs,
    ObDatumRow &row)
{
  return tablet_handle.get_obj()->insert_row(
      relative_table, store_ctx, check_exists, col_descs, row);
}

int ObLSTabletService::check_row_locked_by_myself_wrap(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const ObDatumRowkey &rowkey,
    bool &locked)
{
  return check_row_locked_by_myself(
      tablet_handle, relative_table, store_ctx, rowkey, locked);
}

int ObLSTabletService::get_conflict_rows(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const ObDMLBaseParam &dml_param,
    const ObRowsInfo &rows_info)
{
  int ret = OB_SUCCESS;
  const int64_t row_count = rows_info.get_rowkey_cnt();

  if (OB_UNLIKELY(!rows_info.has_set_error() || !rows_info.have_conflict())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("no confict rows in rows_info", K(ret), K(rows_info));
  // just project the dup row from input row for primary table
  } else if (!relative_table.is_storage_index_table()) {
    if (OB_FAIL(get_conflict_rows_by_project(relative_table, rows_info))) {
    }
  } else if (OB_FAIL(get_conflict_rows_by_multi_get(tablet_handle,
                                                    relative_table,
                                                    store_ctx,
                                                    dml_param,
                                                    rows_info))) {
  }
  return ret;
}

int ObLSTabletService::get_conflict_rows_by_project(
    ObRelativeTable &relative_table,
    const ObRowsInfo &rows_info)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<int32_t, 16> projector;
  const share::schema::ObTableSchemaParam *schema_param = relative_table.get_schema_param();
  const int64_t row_count = rows_info.get_rowkey_cnt();
  const common::ObIArray<uint64_t> &out_col_ids = *rows_info.dup_row_column_ids_;
  blocksstable::ObDatumRowIterator *&dup_row_iter = *rows_info.dup_row_iter_;
  ObValueRowIterator *dup_value_iter = nullptr;

  for (int32_t i = 0; OB_SUCC(ret) && i < out_col_ids.count(); ++i) {
    int idx = OB_INVALID_INDEX;
    if (OB_FAIL(schema_param->get_col_map().get(out_col_ids.at(i), idx))) {
    } else if (OB_FAIL(projector.push_back(idx))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (nullptr == dup_row_iter) {
      if (OB_ISNULL(dup_value_iter = ObQueryIteratorFactory::get_insert_dup_iter())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("no memory to alloc ObValueRowIterator", K(ret));
      } else if (OB_FAIL(dup_value_iter->init())) {
      } else {
        dup_row_iter = dup_value_iter;
      }
    } else {
      dup_value_iter = static_cast<ObValueRowIterator*>(dup_row_iter);
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
    if (rows_info.rowkeys_[i].marked_rowkey_.is_row_duplicate()) {
      if (OB_FAIL(dup_value_iter->add_row(rows_info.rows_[rows_info.rowkeys_[i].row_idx_], projector))) {
      }
    }
  }
  return ret;
}

int ObLSTabletService::get_conflict_rows_by_multi_get(
    ObTabletHandle &tablet_handle,
    ObRelativeTable &relative_table,
    ObStoreCtx &store_ctx,
    const ObDMLBaseParam &dml_param,
    const ObRowsInfo &rows_info)
{
  int ret = OB_SUCCESS;
  const int64_t row_count = rows_info.get_rowkey_cnt();
  ObMemAttr mem_attr("GetConflictRow");
  ObSEArray<ObDatumRowkey, 2> rowkeys;
  rowkeys.set_attr(mem_attr);
  ObArenaAllocator get_allocator(mem_attr);

  for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
    const ObDatumRowkey &datum_rowkey = rows_info.get_rowkey(i);
    if (!rows_info.is_row_duplicate(i)) {
    } else if (OB_FAIL(rowkeys.push_back(datum_rowkey))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(rowkeys.count() == 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect rowkwys count", K(ret));
  } else if (rowkeys.count() > 1 && !rows_info.is_sorted()) { // need sort
    ObDatumComparor<ObDatumRowkey> comparor(*rows_info.get_datum_utils(), ret, false/*reverse*/);
    lib::ob_sort(rowkeys.begin(), rowkeys.end(), comparor);
  }

  if (OB_SUCC(ret)) {
    ObTablet *data_tablet = tablet_handle.get_obj();
    ObDatumRow *out_row = nullptr;
    ObValueRowIterator *dup_value_iter = nullptr;
    blocksstable::ObDatumRowIterator *&dup_row_iter = *rows_info.dup_row_iter_;

    if (OB_ISNULL(data_tablet)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet is null", K(ret), K(tablet_handle));
    } else if (nullptr == dup_row_iter) {
      if (OB_ISNULL(dup_value_iter = ObQueryIteratorFactory::get_insert_dup_iter())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("no memory to alloc ObValueRowIterator", K(ret));
      } else if (OB_FAIL(dup_value_iter->init())) {
      } else {
        dup_row_iter = dup_value_iter;
      }
    } else {
      dup_value_iter = static_cast<ObValueRowIterator*>(dup_row_iter);
    }

    if (OB_SUCC(ret)) {
      ObRowGetter row_getter(get_allocator, *data_tablet);
      if (OB_FAIL(init_row_getter(row_getter,
                                  store_ctx,
                                  dml_param,
                                  *rows_info.dup_row_column_ids_,
                                  relative_table,
                                  rowkeys.count() > 1/*is_multi_get*/,
                                  true/*skip_read_lob*/))) {
      } else if (OB_FAIL(row_getter.open(rowkeys, false/*use_fuse_row_cache*/))) {
      }
      while (OB_SUCC(ret)) {
        if (OB_FAIL(row_getter.get_next_row(out_row))) {
          if (OB_ITER_END != ret) {
            LOG_WARN("failed to get single storage row", K(ret));
          }
        } else if (OB_FAIL(dup_value_iter->add_row(*out_row))) {
        }
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      } else {
        if (nullptr != dup_row_iter) {
          ObQueryIteratorFactory::free_insert_dup_iter(dup_row_iter);
          dup_row_iter = nullptr;
        }
      }
    }
  }
  return ret;
}

int ObLSTabletService::init_row_getter(
    ObRowGetter &row_getter,
    ObStoreCtx &store_ctx,
    const ObDMLBaseParam &dml_param,
    const ObIArray<uint64_t> &out_col_ids,
    ObRelativeTable &relative_table,
    const bool is_multi_get,
    const bool skip_read_lob)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(row_getter.init_dml_access_param(relative_table, out_col_ids, skip_read_lob))) {
  } else if (OB_FAIL(row_getter.prepare_cached_iter_node(dml_param, is_multi_get))) {
  } else if (OB_FAIL(row_getter.init_dml_access_ctx(store_ctx, skip_read_lob))) {
  }

  return ret;
}

int ObLSTabletService::table_refresh_row_wrap(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    blocksstable::ObDatumRow &row)
{
  return table_refresh_row(tablet_handle, run_ctx.relative_table_,
      run_ctx.store_ctx_, run_ctx.dml_param_, *run_ctx.col_descs_,
      run_ctx.dml_param_.lob_allocator_, row);
}

int ObLSTabletService::check_old_row_legitimacy_wrap(
    const ObStoreCmpFuncs &cmp_funcs,
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const int64_t row_count,
    const blocksstable::ObDatumRow *old_rows,
    int64_t &error_row_idx)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
    if (OB_FAIL(check_old_row_legitimacy(cmp_funcs, tablet_handle, run_ctx.relative_table_,
        run_ctx.store_ctx_, run_ctx.dml_param_, run_ctx.column_ids_, run_ctx.col_descs_,
        run_ctx.is_need_check_old_row_, run_ctx.is_udf_, run_ctx.dml_flag_, old_rows[i]))) {
      error_row_idx = i;
    }
  }
  return ret;
}

int ObLSTabletService::process_lob_after_insert(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    blocksstable::ObDatumRow *rows,
    int64_t row_count)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < run_ctx.lob_dml_ctx_.task_count(); ++i) {
    ObLobDataInsertTask &task = run_ctx.lob_dml_ctx_.task(i);
    const ObColDesc &column = run_ctx.col_descs_->at(task.col_idx_);
    blocksstable::ObDatumRow &datum_row = rows[task.row_idx_];
    if (task.col_idx_ >= run_ctx.col_descs_->count() || task.row_idx_ >= row_count) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("col idx or row idx is invalid", K(ret), K(task), KPC(run_ctx.col_descs_), K(row_count));
    } else if (OB_FAIL(ObLobTabletDmlHelper::process_lob_column_after_insert(run_ctx, datum_row, task))) {
    }
  }

  if (OB_SUCC(ret)) {
    run_ctx.lob_dml_ctx_.reuse();
  }
  return ret;
}

int ObLSTabletService::process_lob_after_update(
    ObTabletHandle &tablet_handle,
    ObDMLRunningCtx &run_ctx,
    const ObIArray<int64_t> &update_idx,
    const bool rowkey_change,
    const int64_t row_count,
    blocksstable::ObDatumRow *old_datum_rows,
    blocksstable::ObDatumRow *new_datum_rows)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < run_ctx.lob_dml_ctx_.task_count(); ++i) {
    ObLobDataInsertTask &task = run_ctx.lob_dml_ctx_.task(i);
    blocksstable::ObDatumRow &old_datum_row = old_datum_rows[task.row_idx_];
    blocksstable::ObDatumRow &new_datum_row = new_datum_rows[task.row_idx_];
    if (task.col_idx_ >= run_ctx.col_descs_->count() || task.row_idx_ >= row_count) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("col idx or row idx is invalid", K(ret), K(task), KPC(run_ctx.col_descs_));
    } else if (OB_FAIL(ObLobTabletDmlHelper::process_lob_column_after_update(
        run_ctx, old_datum_row, new_datum_row, rowkey_change, task))) {
    }
  }
  if (OB_SUCC(ret)) {
    run_ctx.lob_dml_ctx_.reuse();
  }
  return ret;
}

int ObLSTabletService::scan_block_stat(
    const ObTabletHandle &tablet_handle,
    ObBlockStatScanParam &scan_param,
    ObBlockStatIterator &iter)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid() || !scan_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_handle), K(scan_param));
  } else if (OB_FAIL(prepare_scan_table_param(*scan_param.get_scan_param(), *(::oceanbase::share::server_service<::oceanbase::share::schema::ObSchemaRuntimeService>()->get_schema_service())))) {
  } else if (OB_UNLIKELY(scan_param.get_scan_param()->fb_snapshot_.is_min())) {
    ret = OB_SNAPSHOT_DISCARDED;
  } else if (OB_FAIL(iter.init(tablet_handle, scan_param))) {
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
