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

#include "ob_tablet_ddl_kv_mgr.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/blocksstable/ob_macro_block_common_header.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"

using namespace oceanbase::common;
using namespace oceanbase::blocksstable;
using namespace oceanbase::share;
using namespace oceanbase::storage;
using namespace oceanbase::transaction;

// Invalid ddl_kv_type means all.
bool ObDDLKVQueryParam::match_ddl_kv(const ObDDLKV &ddl_kv) const
{
  return (!is_valid_ddl_kv(ddl_kv_type_)
           || (ddl_kv_type_ == ddl_kv.get_ddl_kv_type()));
}

ObTabletDDLKvMgr::ObTabletDDLKvMgr()
  : is_inited_(false), 
    tablet_id_(),
    max_freeze_scn_(SCN::min_scn()),
    head_(0), tail_(0), lock_(), idem_checker_(), ref_cnt_(0)
{
}

ObTabletDDLKvMgr::~ObTabletDDLKvMgr()
{
  destroy();
}

void ObTabletDDLKvMgr::destroy()
{
  ObLatchWGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  ATOMIC_STORE(&ref_cnt_, 0);
  for (int64_t pos = head_; pos < tail_; ++pos) {
    const int64_t idx = get_idx(pos);
    free_ddl_kv(idx);
  }
  head_ = 0;
  tail_ = 0;
  for (int64_t i = 0; i < MAX_DDL_KV_CNT_IN_STORAGE; ++i) {
    ddl_kv_handles_[i].reset();
  }
  tablet_id_.reset();
  max_freeze_scn_.set_min();
  idem_checker_.destroy();
  is_inited_ = false;
}

int ObTabletDDLKvMgr::init(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObLSService *ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletDDLKvMgr is already inited", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("ls service should not be null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
  } else if (OB_FAIL(ls->get_ddl_log_handler()->add_tablet(tablet_id))) {
  }

  if (OB_FAIL(ret)) {
  } else {
    ObLatchWGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
    if (OB_FAIL(add_idempotence_checker_nolock())) {
    } else {
      tablet_id_ = tablet_id;
      is_inited_ = true;
    }

  }
  return ret;
}

int ObTabletDDLKvMgr::set_max_freeze_scn(const share::SCN &checkpoint_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!checkpoint_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(checkpoint_scn));
  } else {
    ObLatchWGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
    max_freeze_scn_ = checkpoint_scn;
  }
  return ret;
}

int ObTabletDDLKvMgr::get_rec_scn(SCN &rec_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  }

  // rec scn of ddl redo
  if (OB_SUCC(ret)) {
    bool has_ddl_kv = false;
    if (OB_FAIL(check_has_effective_ddl_kv(has_ddl_kv))) {
    } else if (has_ddl_kv) {
      SCN min_scn;
      if (OB_FAIL(get_ddl_kv_min_scn(min_scn))) {
      } else {
        rec_scn = SCN::min(rec_scn, min_scn);
      }
    }
  }
  return ret;
}

int ObTabletDDLKvMgr::cleanup()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObLatchWGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
    cleanup_unlock();
  }
  return ret;
}

void ObTabletDDLKvMgr::cleanup_unlock()
{
  LOG_INFO("cleanup ddl kv mgr", K(*this));
  for (int64_t pos = head_; pos < tail_; ++pos) {
    const int64_t idx = get_idx(pos);
    free_ddl_kv(idx);
  }
  head_ = 0;
  tail_ = 0;
  for (int64_t i = 0; i < MAX_DDL_KV_CNT_IN_STORAGE; ++i) {
    ddl_kv_handles_[i].reset();
  }
  max_freeze_scn_.set_min();
}


