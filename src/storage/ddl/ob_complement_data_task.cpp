#include "lib/stat/ob_diagnostic_info_guard.h"
#include "share/rc/ob_server_runtime.h"
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
#include "ob_complement_data_task.h"
#include "data_plane/ddl/ob_ddl_coordinator.h"
#include "logservice/ob_log_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/ob_ddl_checksum.h"
#include "share/ob_ddl_sim_point.h"
#include "storage/scheduler/ob_dag_warning_history_mgr.h"
#include "storage/access/ob_multiple_scan_merge.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "share/ob_structured_event_logger.h"
#include "data_plane/report/ob_tablet_report.h"
#include "storage/ddl/ob_ddl_direct_load_utils.h"
#include "storage/ddl/ob_pipeline.h"
#include "storage/ddl/ob_ddl_merge_task_v2.h"

namespace oceanbase
{
using namespace common;
using namespace storage;
using namespace compaction;
using namespace share;
using namespace share::schema;
using namespace sql;
using namespace omt;
using namespace name;
using namespace transaction;
using namespace blocksstable;

namespace storage
{

void add_ddl_event(const ObComplementDataParam *param, const ObString &stmt)
{
  if (OB_NOT_NULL(param)) {
    char table_id_buffer[256];
    char tablet_id_buffer[256];
    snprintf(table_id_buffer, sizeof(table_id_buffer), "source_table_id:%ld, dest_table_id:%ld", param->orig_table_id_, param->dest_table_id_);
    snprintf(tablet_id_buffer, sizeof(tablet_id_buffer), "source_id:%lu, dest_id:%lu", param->orig_tablet_id_.id(), param->dest_tablet_id_.id());

    SERVER_EVENT_ADD("ddl", stmt.ptr(),
      "ret", ret,
      "trace_id", *ObCurTraceId::get_trace_id(),
      "task_id", param->task_id_,
      "table_id", table_id_buffer,
      "schema_version", param->dest_schema_version_,
      "info", tablet_id_buffer);
  }
  LOG_INFO("complement data task.", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()), K(stmt), KPC(param));
}

int ObComplementDataParam::fill_tablet_param()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
  } else if (OB_FAIL(ObDDLStorageUtil::ddl_get_tablet(ls,
                                               dest_tablet_id_,
                                               tablet_handle,
                                               ObMDSGetTabletMode::READ_ALL_COMMITED))) {
  } else if (OB_UNLIKELY(nullptr == tablet_handle.get_obj())) {
    ret = OB_ERR_SYS;
    LOG_WARN("tablet handle is null", K(ret), K(param));
  } else if (OB_FAIL(tablet_handle.get_obj()->load_storage_schema(allocator_, tablet_param_.storage_schema_))) {
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle, true /*try_create]*/))) {
  } else {
    tablet_param_.is_micro_index_clustered_ = tablet_handle.get_obj()->get_tablet_meta().micro_index_clustered_;
    ObTabletBindingMdsUserData mds_data;
    if (OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), mds_data))) {
    } else if (mds_data.lob_meta_tablet_id_.is_valid()) {
      dest_lob_meta_tablet_id_ = mds_data.lob_meta_tablet_id_;
      ObTabletHandle lob_meta_tablet_handle;
      if (OB_FAIL(ObDDLStorageUtil::ddl_get_tablet(ls, mds_data.lob_meta_tablet_id_, lob_meta_tablet_handle, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
      } else if (OB_FAIL(lob_meta_tablet_handle.get_obj()->load_storage_schema(allocator_, lob_meta_tablet_param_.storage_schema_))) {
      } else {
        lob_meta_tablet_param_.is_micro_index_clustered_ = lob_meta_tablet_handle.get_obj()->get_tablet_meta().micro_index_clustered_;
        ObDDLKvMgrHandle lob_ddl_kv_mgr_handle;
        if (OB_FAIL(lob_meta_tablet_handle.get_obj()->get_ddl_kv_mgr(lob_ddl_kv_mgr_handle, true /*try_create]*/))) {
        }
      }
    }
  }
  return ret;
}

int ObComplementDataParam::init(const obcall::ObDDLLocalBuildArg &arg)
{
  int ret = OB_SUCCESS;
  const ObServerRuntimeSchema *runtime_schema = nullptr;
  const ObTableSchema *orig_table_schema = nullptr;
  const ObTableSchema *dest_table_schema = nullptr;
  
  
  const int64_t orig_table_id = arg.source_table_id_;
  const int64_t dest_table_id = arg.dest_schema_id_;
  const int64_t orig_schema_version = arg.schema_version_;
  const int64_t dest_schema_version = arg.dest_schema_version_;
  ObSchemaGetterGuard src_runtime_schema_guard;
  ObSchemaGetterGuard dst_runtime_schema_guard;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObComplementDataParam has been inited before", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else {
    SERVER_MODULE_SCOPE {
      if (OB_FAIL(ObDDLUtil::check_schema_version_refreshed(
              ObMultiVersionSchemaService::get_instance(),
              orig_schema_version))) {
        if (OB_SCHEMA_EAGAIN != ret) {
          LOG_WARN("check schema version refreshed failed", K(ret), K(orig_schema_version));
        }
      } else if (OB_FAIL(ObDDLUtil::check_schema_version_refreshed(
                     ObMultiVersionSchemaService::get_instance(),
                     dest_schema_version))) {
        if (OB_SCHEMA_EAGAIN != ret) {
          LOG_WARN("check schema version refreshed failed", K(ret), K(dest_schema_version));
        }
      } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
                src_runtime_schema_guard, orig_schema_version))) {
      } else if (OB_FAIL(src_runtime_schema_guard.get_server_runtime_info(runtime_schema))) {
      } else if (OB_ISNULL(runtime_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("server runtime schema is not ready", K(ret), K(orig_schema_version));
      } else if (OB_FAIL(src_runtime_schema_guard.get_table_schema( orig_table_id, orig_table_schema))) {
      } else if (OB_ISNULL(orig_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table not exist", K(ret), K(orig_table_id), K(orig_schema_version));
      } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
                dst_runtime_schema_guard, dest_schema_version))) {
      } else if (OB_FAIL(dst_runtime_schema_guard.get_server_runtime_info(runtime_schema))) {
      } else if (OB_ISNULL(runtime_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("server runtime schema is not ready", K(ret), K(dest_schema_version));
      } else if (OB_FAIL(dst_runtime_schema_guard.get_table_schema( dest_table_id, dest_table_schema))) {
      } else if (OB_ISNULL(dest_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table not exist", K(ret), K(dest_table_id), K(dest_schema_version));
      } else if (true
        && OB_UNLIKELY(dest_table_schema->get_association_table_id() != arg.source_table_id_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected error", K(ret), K(arg), K(dest_table_schema->get_association_table_id()));
      } else {
        snapshot_version_ = arg.snapshot_version_;
        orig_schema_tablet_size_ = orig_table_schema->get_tablet_size();
      }
    }
  }

  if (OB_SUCC(ret)) {
    
    
    orig_table_id_ = orig_table_id;
    dest_table_id_ = dest_table_id;
    orig_schema_version_ = orig_schema_version;
    dest_schema_version_ = dest_schema_version;
    orig_tablet_id_ = arg.source_tablet_id_;
    dest_tablet_id_ = arg.dest_tablet_id_;
    task_id_ = arg.task_id_;
    execution_id_ = arg.execution_id_;
    tablet_task_id_ = arg.tablet_task_id_;
    data_format_version_ = arg.data_format_version_;
    user_parallelism_ = arg.parallelism_;
    direct_load_type_ = ObDDLDirectLoadUtil::ddl_get_direct_load_type();
    if (OB_FAIL(ObDDLTableSchema::fill_ddl_table_schema(dest_table_id_, allocator_, ddl_table_schema_))) {
    } else if (OB_FAIL(fill_tablet_param())) {
    } else {
      is_inited_ = true;
      FLOG_INFO("succeed to init ObComplementDataParam", K(ret), KPC(this));
    }
  }
  return ret;
}

