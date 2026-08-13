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

#define USING_LOG_PREFIX TABLELOCK

#include "ob_lock_table.h"

#include "storage/ls/ob_ls.h"                  // ObLS
#include "storage/tablelock/ob_table_lock_iterator.h"
#include "storage/tablelock/ob_lock_memtable.h"
#include "storage/tx_storage/ob_memstore_freezer.h"

namespace oceanbase
{
using namespace common;
using namespace storage;
using namespace share;
using namespace memtable;

namespace transaction
{
namespace tablelock
{

void ObLockTable::CheckObjLockTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(lock_table_.check_and_clear_obj_lock(true /* force_compact */))) {
  }
}

int ObLockTable::restore_lock_table_(ObITable &sstable)
{
  LOG_INFO("ObLockTable::restore_lock_table", K(sstable));

  int ret = OB_SUCCESS;
  ObStoreRowIterator *row_iter = nullptr;
  const ObDatumRow *row = nullptr;

  ObArenaAllocator allocator;
  blocksstable::ObDatumRange whole_range;
  whole_range.set_whole_range();

  ObStoreCtx store_ctx;
  ObTableAccessContext access_context;

  common::ObQueryFlag query_flag;
  query_flag.use_row_cache_ = ObQueryFlag::DoNotUseCache;

  common::ObVersionRange trans_version_range;
  trans_version_range.base_version_ = 0;
  trans_version_range.multi_version_start_ = 0;
  trans_version_range.snapshot_version_ = MERGE_READ_SNAPSHOT_VERSION;


  common::ObSEArray<share::schema::ObColDesc, 2> columns;
  ObTableReadInfo read_info;
  share::schema::ObColDesc key;
  key.col_id_ = OB_APP_MIN_COLUMN_ID;
  key.col_type_.set_int();
  key.col_order_ = ObOrderType::ASC;

  share::schema::ObColDesc value;
  value.col_id_ = OB_APP_MIN_COLUMN_ID + 1;
  value.col_type_.set_binary();

  ObTableIterParam iter_param;
  iter_param.table_id_ = ObTabletID::LS_LOCK_TABLET_ID;
  iter_param.tablet_id_ = LS_LOCK_TABLET;

  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;

  if (OB_FAIL(access_context.init(query_flag,
                                  store_ctx,
                                  allocator,
                                  trans_version_range))) {
  } else if (OB_FAIL(columns.push_back(key))) {
  } else if (OB_FAIL(columns.push_back(value))) {
  } else if (OB_FAIL(read_info.init(allocator, LOCKTABLE_SCHEMA_COLUMN_CNT, LOCKTABLE_SCHEMA_ROEKEY_CNT, columns, nullptr/*storage_cols_index*/))) {
  } else if (FALSE_IT(iter_param.read_info_ = &read_info)) {
  } else if (OB_FAIL(sstable.scan(iter_param,
                                    access_context,
                                    whole_range,
                                    row_iter))) {
  } else if (NULL == row_iter) {
    LOG_INFO("NULL == row_ite, do nothing");
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else {
    memtable->set_flushed_scn(sstable.get_end_scn());
    while (OB_SUCC(ret)) {
      if (OB_FAIL(row_iter->get_next_row(row))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to get next row", K(ret));
        }
      } else if (OB_FAIL(recover_(*row))) {
      }
    }

    if (OB_ITER_END == ret) {
      LOG_INFO("reload lock table in memory OK", KR(ret), K(sstable));
      ret = OB_SUCCESS;
    }
  }

  if (OB_NOT_NULL(row_iter)) {
    row_iter->~ObStoreRowIterator();
    row_iter = nullptr;
  }

  return ret;
}

int ObLockTable::recover_(const blocksstable::ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t idx = row.storage_datums_[TABLE_LOCK_KEY_COLUMN].get_int();
  ObString obj_str = row.storage_datums_[TABLE_LOCK_KEY_COLUMN + 1].get_string();
  ObTableLockOp store_info;
  const int64_t curr_timestamp = ObTimeUtility::current_time();

  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable not inited", K(ret));
  } else if (OB_FAIL(store_info.deserialize(obj_str.ptr(), obj_str.length(), pos))) {
  } else if (FALSE_IT(store_info.create_timestamp_ = OB_MIN(store_info.create_timestamp_,
                                                            curr_timestamp))) {
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else if (OB_FAIL(memtable->recover_obj_lock(store_info))) {
  }
  LOG_INFO("ObLockTable::recover_ finished", K(ret), K(store_info));

  return ret;
}

int ObLockTable::get_table_schema_(
    ObTableSchema &schema)
{
  int ret = OB_SUCCESS;
  uint64_t table_id = ObTabletID::LS_LOCK_TABLET_ID;
  const char *const AUTO_INC_ID = "id";
  const char *const VALUE_NAME = "lock_info";
  const int64_t SCHEMA_VERSION = 1;
  const char *const TABLE_NAME = "lock_table";
  const int64_t MAX_ID_LENGTH = 100; // the real length is no more than 64 + 1
  const int64_t MAX_LOCK_INFO_LENGTH = OB_MAX_USER_ROW_LENGTH - MAX_ID_LENGTH;
  ObObjMeta INC_ID_TYPE;
  INC_ID_TYPE.set_int();
  ObObjMeta DATA_TYPE;
  DATA_TYPE.set_binary();

  ObColumnSchemaV2 id_column;
  
  id_column.set_table_id(table_id);
  id_column.set_column_id(OB_APP_MIN_COLUMN_ID);
  id_column.set_schema_version(SCHEMA_VERSION);
  id_column.set_rowkey_position(1);
  id_column.set_order_in_rowkey(ObOrderType::ASC);
  id_column.set_meta_type(INC_ID_TYPE); // int64_t

  ObColumnSchemaV2 value_column;
  
  value_column.set_table_id(table_id);
  value_column.set_column_id(OB_APP_MIN_COLUMN_ID + 1);
  value_column.set_schema_version(SCHEMA_VERSION);
  value_column.set_data_length(MAX_LOCK_INFO_LENGTH);
  value_column.set_meta_type(DATA_TYPE);

  
  schema.set_database_id(OB_SYS_DATABASE_ID);
  schema.set_table_id(table_id);
  schema.set_schema_version(SCHEMA_VERSION);

  if (OB_FAIL(id_column.set_column_name(AUTO_INC_ID))) {
  } else if (OB_FAIL(value_column.set_column_name(VALUE_NAME))) {
  } else if (OB_FAIL(schema.set_table_name(TABLE_NAME))) {
  } else if (OB_FAIL(schema.add_column(id_column))) {
  } else if (OB_FAIL(schema.add_column(value_column))) {
  } else {
    schema.set_micro_index_clustered(false);
  }
  return ret;
}

int ObLockTable::init(ObLS *parent)
{
  int ret = OB_SUCCESS;
  storage::ObMemtableMgrHandle memtable_mgr_handle;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObLockTable init twice.", K(ret));
  } else if (OB_ISNULL(parent)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(parent));
  } else if (OB_FAIL(parent->get_tablet_svr()->get_lock_memtable_mgr(memtable_mgr_handle))) {
  } else if (OB_ISNULL(lock_mt_mgr_ = static_cast<ObLockMemtableMgr*>(memtable_mgr_handle.get_memtable_mgr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lock memtable mgr pointer", KR(ret), KPC(parent_));
  } else if (OB_FAIL(check_obj_lock_timer_.init("OBJLockCheck", ObMemAttr("OBJLockCheck")))) {
  } else {
    parent_ = parent;
    is_inited_ = true;
  }

  FLOG_INFO("finish init lock table", K(ret), KP(lock_mt_mgr_), KPC(lock_mt_mgr_), KP(parent_), KPC(parent_));
  return ret;
}

int ObLockTable::prepare_for_safe_destroy()
{
  // do nothing
  return OB_SUCCESS;
}

void ObLockTable::destroy()
{
  if (check_obj_lock_timer_.inited()) {
    check_obj_lock_timer_.cancel_task(check_obj_lock_task_);
    check_obj_lock_timer_.wait_task(check_obj_lock_task_);
    check_obj_lock_timer_.destroy();
  }
  parent_ = nullptr;
  lock_mt_mgr_ = nullptr;
  lock_memtable_handle_.reset();
  is_inited_ = false;
}

int ObLockTable::offline()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(parent_)) {
    LOG_INFO("lock table offline");
  }

  // release all lock memtables before clean cache
  if (OB_NOT_NULL(lock_mt_mgr_)) {
    if (OB_FAIL(lock_mt_mgr_->release_memtables())) {
      if (OB_NOT_INIT == ret) {
        ret = OB_SUCCESS;
        LOG_INFO("modify ret code to success because lock memtable mgr is not init and do not need offline.");
      } else {
        LOG_WARN("release all memtable in lock memtable mgr failed", KR(ret), KPC(lock_mt_mgr_));
      }
    }
  }

  // reset lock memtable handle
  TCWLockGuard guard(rw_lock_);
  lock_memtable_handle_.reset();
  return ret;
}