int ObTabletDDLKvMgr::rdlock(const int64_t timeout_us, uint32_t &tid)
{
  int ret = OB_SUCCESS;
  const int64_t abs_timeout_us = timeout_us + ObTimeUtility::current_time();
  if (OB_SUCC(lock_.rdlock(ObLatchIds::TABLET_DDL_KV_MGR_LOCK, abs_timeout_us))) {
    tid = static_cast<uint32_t>(GETTID());
  }
  if (OB_TIMEOUT == ret) {
    ret = OB_EAGAIN;
  }
  return ret;
}

int ObTabletDDLKvMgr::wrlock(const int64_t timeout_us, uint32_t &tid)
{
  int ret = OB_SUCCESS;
  const int64_t abs_timeout_us = timeout_us + ObTimeUtility::current_time();
  if (OB_SUCC(lock_.wrlock(ObLatchIds::TABLET_DDL_KV_MGR_LOCK, abs_timeout_us))) {
    tid = static_cast<uint32_t>(GETTID());
  }
  if (OB_TIMEOUT == ret) {
    ret = OB_EAGAIN;
  }
  return ret;
}

void ObTabletDDLKvMgr::unlock(const uint32_t tid)
{
  if (OB_SUCCESS != lock_.unlock(&tid)) {
    ob_abort();
  }
}

int64_t ObTabletDDLKvMgr::get_count()
{
  int64_t ddl_kv_count = 0;
  {
    ObLatchRGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
    ddl_kv_count = tail_ - head_;
  }
  return ddl_kv_count;
}

bool ObTabletDDLKvMgr::can_freeze()
{
  int64_t ddl_kv_count = get_count();
  return ddl_kv_count < MAX_DDL_KV_CNT_IN_STORAGE;
}

int64_t ObTabletDDLKvMgr::get_count_nolock() const
{
  return tail_ - head_;
}

int64_t ObTabletDDLKvMgr::get_idx(const int64_t pos) const
{
  return pos & (MAX_DDL_KV_CNT_IN_STORAGE - 1);
}

int ObTabletDDLKvMgr::add_idempotence_checker()
{
  int ret = OB_SUCCESS;
  ObLatchWGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  return add_idempotence_checker_nolock();
}


int ObTabletDDLKvMgr::add_idempotence_checker_nolock()
{
  int ret = OB_SUCCESS;
  if (idem_checker_.is_inited()) {
    /* skip */
  } else if (OB_FAIL(idem_checker_.init())) {
  }
  return ret;
}

int ObTabletDDLKvMgr::calc_idem_block_checksum(const ObDDLMacroBlockType block_type,
                                               const ObDirectLoadType direct_load_type,
                                               const char *buf,
                                               const int64_t buf_size,
                                               int64_t &checksum)
{
  return ObDDLMacroIdemChecker::calc_block_checksum(block_type, direct_load_type, buf, buf_size, checksum);
}
/*
* check macro block already exist in ddl kv 
* parameters check logic are set in IdemChker
*/
int ObTabletDDLKvMgr::check_idem_block_exist(const ObDDLMacroBlockType block_type,
                                             const ObDirectLoadType direct_load_type,
                                             const blocksstable::ObLogicMacroBlockId &logic_id,
                                             const int64_t checksum,
                                             const ObITable::TableType table_type,
                                             bool &is_marco_block_already_exist)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(add_idempotence_checker())) {
  } else if (OB_FAIL(idem_checker_.check_block_exist(block_type, direct_load_type, logic_id, checksum, table_type, is_marco_block_already_exist))) {
  }
  return ret;
}

int ObTabletDDLKvMgr::set_idem_block_checksum(const ObDDLMacroBlockType block_type,
                                              const ObDirectLoadType direct_load_type,
                                              const blocksstable::ObLogicMacroBlockId &logic_id,
                                              const int64_t checksum,
                                              const ObITable::TableType table_type)
{
  return idem_checker_.set_block_checksum(block_type, direct_load_type, logic_id, checksum, table_type);
}

int ObTabletDDLKvMgr::remove_idempotence_checker()
{
  int ret = OB_SUCCESS;
  ObLatchWGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  idem_checker_.destroy();
  return ret;
}