int ObComplementDataParam::prepare_task_ranges()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(this));
  } else {
    ranges_.reset();
    concurrent_cnt_ = 0;
    if (user_parallelism_ <= 1) {
      ObDatumRange datum_range;
      datum_range.set_whole_range();
      if (OB_FAIL(ranges_.push_back(datum_range))) {
      } else {
        concurrent_cnt_ = 1;
        LOG_INFO("succeed to to init task ranges", K(ret), K(user_parallelism_), K(concurrent_cnt_), K(ranges_));
      }
    } else if (OB_FAIL(split_task_ranges(task_id_,
                                         orig_tablet_id_,
                                         orig_schema_tablet_size_,
                                         user_parallelism_))) {
    }
  }

  if (OB_SUCC(ret)) {
    {
      SERVER_EVENT_ADD("alter_table", "drop_column_data_complement",
        "task_id", task_id_,
        "trace_id", *ObCurTraceId::get_trace_id(),
        "user_parallelism", user_parallelism_,
        "concurrent_cnt", concurrent_cnt_
      );
    }
  }
  return ret;
}

// split task ranges to do table scan based on the whole range on the specified tablet.
int ObComplementDataParam::split_task_ranges(
    const int64_t task_id,
    const common::ObTabletID &tablet_id,
    const int64_t tablet_size,
    const int64_t hint_parallelism)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ObDDLStorageUtil::get_task_ranges(task_id, tablet_id, tablet_size, hint_parallelism, allocator_, ranges_))) {
  } else {
    ObLS *ls = nullptr;
    ObTabletTableIterator iterator;
    ObLSTabletService *tablet_service = nullptr;
    if (OB_UNLIKELY(task_id <= 0 || !tablet_id.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid arguments", K(ret), K(task_id), K(tablet_id));
    } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
    } else if (OB_ISNULL(tablet_service = ls->get_tablet_svr())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet service is nullptr", K(ret));
    } else {
      if (OB_SUCC(ret)) {
        concurrent_cnt_ = ranges_.count();
        FLOG_INFO("succeed to get concurrent cnt", K(ret), K(task_id), K(tablet_id));
      }
    }
  }
  return ret;
}

int ObComplementDataContext::init(
    const ObComplementDataParam &param, 
    const share::schema::ObTableSchema &hidden_table_schema)
{
  int ret = OB_SUCCESS;
  UNUSED(hidden_table_schema);
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  const ObSSTable *first_major_sstable = nullptr;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObComplementDataContext has already been inited", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(param));
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
  } else if (OB_FAIL(ObDDLStorageUtil::ddl_get_tablet(ls,
                                               param.dest_tablet_id_,
                                               tablet_handle,
                                               ObMDSGetTabletMode::READ_ALL_COMMITED))) {
  } else if (OB_UNLIKELY(nullptr == tablet_handle.get_obj())) {
    ret = OB_ERR_SYS;
    LOG_WARN("tablet handle is null", K(ret), K(param));
  } else if (OB_FAIL(ObTabletDDLUtil::check_and_get_major_sstable(param.dest_tablet_id_, first_major_sstable, table_store_wrapper))) {
  } else if (nullptr != first_major_sstable) {
    LOG_INFO("major exists, skip create tablet direct load mgr", K(ret), K(param));
  } else {
    total_slice_cnt_ = param.ranges_.count();
  }

  /* tablet context only used for following merge task, only part of param are necessary*/
  if (OB_FAIL(ret)) {
  } else if (nullptr != tablet_ctx_) {
    /* skip, when already inti */
  } else {
    char *buf = nullptr;
    common::ObILobReadService *lob_read_service =
        ::oceanbase::share::server_service<::oceanbase::common::ObILobReadService>();
    if (OB_ISNULL(buf = static_cast<char*>(allocator_.alloc(sizeof(ObDDLTabletContext))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret));
    } else if (OB_ISNULL(lob_read_service)) {
      ret = OB_NOT_INIT;
      LOG_WARN("lob read service is not initialized", K(ret));
    } else {
      tablet_ctx_ = new (buf) ObDDLTabletContext();
       if (OB_FAIL(tablet_ctx_->init(
               param.dest_tablet_id_,
               param.user_parallelism_,
               param.snapshot_version_,
               param.direct_load_type_,
               param.ddl_table_schema_,
               *lob_read_service))) {
      }
    }
  }
   
  if (OB_SUCC(ret)) {
    is_major_sstable_exist_ = nullptr != first_major_sstable ? true : false;
    concurrent_cnt_ = param.concurrent_cnt_;
    is_inited_ = true;
  }
  return ret;
}

int ObComplementDataContext::add_column_checksum(const ObIArray<int64_t> &report_col_checksums,
    const ObIArray<int64_t> &report_col_ids)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (0 == report_col_checksums_.count()) {
    if (OB_FAIL(report_col_checksums_.prepare_allocate(report_col_checksums.count()))) {
    }
  }
  if (OB_SUCC(ret) && 0 == report_col_ids_.count()) {
    if (OB_FAIL(report_col_ids_.prepare_allocate(report_col_ids.count()))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (report_col_checksums_.count() != report_col_checksums.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, report col checksum array count is not equal", K(ret), K(report_col_checksums.count()), K(report_col_checksums_.count()));
    } else if (report_col_ids_.count() != report_col_ids.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, report col ids array count is not equal", K(ret), K(report_col_ids.count()), K(report_col_ids_.count()));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < report_col_checksums.count(); ++i) {
        report_col_checksums_.at(i) += report_col_checksums.at(i);
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < report_col_ids.count(); ++i) {
        report_col_ids_.at(i) = report_col_ids.at(i);
      }
    }
  }
  return ret;
}

int ObComplementDataContext::get_column_checksum(ObIArray<int64_t> &report_col_checksums,
    ObIArray<int64_t> &report_col_ids)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (OB_FAIL(report_col_checksums.assign(report_col_checksums_))) {
  } else if (OB_FAIL(report_col_ids.assign(report_col_ids_))) {
  }
  return ret;
}

void ObComplementDataContext::destroy()
{
  is_inited_ = false;
  is_major_sstable_exist_ = false;
  complement_data_ret_ = OB_SUCCESS;
  concurrent_cnt_ = 0;
  row_scanned_ = 0;
  row_inserted_ = 0;
  report_col_checksums_.reset();
  report_col_ids_.reset();
  if (nullptr != tablet_ctx_) {
    tablet_ctx_->~ObDDLTabletContext();
    tablet_ctx_ = nullptr;
  }
  allocator_.reset();
}

ObComplementDataDag::ObComplementDataDag()
  : ObIDag(ObDagType::DAG_TYPE_DDL), is_inited_(false), param_(), context_()
{
}

ObComplementDataDag::~ObComplementDataDag()
{
}

int ObComplementDataDag::init(const obcall::ObDDLLocalBuildArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObComplementDataDag has already been inited", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_FAIL(param_.init(arg))) {
  } else if (OB_UNLIKELY(!param_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected", K(ret), K(param_));
  } else {
    is_inited_ = true;
  }

  LOG_INFO("finish to init complement data dag", K(ret), K(param_));
  return ret;
}

int ObComplementDataDag::calc_total_row_count() 
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("has not been inited ", K(ret));
  } else if (OB_UNLIKELY(!param_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(param_));
  } else if (context_.physical_row_count_ != 0) {
    ret =  OB_INIT_TWICE;
    LOG_WARN("has calculated the row_count", K(ret), K(context_.physical_row_count_));
  } else if (1UL != 1UL) {
    // FIXME(YIREN), How to calc the row count of the source tablet for restore table.
    // RPC?
  } else if (OB_FAIL(ObDDLStorageUtil::get_tablet_physical_row_cnt(
                                  param_.orig_tablet_id_,
                                  true, // calc_sstable = true
                                  true, // calc_memtable = true
                                  context_.physical_row_count_))) {
  }
  return ret;
}

/*
  1.normal data complemet dag:
                WriteTask
              /           \
   PrepareTask- WriteTask - MergePrepareTask - MergeTask
              \           /               (only used for reporting original checksum)
                WriteTask
*/

