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

#include "ob_access_service.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"
#include "share/rc/ob_server_runtime.h"
#include "share/ob_io_device_helper.h" // LOCAL_DEVICE_INSTANCE
#include "share/io/ob_io_manager.h"
#include "storage/ob_tablet_stat_mgr.h"
#include "storage/ob_query_iterator_factory.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "storage/access/ob_dml_table_plan_access.h"
#include "storage/retrieval/ob_block_stat_iter.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_memstore_freezer.h"
#include "storage/ob_table_dml_param.h"
namespace oceanbase
{
using namespace common;
using namespace share;
namespace storage
{

namespace
{
class ObStorageDmlExecutionState final : public data_plane::ObIDmlExecutionState
{
public:
  explicit ObStorageDmlExecutionState(common::ObIAllocator &allocator)
    : allocator_(allocator), params_(), active_index_(0), has_active_(false)
  {}
  ~ObStorageDmlExecutionState() override {}

  void destroy() override
  {
    common::ObIAllocator &allocator = allocator_;
    this->~ObStorageDmlExecutionState();
    allocator.free(this);
  }
  int64_t timeout() const override { return has_active_ ? active_param().timeout_ : -1; }
  void set_skip_flush_redo(const bool skip) override
  {
    if (has_active_) {
      if (skip) {
        active_param().write_flag_.set_skip_flush_redo();
      } else {
        active_param().write_flag_.unset_skip_flush_redo();
      }
    }
  }

  ObDMLBaseParam &fresh_inactive_param(int64_t &index)
  {
    index = has_active_ ? 1 - active_index_ : 0;
    params_[index].~ObDMLBaseParam();
    new (&params_[index]) ObDMLBaseParam();
    return params_[index];
  }
  void commit(const int64_t index)
  {
    active_index_ = index;
    has_active_ = true;
  }
  bool has_active() const { return has_active_; }
  ObDMLBaseParam &active_param() { return params_[active_index_]; }
  const ObDMLBaseParam &active_param() const { return params_[active_index_]; }

private:
  common::ObIAllocator &allocator_;
  ObDMLBaseParam params_[2];
  int64_t active_index_;
  bool has_active_;
};
}

void ObStoreCtxGuard::reset()
{
  int ret = OB_SUCCESS;
  static const int64_t WARN_TIME_US = 5 * 1000 * 1000;
  if (IS_INIT) {
    if (OB_NOT_NULL(ls_)) {
      if (ctx_.is_valid() && OB_FAIL(ls_->revert_store_ctx(ctx_))) {
        LOG_WARN("revert transaction context fail", K(ret));
      }
      ls_ = nullptr;
    }
    const int64_t guard_used_us = ObClockGenerator::getClock() - init_ts_;
    if (guard_used_us >= WARN_TIME_US) {
      LOG_WARN_RET(OB_ERR_TOO_MUCH_TIME, "guard used too much time", K(guard_used_us), K(lbt()));
    }
    ctx_.reset();
    is_inited_ = false;
  }
}

int ObStoreCtxGuard::init(ObLS *ls)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    ctx_.reset();
    ls_ = ls;
    is_inited_ = true;
    init_ts_ = ObClockGenerator::getClock();
  }
  return ret;
}

ObAccessService::ObAccessService()
  : is_inited_(false),
    ls_svr_(nullptr)
{}

ObAccessService::~ObAccessService()
{
  destroy();
}

int ObAccessService::server_module_init(ObAccessService* &access_service)
{
  int ret = OB_SUCCESS;

  return access_service->init(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>());
}

int ObAccessService::init(
    ObLSService *ls_service)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("access service has been inited", K(ret));
  } else if (OB_ISNULL(ls_service)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls_service));
  } else {
    ls_svr_ = ls_service;
    is_inited_ = true;
  }
  return ret;
}

void ObAccessService::destroy()
{
  if (IS_INIT) {
    ls_svr_ = nullptr;
    is_inited_ = false;
  }
}

int ObAccessService::check_memstore_limit_(bool &is_out_of_mem)
{
  int ret = OB_SUCCESS;
  is_out_of_mem = false;
  ObMemstoreFreezer *freezer = nullptr;
  freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>();
  if (OB_FAIL(freezer->check_memstore_full(is_out_of_mem))) {
  } else {
    // do nothing
  }
  return ret;
}

int ObAccessService::pre_check_lock(
    transaction::ObTxDesc &tx_desc,
    const transaction::tablelock::ObLockParam &param)
{
  int ret = OB_SUCCESS;
  ObStoreCtxGuard ctx_guard;
  transaction::ObTxReadSnapshot snapshot;
  snapshot.init_none_read();
  concurrent_control::ObWriteFlag write_flag;
  write_flag.set_is_table_lock();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tx_desc.is_valid())
             || OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tx_desc), K(param));
  } else if (OB_FAIL(get_write_store_ctx_guard_(param.expired_time_, /*timeout*/
                                                tx_desc,
                                                snapshot,
                                                0,/*branch_id*/
                                                write_flag,
                                                ctx_guard))) {
  } else {
    ret = ctx_guard.get_ls()->check_lock_conflict(ctx_guard.get_store_ctx(), param);
  }
  return ret;
}

ERRSIM_POINT_DEF(EN_OB_NOT_MASTER_IN_TABLELOCK)
int ObAccessService::lock_obj(
    transaction::ObTxDesc &tx_desc,
    const transaction::tablelock::ObLockParam &param)
{
  int ret = OB_SUCCESS;
  ObStoreCtxGuard ctx_guard;
  transaction::ObTxReadSnapshot snapshot;
  snapshot.init_none_read();
    concurrent_control::ObWriteFlag write_flag;
  write_flag.set_is_table_lock();

  if (OB_FAIL(EN_OB_NOT_MASTER_IN_TABLELOCK)) {
    FLOG_INFO("meet errsim", KR(ret));
  } else if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tx_desc.is_valid())
             || OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tx_desc), K(param));
  } else if (OB_FAIL(get_write_store_ctx_guard_(param.expired_time_, /*timeout*/
                                                tx_desc,
                                                snapshot,
                                                0, /*branch_id*/
                                                write_flag,
                                                ctx_guard))) {
  } else {
    ret = ctx_guard.get_ls()->lock(ctx_guard.get_store_ctx(), param);
  }
  return ret;
}

int ObAccessService::unlock_obj(
    transaction::ObTxDesc &tx_desc,
    const transaction::tablelock::ObLockParam &param)
{
  int ret = OB_SUCCESS;
  ObStoreCtxGuard ctx_guard;
  transaction::ObTxReadSnapshot snapshot;
  snapshot.init_none_read();
  concurrent_control::ObWriteFlag write_flag;
  write_flag.set_is_table_lock();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tx_desc.is_valid())
             || OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tx_desc), K(param));
  } else if (OB_FAIL(get_write_store_ctx_guard_(param.expired_time_, /*timeout*/
                                                tx_desc,
                                                snapshot,
                                                0,/*branch_id*/
                                                write_flag,
                                                ctx_guard))) {
  } else {
    ret = ctx_guard.get_ls()->unlock(ctx_guard.get_store_ctx(), param);
  }
  return ret;
}