int ObTabletDDLKvMgr::get_active_ddl_kv_impl(ObDDLKVHandle &kv_handle)
{
  int ret = OB_SUCCESS;
  kv_handle.reset();
  if (get_count_nolock() == 0) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    ObDDLKVHandle &tail_kv_handle = ddl_kv_handles_[get_idx(tail_ - 1)];
    ObDDLKV *kv = tail_kv_handle.get_obj();
    if (nullptr == kv) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, kv must not be nullptr", K(ret));
    } else if (kv->is_freezed()) {
      kv = nullptr;
      ret = OB_SUCCESS;
    } else {
      kv_handle = tail_kv_handle;
    }
  }
  return ret;
}

int ObTabletDDLKvMgr::get_or_create_idem_ddl_kv(
    const share::SCN &macro_redo_scn,
    const share::SCN &macro_redo_start_scn,
    const int64_t snapshot_version,
    const uint64_t data_format_version,
    ObDDLKVHandle &kv_handle)
{
  int ret = OB_SUCCESS;
  kv_handle.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!macro_redo_scn.is_valid_and_not_min() 
                      || !macro_redo_start_scn.is_valid_and_not_min()
                      || snapshot_version <= 0
                      || data_format_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(macro_redo_scn), K(macro_redo_start_scn), K(snapshot_version), K(data_format_version));
  } else {
    uint32_t lock_tid = 0; // try lock to avoid hang in clog callback
    if (OB_FAIL(rdlock(TRY_LOCK_TIMEOUT, lock_tid))) {
    } else {
      try_get_ddl_kv_unlock(macro_redo_scn, kv_handle);
    }
    if (lock_tid != 0) {
      unlock(lock_tid);
    }
  }
  if (OB_SUCC(ret) && !kv_handle.is_valid()) {
    uint32_t lock_tid = 0; // try lock to avoid hang in clog callback
    if (OB_FAIL(wrlock(TRY_LOCK_TIMEOUT, lock_tid))) {
    } else {
      try_get_ddl_kv_unlock(macro_redo_scn, kv_handle);
      if (kv_handle.is_valid()) {
        // do nothing
      } else if (OB_FAIL(alloc_ddl_kv(macro_redo_start_scn, snapshot_version, data_format_version, kv_handle, ObDDLKVType::DDL_KV_FULL))) {
      }
    }
    if (lock_tid != 0) {
      unlock(lock_tid);
    }
  }
  return ret;
}

void ObTabletDDLKvMgr::try_get_ddl_kv_unlock(const SCN &scn, ObDDLKVHandle &kv_handle)
{
  int ret = OB_SUCCESS;
  kv_handle.reset();
  if (get_count_nolock() > 0) {
    for (int64_t i = tail_ - 1; OB_SUCC(ret) && i >= head_ && !kv_handle.is_valid(); --i) {
      ObDDLKVHandle &tmp_kv_handle = ddl_kv_handles_[get_idx(i)];
      ObDDLKV *tmp_kv = tmp_kv_handle.get_obj();
      if (OB_ISNULL(tmp_kv)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl kv is null", K(ret), K(tablet_id_), KP(tmp_kv), K(i), K(head_), K(tail_));
      } else if (scn > tmp_kv->get_start_scn() && scn <= tmp_kv->get_freeze_scn()) {
        kv_handle = tmp_kv_handle;
        break;
      }
    }
  }
}