int ObComplementDataDag::create_first_task()
{
  int ret = OB_SUCCESS;
  share::SCN mock_scn;

  ObComplementPrepareTask *prepare_task = nullptr;
  ObComplementWriteTask *write_task = nullptr;
  ObComplementMergeTask *merge_task = nullptr;

  ObDDLMergePrepareTask *data_merge_prepare_task = nullptr;
  ObDDLMergePrepareTask *lob_merge_prepare_task  = nullptr;
  char *buf = nullptr;
  common::ObILobReadService *lob_read_service =
      ::oceanbase::share::server_service<::oceanbase::common::ObILobReadService>();

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!param_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected", K(ret), K(param_));
  } else if (OB_FAIL(mock_scn.convert_for_tx(DDL_START_SCN_VAL))) {
  } else if (OB_UNLIKELY(nullptr != context_.tablet_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_ctx_ not null", K(ret), KP(context_.tablet_ctx_));
  } else if (OB_ISNULL(buf = static_cast<char*>(context_.allocator_.alloc(sizeof(ObDDLTabletContext))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory", K(ret));
  } else if (OB_ISNULL(lob_read_service)) {
    ret = OB_NOT_INIT;
    LOG_WARN("lob read service is not initialized", K(ret));
  } else if (FALSE_IT(context_.tablet_ctx_ = new (buf) ObDDLTabletContext())) {
  } else if (OB_FAIL(context_.tablet_ctx_->init(param_.dest_tablet_id_,
                                                param_.user_parallelism_,
                                                param_.snapshot_version_,
                                                param_.direct_load_type_,
                                                param_.ddl_table_schema_,
                                                *lob_read_service))) {
  } else if (OB_FAIL(alloc_task(prepare_task))) {
  } else if (OB_ISNULL(prepare_task)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr task", K(ret));
  } else if (OB_FAIL(prepare_task->init(param_, context_))) {
  } else if (OB_FAIL(alloc_task(write_task))) {
  } else if (OB_ISNULL(write_task)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr task", K(ret));
  } else if (OB_FAIL(write_task->init(0, param_, context_))) {
  } else if (OB_FAIL(prepare_task->add_child(*write_task))) {
  } else if (OB_FAIL(alloc_task(merge_task))) {
  } else if (OB_ISNULL(merge_task)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr task", K(ret));
  } else if (OB_FAIL(merge_task->init(param_, context_))) {
  }

  if (OB_SUCC(ret)) {
    /* tablet context only used for following merge task, only part of param are necessary*/
    ObDDLTabletMergeDagParamV2 dag_merge_param;
    ObDDLTabletMergeDagParamV2 lob_dag_merge_param;
    ObDDLTaskParam task_param;
    task_param.data_format_version_ = param_.data_format_version_;
    task_param.snapshot_version_    = param_.snapshot_version_;
    task_param.schema_version_      = param_.dest_schema_version_;
    task_param.ddl_task_id_         = param_.task_id_;
    task_param.execution_id_        = param_.execution_id_;
    task_param.target_table_id_     = param_.dest_table_id_;
    
    if (OB_FAIL(dag_merge_param.init(true /* for major */, false /* for lob*/, false /* for replay*/,
                                     mock_scn /* start_scn*/,
                                     param_.direct_load_type_, task_param,
                                     context_.tablet_ctx_))) {
    } else if (OB_FAIL(alloc_task(data_merge_prepare_task))) {
    } else if (OB_FAIL(data_merge_prepare_task->init(dag_merge_param))) {
    } else if (OB_FAIL(write_task->add_child(*data_merge_prepare_task))) {
    } else if (OB_FAIL(data_merge_prepare_task->add_child(*merge_task))) {
    }
    
    if (OB_FAIL(ret)) {
    } else if (!param_.dest_lob_meta_tablet_id_.is_valid()) {
      /* if lob tablet id invalid, skip */
    } else if (OB_FAIL(lob_dag_merge_param.init(true /* for major*/, true /* for lob */, false /* for replay */,
                                     mock_scn /* start_scn*/,
                                     param_.direct_load_type_, task_param,
                                     context_.tablet_ctx_))) {
    } else if (OB_FAIL(alloc_task(lob_merge_prepare_task))) {
    } else if (OB_FAIL(lob_merge_prepare_task->init(lob_dag_merge_param))) {
    } else if (OB_FAIL(write_task->add_child(*lob_merge_prepare_task))) {
    } else if (OB_FAIL(lob_merge_prepare_task->add_child(*merge_task))) {
    } 
  }

  if (OB_FAIL(ret)) { // add task in reverse order
  } else if (OB_FAIL(add_task(*merge_task))) {
  } else if (OB_FAIL(add_task(*data_merge_prepare_task))) {
  } else if (nullptr != lob_merge_prepare_task && OB_FAIL(add_task(*lob_merge_prepare_task))) {
      LOG_WARN("failed to merge prepare task", K(ret));
  } else if (OB_FAIL(add_task(*write_task))) {
  } else if (OB_FAIL(add_task(*prepare_task))) {
  } 
  
  return ret;
}

bool ObComplementDataDag::ignore_warning()
{
  return OB_EAGAIN == dag_ret_
    || OB_NEED_RETRY == dag_ret_
    || OB_TASK_EXPIRED == dag_ret_;
}

int ObComplementDataDag::prepare_context()
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *hidden_table_schema = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObComplementDataDag not init", K(ret));
  } else if (OB_UNLIKELY(!param_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected", K(ret), K(param_));
  } else if (OB_FAIL(param_.prepare_task_ranges())) {
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
             schema_guard, param_.dest_schema_version_))) {
  } else if (OB_FAIL(schema_guard.get_table_schema(
             param_.dest_table_id_, hidden_table_schema))) {
  } else if (OB_ISNULL(hidden_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("hidden table schema not exist", K(ret), K(param_));
  } else if (OB_FAIL(context_.init(param_, *hidden_table_schema))) {
  }
  LOG_INFO("finish to prepare complement context", K(ret), K(param_), K(context_));
  return ret;
}

uint64_t ObComplementDataDag::hash() const
{
  int tmp_ret = OB_SUCCESS;
  uint64_t hash_val = 0;
  if (OB_UNLIKELY(!is_inited_ || !param_.is_valid())) {
    tmp_ret = OB_ERR_SYS;
    LOG_ERROR("table schema must not be NULL", K(tmp_ret), K(is_inited_), K(param_));
  } else {
    hash_val = 1UL + 1UL
             + param_.orig_table_id_ + param_.dest_table_id_
             + param_.orig_tablet_id_.hash() + param_.dest_tablet_id_.hash() + ObDagType::DAG_TYPE_DDL;
  }
  return hash_val;
}

bool ObComplementDataDag::operator==(const ObIDag &other) const
{
  int tmp_ret = OB_SUCCESS;
  bool is_equal = false;
  if (OB_UNLIKELY(this == &other)) {
    is_equal = true;
  } else if (get_type() == other.get_type()) {
    const ObComplementDataDag &dag = static_cast<const ObComplementDataDag &>(other);
    if (OB_UNLIKELY(!param_.is_valid() || !dag.param_.is_valid())) {
      tmp_ret = OB_ERR_SYS;
      LOG_ERROR("invalid argument", K(tmp_ret), K(param_), K(dag.param_));
    } else {
      is_equal = (1UL == 1UL) && (1UL == 1UL) &&
                 (param_.orig_table_id_ == dag.param_.orig_table_id_) && (param_.dest_table_id_ == dag.param_.dest_table_id_) &&
                 (param_.orig_tablet_id_ == dag.param_.orig_tablet_id_) && (param_.dest_tablet_id_ == dag.param_.dest_tablet_id_);
    }
  }
  return is_equal;
}

// build reponse here rather deconstruction of DAG, to avoid temporary dead lock of RS RPC queue.
// 
int ObComplementDataDag::report_local_build_status()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObComplementDataDag has not been inited", K(ret));
  } else if (OB_UNLIKELY(!param_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param", K(ret), K(param_));
  } else {
#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = OB_E(EventTable::EN_DDL_REPORT_LOCAL_BUILD_STATUS_FAIL) OB_SUCCESS;
      LOG_INFO("report local build status errsim", K(ret));
    }
#endif
    obcall::ObDDLLocalBuildResponse arg;
    arg.tablet_id_ = param_.orig_tablet_id_;
    arg.source_table_id_ = param_.orig_table_id_;
    arg.dest_schema_id_ = param_.dest_table_id_;
    arg.ret_code_ = context_.complement_data_ret_;
    arg.snapshot_version_ = param_.snapshot_version_;
    arg.schema_version_ = param_.orig_schema_version_;
    arg.dest_schema_version_ = param_.dest_schema_version_;
    arg.task_id_ = param_.task_id_;
    arg.execution_id_ = param_.execution_id_;
    arg.row_inserted_ = context_.row_inserted_;
    arg.physical_row_count_ = context_.physical_row_count_;
    arg.server_addr_ = GCTX.self_addr();
    FLOG_INFO("send local build status response to RS", K(ret), K(context_), K(arg));
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(data_plane::report_ddl_single_replica_response(arg))) {
    }
  }
  DEBUG_SYNC(HOLD_DDL_COMPLEMENT_DAG_AFTER_REPORT_FINISH);
  FLOG_INFO("complement data finished", K(ret), K(context_.complement_data_ret_));
  return ret;
}