int ObAccessService::replace_obj_lock(
    transaction::ObTxDesc &tx_desc,
    const transaction::tablelock::ObReplaceLockParam &lock_param)
{
  int ret = OB_SUCCESS;
  ObStoreCtxGuard ctx_guard;
  transaction::ObTxReadSnapshot snapshot;
  snapshot.init_none_read();
  concurrent_control::ObWriteFlag write_flag;
  write_flag.set_is_table_lock();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tx_desc.is_valid())
             || OB_UNLIKELY(!lock_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",
             K(ret),
             K(tx_desc),
             K(lock_param),
             K(tx_desc.is_valid()),
             K(lock_param.is_valid()));
  } else if (OB_FAIL(get_write_store_ctx_guard_(lock_param.expired_time_, /*timeout*/
                                                tx_desc,
                                                snapshot,
                                                0,/*branch_id*/
                                                write_flag,
                                                ctx_guard))) {
  } else {
    ret = ctx_guard.get_ls()->replace_lock(ctx_guard.get_store_ctx(), lock_param);
  }
  return ret;
}

int ObAccessService::add_lock_into_queue(transaction::ObTxDesc &tx_desc,
                                         const transaction::tablelock::ObLockParam &param)
{
  int ret = OB_SUCCESS;
  ObStoreCtxGuard ctx_guard;
  transaction::ObTxReadSnapshot snapshot;
  snapshot.init_none_read();
  concurrent_control::ObWriteFlag write_flag;
  write_flag.set_is_table_lock();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tx_desc.is_valid())
             || OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tx_desc), K(param));
  } else if (OB_FAIL(get_write_store_ctx_guard_(param.expired_time_, /*timeout*/
                                                tx_desc,
                                                snapshot,
                                                0, /*branch_id*/
                                                write_flag,
                                                ctx_guard))) {
  } else {
    ret = ctx_guard.get_ls()->add_lock_into_queue(ctx_guard.get_store_ctx(), param);
  }
  return ret;
}

int ObAccessService::table_scan(
    ObVTableScanParam &vparam,
    ObNewRowIterator *&result)
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &data_tablet_id = vparam.tablet_id_;
  ObTableScanIterator *iter = nullptr;
  ObTabletHandle tablet_handle;
  ObTableScanParam &param = static_cast<ObTableScanParam &>(vparam);
  ObStoreAccessType access_type = param.scan_flag_.is_read_latest() ?
    ObStoreAccessType::READ_LATEST : ObStoreAccessType::READ;
  SCN user_specified_snapshot_scn;
  if (ObAccessTypeCheck::is_read_access_type(access_type) && param.fb_snapshot_.is_valid()) {
    //todo lixinze:subsequent will determine if it is valid
    user_specified_snapshot_scn = param.fb_snapshot_;
  }
  NG_TRACE(storage_table_scan_begin);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (!vparam.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(vparam), K(lbt()));
  } else if (OB_NOT_NULL(result)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("The result_ is already pointed to an valid object",
        K(ret), K(vparam), KPC(result), K(lbt()));
  } else if (OB_ISNULL(iter = share::borrow_server_object<ObTableScanIterator>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("alloc table scan iterator fail", K(ret));
  } else if (FALSE_IT(result = iter)) {
    // upper layer responsible for releasing iter object
  } else if (OB_FAIL(check_read_allowed_(data_tablet_id,
                                         access_type,
                                         param,
                                         tablet_handle,
                                         iter->get_ctx_guard(),
                                         user_specified_snapshot_scn))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("fail to check query allowed", K(ret), K(data_tablet_id));
    }
    // skip inner table, one key reason is to let tablet merge going
  } else if (OB_FAIL(iter->get_ctx_guard().get_ls()->get_tablet_svr()->table_scan(
                         tablet_handle, *iter, param))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("Fail to scan table, ", K(ret), K(param));
    }
  } else {
    NG_TRACE(storage_table_scan_end);
  }
  return ret;
}

int ObAccessService::table_rescan(
    ObVTableScanParam &vparam,
    ObNewRowIterator *result)
{
  int ret = OB_SUCCESS;
  ObTableScanParam &param = static_cast<ObTableScanParam &>(vparam);
  ObTabletHandle tablet_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_ISNULL(result) || OB_UNLIKELY(!vparam.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(result), K(vparam), K(lbt()));
  } else if (OB_UNLIKELY(ObNewRowIterator::ObTableScanIterator != result->get_type())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("only table scan iter can be rescan", K(ret), K(result->get_type()));
  } else if (!param.need_switch_param_) {
    if (OB_FAIL(static_cast<ObTableScanIterator*>(result)->rescan(param))) {
    }
  } else {
    ObTableScanIterator *iter =  static_cast<ObTableScanIterator*>(result);
    const common::ObTabletID &data_tablet_id = vparam.tablet_id_;
    ObStoreAccessType access_type = param.scan_flag_.is_read_latest() ?
      ObStoreAccessType::READ_LATEST : ObStoreAccessType::READ;
    SCN user_specified_snapshot_scn;
    if (ObAccessTypeCheck::is_read_access_type(access_type) && param.fb_snapshot_.is_valid()) {
      //todo lixinze:subsequent will determine if it is valid
      user_specified_snapshot_scn = param.fb_snapshot_;
    }
    NG_TRACE(storage_table_scan_begin);
    if (OB_FAIL(check_read_allowed_(data_tablet_id,
                                    access_type,
                                    param, /*scan_param*/
                                    tablet_handle,
                                    iter->get_ctx_guard(),
                                    user_specified_snapshot_scn))) {
      if (OB_TABLET_NOT_EXIST != ret) {
        LOG_WARN("fail to check query allowed", K(ret), K(result), K(data_tablet_id));
      }
    // skip inner table, one key reason is to let tablet merge going
    } else if (OB_FAIL(iter->get_ctx_guard().get_ls()->get_tablet_svr()->table_rescan(
                           tablet_handle, param, result))) {
      if (OB_TABLET_NOT_EXIST != ret) {
        LOG_WARN("Fail to scan table, ", K(ret), K(result), K(param));
      }
    } else {
      NG_TRACE(storage_table_scan_end);
    }
  }
  return ret;
}

int ObAccessService::table_advance_scan(ObVTableScanParam &vparam, ObNewRowIterator *result)
{
  int ret = OB_SUCCESS;
  ObTableScanParam &param = static_cast<ObTableScanParam &>(vparam);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_ISNULL(result) || OB_UNLIKELY(!vparam.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(result), K(vparam), K(lbt()));
  } else if (OB_UNLIKELY(ObNewRowIterator::ObTableScanIterator != result->get_type())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("only table scan iter can be rescan", K(ret), K(result->get_type()));
  } else if (OB_FAIL(static_cast<ObTableScanIterator*>(result)->advance_scan(param))) {
  } else {
  }
  return ret;
}