int ObTabletDDLKvMgr::freeze_ddl_kv(
    const share::SCN &start_scn,
    const int64_t snapshot_version,
    const uint64_t data_format_version,
    const SCN &freeze_scn,
    const ObDDLKVType ddl_kv_type)
{
  int ret = OB_SUCCESS;
  ObDDLKVHandle kv_handle;
  ObLatchWGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_full_ddl_kv(ddl_kv_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only support full ddl kv", K(ret), K(ddl_kv_type));
  } else if (0 == get_count_nolock()) {
    // do nothing
  } else if (OB_FAIL(get_active_ddl_kv_impl(kv_handle))) {
  }
  if (OB_SUCC(ret) && !kv_handle.is_valid() && freeze_scn > max_freeze_scn_) {
    // freeze_scn > 0 only occured when ddl commit
    // assure there is an alive ddl kv, for waiting pre-logs
    if (OB_FAIL(alloc_ddl_kv(start_scn, snapshot_version, data_format_version, kv_handle, ddl_kv_type))) {
    }
  }
  if (OB_SUCC(ret) && kv_handle.is_valid()) {
    ObDDLKV *kv = kv_handle.get_obj();
    if (OB_ISNULL(kv)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl kv is null", K(ret), KP(kv), K(kv_handle));
    } else if (OB_FAIL(kv->freeze(freeze_scn))) {
      if (OB_EAGAIN != ret) {
        LOG_ERROR("fail to freeze active ddl kv", K(ret));
      }
    } else {
      max_freeze_scn_ = SCN::max(max_freeze_scn_, kv->get_freeze_scn());
      FLOG_INFO("freeze ddl kv", K(max_freeze_scn_), "kv", *kv);
    }
  }
  return ret;
}

int ObTabletDDLKvMgr::release_ddl_kvs(const ObDDLKVType ddl_kv_type, const SCN &end_scn)
{
  int ret = OB_SUCCESS;
  DEBUG_SYNC(BEFORE_RELEASE_DDL_KV);
  ObLatchWGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else {
    for (int64_t i = head_; OB_SUCC(ret) && i < tail_; ++i) {
      const int64_t idx = get_idx(head_);
      ObDDLKV *kv = ddl_kv_handles_[idx].get_obj();
      LOG_INFO("try release ddl kv", K(end_scn), KPC(kv));
#ifdef ERRSIM
          if (OB_SUCC(ret)) {
            ret = OB_E(EventTable::EN_DDL_RELEASE_DDL_KV_FAIL) OB_SUCCESS;
            if (OB_FAIL(ret)) {
              LOG_WARN("errsim release ddl kv failed", KR(ret));
            }
          }
#endif
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(kv)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl kv is null", K(ret), K(tablet_id_), KP(kv), K(i), K(head_), K(tail_));
      } else if (kv->is_closed() && kv->get_freeze_scn() <= end_scn && kv->get_ddl_kv_type() == ddl_kv_type) {
        const SCN &freeze_scn = kv->get_freeze_scn();
        free_ddl_kv(idx);
        ++head_;
        LOG_INFO("succeed to release ddl kv", K(tablet_id_), K(freeze_scn));
      }
    }
  }
  return ret;
}

int ObTabletDDLKvMgr::get_ddl_kv_min_scn(SCN &min_scn)
{
  int ret = OB_SUCCESS;
  ObLatchRGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  min_scn = SCN::max_scn();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else {
    for (int64_t i = head_; OB_SUCC(ret) && i < tail_; ++i) {
      const int64_t idx = get_idx(head_);
      ObDDLKV *kv = ddl_kv_handles_[idx].get_obj();
      if (OB_ISNULL(kv)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl kv is null", K(ret), K(tablet_id_), KP(kv), K(i), K(head_), K(tail_));
      } else {
        min_scn = SCN::min(min_scn, kv->get_min_scn());
      }
    }
  }
  return ret;
}

int ObTabletDDLKvMgr::get_ddl_kvs_unlock(
    const bool frozen_only,
    ObIArray<ObDDLKVHandle> &kv_handle_array,
    const ObDDLKVQueryParam &ddl_kv_query_param)
{
  int ret = OB_SUCCESS;
  kv_handle_array.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else {
    for (int64_t pos = head_; OB_SUCC(ret) && pos < tail_; ++pos) {
      const int64_t idx = get_idx(pos);
      ObDDLKVHandle &cur_kv_handle = ddl_kv_handles_[idx];
      ObDDLKV *cur_kv = cur_kv_handle.get_obj();
      if (OB_ISNULL(cur_kv)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl kv is null", K(ret), K(tablet_id_), KP(cur_kv), K(pos), K(head_), K(tail_));
      } else if ((!frozen_only || cur_kv->is_freezed())
                 && ddl_kv_query_param.match_ddl_kv(*cur_kv)) {
        if (OB_FAIL(kv_handle_array.push_back(cur_kv_handle))) {
        }
      }
    }
  }
  return ret;
}