int ObComplementDataDag::fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObComplementDataDag has not been initialized", K(ret));
  } else if (OB_UNLIKELY(!param_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param", K(ret), K(param_));
  } else if (OB_FAIL(ADD_DAG_WARN_INFO_PARAM(out_param, allocator, get_type(), 
                                static_cast<int64_t>(param_.orig_tablet_id_.id()),
                                static_cast<int64_t>(param_.dest_tablet_id_.id()),
                                static_cast<int64_t>(param_.orig_table_id_),
                                static_cast<int64_t>(param_.dest_table_id_),
                                param_.orig_schema_version_,
                                param_.snapshot_version_))) {
  }
  return ret;
}

int ObComplementDataDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObComplementDataDag has not been initialized", K(ret));
  } else if (OB_UNLIKELY(!param_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid params", K(ret), K(param_));
  } else if (OB_FAIL(databuff_printf(buf, buf_len, "source_tablet_id=%ld dest_tablet_id=%ld",
                              param_.orig_tablet_id_.id(), param_.dest_tablet_id_.id()))) {
  }
  return ret;
}

ObComplementPrepareTask::ObComplementPrepareTask()
  : ObITask(TASK_TYPE_COMPLEMENT_PREPARE), is_inited_(false), param_(nullptr), context_(nullptr)
{
}

ObComplementPrepareTask::~ObComplementPrepareTask()
{
}

int ObComplementPrepareTask::init(ObComplementDataParam &param, ObComplementDataContext &context)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObComplementPrepareTask has already been inited", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(param), K(context));
  } else {
    param_ = &param;
    context_ = &context;
    is_inited_ = true;
  }
  return ret;
}

int ObComplementPrepareTask::process()
{
  int ret = OB_SUCCESS;
  ObIDag *tmp_dag = get_dag();
  ObComplementDataDag *dag = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObComplementPrepareTask has not been inited", K(ret));
  } else if (OB_ISNULL(tmp_dag) || ObDagType::DAG_TYPE_DDL != tmp_dag->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag is invalid", K(ret), KP(tmp_dag));
  } else if (FALSE_IT(dag = static_cast<ObComplementDataDag *>(tmp_dag))) {
  } else if (OB_FAIL(dag->prepare_context())) {
  } else if (OB_FAIL(dag->calc_total_row_count())) {
  } else if (context_->is_major_sstable_exist_) {
    FLOG_INFO("major sstable exists, all task should finish", K(ret), K(*param_));
  } else if (OB_FAIL(ObDDLChecksumOperator::delete_checksum(param_->execution_id_,
                                                    param_->orig_table_id_,
                                                    0/*use 0 just to avoid clearing target table chksum*/,
                                                    param_->task_id_,
                                                    *GCTX.sql_proxy_,
                                                    param_->tablet_task_id_))) {
  } else {
    LOG_INFO("finish the complement prepare task", K(ret), KPC(param_), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
  }

  if (OB_FAIL(ret)) {
    context_->complement_data_ret_ = ret;
    ret = OB_SUCCESS;
  }
  
  add_ddl_event(param_, "complement prepare task");
  return ret;
}

int ObComplementWriteMacroOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  if (input_chunk.is_end_chunk()) {
    if (OB_FAIL(slice_writer_.close())) {
    } else {
      output_chunk.set_end_chunk();
      result_state = ObPipelineOperator::NEED_MORE_INPUT;
    }
  } else if (!input_chunk.is_valid() || input_chunk.type_ != ObChunk::DATUM_ROW) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(input_chunk));
  } else if (OB_FAIL(slice_writer_.append_row(*input_chunk.datum_row_))) {
  } else {
    result_state = ObPipelineOperator::NEED_MORE_INPUT;
  }
  return ret;
}

int ObComplementWriteMacroOperator::try_execute_finish(const ObChunk &input_chunk,
                                                       ResultState &result_state,
                                                       ObChunk &output_chunk)
{
  UNUSED(input_chunk);
  UNUSED(result_state);
  UNUSED(output_chunk);
  return OB_SUCCESS;
}

int ObComplementRowIterator::init(ObScan *scan)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    scan_ = scan;
    is_inited_ = true;
  }
  return ret;
}

int ObComplementRowIterator::get_next_row(const blocksstable::ObDatumRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(scan_->get_next_row(row))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("get next row failed", K(ret));
    }
  }
  return ret;
}

ObComplementWriteTask::ObComplementWriteTask()
  : ObWriteMacroPipeline(TASK_TYPE_COMPLEMENT_WRITE),
    is_inited_(false), task_id_(0), param_(nullptr),
    context_(nullptr), col_ids_(), org_col_ids_(), output_projector_(),
    write_op_(this), scan_(nullptr),
    allocator_("CompleWriAlloc", OB_MALLOC_NORMAL_BLOCK_SIZE),
    row_iter_(), slice_row_iter_()
{
}

ObComplementWriteTask::~ObComplementWriteTask()
{
  col_ids_.reset();
  org_col_ids_.reset();
  output_projector_.reset();
  if (nullptr != scan_) {
    scan_->~ObScan();
    allocator_.free(scan_);
    scan_ = nullptr;
  }
  allocator_.reset();
}

