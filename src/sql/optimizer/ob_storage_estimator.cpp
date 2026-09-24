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

#define USING_LOG_PREFIX SQL_OPT
#include "ob_storage_estimator.h"
#include "data_plane/ob_i_storage_estimator.h"
#include "data_plane/transaction/ob_i_read_timestamp_service.h"
#include "observer/namespace_worker_protocol_prototype.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "share/rc/ob_server_runtime.h"
#include <algorithm>

namespace oceanbase {
using namespace storage;
using namespace share;

namespace sql {

int ObStorageEstimator::estimate_row_count(const obcall::ObEstPartArg &arg,
                                           obcall::ObEstPartRes &res,
                                           ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;
  const uint64_t namespace_id =
      observer::namespace_worker_prototype::in_process_session_ns(session);
  //est path rows
  ObTableScanParam param;
  share::SCN max_readable_scn;
  data_plane::ObIReadTimestampService *read_timestamp_service =
      ::oceanbase::share::server_service<::oceanbase::data_plane::ObIReadTimestampService>();
  if (OB_ISNULL(read_timestamp_service)) {
    ret = OB_NOT_INIT;
    LOG_WARN("read timestamp service is not available", K(ret));
  } else if (OB_FAIL(read_timestamp_service->latest_read_scn(max_readable_scn))) {
  } else {
    param.frozen_version_ = static_cast<int64_t>(max_readable_scn.get_val_for_sql());
    param.schema_version_ = arg.schema_version_;
  }
  const int64_t readable_version = param.frozen_version_;
  for (int64_t i = 0; OB_SUCC(ret) && i < arg.index_params_.count(); i++) {
    obcall::ObEstPartResElement est_res;
    const auto &source = arg.index_params_.at(i);
    param.index_id_ = source.index_id_;
    param.frozen_version_ = readable_version;
    param.scan_flag_ = source.scan_flag_;
    param.tablet_id_ = source.tablet_id_;
    param.tx_id_ = source.tx_id_;
    bool inherited_with_cap = false;
    if (namespace_id > 1 && !is_inner_table(param.index_id_)) {
      uint64_t encoded_tablet_id = OB_INVALID_ID;
      common::ObTabletID physical_tablet;
      int64_t fork_cap = 0;
      if (OB_FAIL(storage::NamespaceForkKernelPrototype::storage_object_id(
              namespace_id, param.tablet_id_.id(), encoded_tablet_id))) {
      } else if (OB_FAIL(storage::NamespaceForkKernelPrototype::resolve_read_tablet(
                     common::ObTabletID(encoded_tablet_id), physical_tablet, fork_cap))) {
      } else {
        inherited_with_cap = fork_cap > 0 && physical_tablet.id() != encoded_tablet_id;
        param.tablet_id_ = physical_tablet;
        if (fork_cap > 0) {
          param.frozen_version_ = std::min(param.frozen_version_, fork_cap);
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (inherited_with_cap) {
      // The native row-count estimator counts the backing tablet's latest
      // memtable rows, even when the forked reader is capped at an older SCN.
      // Return an unreliable estimate instead of exposing parent-only rows.
      est_res.reset();
    } else {
      ret = storage_estimate_rowcount(param, source.batch_, est_res);
    }
    if (OB_SUCC(ret)) {
      ret = res.index_param_res_.push_back(est_res);
    }
  }
#if !defined(NDEBUG)
  if (OB_SUCC(ret)) {
    LOG_INFO("[OPT EST] rowcount estimation result", K(arg), K(res));
  }
#endif
  return ret;
}

int ObStorageEstimator::estimate_block_count_and_row_count(const obcall::ObEstBlockArg &arg,
                                                           obcall::ObEstBlockRes &res,
                                                           ObSQLSessionInfo *session,
                                                           uint64_t table_id)
{
  int ret = OB_SUCCESS;
  const uint64_t namespace_id =
      observer::namespace_worker_prototype::in_process_session_ns(session);
  for (int64_t i = 0; OB_SUCC(ret) && i < arg.tablet_params_arg_.count(); ++i) {
    obcall::ObEstBlockResElement est_res;
    auto routed = arg.tablet_params_arg_.at(i);
    if (namespace_id > 1 && !is_inner_table(table_id)) {
      uint64_t encoded_tablet_id = OB_INVALID_ID;
      common::ObTabletID physical_tablet;
      int64_t fork_cap = 0;
      if (OB_FAIL(storage::NamespaceForkKernelPrototype::storage_object_id(
              namespace_id, routed.tablet_id_.id(), encoded_tablet_id))) {
      } else if (OB_FAIL(storage::NamespaceForkKernelPrototype::resolve_read_tablet(
                     common::ObTabletID(encoded_tablet_id), physical_tablet, fork_cap))) {
      } else {
        routed.tablet_id_ = physical_tablet;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(storage_estimate_block_count_and_row_count(routed, est_res))) {
    } else if (OB_FAIL(res.tablet_params_res_.push_back(est_res))) {
    } else {
      LOG_TRACE("[OPT EST]: block count and row count stat", K(est_res), K(i), "param", arg.tablet_params_arg_.at(i));
    }
  }
#if !defined(NDEBUG)
  if (OB_SUCC(ret)) {
    LOG_INFO("[OPT EST] block count and row count estimation result", K(arg), K(res));
  }
#endif
  return ret;
}

// estimate scan rowcount
int ObStorageEstimator::storage_estimate_rowcount(ObTableScanParam &param,
                                                  const ObSimpleBatch &batch,
                                                  obcall::ObEstPartResElement &res)
{
  int ret = OB_SUCCESS;
  double rc_logical = 0;
  double rc_physical = 0;
  if (!batch.is_valid()) {
    // do nothing when there is no scan range
    res.logical_row_count_ = static_cast<int64_t>(rc_logical);
    res.physical_row_count_ = static_cast<int64_t>(rc_physical);
    res.reliable_ = true;
  } else if (OB_FAIL(storage_estimate_partition_batch_rowcount(batch,
                       param,
                       res.est_records_,
                       rc_logical,
                       rc_physical))) {
    LOG_WARN("fail to get partition batch rowcount", K(param.tablet_id_), K(batch), K(ret));
    res.reset();
    ret = OB_SUCCESS;
  } else {
    res.logical_row_count_ = static_cast<int64_t>(rc_logical);
    res.physical_row_count_ = static_cast<int64_t>(rc_physical);
    res.reliable_ = true;
  }
  return ret;
}
//@shanyan.g Adjustment layer operates at the partition level
int ObStorageEstimator::storage_estimate_partition_batch_rowcount(const ObSimpleBatch &batch,
    storage::ObTableScanParam &table_scan_param,
    ObIArray<ObEstRowCountRecord> &est_records,
    double &logical_row_count,
    double &physical_row_count)
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    int64_t rc_logical = 0;
    int64_t rc_physical = 0;
    ObArenaAllocator allocator;
    const int64_t timeout_us = THIS_WORKER.get_timeout_remain();
    data_plane::ObIStorageEstimator *storage_estimator =
        ::oceanbase::share::server_service<::oceanbase::data_plane::ObIStorageEstimator>();

    if (OB_ISNULL(storage_estimator)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null", K(ret), KP(storage_estimator));
    } else if (OB_FAIL(storage_estimator->estimate_row_count_for_batch(
                   table_scan_param,
                   batch,
                   allocator,
                   timeout_us,
                   est_records,
                   rc_logical,
                   rc_physical))) {
    } else {
        logical_row_count = rc_logical < 0 ? 1.0 : static_cast<double>(rc_logical);
        physical_row_count = rc_physical < 0 ? 1.0 : static_cast<double>(rc_physical);
    }
  }

  return ret;
}

int ObStorageEstimator::storage_estimate_block_count_and_row_count(
    const obcall::ObEstBlockArgElement &arg,
    obcall::ObEstBlockResElement &res)
{
  int ret = OB_SUCCESS;
  int64_t macro_block_count = 0;
  int64_t micro_block_count = 0;
  int64_t sstable_row_count = 0;
  int64_t memtable_row_count = 0;

  if (!arg.is_valid()) {
    res.macro_block_count_ = macro_block_count;
    res.micro_block_count_ = micro_block_count;
    res.sstable_row_count_ = sstable_row_count;
    res.memtable_row_count_ = memtable_row_count;
  } else {
    
    SERVER_MODULE_SCOPE {
      const int64_t timeout_us = THIS_WORKER.get_timeout_remain();
      data_plane::ObIStorageEstimator *storage_estimator =
          ::oceanbase::share::server_service<::oceanbase::data_plane::ObIStorageEstimator>();
      if (OB_ISNULL(storage_estimator)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), KP(storage_estimator));
      } else if (OB_FAIL(storage_estimator->estimate_block_count_and_row_count(
                     arg.tablet_id_,
                     timeout_us,
                     macro_block_count,
                     micro_block_count,
                     sstable_row_count,
                     memtable_row_count))) {
      } else {
        res.macro_block_count_ = macro_block_count;
        res.micro_block_count_ = micro_block_count;
        res.sstable_row_count_ = sstable_row_count;
        res.memtable_row_count_ = memtable_row_count;
      }
    }
  }
  return ret;
}

} // end of sql
} // end of oceanbase