int ObTabletDDLKvMgr::get_ddl_kvs(
    const bool frozen_only,
    ObIArray<ObDDLKVHandle> &kv_handle_array,
    const ObDDLKVQueryParam &ddl_kv_query_param)
{
  int ret = OB_SUCCESS;
  kv_handle_array.reset();
  ObLatchRGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else if (OB_FAIL(get_ddl_kvs_unlock(frozen_only,
                                        kv_handle_array,
                                        ddl_kv_query_param))) {
  }
  return ret;
}

int ObTabletDDLKvMgr::get_ddl_kvs_for_query(ObTablet &tablet, ObIArray<ObDDLKVHandle> &kv_handle_array)
{
  int ret = OB_SUCCESS;
  kv_handle_array.reset();
  ObLatchRGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else if (OB_FAIL(get_ddl_kvs_unlock(true/*frozen_only*/, kv_handle_array))) {
  }
  return ret;
}

int ObTabletDDLKvMgr::check_has_effective_ddl_kv(bool &has_ddl_kv)
{
  int ret = OB_SUCCESS;
  ObLatchRGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else {
    has_ddl_kv = 0 != get_count_nolock();
  }
  return ret;
}

int ObTabletDDLKvMgr::check_has_freezed_ddl_kv(bool &has_freezed_ddl_kv)
{
  int ret = OB_SUCCESS;
  has_freezed_ddl_kv = false;
  ObLatchRGuard guard(lock_, ObLatchIds::TABLET_DDL_KV_MGR_LOCK);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else {
    for (int64_t pos = head_; !has_freezed_ddl_kv && OB_SUCC(ret) && pos < tail_; ++pos) {
      const int64_t idx = get_idx(pos);
      ObDDLKVHandle &cur_kv_handle = ddl_kv_handles_[idx];
      ObDDLKV *cur_kv = cur_kv_handle.get_obj();
      if (OB_ISNULL(cur_kv)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl kv is null", K(ret), K(tablet_id_), KP(cur_kv), K(pos), K(head_), K(tail_));
      } else if (cur_kv->is_freezed()) {
        has_freezed_ddl_kv = true;
      }
    }
  }
  return ret;
}