int ObComplementWriteTask::init(
    const int64_t task_id, 
    ObComplementDataParam &param,
    ObComplementDataContext &context)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObComplementWriteTask has already been inited", K(ret));
  } else if (task_id < 0 || !param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(task_id), K(param), K(context));
  } else {
    task_id_ = task_id;
    param_ = &param;
    context_ = &context;
    if (OB_FAIL(fill_writer_param(write_param_))) {
    } else if (OB_FAIL(write_op_.init(write_param_))) {
    } else if (OB_FAIL(add_op(&write_op_))) {
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObComplementWriteTask::get_next_chunk(ObChunk *&next_chunk)
{
  int ret = OB_SUCCESS;
  next_chunk = nullptr;
  const blocksstable::ObDatumRow *row = nullptr;
  if (OB_ISNULL(context_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("complement context is null", K(ret), KP(context_));
  } else if (context_->is_major_sstable_exist_) {
    ret = OB_ITER_END;
  } else if (OB_ISNULL(scan_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("table scan has not been inited", K(ret));
  } else if (OB_FAIL(slice_row_iter_.get_next_row(row))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("get next row failed", K(ret));
    } else {
      chunk_.set_end_chunk();
      next_chunk = &chunk_;
      ObArray<int64_t> report_col_checksums;
      ObArray<int64_t> report_col_ids;
      if (OB_FAIL(scan_->get_origin_table_checksum(report_col_checksums, report_col_ids))) {
      }
      /**
       * For DDL_RESTORE_TABLE, restored source data is read-only. Report its checksum
       * under the destination database; origin_table_id + ddl_task_id avoids conflicts.
       */
      else {
        { /* use new checksum */
          // add checksum to context and report checksum in merge task
          if (OB_FAIL(context_->add_column_checksum(report_col_checksums, report_col_ids))) {
          } else {
            LOG_INFO("use new checksum", K(param_->orig_table_id_), K(report_col_checksums), K(param_->orig_tablet_id_));
          }
        }
      }
    }
  } else {
    chunk_.type_ = ObChunk::DATUM_ROW;
    chunk_.datum_row_ = const_cast<blocksstable::ObDatumRow *>(row);
    next_chunk = &chunk_;
  }
  return ret;
}

int ObComplementWriteTask::fill_writer_param(ObWriteMacroParam &param)
{
  int ret = OB_SUCCESS;
  param.tablet_id_ = param_->dest_tablet_id_;
  param.data_format_version_ = param_->data_format_version_;
  param.schema_version_ = param_->dest_schema_version_;
  param.slice_idx_ = task_id_;
  param.slice_count_ = param_->concurrent_cnt_;
  param.snapshot_version_ = param_->snapshot_version_;
  param.direct_load_type_ = param_->direct_load_type_;
  param.task_id_ = param_->task_id_;
  param.tablet_param_ = param_->tablet_param_;
  param.lob_meta_tablet_param_ = param_->lob_meta_tablet_param_;
  param.lob_meta_tablet_id_ = param_->dest_lob_meta_tablet_id_;
  param.tablet_context_ = context_->tablet_ctx_;
  if (OB_FAIL(param.ddl_table_schema_.assign(param_->ddl_table_schema_))) {
  } else {
    param.is_index_table_ = param_->ddl_table_schema_.table_item_.is_index_table_;
  }
  return ret;
}

int ObComplementWriteTask::preprocess()
{
  int ret = OB_SUCCESS;
  ObIDag *tmp_dag = get_dag();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObComplementWriteTask has not been inited before", K(ret));
  } else if (OB_ISNULL(tmp_dag) || ObDagType::DAG_TYPE_DDL != tmp_dag->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag is invalid", K(ret), KP(tmp_dag));
  } else if (OB_SUCCESS != (context_->complement_data_ret_)) {
  } else if (context_->is_major_sstable_exist_) {
  } else if (OB_FAIL(share::check_server_runtime_ready())) {
  } else if (OB_FAIL(local_scan_by_range())) {
  } else {
    LOG_INFO("finish the complement write task", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
  }

  if (OB_FAIL(ret) || context_->is_major_sstable_exist_) {
  } else if (OB_FAIL(row_iter_.init(scan_))) {
  } else if (OB_FAIL(slice_row_iter_.init(param_->dest_tablet_id_, task_id_, write_param_, row_iter_))) {
  }
  return ret;
}

void ObComplementWriteTask::postprocess(int &ret_code)
{
  add_ddl_event(param_, "complement write task");
  if (OB_ITER_END == ret_code) {
    ret_code = OB_SUCCESS;
  }
  if (OB_SUCCESS != ret_code && OB_NOT_NULL(context_)) {
    context_->complement_data_ret_ = ret_code;
    ret_code = OB_SUCCESS;
  }
}

int ObComplementWriteTask::generate_next_task(ObITask *&next_task)
{
  int ret = OB_SUCCESS;
  ObIDag *tmp_dag = get_dag();
  ObComplementDataDag *dag = nullptr;
  ObComplementWriteTask *write_task = nullptr;
  const int64_t next_task_id = task_id_ + 1;
  next_task = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObComplementWriteTask has not been inited", K(ret));
  } else if (next_task_id >= param_->concurrent_cnt_) {
    ret = OB_ITER_END;
  } else if (OB_ISNULL(tmp_dag)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, dag must not be NULL", K(ret));
  } else if (OB_UNLIKELY(ObDagType::DAG_TYPE_DDL != tmp_dag->get_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, dag type is invalid", K(ret), "dag type", dag->get_type());
  } else if (FALSE_IT(dag = static_cast<ObComplementDataDag *>(tmp_dag))) {
  } else if (OB_FAIL(dag->alloc_task(write_task))) {
  } else if (OB_FAIL(write_task->init(next_task_id, *param_, *context_))) {
  } else {
    next_task = write_task;
    LOG_INFO("generate next complement write task", K(ret), K(param_->dest_table_id_));
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(context_)) {
    if (OB_ITER_END != ret) {
      context_->complement_data_ret_ = ret;
    }
  }
  return ret;
}

//generate col_ids and projector based on table_schema
int ObComplementWriteTask::generate_col_param()
{
  int ret = OB_SUCCESS;
  col_ids_.reuse();
  org_col_ids_.reuse();
  output_projector_.reuse();
  ObArray<ObColDesc> tmp_col_ids;
  ObSchemaGetterGuard runtime_schema_guard;
  const ObTableSchema *data_table_schema = nullptr;
  const ObTableSchema *hidden_table_schema = nullptr;
  SERVER_MODULE_SCOPE {
    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("not init", K(ret));
    } else if (OB_FAIL(
                   ObMultiVersionSchemaService::get_instance()
                       .get_runtime_schema_guard(runtime_schema_guard))) {
    } else if (OB_FAIL(runtime_schema_guard.get_table_schema(
              param_->orig_table_id_, data_table_schema))) {
    } else if (OB_ISNULL(data_table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("data table schema not exist", K(ret), K(arg));
    } else if (OB_FAIL(runtime_schema_guard.get_table_schema(
              param_->dest_table_id_, hidden_table_schema))) {
    } else if (OB_ISNULL(hidden_table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("hidden table schema not exist", K(ret), KPC(param_));
    } else if (OB_FAIL(hidden_table_schema->get_store_column_ids(tmp_col_ids, false))) {
    } else if (OB_FAIL(org_col_ids_.assign(tmp_col_ids))) {
    } else {
      // generate col_ids
      for (int64_t i = 0; OB_SUCC(ret) && i < tmp_col_ids.count(); i++) {
        const uint64_t hidden_column_id = tmp_col_ids.at(i).col_id_;
        const ObColumnSchemaV2 *hidden_column_schema = hidden_table_schema->get_column_schema(hidden_column_id);
        if (OB_ISNULL(hidden_column_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null column schema", K(ret), K(hidden_column_id));
        } else {
          const ObString &hidden_column_name = hidden_column_schema->get_column_name_str();
          const ObColumnSchemaV2 *data_column_schema = data_table_schema->get_column_schema(hidden_column_name);
          ObColDesc tmp_col_desc = tmp_col_ids.at(i);
          if (nullptr == data_column_schema) {
            // may be newly added column, can not find in data table.
          } else if (FALSE_IT(tmp_col_desc.col_id_ = data_column_schema->get_column_id())) {
          } else if (OB_FAIL(col_ids_.push_back(tmp_col_desc))) {
          } else if (data_column_schema->is_extend()) {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("The udt type is not adapted", K(ret), K(*data_column_schema));
          }
        }
      }
    }
    // generate output_projector.
    if (OB_FAIL(ret)) {
    } else {
      // notice that, can not find newly added column, get the row firstly, and then resolve it.
      for (int64_t i = 0; OB_SUCC(ret) && i < tmp_col_ids.count(); i++) {
        const ObColumnSchemaV2 *hidden_column_schema = hidden_table_schema->get_column_schema(tmp_col_ids.at(i).col_id_);
        const ObString &hidden_column_name = hidden_column_schema->get_column_name_str();
        for (int64_t j = 0; OB_SUCC(ret) && j < col_ids_.count(); j++) {
          const ObColumnSchemaV2 *data_column_schema = data_table_schema->get_column_schema(col_ids_.at(j).col_id_);
          if (nullptr == data_column_schema) {
            // may be newly added column.
          } else if (hidden_column_name == data_column_schema->get_column_name_str()) {
            if (OB_FAIL(output_projector_.push_back(static_cast<int32_t>(j)))) {
            }
            break;
          }
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(col_ids_.count() != output_projector_.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error", K(ret), K_(col_ids), K_(output_projector));
    }
  }
  return ret;
}

//For reordering column operations, such as drop column or add column after, we need to rewrite all
//storage data based on the newest table schema.
int ObComplementWriteTask::local_scan_by_range()
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  int64_t concurrent_cnt = 0;
  if (OB_UNLIKELY(OB_ISNULL(param_) || OB_ISNULL(context_) || !param_->is_valid() 
                  || !param_->has_generated_task_ranges())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(param_), KPC(context_));
  } else {
    concurrent_cnt = param_->concurrent_cnt_;
    LOG_INFO("start to do local scan by range", K(task_id_), K(concurrent_cnt), KPC(param_));
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(generate_col_param())) {
  } else if (OB_FAIL(do_local_scan())) {
  } else {
    int64_t cost_time = ObTimeUtility::current_time() - start_time;
    LOG_INFO("finish local scan by range", K(ret), K(cost_time), K(task_id_), K(concurrent_cnt));
  }
  return ret;
}

int ObComplementWriteTask::do_local_scan()
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObLocalScan *scan = nullptr;
  if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObLocalScan)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret));
  } else if (OB_FALSE_IT(scan = new (buf) ObLocalScan())) {
  } else {
    ObQueryFlag query_flag(ObQueryFlag::Forward,
        true, /*is daily merge scan*/
        true, /*is read multiple macro block*/
        false, /*sys task scan, read one macro block in single io*/
        false /*is full row scan?*/,
        false,
        false);
    const bool allow_not_ready = false;
    ObLS *ls = nullptr;
    ObTabletTableIterator iterator;
    ObSSTable *sstable = nullptr;
    
    const int64_t schema_version = param_->dest_schema_version_;
    scan_ = scan;

    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
    } else if (OB_FAIL(DDL_SIM(param_->task_id_, COMPLEMENT_DATA_TASK_LOCAL_SCAN_FAILED))) {
    } else if (OB_FAIL(ls->get_tablet_svr()->get_read_tables(param_->orig_tablet_id_,
        ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US,
        param_->snapshot_version_, param_->snapshot_version_, iterator, allow_not_ready))) {
      if (OB_REPLICA_NOT_READABLE == ret) {
        ret = OB_EAGAIN;
      } else {
        LOG_WARN("snapshot version has been discarded", K(ret));
      }
    } else {
      FLOG_INFO("local scan read tables", K(iterator), KPC(param_));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(param_->ranges_.at(task_id_).prepare_memtable_readable(org_col_ids_, allocator_))) {
    } else {
      ObSchemaGetterGuard schema_guard;
      const ObTableSchema *data_table_schema = nullptr;
      const ObTableSchema *hidden_table_schema = nullptr;
      if (OB_UNLIKELY(1UL != 1UL
                  || param_->orig_schema_version_ != param_->dest_schema_version_)) {
        ret = OB_ERR_SYS;
        LOG_WARN("err sys", K(ret), KPC(param_));
      } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
                schema_guard, schema_version))) {
      } else if (OB_FAIL(schema_guard.get_table_schema(
                param_->orig_table_id_, data_table_schema))) {
      } else if (OB_ISNULL(data_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("data table schema not exist", K(ret), KPC(param_));
      } else if (OB_FAIL(schema_guard.get_table_schema(
                param_->dest_table_id_, hidden_table_schema))) {
      } else if (OB_ISNULL(hidden_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("hidden table schema not exist", K(ret), KPC(param_));
      } else if (OB_FAIL(scan->init(col_ids_,
                                        org_col_ids_,
                                        output_projector_,
                                        *data_table_schema,
                                        param_->snapshot_version_,
                                        *hidden_table_schema,
                                        false/*unique_index_checking*/))) {
      } else if (OB_FAIL(scan->table_scan(*data_table_schema,
                                               param_->orig_tablet_id_,
                                               iterator,
                                               query_flag,
                                               param_->ranges_.at(task_id_)))) {
      }
    }
  }

  return ret;
}