int ObLockTable::online()
{
  int ret = OB_SUCCESS;
  ObTabletHandle handle;
  ObTablet *tablet;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  ObLSTabletService *ls_tablet_svr = nullptr;
  if (OB_NOT_NULL(parent_)) {
    LOG_INFO("online lock table");
  }
  
  CreateMemtableArg arg;
  if (OB_ISNULL(ls_tablet_svr = parent_->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_INFO("get ls tablet svr failed", K(ret));
  } else if (OB_FAIL(ls_tablet_svr->get_tablet(LS_LOCK_TABLET,
                                               handle))) {
  } else if (FALSE_IT(tablet = handle.get_obj())) {
  } else if (OB_FAIL(ls_tablet_svr->create_memtable(LS_LOCK_TABLET, arg))) {
  } else if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper))) {
  } else {
    const ObSSTableArray &sstables = table_store_wrapper.get_member()->get_minor_sstables();
    if (!sstables.empty()) {
      ObStorageMetaHandle loaded_sstable_handle;
      ObSSTable *loaded_sstable = nullptr;
      if (OB_FAIL(ObCacheSSTableHelper::load_sstable_on_demand(
          table_store_wrapper.get_meta_handle(),
          *sstables[0],
          loaded_sstable_handle,
          loaded_sstable))) {
      } else if (OB_FAIL(restore_lock_table_(*loaded_sstable))) {
      }
    }
  }

  return ret;
}