int ObAccessService::get_write_store_ctx_guard(
    const int64_t timeout,
    transaction::ObTxDesc &tx_desc,
    const transaction::ObTxReadSnapshot &snapshot,
    const int16_t branch_id,
    concurrent_control::ObWriteFlag &write_flag,
    ObStoreCtxGuard &ctx_guard,
    const transaction::ObTxSEQ &spec_seq_no)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tx_desc.is_valid() || !snapshot.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tx_desc), K(snapshot));
  } else if (OB_FAIL(get_write_store_ctx_guard_(
              timeout, tx_desc, snapshot, branch_id, write_flag, ctx_guard, spec_seq_no))) {
  }
  return ret;
}

int ObAccessService::acquire_write_context(
    const int64_t timeout,
    transaction::ObTxDesc &tx_desc,
    const transaction::ObTxReadSnapshot &snapshot,
    const int16_t branch_id,
    concurrent_control::ObWriteFlag &write_flag,
    data_plane::ObWriteContext &write_context)
{
  int ret = OB_SUCCESS;
  ObStoreCtxGuard *ctx_guard = new (std::nothrow) ObStoreCtxGuard();
  if (OB_ISNULL(ctx_guard)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "failed to allocate write context", K(ret));
  } else if (OB_FAIL(get_write_store_ctx_guard(
                 timeout,
                 tx_desc,
                 snapshot,
                 branch_id,
                 write_flag,
                 *ctx_guard,
                 transaction::ObTxSEQ::INVL()))) {
    delete ctx_guard;
    ctx_guard = nullptr;
  } else {
    write_context.bind(ctx_guard, [](void *context) {
      delete static_cast<ObStoreCtxGuard *>(context);
    });
  }
  return ret;
}

int ObAccessService::get_write_store_ctx_guard_(
    const int64_t timeout,
    transaction::ObTxDesc &tx_desc,
    const transaction::ObTxReadSnapshot &snapshot,
    const int16_t branch_id,
    const concurrent_control::ObWriteFlag write_flag,
    ObStoreCtxGuard &ctx_guard,
    const transaction::ObTxSEQ &spec_seq_no)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  if (OB_FAIL(ls_svr_->get_ls(ls))) {
  } else if (OB_FAIL(ctx_guard.init(ls))) {
  } else {
    ObStoreCtx &ctx = ctx_guard.get_store_ctx();
    ctx.ls_ = ls;
    ctx.timeout_ = timeout;
    ctx.branch_ = branch_id;
    if (OB_FAIL(ls->get_write_store_ctx(tx_desc, snapshot, write_flag, ctx, spec_seq_no))) {
    }
  }
  if (OB_FAIL(ret)) {
    ctx_guard.reset();
  }
  return ret;
}

int ObAccessService::construct_store_ctx_other_variables_(
    ObLS &ls,
    const common::ObTabletID &tablet_id,
    const int64_t timeout,
    const share::SCN &snapshot,
    ObTabletHandle &tablet_handle,
    ObStoreCtxGuard &ctx_guard)
{
  int ret = OB_SUCCESS;
  ObLSTabletService *tablet_service = ls.get_tablet_svr();
  if (OB_FAIL(tablet_service->get_tablet_with_timeout(
      tablet_id, tablet_handle, timeout, ObMDSGetTabletMode::READ_READABLE_COMMITED, snapshot))) {
  }
  return ret;
}
/*
 * check_read_allowed - check replica can serve transactional read
 *
 * if replica can serve read, store_ctx will be prepared
 */