ObComplementMergeTask::ObComplementMergeTask()
  : ObITask(TASK_TYPE_COMPLEMENT_MERGE), is_inited_(false), param_(nullptr), context_(nullptr)
{
}

ObComplementMergeTask::~ObComplementMergeTask()
{
}

int ObComplementMergeTask::init(ObComplementDataParam &param, ObComplementDataContext &context)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObComplementMergeTask has already been inited", K(ret));
  } else if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(param), K(context));
  } else {
    param_ = &param;
    context_ = &context;
    is_inited_ = true;
  }
  return ret;
}

/* dest major sstable checksum have been report, 
 * only origin sstalbe checksum should be rerprot
*/
int ObComplementMergeTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObIDag *tmp_dag = get_dag();
  ObComplementDataDag *dag = nullptr;
  ObTablet *tablet = nullptr;
  ObArray<int64_t> report_col_checksums;
  ObArray<int64_t> report_col_ids;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  if (OB_ISNULL(tmp_dag) || ObDagType::DAG_TYPE_DDL != tmp_dag->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag is invalid", K(ret), KP(tmp_dag));
  } else if (FALSE_IT(dag = static_cast<ObComplementDataDag *>(tmp_dag))) {
  } else if (OB_SUCCESS != (context_->complement_data_ret_)) {
  } else if (OB_FAIL(share::check_server_runtime_ready())) {
  } else if (context_->is_major_sstable_exist_) {
    ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
    const ObSSTable *first_major_sstable = nullptr;
    ObSSTableMetaHandle sst_meta_hdl;
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
    } else if (OB_FAIL(ObDDLStorageUtil::ddl_get_tablet(ls, param_->dest_tablet_id_, tablet_handle,
      ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    } else if (OB_UNLIKELY(nullptr == tablet_handle.get_obj())) {
      ret = OB_ERR_SYS;
      LOG_WARN("tablet handle is null", K(ret), KPC_(param));
    } else if (OB_FAIL(ObTabletDDLUtil::check_and_get_major_sstable(
        param_->dest_tablet_id_, first_major_sstable, table_store_wrapper))) {
    } else if (OB_ISNULL(first_major_sstable)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, major sstable shoud not be null", K(ret), K(*param_));
    } else if (OB_FAIL(first_major_sstable->get_meta(sst_meta_hdl))) {
    } else {
      const int64_t *column_checksums = sst_meta_hdl.get_sstable_meta().get_col_checksum();
      const int64_t column_count = sst_meta_hdl.get_sstable_meta().get_col_checksum_cnt();
      if (OB_FAIL(ObTabletDDLUtil::report_ddl_checksum(param_->dest_tablet_id_,
                                                         param_->dest_table_id_,
                                                         param_->execution_id_,
                                                         param_->task_id_,
                                                         column_checksums,
                                                         column_count,
                                                         param_->data_format_version_))) {
      } else if (OB_FAIL(data_plane::submit_tablet_update(param_->dest_tablet_id_))) {
      }
    }
  } else if (OB_FAIL(context_->get_column_checksum(report_col_checksums, report_col_ids))) {
  } else if (OB_FAIL(ObDDLChecksumOperator::update_checksum(param_->orig_table_id_,
          param_->orig_tablet_id_.id(),
          param_->task_id_,
          report_col_checksums,
          report_col_ids,
          param_->execution_id_,
          param_->orig_tablet_id_.id(),
          param_->data_format_version_,
          *GCTX.sql_proxy_))) {
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(context_)) {
    context_->complement_data_ret_ = ret;
    ret = OB_SUCCESS;
  }

  if (OB_NOT_NULL(dag) &&
    OB_SUCCESS != (tmp_ret = dag->report_local_build_status())) {
    // do not override ret if it has already failed.
    ret = OB_SUCCESS == ret ? tmp_ret : ret;
    LOG_WARN("fail to report local build status", K(ret), K(tmp_ret));
  }

  add_ddl_event(param_, "complement merge task");
  return ret;
}

/**
 * -----------------------------------ObLocalScan-----------------------------------------
 */

ObLocalScan::ObLocalScan() : is_inited_(false), table_id_(OB_INVALID_ID),
    dest_table_id_(OB_INVALID_ID), schema_version_(0), extended_gc_(),
    default_row_(), write_row_(), row_iter_(nullptr), scan_merge_(nullptr), ctx_(), access_param_(),
    access_ctx_(), get_table_param_(), allocator_("ObLocalScan", OB_MALLOC_NORMAL_BLOCK_SIZE),
    calc_buf_(ObModIds::OB_SQL_EXPR_CALC, OB_MALLOC_NORMAL_BLOCK_SIZE), col_params_(), read_info_(),
    exist_column_mapping_(allocator_)
{}

ObLocalScan::~ObLocalScan()
{
  if (OB_NOT_NULL(scan_merge_)) {
    scan_merge_->~ObMultipleScanMerge();
    scan_merge_ = NULL;
  }
  for (int64_t i = 0; i < col_params_.count(); i++) {
    ObColumnParam *&tmp_col_param = col_params_.at(i);
    if (OB_NOT_NULL(tmp_col_param)) {
      tmp_col_param->~ObColumnParam();
      allocator_.free(tmp_col_param);
      tmp_col_param = nullptr;
    }
  }
  default_row_.reset();
  write_row_.reset();
  access_ctx_.reset();
}