int ObLockTable::create_tablet(const SCN &create_scn)
{
  int ret = OB_SUCCESS;
  
  share::schema::ObTableSchema table_schema;
  ObIMemtableMgr *memtable_mgr = nullptr;
  ObMemtableMgrHandle memtable_mgr_handle;
  ObArenaAllocator arena_allocator;
  ObCreateTabletSchema create_tablet_schema;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable not inited", K(ret));
  } else if (OB_FAIL(get_table_schema_(table_schema))) {
  } else if (OB_FAIL(create_tablet_schema.init(arena_allocator, table_schema,
        false/*skip_column_info*/))) {
  } else if (OB_FAIL(parent_->create_ls_inner_tablet(LS_LOCK_TABLET,
                                                     ObLS::LS_INNER_TABLET_FROZEN_SCN,
                                                     create_tablet_schema,
                                                     create_scn))) {
  } else if (OB_FAIL(parent_->get_tablet_svr()->
                     get_lock_memtable_mgr(memtable_mgr_handle))) {
  } else if (OB_ISNULL(memtable_mgr = memtable_mgr_handle.get_memtable_mgr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get_memtable_mgr from memtable mgr handle failed", K(ret));
  } else {
    // do nothing
  }
  return ret;
}

int ObLockTable::remove_tablet()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    LOG_WARN("lock table does not inited, remove do nothing");
  } else if (OB_FAIL(parent_->remove_ls_inner_tablet(LS_LOCK_TABLET))) {
    LOG_ERROR("failed to remove ls inner tablet", K(ret), K(LS_LOCK_TABLET));
    ob_usleep(1000 * 1000);
    ob_abort();
  }
  return ret;
}

int ObLockTable::get_lock_memtable(ObTableHandleV2 &handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("lock table is not inited", KR(ret));
  } else {
    while (OB_SUCC(ret)) {
      {
        // most case : acquire read lock to copy assigne lock_memtable_handle
        TCRLockGuard guard(rw_lock_);
        if (lock_memtable_handle_.is_valid()) {
          handle = lock_memtable_handle_;
          break;
        }
      }

      {
        // acquire write lock to get active lock memtable from lock memtable mgr
        TCWLockGuard guard(rw_lock_);
        if (OB_ISNULL(lock_mt_mgr_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("lock memtable mgr is unexpected nullptr", KR(ret), K(is_inited_), KPC(parent_), KPC(lock_mt_mgr_));
        } else if (OB_FAIL(lock_mt_mgr_->get_active_memtable(lock_memtable_handle_))) {
        } else {
          // loop and get memtable handle
        }
      }
    }
  }
  return ret;
}

