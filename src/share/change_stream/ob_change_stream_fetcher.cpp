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

#define USING_LOG_PREFIX SHARE
#include "lib/oblog/ob_log_module.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/thread/ob_thread_name.h"
#include "lib/utility/serialization.h"
#include "share/change_stream/ob_change_stream_fetcher.h"
#include "share/change_stream/ob_change_stream_mgr.h"
#include "share/ob_global_stat_proxy.h"
#include "storage/tx/ob_tx_log.h"
#include "storage/tx/ob_multi_data_source.h"
#include "storage/memtable/ob_memtable_mutator.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_service.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/vector_index/ob_vector_index_util.h"
#include "logservice/ob_log_base_header.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_ls_handle.h"
#include "logservice/ob_log_handler.h"
#include "logservice/palf/log_io_context.h"
#include "share/rc/ob_tenant_base.h"
#include "storage/tx/ob_ts_mgr.h"

namespace oceanbase
{
namespace share
{

ObCSFetcher::ObCSFetcher()
  : share::ObThreadPool(1),
    is_inited_(false),
    dispatcher_(nullptr),
    ls_id_(ObLSID::SYS_LS_ID),
    iter_(),
    current_lsn_(),
    current_scn_(),
    current_schema_version_(0),
    total_tx_committed_(0),
    running_mode_(IDLE),
    has_async_index_tables_(false),
    last_checked_schema_version_(0),
    current_processing_tx_id_()
{
}

ObCSFetcher::~ObCSFetcher()
{
  destroy();
}

int ObCSFetcher::init(ObCSDispatcher *dispatcher)
{
  int ret = common::OB_SUCCESS;
  if (is_inited_) {
    ret = common::OB_INIT_TWICE;
  } else if (OB_ISNULL(dispatcher)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("CSFetcher: dispatcher is null", KR(ret));
  } else if (OB_FAIL(tx_info_.create(CS_FETCHER_TX_INFO_BUCKET_CNT, "CSFetcherTx"))) {
    LOG_WARN("CSFetcher: fail to create tx_info map", KR(ret));
  } else if (FALSE_IT(ObThreadPool::set_run_wrapper(MTL_CTX()))) {
  } else if (OB_FAIL(ObThreadPool::init())) {
    LOG_WARN("CSFetcher: fail to init thread pool", KR(ret));
  } else {
    dispatcher_ = dispatcher;
    current_scn_.set_min();
    total_tx_committed_ = 0;
    is_inited_ = true;
  }
  return ret;
}

int ObCSFetcher::init_consumption_position_()
{
  int ret = common::OB_SUCCESS;
  int64_t persisted_min_dep_lsn = 0;
  palf::LSN start_lsn;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("CSFetcher: sql_proxy is null", KR(ret));
  } else if (OB_FAIL(ObGlobalStatProxy::get_change_stream_min_dep_lsn(
                 *GCTX.sql_proxy_, MTL_ID(), false, persisted_min_dep_lsn))) {
    LOG_WARN("CSFetcher: fail to load change_stream_min_dep_lsn", KR(ret));
  } else {
    start_lsn = palf::LSN(persisted_min_dep_lsn);
    if (current_lsn_.is_valid() && start_lsn < current_lsn_) {
      start_lsn = current_lsn_;
    }
    if (OB_FAIL(logservice::seek_log_iterator(ls_id_, start_lsn, iter_))) {
      LOG_WARN("CSFetcher: fail to seek_log_iterator by min_dep_lsn", KR(ret), K(start_lsn));
    } else {
      current_lsn_ = start_lsn;
      FLOG_INFO("CSFetcher: initialized from global_stat min_dep_lsn", K(start_lsn));
    }
  }
  if (OB_SUCC(ret)) {
    palf::LogIOContext io_ctx(MTL_ID(), ls_id_.id(), palf::LogIOUser::CDC);
    if (OB_FAIL(iter_.set_io_context(io_ctx))) {
      LOG_WARN("CSFetcher: fail to set_io_context", KR(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(GCTX.schema_service_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("CSFetcher: schema_service is null", KR(ret));
    } else if (0 == persisted_min_dep_lsn) {
      current_scn_.set_min();
      LOG_INFO("CSFetcher: min_dep_lsn is 0, schema_version stays 0");
    } else {
      int64_t timestamp_us = 0;
      palf::LogEntry peek_entry;
      palf::LSN peek_lsn;
      if (OB_FAIL(iter_.next())) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          current_scn_.set_min();
          LOG_INFO("CSFetcher: no logs at min_dep_lsn, schema_version stays 0");
        } else {
          LOG_WARN("CSFetcher: iter_.next() failed", KR(ret));
        }
      } else if (OB_FAIL(iter_.get_entry(peek_entry, peek_lsn))) {
        LOG_WARN("CSFetcher: fail to get_entry for schema init", KR(ret));
      } else {
        current_scn_ = peek_entry.get_scn();
        timestamp_us = current_scn_.convert_to_ts(true /* ignore_invalid */);
        if (timestamp_us <= 0) {
          LOG_WARN("CSFetcher: scn convert_to_ts invalid, schema_version stays 0");
        } else {
          schema::ObRefreshSchemaStatus schema_status;
          schema_status.tenant_id_ = MTL_ID();
          if (OB_FAIL(GCTX.schema_service_->get_schema_version_by_timestamp(
                          schema_status, MTL_ID(), timestamp_us, current_schema_version_))) {
            LOG_WARN("CSFetcher: get_schema_version_by_timestamp failed", KR(ret), K(timestamp_us));
          } else if (current_schema_version_ <= 0 || !ObSchemaService::is_formal_version(current_schema_version_)) {
            ret = OB_SCHEMA_EAGAIN;
            LOG_WARN("CSFetcher: schema version not formal", KR(ret), K(current_schema_version_));
          } else {
            LOG_INFO("CSFetcher: schema version initialized by SCN", K(current_schema_version_));
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(logservice::seek_log_iterator(ls_id_, start_lsn, iter_))) {
          LOG_WARN("CSFetcher: fail to seek back after schema init", KR(ret));
        }
      }
    }
  }
  return ret;
}

int ObCSFetcher::start()
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_) {
    ret = common::OB_NOT_INIT;
  } else if (OB_FAIL(ObThreadPool::start())) {
    LOG_WARN("CSFetcher: fail to start thread pool", KR(ret));
  }
  return ret;
}

void ObCSFetcher::stop()
{
  ObThreadPool::stop();
}

void ObCSFetcher::wait()
{
  ObThreadPool::wait();
}

void ObCSFetcher::destroy()
{
  if (is_inited_) {
    stop();
    wait();
    // Clean up any remaining in-flight transactions.
    for (common::hash::ObHashMap<int64_t, ObCSTxInfo *>::iterator it = tx_info_.begin();
         it != tx_info_.end(); ++it) {
      if (OB_NOT_NULL(it->second)) {
        it->second->destroy();
        OB_DELETE(ObCSTxInfo, "CSTxInfo", it->second);
      }
    }
    tx_info_.destroy();
    dispatcher_ = nullptr;
    current_lsn_.reset();
    current_scn_.reset();
    is_inited_ = false;
  }
}

void ObCSTxInfo::destroy()
{
  for (int64_t i = 0; i < redo_list_.count(); ++i) {
    if (redo_list_.at(i).redo_buf_ != nullptr) {
      common::ob_free(redo_list_.at(i).redo_buf_);
      redo_list_.at(i).redo_buf_ = nullptr;
    }
  }
  redo_list_.reset();
  reset();
}

// ---------------------------------------------------------------------------
// get_min_dep_lsn: get end_lsn first, then real-time async-index check.
//   With async-index tables: min(tx_info_.start_lsn) if non-empty, else current_lsn_.
//   Without async-index tables: end_lsn — allow PALF to reclaim up to the log tail.
//   On failure returns error code and caller should not advance.
// ---------------------------------------------------------------------------

int ObCSFetcher::get_min_dep_lsn(palf::LSN &min_lsn)
{
  int ret = OB_SUCCESS;
  min_lsn.reset();

  palf::LSN end_lsn;
  storage::ObLSHandle tmp_handle;
  storage::ObLS *ls = nullptr;
  logservice::ObLogHandler *log_handler = nullptr;
  if (OB_FAIL(MTL(storage::ObLSService*)->get_ls(ls_id_, tmp_handle, storage::ObLSGetMod::LOG_MOD))
      || OB_ISNULL(ls = tmp_handle.get_ls())
      || OB_ISNULL(log_handler = ls->get_log_handler())
      || OB_FAIL(log_handler->get_end_lsn(end_lsn))) {
    LOG_WARN("CSFetcher: fail to get end_lsn for min_dep_lsn", KR(ret), K(ls_id_));
    return ret;
  }

  bool has_async = false;
  if (OB_FAIL(get_has_async_cached_(has_async))) {
    LOG_WARN("CSFetcher: get_has_async_cached_ failed in get_min_dep_lsn", KR(ret));
    return ret;
  }

  if (!has_async) {
    min_lsn = end_lsn;
    return OB_SUCCESS;
  }

  // Has async-index tables: retain logs up to the earliest in-flight transaction.
  if (!tx_info_.empty()) {
    palf::LSN tx_min;
    tx_min.reset();
    for (common::hash::ObHashMap<int64_t, ObCSTxInfo *>::const_iterator it = tx_info_.begin();
         it != tx_info_.end(); ++it) {
      if (OB_NOT_NULL(it->second) && it->second->start_lsn_.is_valid()) {
        if (!tx_min.is_valid() || it->second->start_lsn_ < tx_min) {
          tx_min = it->second->start_lsn_;
        }
      }
    }
    min_lsn = tx_min.is_valid() ? tx_min : current_lsn_;
  } else {
    min_lsn = current_lsn_;
  }
  return OB_SUCCESS;
}

// ---------------------------------------------------------------------------
// get_refresh_scn: get GTS, then decide refresh_scn based on async-index state:
//   1. !has_async: return GTS — no async vector index tables.
//   2. has_async && tx_info_ not empty: return OB_SUCCESS with invalid refresh_scn — worker handles.
//   3. has_async && current_lsn_ >= max_lsn: return GTS — no pending logs to consume.
//   4. has_async && current_lsn_ < max_lsn: return current_scn_ — still consuming logs.
// ---------------------------------------------------------------------------
int ObCSFetcher::get_refresh_scn(SCN &refresh_scn)
{
  int ret = OB_SUCCESS;
  refresh_scn.reset();

  SCN gts_scn;
  if (OB_FAIL(OB_TS_MGR.get_gts(MTL_ID(), NULL, gts_scn))) {
    LOG_WARN("CSFetcher: fail to get GTS for refresh_scn", KR(ret));
    return ret;
  }

  bool has_async = false;
  if (OB_FAIL(get_has_async_cached_(has_async))) {
    return ret;
  }

  if (!has_async) {
    // Case 1: no async vector index tables — advance to GTS.
    refresh_scn = gts_scn;
    return ret;
  }

  // Case 2: in-flight tx — worker will advance refresh_scn after draining; skip here.
  if (!tx_info_.empty()) {
    return OB_SUCCESS;
  }

  // Fetch max_lsn to distinguish case 3 and case 4.
  palf::LSN max_lsn;
  {
    storage::ObLSHandle tmp_handle;
    storage::ObLS *ls = nullptr;
    logservice::ObLogHandler *log_handler = nullptr;
    if (OB_FAIL(MTL(storage::ObLSService*)->get_ls(ls_id_, tmp_handle, storage::ObLSGetMod::LOG_MOD))
        || OB_ISNULL(ls = tmp_handle.get_ls())
        || OB_ISNULL(log_handler = ls->get_log_handler())
        || OB_FAIL(log_handler->get_max_lsn(max_lsn))) {
      LOG_WARN("CSFetcher: fail to get max_lsn in get_refresh_scn", KR(ret), K(ls_id_));
      return ret;
    }
  }

  if (current_lsn_ >= max_lsn) {
    // Case 3: caught up — no pending logs, advance to GTS.
    SCN gts_scn;
    if (OB_FAIL(OB_TS_MGR.get_gts(MTL_ID(), NULL, gts_scn))) {
      LOG_WARN("CSFetcher: fail to get GTS for refresh_scn (caught-up)", KR(ret));
    } else {
      refresh_scn = gts_scn;
    }
  } else {
    // Case 4: still consuming logs — advance only to current_scn_.
    refresh_scn = current_scn_;
  }
  return ret;
}

// get_has_async_cached_: use last_checked_schema_version_ / has_async_index_tables_; refresh cache when schema version changed.
int ObCSFetcher::get_has_async_cached_(bool &has_async)
{
  int ret = OB_SUCCESS;
  has_async = false;
  int64_t refreshed_version = 0;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("CSFetcher: schema_service is null", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_refreshed_schema_version(MTL_ID(), refreshed_version))) {
    LOG_WARN("CSFetcher: get_tenant_refreshed_schema_version failed", KR(ret));
  } else if (refreshed_version == last_checked_schema_version_) {
    has_async = has_async_index_tables_;
  } else {
    if (OB_FAIL(check_has_async_index_tables_(has_async))) {
      LOG_WARN("CSFetcher: check_has_async_index_tables_ failed in get_has_async_cached_", KR(ret));
    } else {
      last_checked_schema_version_ = refreshed_version;
      has_async_index_tables_ = has_async;
    }
  }
  return ret;
}