int ObLocalScan::init(
    const ObIArray<share::schema::ObColDesc> &col_ids,
    const ObIArray<share::schema::ObColDesc> &org_col_ids,
    const ObIArray<int32_t> &projector,
    const ObTableSchema &data_table_schema,
    const int64_t snapshot_version,
    const ObTableSchema &hidden_table_schema,
    const bool unique_index_checking)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObLocalScan has been initialized before", K(ret));
  } else if (org_col_ids.count() < 1 || col_ids.count() < 1 || projector.count() < 1
      || !data_table_schema.is_valid() || !hidden_table_schema.is_valid() || snapshot_version < 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid auguments", K(ret), K(data_table_schema), K(hidden_table_schema),
        K(col_ids), K(org_col_ids), K(projector), K(snapshot_version));
  } else {
    unique_index_checking_ = unique_index_checking;
    snapshot_version_ = snapshot_version;
    ObDatumRow tmp_default_row;
    const int64_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
    if (OB_FAIL(check_generated_column_exist(hidden_table_schema, org_col_ids))) {
    } else if (OB_FAIL(extended_gc_.extended_col_ids_.assign(col_ids))) {
    } else if (OB_FAIL(extended_gc_.org_extended_col_ids_.assign(org_col_ids))) {
    } else if (OB_FAIL(extended_gc_.output_projector_.assign(projector))) {
    } else if (OB_FAIL(get_exist_column_mapping(data_table_schema, hidden_table_schema))){
    } else if (OB_FAIL(checksum_calculator_.init(org_col_ids.count() + extra_rowkey_cnt))) {
    } else if (OB_FAIL(hidden_table_schema.get_multi_version_column_descs(mult_version_cols_desc_))) {
    } else if (OB_FAIL(tmp_default_row.init(allocator_, org_col_ids.count()))) {
    } else if (OB_FAIL(default_row_.init(allocator_, org_col_ids.count()))) {
    } else if (unique_index_checking && OB_FAIL(write_row_.init(allocator_, org_col_ids.count()))) { // without extra rowkey for unique index check.
      STORAGE_LOG(WARN, "Failed to init datum row", K(ret));
    } else if (!unique_index_checking && OB_FAIL(write_row_.init(allocator_, org_col_ids.count() + extra_rowkey_cnt))) { // with extra rowkey.
      STORAGE_LOG(WARN, "Failed to init datum row", K(ret));
    } else {
      tmp_default_row.row_flag_.set_flag(ObDmlFlag::DF_INSERT); // default_row.row_flag_ will be set by deep_copy
      if (OB_FAIL(storage::get_orig_default_row(hidden_table_schema, org_col_ids, tmp_default_row))) {
      } else if (OB_FAIL(default_row_.deep_copy(tmp_default_row, allocator_))) {
      } else {
        table_id_ = data_table_schema.get_table_id();
        dest_table_id_ = hidden_table_schema.get_table_id();
        schema_version_ = hidden_table_schema.get_schema_version();
        schema_rowkey_cnt_ = hidden_table_schema.get_rowkey_column_num();
        is_inited_ = true;
      }
    }
  }
  return ret;
}

int ObLocalScan::get_output_columns(
    const ObTableSchema &hidden_table_schema,
    ObIArray<ObColDesc> &col_ids)
{
  int ret = OB_SUCCESS;
  col_ids.reset();
  if (unique_index_checking_) {
    if (OB_FAIL(col_ids.assign(extended_gc_.org_extended_col_ids_))) {
    }
  } else {
    if (OB_FAIL(hidden_table_schema.get_store_column_ids(col_ids, false))) {
    }
  }
  return ret;
}

// record the position of data table columns in hidden table by exist_column_mapping_.
int ObLocalScan::get_exist_column_mapping(
    const ObTableSchema &data_table_schema,
    const ObTableSchema &hidden_table_schema)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObArray<ObColDesc> tmp_col_ids;

  if (OB_FAIL(get_output_columns(hidden_table_schema, tmp_col_ids))) {
  } else if (exist_column_mapping_.is_inited() && OB_FAIL(exist_column_mapping_.reserve(tmp_col_ids.count()))) {
    LOG_WARN("fail to expand size of bitmap", K(ret));
  } else if (!exist_column_mapping_.is_inited() && OB_FAIL(exist_column_mapping_.init(tmp_col_ids.count(), false))) {
    LOG_WARN("fail to init exist column mapping", K(ret));
  } else {
    exist_column_mapping_.reuse(false);
    for (int64_t i = 0; OB_SUCC(ret) && i < tmp_col_ids.count(); i++) {
      const ObColumnSchemaV2 *hidden_column_schema = hidden_table_schema.get_column_schema(tmp_col_ids.at(i).col_id_);
      const ObString &hidden_column_name = hidden_column_schema->get_column_name_str();
      const ObColumnSchemaV2 *data_column_schema = data_table_schema.get_column_schema(hidden_column_name);
      if (nullptr == data_column_schema) {
        // newly added column, can not find in data table.
      } else if (OB_FAIL(exist_column_mapping_.set(i))) {
      } else if (data_column_schema->is_extend()) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("The udt type is not adapted", K(ret), K(*data_column_schema));
      } else {/* do nothing. */}
    }
  }
  return ret;
}

int ObLocalScan::check_generated_column_exist(
    const ObTableSchema &hidden_table_schema,
    const ObIArray<share::schema::ObColDesc> &org_col_ids)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < org_col_ids.count(); ++i) {
    const ObColumnSchemaV2 *column_schema = nullptr;
    if (OB_ISNULL(column_schema = hidden_table_schema.get_column_schema(org_col_ids.at(i).col_id_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("The column schema should not be null", K(ret), K(org_col_ids.at(i)));
    } else if (OB_UNLIKELY(column_schema->is_stored_generated_column())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, table redefinition is selected for table with stored column", K(ret), K(*column_schema));
    } else {/* do nothing. */}
  }
  return ret;
}

int ObLocalScan::table_scan(
    const ObTableSchema &data_table_schema,
    const ObTabletID &tablet_id,
    ObTabletTableIterator &table_iter,
    ObQueryFlag &query_flag,
    blocksstable::ObDatumRange &range)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(construct_column_schema(data_table_schema))) {
  } else if (OB_FAIL(construct_access_param(data_table_schema, tablet_id))) {
  } else if (OB_FAIL(construct_range_ctx(query_flag))) {
  } else if (OB_FAIL(construct_multiple_scan_merge(table_iter, range))) {
  } else if (OB_FAIL(ObLobManager::fill_lob_header(allocator_, extended_gc_.org_extended_col_ids_, default_row_))) {
  }
  return ret;
}

//convert column schema to column param
int ObLocalScan::construct_column_schema(const ObTableSchema &data_table_schema)
{
  int ret = OB_SUCCESS;
  const ObArray<ObColDesc> &extended_col_ids = extended_gc_.extended_col_ids_;
  for (int64_t i = 0; OB_SUCC(ret) && i < extended_col_ids.count(); i++) {
    const ObColumnSchemaV2 *col = data_table_schema.get_column_schema(extended_col_ids.at(i).col_id_);
    if (OB_ISNULL(col)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get column schema", K(ret), K(extended_col_ids.at(i).col_id_));
    } else {
      void *buf = allocator_.alloc(sizeof(ObColumnParam));
      ObColumnParam *tmp_col_param = nullptr;
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc memory", K(ret));
      } else {
        tmp_col_param = new (buf) ObColumnParam(allocator_);
        if (OB_FAIL(ObTableParam::convert_column_schema_to_param(*col, *tmp_col_param))) {
        } else if (OB_FAIL(col_params_.push_back(tmp_col_param))) {
        }
        if (OB_FAIL(ret) && OB_NOT_NULL(tmp_col_param)) {
          tmp_col_param->~ObColumnParam();
          allocator_.free(tmp_col_param);
          tmp_col_param = nullptr;
        }
      }
    }
  }
  if (OB_FAIL(ret)) {     //clear col_params
    for (int64_t i = 0; i < col_params_.count(); i++) {
      ObColumnParam *&tmp_col_param = col_params_.at(i);
      if (OB_NOT_NULL(tmp_col_param)) {
        tmp_col_param->~ObColumnParam();
        allocator_.free(tmp_col_param);
        tmp_col_param = nullptr;
      }
    }
  }
  return ret;
}