int ObLockTable::check_lock_conflict(
    ObStoreCtx &ctx,
    const ObLockParam &param)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  ObMemtableCtx *mem_ctx = nullptr;
  ObTxIDSet unused_conflict_tx_set;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable not inited", K(ret));
  } else if (OB_UNLIKELY(!ctx.is_write()) ||
             OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ctx), K(param));
  } else if (FALSE_IT(mem_ctx = static_cast<ObMemtableCtx *>(ctx.mvcc_acc_ctx_.mem_ctx_))) {
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else {
    const int64_t lock_timestamp = ObTimeUtility::current_time();
    const bool include_finish_tx = false;
    const bool only_check_dml_lock = true;
    ObTableLockOp lock_op(param.lock_id_,
                          param.lock_mode_,
                          param.owner_id_,
                          ctx.mvcc_acc_ctx_.get_tx_id(),
                          param.op_type_,
                          LOCK_OP_DOING,
                          ctx.mvcc_acc_ctx_.tx_scn_,
                          lock_timestamp,
                          param.schema_version_);
    if (OB_FAIL(memtable->check_lock_conflict(mem_ctx,
                                              lock_op,
                                              unused_conflict_tx_set,
                                              include_finish_tx,
                                              only_check_dml_lock,
                                              param.expired_time_))) {
      if (ret != OB_TRY_LOCK_ROW_CONFLICT) {
        LOG_WARN("lock failed.", K(ret), K(lock_op));
      }
    }
  }
  return ret;
}

int ObLockTable::check_lock_conflict(
    const ObMemtableCtx *mem_ctx,
    const ObTableLockOp &lock_op,
    ObTxIDSet &conflict_tx_set,
    const bool include_finish_tx)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable not inited", K(ret));
  } else if (OB_ISNULL(mem_ctx) ||
             OB_UNLIKELY(!lock_op.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(mem_ctx), K(lock_op));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else if (OB_FAIL(memtable->check_lock_conflict(mem_ctx,
                                                   lock_op,
                                                   conflict_tx_set,
                                                   include_finish_tx))) {
    if (ret != OB_TRY_LOCK_ROW_CONFLICT) {
      LOG_WARN("check_lock_conflict failed.", K(ret), K(lock_op));
    }
  } else {
    // do nothing
  }
  return ret;
}

int ObLockTable::lock(
    ObStoreCtx &ctx,
    const ObLockParam &param)
{
  int ret = OB_SUCCESS;
  ObLockMemtable *memtable = nullptr;
  ObTransID tx_id = ctx.mvcc_acc_ctx_.get_tx_id();
  TCRLockGuard guard(rw_lock_);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable not inited", K(ret));
  } else if (OB_UNLIKELY(!ctx.is_write()) ||
             OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ctx), K(param));
  } else if (OB_UNLIKELY(!tx_id.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid argument", K(ret), K(ctx), K(param), K(ctx.mvcc_acc_ctx_));
    ob_abort();
  } else if (OB_FAIL(ctx.mvcc_acc_ctx_.mem_ctx_->get_lock_mem_ctx().get_lock_memtable(memtable))) {
  } else if (OB_ISNULL(memtable)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("lock memtable is null", K(ret), K(ctx));
  } else {
    const int64_t lock_timestamp = ObTimeUtility::current_time();
    ObTableLockOp lock_op(param.lock_id_,
                          param.lock_mode_,
                          param.owner_id_,
                          tx_id,
                          param.op_type_,
                          LOCK_OP_DOING,
                          ctx.mvcc_acc_ctx_.tx_scn_,
                          lock_timestamp,
                          param.schema_version_);
    if (OB_FAIL(memtable->lock(param,
                               ctx,
                               lock_op))) {
      if (ret != OB_TRY_LOCK_ROW_CONFLICT) {
        LOG_WARN("lock failed.", K(ret), K(lock_op));
      }
    }
  }
  return ret;
}