int ObTabletDDLKvMgr::alloc_ddl_kv(
    const share::SCN &start_scn,
    const int64_t snapshot_version,
    const uint64_t data_format_version,
    ObDDLKVHandle &kv_handle,
    const ObDDLKVType ddl_kv_type)
{
  int ret = OB_SUCCESS;
  kv_handle.reset();
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  ObDDLKVHandle tmp_kv_handle;
  ObDDLKV *kv = nullptr;
  ObDDLMemtable *ddl_memtable = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ddl kv manager not init", K(ret));
  } else if (OB_UNLIKELY(!(storage::is_full_ddl_kv(ddl_kv_type)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only support full ddl kv", KR(ret), K(ddl_kv_type));
  } else if (OB_FAIL(handle_ddl_kv_queue_overflow(ddl_kv_type))) {
    if (OB_EAGAIN == ret) {
      if (REACH_TIME_INTERVAL(10 * 1000 * 1000L)) { // 10s
        LOG_INFO("too much ddl kv count, need retry", KR(ret), K(ddl_kv_type));
      }
    } else {
      LOG_WARN("error unexpected, too much ddl kv count", KR(ret), K(ddl_kv_type));
    }
  } else if (OB_FAIL(t3m->acquire_ddl_kv(tmp_kv_handle))) {
  } else if (OB_ISNULL(kv = tmp_kv_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl kv is null", K(ret));
  } else if (OB_FAIL(kv->init(tablet_id_,
                              start_scn,
                              snapshot_version,
                              max_freeze_scn_,
                              data_format_version,
                              ddl_kv_type))) {
  } else {
    const int64_t idx = get_idx(tail_);
    tail_++;
    ddl_kv_handles_[idx] = tmp_kv_handle;
    kv_handle = tmp_kv_handle;
    FLOG_INFO("succeed to add ddl kv", K(tablet_id_), K(head_), K(tail_), K(max_freeze_scn_), "ddl_kv_cnt", get_count_nolock(), KP(kv));
  }
  return ret;
}

void ObTabletDDLKvMgr::set_ddl_kv(const int64_t idx, ObDDLKVHandle &kv_handle)
{
  //only for unittest
  ddl_kv_handles_[idx] = kv_handle;
  tail_++;
}

void ObTabletDDLKvMgr::free_ddl_kv(const int64_t idx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletDDLKvMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(idx < 0 || idx >= MAX_DDL_KV_CNT_IN_STORAGE)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(idx));
  } else {
    FLOG_INFO("free ddl kv", K(tablet_id_), KPC(ddl_kv_handles_[idx].get_obj()));
    ddl_kv_handles_[idx].reset();
  }
}