//construct table access param
int ObLocalScan::construct_access_param(
    const ObTableSchema &data_table_schema,
    const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  read_info_.reset();
  ObArray<int32_t> cols_index;
  ObArray<ObColDesc> tmp_col_ids;
  // to construct column index, i.e., cols_index.
  if (OB_FAIL(data_table_schema.get_store_column_ids(tmp_col_ids, false))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < extended_gc_.extended_col_ids_.count(); i++) {
      bool is_found = false;
      for (int64_t j = 0; OB_SUCC(ret) && !is_found && j < tmp_col_ids.count(); j++) {
        if (extended_gc_.extended_col_ids_.at(i).col_id_ == tmp_col_ids.at(j).col_id_) {
          if (OB_FAIL(cols_index.push_back(j))) {
          } else {
            is_found = true;
          }
        }
      }
      if (OB_SUCC(ret) && !is_found) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, column is not in data table", K(ret),
          K(extended_gc_.extended_col_ids_.at(i)), K(tmp_col_ids), K(data_table_schema));
      }
    }
  }
  
  if (OB_FAIL(ret)) {
  } else if (cols_index.count() != extended_gc_.extended_col_ids_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret), K(cols_index), K(extended_gc_));
  } else if (OB_FAIL(read_info_.init(allocator_,
                                     data_table_schema.get_column_count(),
                                     data_table_schema.get_rowkey_column_num(),
                                     extended_gc_.extended_col_ids_, // TODO @yiren, remove column id.
                                     &cols_index,
                                     &col_params_,
                                     nullptr /* no column extensions */))) {
  } else {
    ObArray<ObColDesc> &extended_col_ids = extended_gc_.extended_col_ids_;
    ObArray<int32_t> &output_projector = extended_gc_.output_projector_;
    access_param_.iter_param_.tablet_id_ = tablet_id;
    access_param_.iter_param_.table_id_ = data_table_schema.get_table_id();
    access_param_.iter_param_.out_cols_project_ = &output_projector;
    access_param_.iter_param_.read_info_ = &read_info_;
    if (OB_FAIL(access_param_.iter_param_.refresh_lob_column_out_status())) {
    } else {
      access_param_.is_inited_ = true;
    }
  }
  LOG_INFO("construct table access param", K(ret), K(tmp_col_ids), K(cols_index), K(extended_gc_.extended_col_ids_),
      K(extended_gc_.output_projector_), K(access_param_));
  return ret;
}

//construct version range and ctx
int ObLocalScan::construct_range_ctx(ObQueryFlag &query_flag)
{
  int ret = OB_SUCCESS;
  common::ObVersionRange trans_version_range;
  trans_version_range.snapshot_version_ = snapshot_version_;
  trans_version_range.multi_version_start_ = snapshot_version_;
  trans_version_range.base_version_ = 0;
  SCN tmp_scn;
  if (OB_FAIL(tmp_scn.convert_for_tx(snapshot_version_))) {
  } else if (OB_FAIL(ctx_.init_for_read(access_param_.iter_param_.tablet_id_,
                                        INT64_MAX,
                                        -1,
                                        tmp_scn))) {
  } else if (OB_FAIL(access_ctx_.init(query_flag, ctx_, allocator_, allocator_, trans_version_range))) {
  }
  return ret;
}

//construct multiple scan merge
int ObLocalScan::construct_multiple_scan_merge(
    ObTabletTableIterator &table_iter,
    ObDatumRange &range)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  LOG_INFO("start to do output_store.scan");
  if (OB_FAIL(get_table_param_.tablet_iter_.assign(table_iter))) {
  } else if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObMultipleScanMerge)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory for ObMultipleScanMerge", K(ret));
  } else if (FALSE_IT(scan_merge_ = new(buf)ObMultipleScanMerge())) {
    ret = OB_ERR_SYS;
    LOG_WARN("fail to do placement new", K(ret));
  } else if (OB_FAIL(scan_merge_->init(access_param_, access_ctx_, get_table_param_))) {
  } else if (OB_FAIL(scan_merge_->open(range))) {
  } else {
    scan_merge_->disable_padding();
    scan_merge_->disable_fill_virtual_column();
    row_iter_ = scan_merge_;
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(scan_merge_)) {
    scan_merge_->~ObMultipleScanMerge();
    allocator_.free(scan_merge_);
    scan_merge_ = nullptr;
    row_iter_ = nullptr;
  }
  return ret;
}

int ObLocalScan::get_origin_table_checksum(
    ObArray<int64_t> &report_col_checksums,
    ObArray<int64_t> &report_col_ids)
{
  int ret = OB_SUCCESS;
  report_col_checksums.reuse();
  report_col_ids.reuse();
  ObArray<ObColDesc> tmp_col_ids;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *data_table_schema = nullptr;
  const ObTableSchema *hidden_table_schema = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
             schema_guard, schema_version_))) {
  } else if (OB_FAIL(schema_guard.get_table_schema(
             table_id_, data_table_schema))) {
  } else if (OB_ISNULL(data_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("data table not exist", K(ret), K(table_id_));
  } else if (OB_FAIL(schema_guard.get_table_schema(
             dest_table_id_, hidden_table_schema))) {
  } else if (OB_ISNULL(hidden_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("hidden table schema not exist", K(ret), K(dest_table_id_));
  } else if (OB_FAIL(get_output_columns(*hidden_table_schema, tmp_col_ids))) {
  } else if (tmp_col_ids.size() != exist_column_mapping_.size()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret), K(tmp_col_ids), K(exist_column_mapping_.size()));
  } else {
    const int64_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
    // get data table columns id and corresponding checksum.
    for (int64_t i = 0; OB_SUCC(ret) && i < exist_column_mapping_.size(); i++) {
      if (exist_column_mapping_.test(i)) {
        const ObColumnSchemaV2 *hidden_col_schema = hidden_table_schema->get_column_schema(tmp_col_ids.at(i).col_id_);
        const ObString &hidden_column_name = hidden_col_schema->get_column_name_str();
        const ObColumnSchemaV2 *data_col_schema = data_table_schema->get_column_schema(hidden_column_name);
        const int64_t index_in_array = i < schema_rowkey_cnt_ ? i : i + extra_rowkey_cnt;
        if (OB_ISNULL(data_col_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("data column schema should not be null", K(ret), K(hidden_column_name));
        } else if (OB_FAIL(report_col_ids.push_back(data_col_schema->get_column_id()))) {
        } else if (OB_FAIL(report_col_checksums.push_back(checksum_calculator_.get_column_checksum()[index_in_array]))) {
        } else if (data_col_schema->is_extend()) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("The udt type is not adapted", K(ret), K(*data_col_schema));
        } else {/* do nothing. */}
      } else {/* do nothing. */}
    }
  }
  return ret;
}

int ObLocalScan::get_next_row(const ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  datum_row = nullptr;
  calc_buf_.reuse();
  const ObDatumRow *row = nullptr;
  if (OB_FAIL(row_iter_->get_next_row(row))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      LOG_WARN("fail to get next row", K(ret));
    }
  } else if (OB_ISNULL(row) || !row->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(row));
  } else {
    write_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
    for (int64_t i = 0, j = 0; OB_SUCC(ret) && i < exist_column_mapping_.size(); i++) {
      const int64_t in_row_index = unique_index_checking_ ? i : storaged_index_with_extra_rowkey(i);
      ObObjMeta &obj_meta = extended_gc_.org_extended_col_ids_.at(i).col_type_;
      if (exist_column_mapping_.test(i)) {
        // fill with value stored in origin data table.
        if (OB_UNLIKELY(j >= extended_gc_.extended_col_ids_.count())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected error", K(ret), K(j), K(extended_gc_.extended_col_ids_.count()));
        } else {
          write_row_.storage_datums_[in_row_index] = row->storage_datums_[j++];
        }
      } else {
        // the column is newly added, thus fill with default value.
        write_row_.storage_datums_[in_row_index] = default_row_.storage_datums_[i];
      }
      if (OB_FAIL(ret)) {
      } else if (obj_meta.is_fixed_len_char_type()
        && OB_FAIL(ObDDLUtil::reshape_ddl_column_obj(write_row_.storage_datums_[in_row_index], obj_meta))) {
        LOG_WARN("reshape failed", K(ret), K(obj_meta));
      }
    }
    if (OB_SUCC(ret) && !unique_index_checking_) {
      write_row_.storage_datums_[schema_rowkey_cnt_].set_int(-snapshot_version_);
      write_row_.storage_datums_[schema_rowkey_cnt_ + 1].set_int(0);
      if (OB_FAIL(checksum_calculator_.calc_column_checksum(mult_version_cols_desc_, &write_row_, nullptr/*old_row*/, nullptr/*is_column_changed*/))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    datum_row = &write_row_;
  }
  return ret;
}

} //end namespace stroage
} //end namespace oceanbase