int ObAccessService::check_read_allowed_(
    const common::ObTabletID &tablet_id,
    const ObStoreAccessType access_type,
    const ObTableScanParam &scan_param,
    ObTabletHandle &tablet_handle,
    ObStoreCtxGuard &ctx_guard,
    SCN user_specified_snapshot)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;

  if (OB_FAIL(ls_svr_->get_ls(ls))) {
  } else if (OB_FAIL(ctx_guard.init(ls))) {
  } else {
    ObStoreCtx &ctx = ctx_guard.get_store_ctx();
    ctx.ls_ = ls;
    ctx.timeout_ = scan_param.timeout_;
    ctx.tablet_id_ = tablet_id;
    if (user_specified_snapshot.is_valid()) {
      if (OB_FAIL(ls->get_read_store_ctx(user_specified_snapshot,
                                         scan_param.tx_lock_timeout_,
                                         ctx))) {
      }
    } else {
      bool read_latest = access_type == ObStoreAccessType::READ_LATEST;
      if (user_specified_snapshot.is_valid()) {
        transaction::ObTxReadSnapshot spec_snapshot;
        if (OB_FAIL(spec_snapshot.assign(scan_param.snapshot_))) {
        } else if (FALSE_IT(spec_snapshot.specify_snapshot_scn(user_specified_snapshot))) {
        } else if (OB_FAIL(ls->get_read_store_ctx(spec_snapshot,
                                                  read_latest,
                                                  scan_param.tx_lock_timeout_,
                                                  ctx))) {
        }
      } else if (OB_FAIL(ls->get_read_store_ctx(scan_param.snapshot_,
                                                read_latest,
                                                scan_param.tx_lock_timeout_,
                                                ctx,
                                                scan_param.trans_desc_))) {
      }
      if (OB_FAIL(ret)) {
      } else if (read_latest) {
        if (!scan_param.tx_id_.is_valid()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("readlatest need scan_param.tx_id_ valid", K(ret));
        } else {
          ctx.mvcc_acc_ctx_.tx_id_ = scan_param.tx_id_;
        }
      }
    }

    // If this select is for foreign key check,
    // we should get tx_id and tx_desc for deadlock detection.
    if (OB_SUCC(ret)) {
      if (scan_param.is_for_foreign_check_) {
        if (scan_param.tx_id_.is_valid()) {
          ctx.mvcc_acc_ctx_.tx_id_ = scan_param.tx_id_;
        } else {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("foreign key check need scan_param.tx_id_ valid", K(ret), K(scan_param.tx_id_));
        }
        if (OB_NOT_NULL(scan_param.trans_desc_) && scan_param.trans_desc_->is_valid()) {
          ctx.mvcc_acc_ctx_.tx_desc_ = scan_param.trans_desc_;
        } else {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("foreign key check need scan_param.trans_desc_ valid", K(ret), KPC(scan_param.trans_desc_));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(construct_store_ctx_other_variables_(*ls, tablet_id, scan_param.timeout_,
         ctx.mvcc_acc_ctx_.get_snapshot_version(), tablet_handle, ctx_guard))) {
      if (OB_SNAPSHOT_DISCARDED == ret && scan_param.fb_snapshot_.is_valid()) {
        ret = OB_TABLE_DEFINITION_CHANGED;
      } else {
        LOG_WARN("failed to check replica allow to read", K(ret), K(tablet_id), "timeout", scan_param.timeout_);
      }
    }
  }
  return ret;
}


/*
 * check_write_allowed - check replica can serve transactional write
 *
 * if can serve write, store_ctx will be prepared
 */
int ObAccessService::check_write_allowed_(
    const common::ObTabletID &tablet_id,
    const ObStoreAccessType access_type,
    const ObDMLBaseParam &dml_param,
    const int64_t lock_wait_timeout_ts,
    transaction::ObTxDesc &tx_desc,
    ObTabletHandle &tablet_handle,
    ObStoreCtxGuard &ctx_guard)
{
  int ret = OB_SUCCESS;
  bool is_out_of_mem = false;
  ObLS *ls = nullptr;
  ObLockID lock_id;
  ObLockParam lock_param;
  const ObTableLockMode lock_mode = ROW_EXCLUSIVE;
  const ObTableLockOpType lock_op_type = IN_TRANS_DML_LOCK;
  ObTableLockOwnerID lock_owner;
  lock_owner.set_default();
  const bool is_deadlock_avoid_enabled = false;
  bool is_try_lock = lock_wait_timeout_ts <= 0;
  const int64_t abs_timeout_ts = MIN(lock_wait_timeout_ts, tx_desc.get_expire_ts());
  bool enable_table_lock = true;
  ret = OB_E(EventTable::EN_ENABLE_TABLE_LOCK) OB_SUCCESS;
  if (OB_ERR_UNEXPECTED == ret) {
    enable_table_lock = false;
    ret = OB_SUCCESS;
  }
  if (OB_FAIL(check_memstore_limit_(is_out_of_mem))) {
  } else if (is_out_of_mem && !tablet_id.is_inner_tablet()) {
    ret = OB_SERVER_RUNTIME_OUT_OF_MEM;
    LOG_WARN("server runtime is already out of memstore memory", K(ret));
  } else if (OB_ISNULL(ls = ctx_guard.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret), K(tablet_id));
  } else {
    ObStoreCtx &store_ctx = ctx_guard.get_store_ctx();
    store_ctx.tablet_id_ = tablet_id;
    store_ctx.timeout_ = abs_timeout_ts;
    store_ctx.mvcc_acc_ctx_.set_write_flag(dml_param.write_flag_);
    store_ctx.mvcc_acc_ctx_.set_abs_lock_timeout_ts(abs_timeout_ts);
    store_ctx.tablet_stat_.reset();
    store_ctx.clear_mds_filter();

    const int64_t lock_expired_ts = MIN(dml_param.timeout_, tx_desc.get_expire_ts());
    const ObTableSchemaParam &schema_param = dml_param.table_param_->get_data_table();
    const bool is_local_index_table = schema_param.is_index_table() && schema_param.is_index_local_storage();

    if (!enable_table_lock) {
      // do nothing
    } else if (is_local_index_table) {
      // skip table lock
    } else if (OB_FAIL(get_lock_id(tablet_id, lock_id))) {
    } else if (OB_FAIL(lock_param.set(lock_id,
                                      lock_mode,
                                      lock_owner,
                                      lock_op_type,
                                      dml_param.schema_version_,
                                      is_deadlock_avoid_enabled,
                                      is_try_lock,
                                      // we can not use abs_timeout_ts here,
                                      // because we may meet select-for-update nowait,
                                      // and abs_timeout_ts is 0. We will judge
                                      // timeout before meet lock conflict in tablelock,
                                      // so it will lead to incorrect error
                                      lock_expired_ts))) {
    } else if (OB_FAIL(ls->lock(ctx_guard.get_store_ctx(), lock_param))) {
    } else {
      // do nothing
    }
  }
  // After locking the table, it can prevent the tablet from being deleted.
  // It is necessary to obtain the tablet handle after locking the table to avoid operating the deleted tablet.
  if (OB_SUCC(ret) && OB_FAIL(construct_store_ctx_other_variables_(*ls, tablet_id, dml_param.timeout_,
      share::SCN::max_scn(), tablet_handle, ctx_guard))) {
    LOG_WARN("failed to check replica allow to read", K(ret), K(tablet_id));
  }
  return ret;
}

int ObAccessService::prepare_execution(
    const data_plane::ObDmlWriteSpec &write_spec,
    const data_plane::ObDmlTablePlan &table_plan,
    const transaction::ObTxReadSnapshot &snapshot,
    common::ObIAllocator &allocator,
    const data_plane::ObWriteContext &write_context,
    const concurrent_control::ObWriteFlag &write_flag,
    data_plane::ObDmlExecution &execution)
{
  int ret = OB_SUCCESS;
  const share::schema::ObTableDMLParam *legacy_table_plan =
      ObDmlTablePlanAccess::get(table_plan);
  ObStorageDmlExecutionState *state =
      static_cast<ObStorageDmlExecutionState *>(execution_state(execution));
  bool created_state = false;
  int64_t staged_index = -1;
  if (OB_ISNULL(legacy_table_plan) || OB_UNLIKELY(!write_context.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid DML execution input", K(ret), KP(legacy_table_plan),
        K(write_context.is_valid()));
  } else if (nullptr == state) {
    void *buf = allocator.alloc(sizeof(ObStorageDmlExecutionState));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate DML execution state", K(ret));
    } else {
      state = new (buf) ObStorageDmlExecutionState(allocator);
      created_state = true;
    }
  }

  if (OB_SUCC(ret)) {
    const bool inherit_active_state = state->has_active();
    const bool inherited_has_async_index =
        inherit_active_state && state->active_param().has_async_index_;
    const uint64_t inherited_write_flags =
        inherit_active_state ? state->active_param().write_flag_.flag_ : 0;
    ObDMLBaseParam &dml_param = state->fresh_inactive_param(staged_index);
    dml_param.timeout_ = write_spec.timeout_;
    dml_param.schema_version_ = write_spec.schema_version_;
    dml_param.is_total_quantity_log_ = write_spec.is_total_quantity_log_;
    dml_param.tz_info_ = write_spec.tz_info_;
    dml_param.sql_mode_ = static_cast<ObSQLMode>(write_spec.sql_mode_);
    dml_param.table_param_ = legacy_table_plan;
    dml_param.prelock_ = write_spec.prelock_;
    dml_param.is_batch_stmt_ = write_spec.is_batch_stmt_;
    dml_param.dml_allocator_ = &allocator;
    dml_param.is_main_table_in_fts_ddl_ = write_spec.is_main_table_in_fts_ddl_;
    dml_param.check_schema_version_ = write_spec.check_schema_version_;
    dml_param.has_async_index_ =
        legacy_table_plan->get_data_table().has_async_index()
        || inherited_has_async_index;
    if (dml_param.has_async_index_
        && write_spec.access_vector_id_as_master_table_
        && legacy_table_plan->get_data_table().is_vector_index_id()) {
      dml_param.check_schema_version_ = false;
    }
    if (OB_FAIL(dml_param.snapshot_.assign(snapshot))) {
    } else {
      dml_param.branch_id_ = write_spec.branch_id_;
      dml_param.store_ctx_guard_ =
          static_cast<storage::ObStoreCtxGuard *>(write_context.native_handle());
      dml_param.write_flag_.flag_ = write_flag.flag_ | inherited_write_flags;
    }
  }
  if (OB_SUCC(ret)) {
    state->commit(staged_index);
    if (created_state) {
      bind_execution(execution, state);
    }
  } else if (created_state && nullptr != state) {
    state->destroy();
  }
  return ret;
}