int ObLockTable::unlock(
    ObStoreCtx &ctx,
    const ObLockParam &param)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable not inited", K(ret));
  } else if (OB_UNLIKELY(!ctx.is_write()) ||
             OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ctx), K(param));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else {
    const bool is_try_lock = param.is_try_lock_;
    const int64_t expired_time = param.expired_time_;
    const int64_t unlock_timestamp = ObTimeUtility::current_time();
    ObTableLockOp unlock_op(param.lock_id_,
                            param.lock_mode_,
                            param.owner_id_,
                            ctx.mvcc_acc_ctx_.get_tx_id(),
                            param.op_type_,
                            LOCK_OP_DOING,
                            ctx.mvcc_acc_ctx_.tx_scn_,
                            unlock_timestamp,
                            param.schema_version_);
    if (OB_FAIL(memtable->unlock(ctx,
                                 unlock_op,
                                 is_try_lock,
                                 expired_time))) {
    }
  }
  return ret;
}

int ObLockTable::replace_lock(
    ObStoreCtx &ctx,
    const ObReplaceLockParam &param)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable not inited", K(ret));
  } else if (OB_UNLIKELY(!ctx.is_write()) ||
             OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ctx), K(param));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else {
    const int64_t unlock_timestamp = ObTimeUtility::current_time();
    ObTableLockOp unlock_op(param.lock_id_,
                            param.lock_mode_,
                            param.owner_id_,
                            ctx.mvcc_acc_ctx_.get_tx_id(),
                            OUT_TRANS_UNLOCK,
                            LOCK_OP_DOING,
                            ctx.mvcc_acc_ctx_.tx_scn_,
                            unlock_timestamp,
                            param.schema_version_);
    ObTableLockOp lock_op(param.lock_id_,
                          param.new_lock_mode_,
                          param.new_owner_id_,
                          ctx.mvcc_acc_ctx_.get_tx_id(),
                          OUT_TRANS_LOCK,
                          LOCK_OP_DOING,
                          ctx.mvcc_acc_ctx_.tx_scn_,
                          unlock_timestamp,
                          param.schema_version_);

    if (OB_FAIL(memtable->replace(ctx, param, unlock_op, lock_op))) {
    }
  }
  return ret;
}

int ObLockTable::get_lock_id_iter(ObLockIDIterator &iter)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TABLELOCK_LOG(WARN, "ObLockTable not inited", K(ret));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else {
    if (OB_FAIL(memtable->get_lock_id_iter(iter))) {
    }
  }
  return ret;
}

int ObLockTable::get_lock_op_iter(const ObLockID &lock_id,
                                  ObLockOpIterator &iter)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TABLELOCK_LOG(WARN, "ObLockTable not inited", K(ret));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else {
    if (OB_FAIL(memtable->get_lock_op_iter(lock_id,
                                           iter))) {
    }
  }
  return ret;
}

int ObLockTable::admin_remove_lock_op(const ObTableLockOp &op_info)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TABLELOCK_LOG(WARN, "ObLockTable not inited", K(ret));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else {
    memtable->remove_lock_record(op_info);
  }
  TABLELOCK_LOG(INFO, "ObLockTable::admin_remove_lock_op", K(ret), K(op_info));
  return ret;
}

int ObLockTable::admin_update_lock_op(const ObTableLockOp &op_info,
                                      const share::SCN &commit_version,
                                      const share::SCN &commit_scn,
                                      const ObTableLockOpStatus status)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TABLELOCK_LOG(WARN, "ObLockTable not inited", K(ret));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else if (OB_FAIL(memtable->update_lock_status(op_info,
                                                  commit_version,
                                                  commit_scn,
                                                  status))) {
  }
  TABLELOCK_LOG(INFO, "ObLockTable::admin_update_lock_op", K(ret), K(op_info));
  return ret;
}

int ObLockTable::check_and_clear_obj_lock(const bool force_compact)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *lock_memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable is not inited", K(ret));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(lock_memtable))) {
  } else if (OB_FAIL(lock_memtable->check_and_clear_obj_lock(force_compact))) {
  }
  return ret;
}