// check_has_async_index_tables_: uses get_tenant_schema_guard_with_version_in_inner_table
// so that OB_SCHEMA_EAGAIN is handled (refresh + retry). On success sets has_async to true
// only if at least one vector index has sync_mode=ASYNC in index_params.
int ObCSFetcher::check_has_async_index_tables_(bool &has_async)
{
  int ret = OB_SUCCESS;
  has_async = false;
  if (OB_ISNULL(GCTX.schema_service_) || OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("CSFetcher: schema_service or sql_proxy is null", KR(ret));
  } else {
    schema::ObSchemaGetterGuard guard;
    if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard_with_version_in_inner_table(MTL_ID(), guard))) {
      LOG_WARN("CSFetcher: fail to get_tenant_schema_guard_with_version_in_inner_table", KR(ret));
    } else {
      bool has_ivf_index = false;
      common::ObArray<uint64_t> table_ids;
      if (OB_FAIL(guard.get_vector_info_index_ids_in_tenant(MTL_ID(), has_ivf_index, table_ids))) {
        LOG_WARN("CSFetcher: fail to get_vector_info_index_ids_in_tenant", KR(ret));
      } else {
        for (int64_t i = 0; !has_async && i < table_ids.count(); ++i) {
          const schema::ObTableSchema *table_schema = nullptr;
          if (OB_FAIL(guard.get_table_schema(MTL_ID(), table_ids.at(i), table_schema))) {
            LOG_WARN("CSFetcher: fail to get_table_schema for vector index", KR(ret), K(table_ids.at(i)));
          } else if (OB_ISNULL(table_schema) || !table_schema->is_vec_index()) {
            continue;
          } else {
            const common::ObString &index_params = table_schema->get_index_params();
            if (index_params.empty()) {
              continue;
            }
            share::ObVectorIndexType index_type = share::ObVectorIndexType::VIT_MAX;
            if (table_schema->is_vec_ivf_index()) {
              index_type = share::ObVectorIndexType::VIT_IVF_INDEX;
            } else if (table_schema->is_vec_hnsw_index()) {
              index_type = share::ObVectorIndexType::VIT_HNSW_INDEX;
            } else if (table_schema->is_vec_spiv_index()) {
              index_type = share::ObVectorIndexType::VIT_SPIV_INDEX;
            }
            if (index_type != share::ObVectorIndexType::VIT_MAX) {
              share::ObVectorIndexParam param;
              if (OB_FAIL(share::ObVectorIndexUtil::parser_params_from_string(
                              index_params, index_type, param, false))) {
                LOG_WARN("CSFetcher: fail to parser_params_from_string", KR(ret), K(table_ids.at(i)));
              } else if (param.sync_mode_async_) {
                has_async = true;
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

transaction::ObTransID ObCSFetcher::get_current_processing_tx_id() const
{
  return current_processing_tx_id_;
}

int64_t ObCSFetcher::get_current_processing_tx_count() const
{
  return tx_info_.size();
}

palf::LSN ObCSFetcher::get_current_lsn() const
{
  return current_lsn_;
}

SCN ObCSFetcher::get_current_scn() const
{
  return current_scn_;
}

void ObCSFetcher::try_advance_min_dep_lsn_()
{
  if (!REACH_TIME_INTERVAL(CS_FETCHER_MIN_DEP_LSN_ADVANCE_INTERVAL_US)
      || OB_ISNULL(GCTX.sql_proxy_)) {
    return;
  }
  int ret = OB_SUCCESS;
  palf::LSN min_lsn;
  if (OB_FAIL(get_min_dep_lsn(min_lsn))) {
    LOG_WARN("CSFetcher: get_min_dep_lsn failed, skip advance this round", KR(ret));
    return;
  }
  if (min_lsn.is_valid()) {
    int64_t affected = 0;
    if (OB_FAIL(ObGlobalStatProxy::advance_change_stream_min_dep_lsn(
                    *GCTX.sql_proxy_, MTL_ID(), static_cast<int64_t>(min_lsn.val_), affected))) {
      LOG_WARN("CSFetcher: fail to advance_change_stream_min_dep_lsn", KR(ret), K(min_lsn));
    } else {
      LOG_INFO("CSFetcher: min_dep_lsn advanced",
               "mode", running_mode_ == ACTIVE ? "ACTIVE" : "IDLE",
               K(min_lsn), K(current_lsn_), "inflight_tx_count", tx_info_.size());
    }
  }
}

void ObCSFetcher::try_advance_refresh_scn_()
{
  if (!REACH_TIME_INTERVAL(CS_FETCHER_REFRESH_SCN_ADVANCE_INTERVAL_US)
      || OB_ISNULL(GCTX.sql_proxy_)) {
    return;
  }
  int ret = OB_SUCCESS;
  SCN refresh_scn;
  if (OB_FAIL(get_refresh_scn(refresh_scn))) {
    LOG_WARN("CSFetcher: get_refresh_scn failed, skip advance this round", KR(ret));
    return;
  }
  if (refresh_scn.is_valid()) {
    int64_t affected = 0;
    if (OB_FAIL(ObGlobalStatProxy::advance_change_stream_refresh_scn(
                    *GCTX.sql_proxy_, MTL_ID(), refresh_scn, affected))) {
      LOG_WARN("CSFetcher: fail to advance_change_stream_refresh_scn", KR(ret), K(refresh_scn));
    } else if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {
      LOG_INFO("CSFetcher: refresh_scn advanced",
               "mode", running_mode_ == ACTIVE ? "ACTIVE" : "IDLE",
               K(refresh_scn), "inflight_tx_count", tx_info_.size());
    }
  }
}

int ObCSFetcher::release_committed_tx(int64_t tx_id)
{
  int ret = common::OB_SUCCESS;
  ObCSTxInfo *tx = nullptr;
  if (OB_FAIL(tx_info_.erase_refactored(tx_id, &tx))) {
    LOG_WARN("CSFetcher: fail to release_committed_tx erase", KR(ret), K(tx_id));
  } else if (OB_NOT_NULL(tx)) {
    tx->destroy();
    OB_DELETE(ObCSTxInfo, "CSTxInfo", tx);
  }
  return ret;
}

// ---------------------------------------------------------------------------
// extract_ddl_schema_version_: scan DDL tx redo for __all_ddl_operation rows,
// extract schema_version from the rowkey (single int64 column).
// ---------------------------------------------------------------------------
int ObCSFetcher::extract_ddl_schema_version_(ObCSTxInfo *tx, int64_t &schema_version)
{
  int ret = common::OB_SUCCESS;
  schema_version = 0;
  if (OB_ISNULL(tx)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("CSFetcher: extract_ddl_schema_version_ tx is null", KR(ret));
    return ret;
  }
  for (int64_t r = 0; OB_SUCC(ret) && r < tx->redo_list_.count(); ++r) {
    const char *buf = tx->redo_list_.at(r).redo_buf_;
    const int64_t buf_len = tx->redo_list_.at(r).redo_len_;
    int64_t pos = 0;
    memtable::ObMemtableMutatorMeta meta;
    if (OB_FAIL(meta.deserialize(buf, buf_len, pos))) {
      LOG_WARN("CSFetcher: fail to deserialize mutator meta", KR(ret), K(r));
      break;
    }
    while (OB_SUCC(ret) && pos < buf_len) {
      memtable::ObMutatorRowHeader row_header;
      if (OB_FAIL(row_header.deserialize(buf, buf_len, pos))) {
        LOG_WARN("CSFetcher: fail to deserialize row_header", KR(ret), K(pos));
        break;
      }
      const int64_t row_payload_start = pos;
      if (row_header.tablet_id_.id() == OB_ALL_DDL_OPERATION_TID) {
        memtable::ObMemtableMutatorRow mut_row;
        if (OB_FAIL(mut_row.deserialize(buf, buf_len, pos))) {
          LOG_WARN("CSFetcher: fail to deserialize mut_row", KR(ret));
          break;
        }
        if (mut_row.rowkey_.get_obj_cnt() >= 1) {
          int64_t sv = 0;
          if (OB_FAIL(mut_row.rowkey_.get_obj_ptr()[0].get_int(sv))) {
            LOG_WARN("CSFetcher: fail to get_int from rowkey", KR(ret));
          } else if (sv > schema_version) {
            schema_version = sv;
          }
        }
      } else {
        int32_t entry_len = 0;
        if (OB_FAIL(common::serialization::decode_i32(buf, buf_len, pos, &entry_len))) {
          LOG_WARN("CSFetcher: fail to decode entry_len", KR(ret), K(pos));
          break;
        }
        pos = row_payload_start + static_cast<int64_t>(entry_len);
        if (OB_UNLIKELY(pos < 0 || pos > buf_len)) {
          ret = common::OB_ERR_UNEXPECTED;
          LOG_WARN("CSFetcher: extract_ddl skip overflow", KR(ret), K(pos), K(buf_len));
        }
      }
    }
  }
  if (OB_SUCC(ret) && schema_version <= 0) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_DEBUG("CSFetcher: no __all_ddl_operation row found in DDL redo", KR(ret), K(tx->tx_id_));
  }
  return ret;
}

// ---------------------------------------------------------------------------
// get_or_create_tx_info_: shared by handle_redo_log_ and MDS DDL branch.
// ---------------------------------------------------------------------------
int ObCSFetcher::get_or_create_tx_info_(int64_t tid, const palf::LSN &lsn, ObCSTxInfo *&tx)
{
  int ret = OB_SUCCESS;
  tx = nullptr;
  if (OB_FAIL(tx_info_.get_refactored(tid, tx))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      tx = OB_NEW(ObCSTxInfo, "CSTxInfo");
      if (OB_ISNULL(tx)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("CSFetcher: fail to alloc ObCSTxInfo", KR(ret), K(tid));
      } else {
        tx->tx_id_ = tid;
        tx->start_lsn_ = lsn;
        if (OB_FAIL(tx_info_.set_refactored(tid, tx))) {
          LOG_WARN("CSFetcher: fail to insert tx_info", KR(ret), K(tid));
          tx->destroy();
          OB_DELETE(ObCSTxInfo, "CSTxInfo", tx);
          tx = nullptr;
        }
      }
    } else {
      LOG_WARN("CSFetcher: fail to get tx_info", KR(ret), K(tid));
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Transaction log handlers
// ---------------------------------------------------------------------------

int ObCSFetcher::handle_redo_log_(
    const transaction::ObTransID &tx_id,
    const char *mutator_buf,
    int64_t mutator_size,
    const palf::LSN &lsn)
{
  int ret = common::OB_SUCCESS;
  ObCSTxInfo *tx = nullptr;
  int64_t tid = tx_id.get_id();

  if (mutator_size <= 0 || mutator_size > CS_FETCHER_MAX_REDO_MUTATOR_SIZE) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("CSFetcher: invalid mutator_size", KR(ret), K(mutator_size), K(tid));
    return ret;
  }

  if (OB_FAIL(get_or_create_tx_info_(tid, lsn, tx))) {
    // error already logged
  } else if (OB_NOT_NULL(tx)) {
    char *buf = static_cast<char *>(common::ob_malloc(mutator_size, "CSRedoBuf"));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("CSFetcher: fail to alloc redo buf", KR(ret), K(mutator_size));
    } else {
      MEMCPY(buf, mutator_buf, mutator_size);
      ObCSRedoRecord rec;
      rec.redo_buf_ = buf;
      rec.redo_len_ = mutator_size;
      if (OB_FAIL(tx->redo_list_.push_back(rec))) {
        LOG_WARN("CSFetcher: fail to push redo record", KR(ret), K(tid));
        common::ob_free(buf);
      }
    }
  }
  return ret;
}

int ObCSFetcher::handle_commit_log_(
    const transaction::ObTransID &tx_id,
    const SCN &commit_version)
{
  int ret = common::OB_SUCCESS;
  ObCSTxInfo *tx = nullptr;
  int64_t tid = tx_id.get_id();

  if (OB_FAIL(tx_info_.get_refactored(tid, tx))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("CSFetcher: fail to get tx_info on commit", KR(ret), K(tid));
    }
  } else if (OB_NOT_NULL(tx)) {
    tx->commit_version_ = commit_version.get_val_for_gts();
    if (tx->is_ddl_) {
      // DDL transaction: extract schema_version from __all_ddl_operation redo, then discard.
      int64_t ddl_schema_version = 0;
      int ddl_ret = extract_ddl_schema_version_(tx, ddl_schema_version);
      if (OB_SUCCESS == ddl_ret && ddl_schema_version > 0) {
        current_schema_version_ = ddl_schema_version;
        LOG_INFO("CSFetcher: DDL commit, schema_version advanced",
                 K(tid), K(ddl_schema_version), K(current_schema_version_));
      } else {
        LOG_DEBUG("CSFetcher: DDL commit, extract schema_version failed, unchanged",
                  KR(ddl_ret), K(tid), K(current_schema_version_));
      }
      tx_info_.erase_refactored(tid);
      tx->destroy();
      OB_DELETE(ObCSTxInfo, "CSTxInfo", tx);
      total_tx_committed_++;
    } else {
      // DML transaction: assign current schema_version and push to Dispatcher.
      // tx_info_ erase is deferred to release_committed_tx() called by Dispatcher
      // after Worker finishes processing the batch.
      tx->schema_version_ = current_schema_version_;
      if (OB_ISNULL(dispatcher_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("CSFetcher: dispatcher_ is null", KR(ret));
      } else if (OB_FAIL(dispatcher_->push(tx))) {
        LOG_WARN("CSFetcher: fail to push tx to dispatcher, destroy tx to avoid leak", KR(ret), K(tid));
        (void)tx_info_.erase_refactored(tid);
        tx->destroy();
        OB_DELETE(ObCSTxInfo, "CSTxInfo", tx);
      } else {
        total_tx_committed_++;
      }
    }
  }
  return ret;
}

int ObCSFetcher::handle_abort_log_(const transaction::ObTransID &tx_id)
{
  int ret = common::OB_SUCCESS;
  ObCSTxInfo *tx = nullptr;
  int64_t tid = tx_id.get_id();

  if (OB_FAIL(tx_info_.erase_refactored(tid, &tx))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS; // Skip silently.
    } else {
      LOG_WARN("CSFetcher: fail to erase tx on abort", KR(ret), K(tid));
    }
  } else if (OB_NOT_NULL(tx)) {
    tx->destroy();
    OB_DELETE(ObCSTxInfo, "CSTxInfo", tx);
  }
  return ret;
}

int ObCSFetcher::handle_rollback_to_log_(
    const transaction::ObTransID &tx_id,
    const transaction::ObTxSEQ &from,
    const transaction::ObTxSEQ &to)
{
  int ret = common::OB_SUCCESS;
  ObCSTxInfo *tx = nullptr;
  int64_t tid = tx_id.get_id();

  if (OB_FAIL(tx_info_.get_refactored(tid, tx))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS; // Skip silently.
    } else {
      LOG_WARN("CSFetcher: fail to get tx_info on rollback_to", KR(ret), K(tid));
    }
  } else if (OB_NOT_NULL(tx)) {
    ObCSRollbackRange range(to, from);
    if (OB_FAIL(tx->rollback_list_.push_back(range))) {
      LOG_WARN("CSFetcher: fail to push rollback range", KR(ret), K(tid), K(from), K(to));
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Main Fetcher loop
// ---------------------------------------------------------------------------

void ObCSFetcher::run1()
{
  lib::set_thread_name("CSFetcher");

  int ret = OB_SUCCESS;

  // Wait until sql_proxy and schema service are ready.
  while (!has_set_stop()) {
    if (OB_ISNULL(GCTX.sql_proxy_) || GCTX.in_bootstrap_ || GCTX.start_service_time_ <= 0
        || OB_ISNULL(GCTX.schema_service_)) {
      usleep(CS_FETCHER_INIT_RETRY_SLEEP_US);
    } else {
      break;
    }
  }

  // Determine initial mode: check if any async vector index tables exist.
  if (!has_set_stop()) {
    int64_t refreshed_version = 0;
    if (OB_SUCC(GCTX.schema_service_->get_tenant_refreshed_schema_version(
                    MTL_ID(), refreshed_version))) {
      last_checked_schema_version_ = refreshed_version;
    }
    bool has_async = false;
    int check_ret = check_has_async_index_tables_(has_async);
    if (OB_SUCC(check_ret)) {
      has_async_index_tables_ = has_async;
      running_mode_ = has_async_index_tables_ ? ACTIVE : IDLE;
      FLOG_INFO("CSFetcher: initial mode determined",
                "mode", running_mode_ == ACTIVE ? "ACTIVE" : "IDLE",
                K(has_async_index_tables_), K(last_checked_schema_version_));
    } else {
      has_async_index_tables_ = false;
      running_mode_ = IDLE;
      LOG_WARN("CSFetcher: check_has_async_index_tables_ failed, start in IDLE", KR(check_ret));
    }
  }

  bool iter_ready = false;

  // Main state-machine loop.
  while (!has_set_stop()) {
    ret = OB_SUCCESS;

    // Periodic schema version check: detect schema changes and switch mode.
    if (REACH_TIME_INTERVAL(CS_FETCHER_SCHEMA_CHECK_INTERVAL_US)
        && OB_NOT_NULL(GCTX.schema_service_)) {
      bool old_has_async = has_async_index_tables_;
      bool new_has_async = false;
      if (OB_SUCC(get_has_async_cached_(new_has_async))) {
        running_mode_ = has_async_index_tables_ ? ACTIVE : IDLE;
        if (ACTIVE == running_mode_) {
          iter_ready = false;
        }
        if (old_has_async != new_has_async || REACH_TIME_INTERVAL(5 * 1000 * 1000)) {
          FLOG_INFO("CSFetcher: mode switched",
                  "mode", running_mode_ == ACTIVE ? "ACTIVE" : "IDLE",
                  K(has_async_index_tables_), K(last_checked_schema_version_));
        }
      }
    }

    // Advance min_dep_lsn regardless of mode (rate-limited internally; real-time async check inside).
    try_advance_min_dep_lsn_();
    try_advance_refresh_scn_();

    // IDLE branch: no async-index tables — sleep and wait for schema change.
    if (IDLE == running_mode_) {
      usleep(CS_FETCHER_IDLE_SLEEP_US);
      continue;
    }

    // ACTIVE branch: initialize iterator if not yet ready.
    if (!iter_ready) {
      if (OB_SUCC(init_consumption_position_())) {
        iter_ready = true;
        FLOG_INFO("CSFetcher: iterator initialized, starting consumption",
                  K(current_lsn_), K(current_scn_), K(ls_id_));
      } else {
        LOG_WARN("CSFetcher: init consumption position failed, retry", KR(ret));
        usleep(CS_FETCHER_INIT_FAIL_SLEEP_US);
        continue;
      }
    }

    if (OB_FAIL(iter_.next())) {
      if (OB_ITER_END == ret) {
        usleep(CS_FETCHER_ITER_END_SLEEP_US);
        continue;
      }
      LOG_WARN("CSFetcher: fail to iter.next", KR(ret));
      usleep(CS_FETCHER_ITER_ERR_SLEEP_US);
      continue;
    }

    palf::LogEntry log_entry;
    palf::LSN lsn;
    if (OB_FAIL(iter_.get_entry(log_entry, lsn))) {
      LOG_WARN("CSFetcher: fail to get_entry", KR(ret));
      continue;
    }

    const char *buf = log_entry.get_data_buf();
    const int64_t buf_len = log_entry.get_data_len();
    const SCN scn = log_entry.get_scn();

    // Pre-check: only process transaction logs.
    logservice::ObLogBaseHeader base_header;
    int64_t header_pos = 0;
    if (OB_FAIL(base_header.deserialize(buf, buf_len, header_pos))) {
      LOG_WARN("CSFetcher: fail to deserialize ObLogBaseHeader", KR(ret), K(lsn));
      continue;
    }
    if (base_header.get_log_type() != logservice::ObLogBaseType::TRANS_SERVICE_LOG_BASE_TYPE) {
      // Not a transaction log — skip.
      current_lsn_ = lsn + log_entry.get_header_size() + buf_len;
      current_scn_ = scn;
      continue;
    }

    // Deserialize the transaction log block.
    transaction::ObTxLogBlock tx_log_block;
    if (OB_FAIL(tx_log_block.init_for_replay(buf, buf_len))) {
      LOG_WARN("CSFetcher: fail to init_for_replay", KR(ret), K(lsn));
      continue;
    }

    const transaction::ObTxLogBlockHeader &block_header = tx_log_block.get_header();
    // Log filter: only consume logs with has_async_index (tables with async vector index or DDL).
    if (!block_header.has_async_index()) {
      LOG_DEBUG("CSFetcher: skip log without has_async_index", K(lsn));
      current_lsn_ = lsn + log_entry.get_header_size() + buf_len;
      current_scn_ = scn;
      continue;
    }

    const transaction::ObTransID tx_id = block_header.get_tx_id();
    current_processing_tx_id_ = tx_id;

    // Iterate transaction log entries within this block.
    transaction::ObTxLogHeader tx_header;
    while (OB_SUCC(ret)) {
      if (OB_FAIL(tx_log_block.get_next_log(tx_header))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        }
        LOG_WARN("CSFetcher: fail to get_next_log", KR(ret), K(lsn));
        break;
      }

      const transaction::ObTxLogType log_type = tx_header.get_tx_log_type();
      switch (log_type) {
        case transaction::ObTxLogType::TX_REDO_LOG: {
          transaction::ObTxRedoLogTempRef tmp_ref;
          transaction::ObTxRedoLog redo_log(tmp_ref);
          if (OB_FAIL(tx_log_block.deserialize_log_body(redo_log))) {
            LOG_WARN("CSFetcher: fail to deserialize redo", KR(ret), K(lsn));
          } else {
            const char *mutator_buf = redo_log.get_replay_mutator_buf();
            const int64_t mutator_size = redo_log.get_mutator_size();
            if (OB_NOT_NULL(mutator_buf) && mutator_size > 0) {
              if (OB_FAIL(handle_redo_log_(tx_id, mutator_buf, mutator_size, lsn))) {
                LOG_WARN("CSFetcher: fail to handle_redo_log", KR(ret), K(tx_id), K(lsn));
              }
            }
          }
          break;
        }
        case transaction::ObTxLogType::TX_MULTI_DATA_SOURCE_LOG: {
          transaction::ObTxMultiDataSourceLog mds_log;
          if (OB_FAIL(tx_log_block.deserialize_log_body(mds_log))) {
            LOG_WARN("CSFetcher: fail to deserialize MDS log", KR(ret), K(lsn));
          } else {
            const transaction::ObTxBufferNodeArray &mds_data = mds_log.get_data();
            for (int64_t i = 0; i < mds_data.count(); ++i) {
              if (mds_data.at(i).get_data_source_type()
                  == transaction::ObTxDataSourceType::DDL_TRANS) {
                ObCSTxInfo *tx = nullptr;
                int64_t tid = tx_id.get_id();
                if (OB_FAIL(get_or_create_tx_info_(tid, lsn, tx))) {
                  LOG_WARN("CSFetcher: fail to get tx_info on MDS DDL", KR(ret), K(tid));
                } else if (OB_NOT_NULL(tx)) {
                  tx->is_ddl_ = true;
                }
                break;
              }
            }
          }
          break;
        }
        case transaction::ObTxLogType::TX_COMMIT_LOG: {
          transaction::ObTxCommitLogTempRef tmp_ref;
          transaction::ObTxCommitLog commit_log(tmp_ref);
          if (OB_FAIL(tx_log_block.deserialize_log_body(commit_log))) {
            LOG_WARN("CSFetcher: fail to deserialize commit", KR(ret), K(lsn));
          } else {
            const int64_t cv_in_log = commit_log.get_commit_version().get_val_for_logservice();
            SCN commit_version;
            if (transaction::ObTransVersion::INVALID_TRANS_VERSION == cv_in_log) {
              commit_version = scn;
            } else {
              commit_version = commit_log.get_commit_version();
            }
            if (OB_FAIL(handle_commit_log_(tx_id, commit_version))) {
              LOG_WARN("CSFetcher: fail to handle_commit_log", KR(ret), K(tx_id), K(lsn));
            }
          }
          break;
        }
        case transaction::ObTxLogType::TX_ABORT_LOG: {
          transaction::ObTxAbortLogTempRef tmp_ref;
          transaction::ObTxAbortLog abort_log(tmp_ref);
          if (OB_FAIL(tx_log_block.deserialize_log_body(abort_log))) {
            LOG_WARN("CSFetcher: fail to deserialize abort", KR(ret), K(lsn));
          } else if (OB_FAIL(handle_abort_log_(tx_id))) {
            LOG_WARN("CSFetcher: fail to handle_abort_log", KR(ret), K(tx_id), K(lsn));
          }
          break;
        }
        case transaction::ObTxLogType::TX_ROLLBACK_TO_LOG: {
          transaction::ObTxRollbackToLog rollback_log;
          if (OB_FAIL(tx_log_block.deserialize_log_body(rollback_log))) {
            LOG_WARN("CSFetcher: fail to deserialize rollback_to", KR(ret), K(lsn));
          } else {
            if (OB_FAIL(handle_rollback_to_log_(tx_id,
                                                 rollback_log.get_from(),
                                                 rollback_log.get_to()))) {
              LOG_WARN("CSFetcher: fail to handle_rollback_to", KR(ret), K(tx_id), K(lsn));
            }
          }
          break;
        }
        default:
          break;
      }
    }

    // Advance progress tracking.
    current_lsn_ = lsn + log_entry.get_header_size() + log_entry.get_data_len();
    current_scn_ = scn;

    if (REACH_TIME_INTERVAL(CS_FETCHER_PROGRESS_LOG_INTERVAL_US)) {
      LOG_INFO("CSFetcher: progress",
               "mode", running_mode_ == ACTIVE ? "ACTIVE" : "IDLE",
               K(current_lsn_), K(current_scn_), K(total_tx_committed_),
               K(current_schema_version_), "inflight_tx_count", tx_info_.size());
    }
  }

  FLOG_INFO("CSFetcher: stopped",
            "mode", running_mode_ == ACTIVE ? "ACTIVE" : "IDLE",
            K(current_lsn_), K(current_scn_), K(total_tx_committed_));
}

}  // namespace share
}  // namespace oceanbase