int ObAccessService::delete_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const data_plane::ObDmlExecution &execution,
    const common::ObIArray<uint64_t> &column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  ObStorageDmlExecutionState *state =
      static_cast<ObStorageDmlExecutionState *>(execution_state(execution));
  return OB_ISNULL(state) || !state->has_active()
      ? OB_INVALID_ARGUMENT
      : delete_rows(tablet_id, tx_desc, state->active_param(),
                    column_ids, row_iter, affected_rows);
}

int ObAccessService::put_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const data_plane::ObDmlExecution &execution,
    const common::ObIArray<uint64_t> &column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  ObStorageDmlExecutionState *state =
      static_cast<ObStorageDmlExecutionState *>(execution_state(execution));
  return OB_ISNULL(state) || !state->has_active()
      ? OB_INVALID_ARGUMENT
      : put_rows(tablet_id, tx_desc, state->active_param(),
                 column_ids, row_iter, affected_rows);
}

int ObAccessService::insert_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const data_plane::ObDmlExecution &execution,
    const common::ObIArray<uint64_t> &column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  ObStorageDmlExecutionState *state =
      static_cast<ObStorageDmlExecutionState *>(execution_state(execution));
  return OB_ISNULL(state) || !state->has_active()
      ? OB_INVALID_ARGUMENT
      : insert_rows(tablet_id, tx_desc, state->active_param(),
                    column_ids, row_iter, affected_rows);
}

int ObAccessService::insert_rows_fetch_duplicates(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const data_plane::ObDmlExecution &execution,
    const common::ObIArray<uint64_t> &column_ids,
    const common::ObIArray<uint64_t> &duplicated_column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    const data_plane::ObDuplicateReturnMode return_mode,
    int64_t &affected_rows,
    blocksstable::ObDatumRowIterator *&duplicated_rows)
{
  ObStorageDmlExecutionState *state =
      static_cast<ObStorageDmlExecutionState *>(execution_state(execution));
  return OB_ISNULL(state) || !state->has_active()
      ? OB_INVALID_ARGUMENT
      : insert_rows_with_fetch_dup(
            tablet_id, tx_desc, state->active_param(), column_ids,
            duplicated_column_ids, row_iter,
            static_cast<ObInsertFlag>(return_mode),
            affected_rows, duplicated_rows);
}

void ObAccessService::free_duplicate_rows_iterator(
    blocksstable::ObDatumRowIterator *iterator)
{
  ObQueryIteratorFactory::free_insert_dup_iter(iterator);
}

int ObAccessService::update_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const data_plane::ObDmlExecution &execution,
    const common::ObIArray<uint64_t> &column_ids,
    const common::ObIArray<uint64_t> &updated_column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  ObStorageDmlExecutionState *state =
      static_cast<ObStorageDmlExecutionState *>(execution_state(execution));
  return OB_ISNULL(state) || !state->has_active()
      ? OB_INVALID_ARGUMENT
      : update_rows(tablet_id, tx_desc, state->active_param(),
                    column_ids, updated_column_ids, row_iter, affected_rows);
}

int ObAccessService::lock_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const data_plane::ObDmlExecution &execution,
    const int64_t abs_lock_timeout,
    const data_plane::ObRowLockMode lock_mode,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  ObStorageDmlExecutionState *state =
      static_cast<ObStorageDmlExecutionState *>(execution_state(execution));
  return OB_ISNULL(state) || !state->has_active()
      ? OB_INVALID_ARGUMENT
      : lock_rows(tablet_id, tx_desc, state->active_param(),
                  abs_lock_timeout, static_cast<ObLockFlag>(lock_mode),
                  row_iter, affected_rows);
}

int ObAccessService::delete_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const ObDMLBaseParam &dml_param,
    const common::ObIArray<uint64_t> &column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  ObLSTabletService *tablet_service = nullptr;
  // Attention!!! This handle is only used for ObLSTabletService, will be reset inside ObLSTabletService.
  ObTabletHandle tablet_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!tx_desc.is_valid())
      || OB_UNLIKELY(!dml_param.is_valid())
      || OB_UNLIKELY(column_ids.count() <= 0)
      || OB_ISNULL(row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(tx_desc),
             K(dml_param), K(column_ids), KP(row_iter));
  } else if (OB_FAIL(check_write_allowed_(tablet_id,
                                          ObStoreAccessType::MODIFY,
                                          dml_param,
                                          dml_param.timeout_,
                                          tx_desc,
                                          tablet_handle,
                                          *dml_param.store_ctx_guard_))) {
  } else if (OB_ISNULL(tablet_service = dml_param.store_ctx_guard_->get_ls()->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet service should not be null.", K(ret));
  } else {
    ret = tablet_service->delete_rows(tablet_handle,
                                      dml_param.store_ctx_guard_->get_store_ctx(),
                                      dml_param,
                                      column_ids,
                                      row_iter,
                                      affected_rows);
  }
  return ret;
}

int ObAccessService::put_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const ObDMLBaseParam &dml_param,
    const common::ObIArray<uint64_t> &column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  ObLSTabletService *tablet_service = nullptr;
  // Attention!!! This handle is only used for ObLSTabletService, will be reset inside ObLSTabletService.
  ObTabletHandle tablet_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!tx_desc.is_valid())
      || OB_UNLIKELY(!dml_param.is_valid())
      || OB_UNLIKELY(column_ids.count() <= 0)
      || OB_ISNULL(row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(tx_desc),
             K(dml_param), K(column_ids), K(row_iter));
  } else if (OB_FAIL(check_write_allowed_(tablet_id,
                                          ObStoreAccessType::MODIFY,
                                          dml_param,
                                          dml_param.timeout_,
                                          tx_desc,
                                          tablet_handle,
                                          *dml_param.store_ctx_guard_))) {
  } else if (OB_ISNULL(tablet_service = dml_param.store_ctx_guard_->get_ls()->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet service should not be null.", K(ret));
  } else {
    ret = tablet_service->put_rows(tablet_handle,
                                   dml_param.store_ctx_guard_->get_store_ctx(),
                                   dml_param,
                                   column_ids,
                                   row_iter,
                                   affected_rows);
  }
  return ret;
}