int ObTabletDDLKvMgr::handle_ddl_kv_queue_overflow(const ObDDLKVType ddl_kv_type)
{
  int ret = OB_SUCCESS;
  if (get_count_nolock() == MAX_DDL_KV_CNT_IN_STORAGE) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

ObDDLIdemKey::ObDDLIdemKey():
logic_block_id_(), table_type_(ObITable::TableType::MAX_TABLE_TYPE)
{}

ObDDLIdemKey::~ObDDLIdemKey()
{}

ObDDLIdemKey::ObDDLIdemKey(const ObDDLIdemKey &other)
{
  logic_block_id_ = other.logic_block_id_;
  table_type_ = other.table_type_;
}


int ObDDLIdemKey::init(const ObLogicMacroBlockId &logic_block_id,
                       const ObITable::TableType table_type)
{
  int ret = OB_SUCCESS;
  table_type_ = table_type;
  if (!logic_block_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid logic block id", K(ret), K(logic_block_id));
  } else {
    logic_block_id_ = logic_block_id;
  }
  return ret;
}

uint64_t ObDDLIdemKey::hash() const
{
  uint64_t hash_val = 0;
  uint64_t idem_type = table_type_;
  hash_val = logic_block_id_.hash();
  hash_val = murmurhash(&idem_type, sizeof(table_type_), hash_val);
  return hash_val;
}

int ObDDLIdemKey::hash(uint64_t &hash_val) const
{
  int ret = OB_SUCCESS;
  hash_val = hash();
  return ret;
}

bool ObDDLIdemKey::operator==(const ObDDLIdemKey &other) const
{
  bool ret = false;
  ret = logic_block_id_ == other.logic_block_id_ && table_type_ == other.table_type_;
  return ret;
}

ObDDLIdemKey& ObDDLIdemKey::operator=(const ObDDLIdemKey &other)
{
  logic_block_id_ = other.logic_block_id_;
  table_type_ = other.table_type_;
  return *this;
}

ObDDLMacroIdemChecker::ObDDLMacroIdemChecker():
 checksum_map_(), allocator_(ObMemAttr("DDL_IDEM_CHECK"))
{}

ObDDLMacroIdemChecker::~ObDDLMacroIdemChecker()
{
  destroy();
}

int ObDDLMacroIdemChecker::init()
{
  int ret = OB_SUCCESS;
  if (is_inited()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("idem chekcer has already been inited", K(ret));
  } else if (OB_FAIL(checksum_map_.create(997, ObMemAttr("idem_checker")))) {
  }
  return ret;
}

bool ObDDLMacroIdemChecker::is_inited()
{
  return checksum_map_.created();
}

bool ObDDLMacroIdemChecker::need_check_block_checksum(const ObDirectLoadType direct_load_type)
{
  return is_idem_type(direct_load_type);
}

int ObDDLMacroIdemChecker::calc_block_checksum(const ObDDLMacroBlockType block_type, 
                                               const ObDirectLoadType direct_load_type,
                                               const char *buf, 
                                               const int64_t buf_size, 
                                               int64_t &checksum)
{
  int ret = OB_SUCCESS;
  checksum = 0;
  if (!ObDDLMacroIdemChecker::need_check_block_checksum(direct_load_type)) {
    checksum = 0;
  } else {
    if (nullptr == buf || buf_size <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid value, buf & buf_size should not be empty", K(ret), K(block_type), K(direct_load_type), KP(buf), K(buf_size));
    } else if (ObDDLMacroBlockType::DDL_MB_DATA_TYPE == block_type || ObDDLMacroBlockType::DDL_MB_INDEX_TYPE == block_type) {
      const ObMacroBlockCommonHeader *common_header = reinterpret_cast<const ObMacroBlockCommonHeader *>(buf);
      if (OB_FAIL(common_header->check_integrity())) {
      } else {
        checksum = common_header->get_payload_checksum();
      }
    } else {
      checksum = ob_crc64(buf, buf_size);
    }
  }
  return ret;
}


int ObDDLMacroIdemChecker::check_block_exist(const ObDDLMacroBlockType block_type, 
                                             const ObDirectLoadType direct_load_type,
                                             const blocksstable::ObLogicMacroBlockId &logic_id,
                                             const int64_t checksum,
                                             ObITable::TableType table_type,
                                             bool &is_marco_block_already_exist)
{
  int ret = OB_SUCCESS;
  ObDDLIdemKey key;
  is_marco_block_already_exist = false;
  if (!ObDDLMacroIdemChecker::need_check_block_checksum(direct_load_type)) {
    /* skip */
  } else if (OB_FAIL(key.init(logic_id, table_type))) {
  } else if (!checksum_map_.created())  {
    ret = OB_TASK_EXPIRED;
    LOG_ERROR("macro block checksum map not created", K(ret), K(checksum_map_.created()));
  } else {
    int64_t prev_checksum = 0;
    if (OB_FAIL(checksum_map_.get_refactored(key, prev_checksum))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to get refactored", K(ret), K(logic_id));
      }
    } else if (prev_checksum == checksum) {
      is_marco_block_already_exist = true;
      LOG_INFO("macro block already exist, skip replay it", K(logic_id), K(checksum));
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("checksum not match", K(ret), K(logic_id), K(prev_checksum), K(checksum));
    }
  }
  return ret;
}

int ObDDLMacroIdemChecker::set_block_checksum(const ObDDLMacroBlockType block_type, 
                                              const ObDirectLoadType direct_load_type,
                                              const blocksstable::ObLogicMacroBlockId &logic_id,
                                              const int64_t checksum,
                                              const ObITable::TableType table_type)

{
  int ret = OB_SUCCESS;
  ObDDLIdemKey key;
  bool is_block_exist = false;
  /* check block exist */
  if (!ObDDLMacroIdemChecker::need_check_block_checksum(direct_load_type)) {
    /* skip */
  } else if (OB_FAIL(check_block_exist(block_type, direct_load_type, logic_id, checksum, table_type, is_block_exist))) {
  } else if (is_block_exist) {
    LOG_INFO("block already exist, skip set checksum", K(logic_id), K(checksum), K(table_type));
  } else if (OB_FAIL(key.init(logic_id, table_type))) {
  } else if (OB_FAIL(checksum_map_.set_refactored(key, checksum))) {
  }
  return ret;
}
void ObDDLMacroIdemChecker::destroy()
{
  if (is_inited()) {
    checksum_map_.destroy();
  }
}