int ObLockTable::add_lock_into_queue(storage::ObStoreCtx &ctx, const ObLockParam &param)
{
  int ret = OB_SUCCESS;
  ObLockMemtable *memtable = nullptr;
  ObTransID tx_id = ctx.mvcc_acc_ctx_.get_tx_id();
  TCRLockGuard guard(rw_lock_);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable not inited", K(ret));
  } else if (OB_UNLIKELY(!ctx.is_write()) ||
             OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ctx), K(param));
  } else if (!param.is_two_phase_lock_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("unexpected operation", K(ret), K(ctx), K(param));
  } else if (OB_UNLIKELY(!tx_id.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid argument", K(ret), K(ctx), K(param), K(ctx.mvcc_acc_ctx_));
    ob_abort();
  } else if (OB_FAIL(ctx.mvcc_acc_ctx_.mem_ctx_->get_lock_mem_ctx().get_lock_memtable(memtable))) {
  } else if (OB_ISNULL(memtable)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("lock memtable is null", K(ret), K(ctx));
  } else {
    // create ts generated by priority queue
    const int64_t lock_timestamp = 0;
    ObTableLockOp lock_op(param.lock_id_,
                          param.lock_mode_,
                          param.owner_id_,
                          tx_id,
                          param.op_type_,
                          LOCK_OP_DOING,
                          ctx.mvcc_acc_ctx_.tx_scn_,
                          lock_timestamp,
                          param.schema_version_);
    if (OB_FAIL(memtable->add_priority_task(param,
                                            ctx,
                                            lock_op))) {
    } else if (0 >= lock_op.create_timestamp_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected create ts", K(ret), K(lock_op));
    }
  }
  return ret;
}

int ObLockTable::activate()
{
  int ret = OB_SUCCESS;
  common::ObTimeGuard timeguard("switch_to_leader", 10 * 1000);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable is not inited", K(ret));
  } else {
    timeguard.click();
    if (OB_NOT_NULL(parent_)) {
      LOG_INFO("start to check and clear obj lock when switch to leader", K(ret));
    }
    if (OB_FAIL(check_obj_lock_timer_.schedule(check_obj_lock_task_,
                                               0 /* delay */,
                                               false /* repeat */))) {
    }
  }
  timeguard.click();

  if (OB_FAIL(ret)) {
    if (OB_ISNULL(parent_)) {
      // ignore ret
      LOG_WARN("parent ls of ObLockTable is null", K(ret));
    } else {
      LOG_WARN("collect obj lock garbage when switch to leader failed", K(ret));
    }
  } else {
    // switch to leader for lock memtable
    ObTableHandleV2 handle;
    ObLockMemtable *lock_memtable = nullptr;
    if (OB_FAIL(get_lock_memtable(handle))) {
    } else if (OB_FAIL(handle.get_lock_memtable(lock_memtable))) {
    } else if (OB_FAIL(lock_memtable->switch_to_leader())) {
    }
  }
  timeguard.click();
  return ret;
}

void ObLockTable::deactivate()
{
  int ret = OB_SUCCESS;
  common::ObTimeGuard timeguard("switch_to_follower", 10 * 1000);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLockTable is not inited", K(ret));
  } else if (OB_FAIL(switch_to_follower_())) {
  }
  timeguard.click();
}

int ObLockTable::switch_to_follower_()
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *lock_memtable = nullptr;
  if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(lock_memtable))) {
  } else if (OB_FAIL(lock_memtable->switch_to_follower())) {
  }
  return ret;
}

share::SCN ObLockTable::get_rec_scn()
{
  int ret = OB_SUCCESS;
  share::SCN rec_scn = share::SCN::max_scn();
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TABLELOCK_LOG(WARN, "ObLockTable not inited", K(ret));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else {
    rec_scn = memtable->get_rec_scn();
  }
  return rec_scn;
}

int ObLockTable::flush(share::SCN &scn)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;
  ObLockMemtable *memtable = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TABLELOCK_LOG(WARN, "ObLockTable not inited", K(ret));
  } else if (OB_FAIL(get_lock_memtable(handle))) {
  } else if (OB_FAIL(handle.get_lock_memtable(memtable))) {
  } else if (OB_FAIL(memtable->flush(scn))) {
  }
  return ret;
}




} // tablelock
} // transaction
} // oceanbase