int ObAccessService::insert_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const ObDMLBaseParam &dml_param,
    const common::ObIArray<uint64_t> &column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  ObLSTabletService *tablet_service = nullptr;
  // Attention!!! This handle is only used for ObLSTabletService, will be reset inside ObLSTabletService.
  ObTabletHandle tablet_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!tx_desc.is_valid())
      || OB_UNLIKELY(!dml_param.is_valid())
      || OB_UNLIKELY(column_ids.count() <= 0)
      || OB_ISNULL(row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(tx_desc),
             K(dml_param), K(column_ids), KP(row_iter));
  } else if (OB_FAIL(check_write_allowed_(tablet_id,
                                          ObStoreAccessType::MODIFY,
                                          dml_param,
                                          dml_param.timeout_,
                                          tx_desc,
                                          tablet_handle,
                                          *dml_param.store_ctx_guard_))) {
  } else if (OB_ISNULL(tablet_service = dml_param.store_ctx_guard_->get_ls()->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet service should not be null.", K(ret));
  } else {
    ret = tablet_service->insert_rows(tablet_handle,
                                      dml_param.store_ctx_guard_->get_store_ctx(),
                                      dml_param,
                                      column_ids,
                                      row_iter,
                                      affected_rows);
  }
  return ret;
}

int ObAccessService::insert_rows_with_fetch_dup(const common::ObTabletID &tablet_id,
                                                transaction::ObTxDesc &tx_desc,
                                                const ObDMLBaseParam &dml_param,
                                                const common::ObIArray<uint64_t> &column_ids,
                                                const common::ObIArray<uint64_t> &duplicated_column_ids,
                                                blocksstable::ObDatumRowIterator *row_iter,
                                                const ObInsertFlag flag,
                                                int64_t &affected_rows,
                                                blocksstable::ObDatumRowIterator *&duplicated_rows)
{
  int ret = OB_SUCCESS;
  ObLSTabletService *tablet_service = nullptr;
  // Attention!!! This handle is only used for ObLSTabletService, will be reset inside ObLSTabletService.
  ObTabletHandle tablet_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!tx_desc.is_valid())
      || OB_UNLIKELY(!dml_param.is_valid())
      || OB_UNLIKELY(column_ids.count() <= 0)
      || OB_UNLIKELY(duplicated_column_ids.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(tx_desc),
             K(dml_param), K(column_ids), K(duplicated_column_ids));
  } else if (OB_FAIL(check_write_allowed_(tablet_id,
                                          ObStoreAccessType::MODIFY,
                                          dml_param,
                                          dml_param.timeout_,
                                          tx_desc,
                                          tablet_handle,
                                          *dml_param.store_ctx_guard_))) {
  } else if (OB_ISNULL(tablet_service = dml_param.store_ctx_guard_->get_ls()->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet service should not be null.", K(ret));
  } else {
    ret = tablet_service->insert_rows_with_fetch_dup(tablet_handle,
                                                     dml_param.store_ctx_guard_->get_store_ctx(),
                                                     dml_param,
                                                     column_ids,
                                                     duplicated_column_ids,
                                                     row_iter,
                                                     flag,
                                                     affected_rows,
                                                     duplicated_rows);
  }
  return ret;
}


int ObAccessService::update_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const ObDMLBaseParam &dml_param,
    const common::ObIArray<uint64_t> &column_ids,
    const common::ObIArray< uint64_t> &updated_column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  ObLSTabletService *tablet_service = nullptr;
  // Attention!!! This handle is only used for ObLSTabletService, will be reset inside ObLSTabletService.
  ObTabletHandle tablet_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!tx_desc.is_valid())
      || OB_UNLIKELY(!dml_param.is_valid())
      || OB_UNLIKELY(column_ids.count() <= 0)
      || OB_UNLIKELY(updated_column_ids.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(tx_desc),
             K(dml_param), K(column_ids), K(updated_column_ids));
  } else if (OB_FAIL(check_write_allowed_(tablet_id,
                                          ObStoreAccessType::MODIFY,
                                          dml_param,
                                          dml_param.timeout_,
                                          tx_desc,
                                          tablet_handle,
                                          *dml_param.store_ctx_guard_))) {
  } else if (OB_ISNULL(tablet_service = dml_param.store_ctx_guard_->get_ls()->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet service should not be null.", K(ret));
  } else {
    ret = tablet_service->update_rows(tablet_handle,
                                      dml_param.store_ctx_guard_->get_store_ctx(),
                                      dml_param,
                                      column_ids,
                                      updated_column_ids,
                                      row_iter,
                                      affected_rows);
  }
  return ret;
}

int ObAccessService::lock_rows(
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const ObDMLBaseParam &dml_param,
    const int64_t abs_lock_timeout,
    const ObLockFlag lock_flag,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  ObLSTabletService *tablet_service = nullptr;
  // Attention!!! This handle is only used for ObLSTabletService, will be reset inside ObLSTabletService.
  ObTabletHandle tablet_handle;
  int64_t lock_wait_timeout_ts = get_lock_wait_timeout_(abs_lock_timeout, dml_param.timeout_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!tx_desc.is_valid())
      || OB_UNLIKELY(!dml_param.is_valid())
      || OB_ISNULL(row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(tx_desc),
             K(dml_param), K(abs_lock_timeout), K(lock_flag), KP(row_iter));
  } else if (OB_FAIL(check_write_allowed_(tablet_id,
                                          ObStoreAccessType::ROW_LOCK,
                                          dml_param,
                                          lock_wait_timeout_ts,
                                          tx_desc,
                                          tablet_handle,
                                          *dml_param.store_ctx_guard_))) {
  } else if (OB_ISNULL(tablet_service = dml_param.store_ctx_guard_->get_ls()->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet service should not be null.", K(ret));
  } else {
    ret = tablet_service->lock_rows(tablet_handle,
                                    dml_param.store_ctx_guard_->get_store_ctx(),
                                    dml_param,
                                    lock_flag,
                                    false,
                                    row_iter,
                                    affected_rows);
  }
  return ret;
}


int ObAccessService::estimate_row_count(
    const ObTableScanParam &param,
    const ObTableScanRange &scan_range,
    const int64_t timeout_us,
    common::ObIArray<ObEstRowCountRecord> &est_records,
    int64_t &logical_row_count,
    int64_t &physical_row_count) const
{
  int ret = OB_SUCCESS;
  ObLS *tenant_ls = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_ERROR;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!param.is_estimate_valid() || !scan_range.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(param), K(scan_range), K(ret));
  } else if (OB_FAIL(ls_svr_->get_ls(tenant_ls))) {
  } else if (OB_FAIL(tenant_ls->get_tablet_svr()->estimate_row_count(
      param, scan_range, timeout_us, est_records,
      logical_row_count, physical_row_count))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("failed to estimate row count", K(ret), K(param), K(scan_range), K(timeout_us));
    }
  }
  return ret;
}

int ObAccessService::estimate_row_count_for_batch(
    ObTableScanParam &param,
    const common::ObSimpleBatch &batch,
    common::ObIAllocator &allocator,
    const int64_t timeout_us,
    common::ObIArray<common::ObEstRowCountRecord> &est_records,
    int64_t &logical_row_count,
    int64_t &physical_row_count) const
{
  int ret = OB_SUCCESS;
  ObTableScanRange scan_range;
  if (OB_FAIL(scan_range.init(param, batch, allocator))) {
  } else if (OB_FAIL(estimate_row_count(
                 param, scan_range, timeout_us, est_records,
                 logical_row_count, physical_row_count))) {
  }
  return ret;
}

int ObAccessService::estimate_block_count_and_row_count(
    const common::ObTabletID &tablet_id,
    const int64_t timeout_us,
    int64_t &macro_block_count,
    int64_t &micro_block_count,
    int64_t &sstable_row_count,
    int64_t &memtable_row_count) const
{
  int ret = OB_SUCCESS;
  ObLS *tenant_ls = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(tablet_id), K(ret));
  } else if (OB_FAIL(ls_svr_->get_ls(tenant_ls))) {
  } else if (OB_FAIL(tenant_ls->get_tablet_svr()->estimate_block_count_and_row_count(
      tablet_id, timeout_us,
      macro_block_count, micro_block_count,
      sstable_row_count, memtable_row_count))) {
  }
  return ret;
}

int ObAccessService::get_latest_tablet_row_count_delta(
    const common::ObTabletID &tablet_id,
    int64_t &row_count_delta) const
{
  int ret = OB_SUCCESS;
  row_count_delta = 0;
  ObTabletStatMgr *stat_mgr = ::oceanbase::share::server_service<::oceanbase::storage::ObTabletStatMgr>();
  ObTabletStat tablet_stat;
  ObTabletStat unused_tablet_stat;
  share::schema::ObTableModeFlag unused_mode;
  if (OB_NOT_NULL(stat_mgr)
      && OB_FAIL(stat_mgr->get_latest_tablet_stat(
          tablet_id, tablet_stat, unused_tablet_stat, unused_mode))) {
    if (OB_HASH_NOT_EXIST != ret) {
      STORAGE_LOG(WARN, "failed to get latest tablet stat",
          K(ret), K(tablet_id));
    }
  } else if (OB_NOT_NULL(stat_mgr)) {
    row_count_delta =
        tablet_stat.insert_row_cnt_ - tablet_stat.delete_row_cnt_;
  }
  return ret;
}

int ObAccessService::run_io_benchmark(
    common::ObIAllocator &allocator,
    int64_t &disk_random_read_speed,
    int64_t &disk_sequential_read_speed) const
{
  int ret = OB_SUCCESS;
  disk_random_read_speed = 0;
  disk_sequential_read_speed = 0;
  const int64_t load_size = 16 * 1024;
  int64_t io_count = 0;
  int64_t rt_us = 0;
  int64_t data_size = 0;
  char *read_buf = nullptr;
  ObIOInfo io_info;
  ObIOHandle io_handle;
  ObSEArray<blocksstable::ObStorageObjectHandle, 16> block_handles;

  io_info.size_ = load_size;
  io_info.buf_ = nullptr;
  io_info.flag_.set_mode(ObIOMode::READ);
  io_info.flag_.set_sys_module_id(ObIOModule::CALIBRATION_IO);
  io_info.flag_.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
  io_info.flag_.set_unlimited(true);
  io_info.timeout_us_ = MAX_IO_WAIT_TIME_MS;

  const double min_free_space_percentage = 0.1;
  const int64_t min_calibration_block_count =
      1024L * 1024L * 1024L / OB_DEFAULT_MACRO_BLOCK_SIZE;
  const int64_t max_calibration_block_count =
      20LL * 1024LL * 1024LL * 1024LL / OB_DEFAULT_MACRO_BLOCK_SIZE;
  const int64_t free_block_count =
      OB_STORAGE_OBJECT_MGR.get_free_macro_block_count();
  const int64_t total_block_count =
      OB_STORAGE_OBJECT_MGR.get_total_macro_block_count();
  int64_t benchmark_block_count = free_block_count * 0.2;
  const int64_t max_random_read_offset =
      OB_DEFAULT_MACRO_BLOCK_SIZE - load_size;

  if (free_block_count <= min_calibration_block_count
      || 1.0 * free_block_count / total_block_count
             < min_free_space_percentage) {
    ret = OB_SERVER_OUTOF_DISK_SPACE;
    STORAGE_LOG(WARN, "out of space", K(ret),
        K(free_block_count), K(total_block_count));
  } else if (OB_ISNULL(
                 read_buf = static_cast<char *>(
                     allocator.alloc(OB_DEFAULT_MACRO_BLOCK_SIZE)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "allocate read memory failed", K(ret));
  } else {
    benchmark_block_count =
        min(benchmark_block_count, max_calibration_block_count);
    benchmark_block_count =
        max(benchmark_block_count, min_calibration_block_count);
    io_info.user_data_buf_ = read_buf;
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < benchmark_block_count; ++i) {
    blocksstable::ObStorageObjectOpt object_opt;
    object_opt.set_data_macro_object_opt();
    blocksstable::ObStorageObjectHandle block_handle;
    if (OB_FAIL(OB_STORAGE_OBJECT_MGR.alloc_object(
            object_opt, block_handle))) {
    } else if (OB_FAIL(block_handles.push_back(block_handle))) {
    }
  }

  while (OB_SUCC(ret) && io_count < 100) {
    io_handle.reset();
    const int64_t block_idx =
        ObRandom::rand(benchmark_block_count / 2,
                       benchmark_block_count - 1);
    io_info.fd_.first_id_ =
        block_handles[block_idx].get_macro_id().first_id();
    io_info.fd_.second_id_ =
        block_handles[block_idx].get_macro_id().second_id();
    io_info.fd_.device_handle_ = &LOCAL_DEVICE_INSTANCE;
    io_info.offset_ = ObRandom::rand(0, max_random_read_offset);
    io_info.size_ = load_size;
    const int64_t begin_ts = ObTimeUtility::fast_current_time();
    if (OB_FAIL(OB_IO_MANAGER.read(io_info, io_handle))) {
    } else {
      ++io_count;
      rt_us += ObTimeUtility::fast_current_time() - begin_ts;
      data_size += io_handle.get_data_size();
    }
  }
  if (OB_SUCC(ret)) {
    rt_us = max(rt_us, static_cast<int64_t>(1));
    disk_random_read_speed = data_size / rt_us;
  }

  io_count = 0;
  rt_us = 0;
  data_size = 0;
  io_info.offset_ = 0;
  while (OB_SUCC(ret)
         && io_count < 100
         && io_count < benchmark_block_count / 2) {
    io_handle.reset();
    const int64_t block_idx = io_count;
    io_info.fd_.first_id_ =
        block_handles[block_idx].get_macro_id().first_id();
    io_info.fd_.second_id_ =
        block_handles[block_idx].get_macro_id().second_id();
    io_info.fd_.device_handle_ = &LOCAL_DEVICE_INSTANCE;
    io_info.size_ = OB_DEFAULT_MACRO_BLOCK_SIZE;
    const int64_t begin_ts = ObTimeUtility::fast_current_time();
    if (OB_FAIL(OB_IO_MANAGER.read(io_info, io_handle))) {
    } else {
      ++io_count;
      rt_us += ObTimeUtility::fast_current_time() - begin_ts;
      data_size += io_handle.get_data_size();
    }
  }
  if (OB_SUCC(ret)) {
    rt_us = max(rt_us, static_cast<int64_t>(1));
    disk_sequential_read_speed = data_size / rt_us;
  }
  if (OB_NOT_NULL(read_buf)) {
    allocator.free(read_buf);
  }
  return ret;
}

int ObAccessService::get_multi_ranges_cost(
    const common::ObTabletID &tablet_id,
    const int64_t timeout_us,
    const common::ObIArray<common::ObStoreRange> &ranges,
    int64_t &total_size)
{
  int ret = OB_SUCCESS;
  ObLS *tenant_ls = nullptr;
  ObLSTabletService *tablet_service = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_ERROR;
    LOG_WARN("ob access service is not running", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(ranges.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_FAIL(ls_svr_->get_ls(tenant_ls))) {
  } else if (OB_ISNULL(tablet_service = tenant_ls->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet service should not be null", K(ret));
  } else if (OB_FAIL(tablet_service->get_multi_ranges_cost(tablet_id, timeout_us, ranges, total_size))) {
  }
  return ret;
}

int ObAccessService::reuse_scan_iter(const bool switch_param, ObNewRowIterator *iter)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("access service is not initiated", K(ret));
  } else if (OB_ISNULL(iter)) {
    //do nothing
  } else if (iter->get_type() == ObNewRowIterator::ObTableScanIterator) {
    ObTableScanIterator *scan_iter = static_cast<ObTableScanIterator*>(iter);
    if (OB_LIKELY(!switch_param)) {
      scan_iter->reuse();
    } else {
      scan_iter->reset_for_switch();
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("only local das scan task can be reuse", K(ret), K(iter->get_type()));
  }
  return ret;
}

int ObAccessService::revert_scan_iter(ObNewRowIterator *iter)
{
  int ret = OB_SUCCESS;
  NG_TRACE(S_revert_iter_begin);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("access service is not initiated", K(ret));
  } else if (OB_ISNULL(iter)) {
    //do nothing
  } else if (iter->get_type() == ObNewRowIterator::ObTableScanIterator) {
    ObTableScanIterator *table_scan_iter = nullptr;
    table_scan_iter = static_cast<ObTableScanIterator *>(iter);
    share::return_server_object(table_scan_iter);
  } else {
    iter->~ObNewRowIterator();
  }
  iter = nullptr;
  NG_TRACE(S_revert_iter_end);
  return ret;
}

int ObAccessService::split_multi_ranges(
    const common::ObTabletID &tablet_id,
    const int64_t timeout_us,
    const ObIArray<ObStoreRange> &ranges,
    const int64_t expected_task_count,
    common::ObIAllocator &allocator,
    ObArrayArray<ObStoreRange> &multi_range_split_array)
{
  int ret = OB_SUCCESS;
  ObLS *tenant_ls = nullptr;
  ObLSTabletService *tablet_service = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_ERROR;
    LOG_WARN("ob access service is not running", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(ranges.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_FAIL(ls_svr_->get_ls(tenant_ls))) {
  } else if (OB_ISNULL(tablet_service = tenant_ls->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet service should not be null", K(ret));
  } else if (OB_FAIL(tablet_service->split_multi_ranges(
      tablet_id, timeout_us, ranges,
      expected_task_count, allocator, multi_range_split_array))) {
  }
  return ret;
}

int ObAccessService::inner_tablet_scan(
    const common::ObTabletID &tablet_id,
    ObTableScanParam &param,
    ObNewRowIterator *&result)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(lbt()));
  } else if (OB_FAIL(do_table_scan_(tablet_id, param, result))) {
  }
  return ret;
}

int ObAccessService::do_table_scan_(
    const common::ObTabletID &data_tablet_id,
    ObTableScanParam &param,
    ObNewRowIterator *&result)
{
  int ret = OB_SUCCESS;
  ObTableScanIterator *iter = nullptr;
  ObTabletHandle tablet_handle;
  ObStoreAccessType access_type = param.scan_flag_.is_read_latest() ?
    ObStoreAccessType::READ_LATEST : ObStoreAccessType::READ;
  SCN user_specified_snapshot_scn;
  if (ObAccessTypeCheck::is_read_access_type(access_type) && param.fb_snapshot_.is_valid()) {
    user_specified_snapshot_scn = param.fb_snapshot_;
  }
  NG_TRACE(storage_table_scan_begin);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob access service is not running.", K(ret));
  } else if (!data_tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(data_tablet_id), K(lbt()));
  } else if (OB_NOT_NULL(result)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("The result_ is already pointed to an valid object",
        K(ret), K(data_tablet_id), KPC(result), K(lbt()));
  } else if (OB_ISNULL(iter = share::borrow_server_object<ObTableScanIterator>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("alloc table scan iterator fail", K(ret));
  } else if (FALSE_IT(result = iter)) {
    // upper layer responsible for releasing iter object
  } else if (OB_FAIL(check_read_allowed_(data_tablet_id,
                                         access_type,
                                         param,
                                         tablet_handle,
                                         iter->get_ctx_guard(),
                                         user_specified_snapshot_scn))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("fail to check query allowed", K(ret), K(data_tablet_id));
    }
    // skip inner table, one key reason is to let tablet merge going
  } else if (OB_FAIL(iter->get_ctx_guard().get_ls()->get_tablet_svr()->table_scan(
                         tablet_handle, *iter, param))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("Fail to scan table, ", K(ret), K(param));
    }
  } else {
    NG_TRACE(storage_table_scan_end);
  }
  return ret;
}

int ObAccessService::scan_block_stat(ObBlockStatScanParam &scan_param, ObBlockStatIterator &iter)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!scan_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(scan_param));
  } else {
    ObTableScanParam &table_scan_param = *scan_param.get_scan_param();
    ObStoreCtxGuard &ctx_guard = iter.get_ctx_guard();
    ObTabletID tablet_id = table_scan_param.tablet_id_;
    ObTabletHandle tablet_handle;
    ObStoreAccessType access_type = table_scan_param.scan_flag_.is_read_latest() ?
        ObStoreAccessType::READ_LATEST : ObStoreAccessType::READ;
    SCN user_specified_snapshot_scn;
    if (ObAccessTypeCheck::is_read_access_type(access_type) && table_scan_param.fb_snapshot_.is_valid()) {
      user_specified_snapshot_scn = table_scan_param.fb_snapshot_;
    }
    if (OB_FAIL(check_read_allowed_(
        tablet_id,
        access_type,
        table_scan_param,
        tablet_handle,
        ctx_guard,
        user_specified_snapshot_scn))) {
      if (OB_UNLIKELY(OB_TABLET_NOT_EXIST != ret)) {
        LOG_WARN("fail to check read allowed", K(ret), K(tablet_id), K(access_type));
      }
    } else if (OB_FAIL(ctx_guard.get_ls()->get_tablet_svr()->scan_block_stat(
                           tablet_handle, scan_param, iter))) {
      if (OB_UNLIKELY(OB_TABLET_NOT_EXIST != ret)) {
        LOG_WARN("fail to scan block stat", K(ret), K(tablet_id), K(scan_param));
      }
    }
  }
  return ret;
}

}
}
