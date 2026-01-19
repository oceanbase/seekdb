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

#include "storage/ddl/ob_tablet_fork_task.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/tablet/ob_tablet_table_store.h"
#include "storage/ob_storage_struct.h"
#include "storage/blocksstable/ob_sstable.h"
#include "storage/ddl/ob_ddl_merge_task.h" // For ObDDLUtil
#include "storage/meta_mem/ob_tablet_handle.h"
#include "storage/ob_storage_schema.h"
#include "storage/blocksstable/ob_data_store_desc.h"
#include "storage/blocksstable/ob_macro_block_writer.h"
#include "storage/compaction/ob_compaction_util.h"
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "storage/access/ob_sstable_row_whole_scanner.h"
#include "storage/tablet/ob_tablet_obj_load_helper.h"
#include "share/scn.h"
#include "storage/blocksstable/index_block/ob_index_block_dual_meta_iterator.h"
#include "storage/compaction/ob_sstable_builder.h"
#include "storage/blocksstable/ob_sstable_private_object_cleaner.h"
#include "share/scheduler/ob_dag_warning_history_mgr.h"
#include "storage/ls/ob_ls_tablet_service.h"
#include "storage/tx_storage/ob_tenant_freezer.h"
#include "storage/ls/ob_freezer_define.h"
#include "storage/ddl/ob_ddl_clog.h" // For ObTableForkInfo
#include "storage/compaction/ob_schedule_dag_func.h" // For ObScheduleDagFunc
#include "storage/ddl/ob_tablet_rebuild_util.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace blocksstable;
using namespace compaction;

namespace storage
{
ObForkSnapshotRowScan::ObForkSnapshotRowScan()
  : is_inited_(false),
    row_iter_(nullptr),
    ctx_(),
    access_ctx_(),
    rowkey_read_info_(nullptr),
    access_param_(),
    allocator_("ForkSnapScan"),
    ls_id_(),
    fork_snapshot_version_(0)
{}

ObForkSnapshotRowScan::~ObForkSnapshotRowScan()
{
  if (OB_NOT_NULL(row_iter_)) {
    row_iter_->~ObSSTableRowWholeScanner();
    row_iter_ = nullptr;
  }
  if (nullptr != rowkey_read_info_) {
    rowkey_read_info_->~ObRowkeyReadInfo();
    allocator_.free(rowkey_read_info_);
    rowkey_read_info_ = nullptr;
  }
  allocator_.reset();
}

int ObForkSnapshotRowScan::build_rowkey_read_info(const ObForkScanParam &param)
{
  int ret = OB_SUCCESS;
  int64_t full_stored_col_cnt = 0;
  ObSEArray<share::schema::ObColDesc, 16> cols_desc;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(param));
  } else if (OB_FAIL(param.storage_schema_->get_mulit_version_rowkey_column_ids(cols_desc))) {
    LOG_WARN("fail to get rowkey column ids", K(ret), KPC(param.storage_schema_));
  } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator_, rowkey_read_info_))) {
    LOG_WARN("fail to allocate and new rowkey read info", K(ret));
  } else if (OB_FAIL(param.storage_schema_->get_store_column_count(full_stored_col_cnt, true/*full col*/))) {
    LOG_WARN("fail to get store column count", K(ret), KPC(param.storage_schema_));
  } else if (OB_FAIL(rowkey_read_info_->init(allocator_,
                                             full_stored_col_cnt,
                                             param.storage_schema_->get_rowkey_column_num(),
                                             param.storage_schema_->is_oracle_mode(),
                                             cols_desc,
                                             false /*is_cg_sstable*/,
                                             false /*use_default_compat_version*/,
                                             false/*is_cs_replica_compat*/))) {
    LOG_WARN("fail to init rowkey read info", K(ret), KPC(param.storage_schema_));
  }
  if (OB_FAIL(ret) && nullptr != rowkey_read_info_) {
    rowkey_read_info_->~ObRowkeyReadInfo();
    allocator_.free(rowkey_read_info_);
    rowkey_read_info_ = nullptr;
  }
  return ret;
}

int ObForkSnapshotRowScan::construct_access_param(const ObForkScanParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(param));
  } else if (OB_FAIL(build_rowkey_read_info(param))) {
    LOG_WARN("build rowkey read info failed", K(ret), K(param));
  } else {
    const ObTabletID &tablet_id = param.tablet_handle_.get_obj()->get_tablet_meta().tablet_id_;
    if (OB_FAIL(access_param_.init_merge_param(
          param.table_id_,
          tablet_id,
          *rowkey_read_info_,
          true/*is_multi_version_minor_merge*/,
          false/*is_delete_insert*/))) {
      LOG_WARN("init table access param failed", K(ret), KPC(rowkey_read_info_), K(param));
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("construct table access param failed", KR(ret), K(param));
  } else {
    const ObTabletID &tablet_id = param.tablet_handle_.get_obj()->get_tablet_meta().tablet_id_;
    LOG_DEBUG("construct table access param finished", "table_id", param.table_id_, K(tablet_id));
  }
  return ret;
}

int ObForkSnapshotRowScan::construct_access_ctx(
    const share::ObLSID &ls_id,
    const ObTabletID &tablet_id,
    const int64_t fork_snapshot_version)
{
  int ret = OB_SUCCESS;
  ObQueryFlag query_flag(ObQueryFlag::Forward,
      false, /* daily merge*/
      true,  /* use *optimize */
      true, /* use whole macro scan*/
      false, /* not full row*/
      false, /* not index_back*/
      false);/* query stat */
  query_flag.disable_cache();
  query_flag.set_skip_running_tx(true);

  common::ObVersionRange trans_version_range;
  trans_version_range.snapshot_version_ = fork_snapshot_version;  // Use fork_snapshot_version to filter rows
  trans_version_range.multi_version_start_ = 1;
  trans_version_range.base_version_ = 0;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(ls_id));
  } else {
    share::SCN snapshot_scn;
    if (OB_FAIL(snapshot_scn.convert_for_tx(fork_snapshot_version))) {
      LOG_WARN("failed to convert snapshot version", K(ret), K(fork_snapshot_version));
    } else if (OB_FAIL(ctx_.init_for_read(ls_id,
                                          tablet_id,
                                          INT64_MAX, // query_expire_ts
                                          -1, // lock_timeout_us
                                          snapshot_scn))) {  // Use fork_snapshot_version SCN instead of SCN::max_scn()
      LOG_WARN("fail to init store ctx", K(ret), K(ls_id), K(snapshot_scn));
    } else if (OB_FAIL(access_ctx_.init(query_flag,
                                        ctx_,
                                        allocator_,
                                        allocator_,
                                        trans_version_range))) {
      LOG_WARN("fail to init accesss ctx", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("construct access ctx failed", KR(ret), K(ls_id), K(tablet_id), K(fork_snapshot_version));
  } else {
    LOG_DEBUG("construct access ctx finished", K(ls_id), K(tablet_id), K(fork_snapshot_version));
  }
  return ret;
}

int ObForkSnapshotRowScan::init(
    const ObForkScanParam &param,
    blocksstable::ObSSTable &sstable,
    const share::ObLSID &ls_id,
    const int64_t fork_snapshot_version)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid() || !sstable.is_valid() || !ls_id.is_valid() || fork_snapshot_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(param), K(sstable), K(ls_id), K(fork_snapshot_version));
  } else if (OB_FAIL(construct_access_param(param))) {
    LOG_WARN("construct access param failed", K(ret), K(param));
  } else {
    const ObTabletID &tablet_id = param.tablet_handle_.get_obj()->get_tablet_meta().tablet_id_;
    if (OB_FAIL(construct_access_ctx(ls_id, tablet_id, fork_snapshot_version))) {
      LOG_WARN("construct access ctx failed", K(ret), K(ls_id), K(tablet_id), K(fork_snapshot_version));
    } else if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObSSTableRowWholeScanner)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc mem failed", K(ret));
    } else if (FALSE_IT(row_iter_ = new(buf)ObSSTableRowWholeScanner())) {
    } else if (OB_FAIL(row_iter_->init(access_param_.iter_param_,
                                       access_ctx_,
                                       &sstable,
                                       param.query_range_))) {
      LOG_WARN("construct iterator failed", K(ret));
    } else {
      ls_id_ = ls_id;
      fork_snapshot_version_ = fork_snapshot_version;
      is_inited_ = true;
    }
  }
  if (OB_FAIL(ret)) {
    if (nullptr != row_iter_) {
      row_iter_->~ObSSTableRowWholeScanner();
      row_iter_ = nullptr;
    }
    if (nullptr != buf) {
      allocator_.free(buf);
      buf = nullptr;
    }
  }
  return ret;
}

int ObForkSnapshotRowScan::get_next_row(const ObDatumRow *&tmp_row)
{
  int ret = OB_SUCCESS;
  tmp_row = nullptr;
  const ObDatumRow *row = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(row_iter_->get_next_row(row))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      LOG_WARN("fail to get next row", K(ret));
    }
  } else if (OB_UNLIKELY(nullptr == row || !row->is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected datum row", K(ret), KPC(row));
  } else {
    tmp_row = row;
  }
  return ret;
}

ObTabletForkParam::ObTabletForkParam()
  : is_inited_(false),
    tenant_id_(OB_INVALID_ID),
    ls_id_(),
    table_id_(OB_INVALID_ID),
    schema_version_(0),
    task_id_(0),
    source_tablet_id_(),
    dest_tablet_id_(),
    fork_snapshot_version_(0),
    compat_mode_(lib::Worker::CompatMode::MYSQL),
    data_format_version_(0),
    consumer_group_id_(0)
{
}

ObTabletForkParam::ObTabletForkParam(const ObTabletForkParam &other)
  : is_inited_(other.is_inited_),
    tenant_id_(other.tenant_id_),
    ls_id_(other.ls_id_),
    table_id_(other.table_id_),
    schema_version_(other.schema_version_),
    task_id_(other.task_id_),
    source_tablet_id_(other.source_tablet_id_),
    dest_tablet_id_(other.dest_tablet_id_),
    fork_snapshot_version_(other.fork_snapshot_version_),
    compat_mode_(other.compat_mode_),
    data_format_version_(other.data_format_version_),
    consumer_group_id_(other.consumer_group_id_)
{
}

ObTabletForkParam &ObTabletForkParam::operator=(const ObTabletForkParam &other)
{
  if (this != &other) {
    is_inited_ = other.is_inited_;
    tenant_id_ = other.tenant_id_;
    ls_id_ = other.ls_id_;
    table_id_ = other.table_id_;
    schema_version_ = other.schema_version_;
    task_id_ = other.task_id_;
    source_tablet_id_ = other.source_tablet_id_;
    dest_tablet_id_ = other.dest_tablet_id_;
    fork_snapshot_version_ = other.fork_snapshot_version_;
    compat_mode_ = other.compat_mode_;
    data_format_version_ = other.data_format_version_;
    consumer_group_id_ = other.consumer_group_id_;
  }
  return *this;
}

ObTabletForkParam::~ObTabletForkParam()
{
  reset();
}

void ObTabletForkParam::reset()
{
  is_inited_ = false;
  tenant_id_ = OB_INVALID_ID;
  ls_id_.reset();
  table_id_ = OB_INVALID_ID;
  schema_version_ = 0;
  task_id_ = 0;
  source_tablet_id_.reset();
  dest_tablet_id_.reset();
  fork_snapshot_version_ = 0;
  compat_mode_ = lib::Worker::CompatMode::MYSQL;
  data_format_version_ = 0;
  consumer_group_id_ = 0;
}

bool ObTabletForkParam::is_valid() const
{
  return OB_INVALID_ID != tenant_id_
      && ls_id_.is_valid()
      && OB_INVALID_ID != table_id_
      && schema_version_ > 0
      && task_id_ > 0
      && source_tablet_id_.is_valid()
      && dest_tablet_id_.is_valid()
      && fork_snapshot_version_ > 0
      && compat_mode_ != lib::Worker::CompatMode::INVALID
      && data_format_version_ > 0
      && consumer_group_id_ >= 0;
}

int ObTabletForkParam::init(const ObTabletForkParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(param));
  } else {
    tenant_id_ = param.tenant_id_;
    ls_id_ = param.ls_id_;
    table_id_ = param.table_id_;
    schema_version_ = param.schema_version_;
    task_id_ = param.task_id_;
    source_tablet_id_ = param.source_tablet_id_;
    dest_tablet_id_ = param.dest_tablet_id_;
    fork_snapshot_version_ = param.fork_snapshot_version_;
    compat_mode_ = param.compat_mode_;
    data_format_version_ = param.data_format_version_;
    consumer_group_id_ = param.consumer_group_id_;
    is_inited_ = true;
  }
  return ret;
}

ObTabletForkCtx::ObTabletForkCtx()
  : range_allocator_("ForkRangeCtx", OB_MALLOC_NORMAL_BLOCK_SIZE /*8KB*/, MTL_ID()),
    allocator_("ForkCtx", OB_MALLOC_NORMAL_BLOCK_SIZE /*8KB*/, MTL_ID()),
    allocator_lock_(),
    is_inited_(false),
    complement_data_ret_(OB_SUCCESS),
    ls_handle_(),
    src_tablet_handle_(),
    dst_tablet_handle_(),
    index_builder_map_(),
    clipped_schemas_map_(),
    created_sstable_handles_(),
    created_sstable_handles_lock_(),
    row_inserted_(0),
    ls_rebuild_seq_(-1)
{
}

ObTabletForkCtx::~ObTabletForkCtx()
{
  is_inited_ = false;
  ls_rebuild_seq_ = -1;
  complement_data_ret_ = OB_SUCCESS;
  ls_handle_.reset();
  src_tablet_handle_.reset();
  dst_tablet_handle_.reset();
  (void)ObTabletRebuildUtil::destroy_value_ptr_map<ObForkSSTableTaskKey, ObSSTableIndexBuilder>(allocator_, index_builder_map_);
  (void)ObTabletRebuildUtil::destroy_value_ptr_map<ObITable::TableKey, ObStorageSchema>(allocator_, clipped_schemas_map_);
  created_sstable_handles_.reset();
  range_allocator_.reset();
  allocator_.reset();
}

bool ObTabletForkCtx::is_valid() const
{
  return is_inited_ && ls_handle_.is_valid() && src_tablet_handle_.is_valid() && dst_tablet_handle_.is_valid();
}

int ObTabletForkCtx::add_created_sstable(const ObTableHandleV2 &handle)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(created_sstable_handles_lock_);
  if (OB_FAIL(created_sstable_handles_.add_table(handle))) {
    LOG_WARN("failed to add table handle", K(ret), K(handle));
  }
  return ret;
}

int ObTabletForkCtx::get_created_sstables(common::ObIArray<ObITable *> &tables)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(created_sstable_handles_lock_);
  if (OB_FAIL(created_sstable_handles_.get_tables(tables))) {
    LOG_WARN("get created sstables failed", K(ret));
  }
  return ret;
}

int ObTabletForkCtx::get_created_sstable(const int64_t idx, ObTableHandleV2 &table_handle)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(created_sstable_handles_lock_);
  if (OB_FAIL(created_sstable_handles_.get_table(idx, table_handle))) {
    LOG_WARN("get table handle failed", K(ret), K(idx));
  }
  return ret;
}

int ObTabletForkCtx::init(const ObTabletForkParam &param)
{
  int ret = OB_SUCCESS;
  bool is_satisfied = false;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(param));
  } else if (OB_FAIL(ObTabletForkUtil::check_satisfy_fork_condition(param, is_satisfied))) {
    LOG_WARN("check satisfy fork condition failed", K(ret), K(param));
  } else if (!is_satisfied) {
    ret = OB_NEED_RETRY;
    if (REACH_TIME_INTERVAL(5L * 1000L * 1000L)) { // 5s
      LOG_INFO("fork condition not satisfied yet, need retry", K(param));
    } else {
      LOG_DEBUG("fork condition not satisfied yet, need retry", K(param));
    }
  } else if (OB_FAIL(MTL(ObLSService *)->get_ls(param.ls_id_, ls_handle_, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("failed to get log stream", K(ret), K(param));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls_handle_,
      param.source_tablet_id_, src_tablet_handle_, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
    LOG_WARN("get source tablet failed", K(ret), K(param.source_tablet_id_));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls_handle_,
      param.dest_tablet_id_, dst_tablet_handle_, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
    LOG_WARN("get destination tablet failed", K(ret), K(param.dest_tablet_id_));
  } else {
    ls_rebuild_seq_ = ls_handle_.get_ls()->get_rebuild_seq();
    complement_data_ret_ = OB_SUCCESS;
    is_inited_ = true;
  }

  return ret;
}

int ObTabletForkCtx::prepare_index_builder(const ObTabletForkParam &param)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> sstables;
  const ObStorageSchema *storage_schema = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(param));
  } else if (OB_UNLIKELY(index_builder_map_.created())) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_FAIL(index_builder_map_.create(8, "ForkSstIdxMap"))) {
    LOG_WARN("create index builder map failed", K(ret));
  } else {
    ObTableStoreIterator table_store_iterator;
    if (OB_FAIL(src_tablet_handle_.get_obj()->get_all_tables(table_store_iterator))) {
      LOG_WARN("fail to fetch table store", K(ret));
    } else if (OB_FAIL(ObTabletForkUtil::get_participants(table_store_iterator, param.fork_snapshot_version_, sstables))) {
      LOG_WARN("get participant sstables failed", K(ret));
    } else {
      ObStorageSchema *tmp_storage_schema = nullptr;
      if (OB_FAIL(src_tablet_handle_.get_obj()->load_storage_schema(allocator_, tmp_storage_schema))) {
        LOG_WARN("failed to load storage schema", K(ret));
      } else if (OB_ISNULL(tmp_storage_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("storage schema is null", K(ret));
      } else {
        storage_schema = tmp_storage_schema;
      }
    }
  }

  if (OB_SUCC(ret) && OB_NOT_NULL(storage_schema)) {
    compaction::ObExecMode exec_mode = ObExecMode::EXEC_MODE_LOCAL;

    for (int64_t i = 0; OB_SUCC(ret) && i < sstables.count(); i++) {
      blocksstable::ObSSTable *sstable = static_cast<blocksstable::ObSSTable *>(sstables.at(i));
      void *buf = nullptr;
      ObWholeDataStoreDesc data_desc;
      ObSSTableIndexBuilder *sstable_index_builder = nullptr;
      ObForkSSTableTaskKey key;
      key.src_sst_key_ = sstable->get_key();
      key.dest_tablet_id_ = param.dest_tablet_id_;

      const ObMergeType merge_type = sstable->is_major_sstable() ? MAJOR_MERGE : MINOR_MERGE;
      // For fork table, use fork_snapshot_version instead of sstable's snapshot_version
      const int64_t snapshot_version = param.fork_snapshot_version_;
      const ObStorageSchema *clipped_storage_schema = nullptr;

      if (OB_FAIL(ObTabletRebuildUtil::get_clipped_storage_schema_on_demand(
          allocator_, clipped_schemas_map_,
          param.source_tablet_id_, *sstable, *storage_schema,
          true/*try_create*/, clipped_storage_schema))) {
        LOG_WARN("get storage schema via sstable failed", K(ret));
      } else if (OB_FAIL(data_desc.init(
          true/*is_ddl*/, *clipped_storage_schema, param.ls_id_,
          param.dest_tablet_id_, merge_type, snapshot_version, param.data_format_version_,
          dst_tablet_handle_.get_obj()->get_tablet_meta().micro_index_clustered_,
          dst_tablet_handle_.get_obj()->get_transfer_seq(),
          0/*concurrent_cnt*/,
          sstable->get_end_scn(),
          nullptr/*cg_schema*/,
          0/*table_cg_idx*/,
          exec_mode))) {
        LOG_WARN("fail to init data store desc", K(ret), K(param.dest_tablet_id_), K(param));
      } else if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObSSTableIndexBuilder)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc memory failed", K(ret));
      } else if (FALSE_IT(sstable_index_builder = new (buf) ObSSTableIndexBuilder(false/*use double write buffer*/))) {
      } else if (OB_FAIL(sstable_index_builder->init(data_desc.get_desc(), ObSSTableIndexBuilder::DISABLE))) {
        LOG_WARN("init sstable index builder failed", K(ret));
      } else if (OB_FAIL(index_builder_map_.set_refactored(key, sstable_index_builder))) {
        LOG_WARN("set refactored failed", K(ret));
      }

      if (OB_FAIL(ret)) {
        if (nullptr != sstable_index_builder) {
          sstable_index_builder->~ObSSTableIndexBuilder();
          sstable_index_builder = nullptr;
        }
        if (nullptr != buf) {
          allocator_.free(buf);
          buf = nullptr;
        }
      }
    }
  }
  return ret;
}

ObTabletForkDag::ObTabletForkDag()
  : ObIDag(ObDagType::DAG_TYPE_FORK_TABLE),
    is_inited_(false),
    param_(),
    context_(),
    consumer_group_id_(0)
{
}

ObTabletForkDag::~ObTabletForkDag()
{
}

int ObTabletForkDag::init_by_param(const share::ObIDagInitParam *param)
{
  int ret = OB_SUCCESS;
  const ObTabletForkParam *tmp_param = static_cast<const ObTabletForkParam *>(param);
  if (OB_UNLIKELY(nullptr == tmp_param || !tmp_param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), KPC(tmp_param));
  } else if (OB_FAIL(param_.init(*tmp_param))) {
    LOG_WARN("init fork table param failed", K(ret));
  } else if (OB_UNLIKELY(!param_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(param_));
  } else if (OB_FAIL(context_.init(param_))) {
    if (OB_NEED_RETRY != ret) {
      LOG_WARN("init failed", K(ret));
    } else if (REACH_COUNT_INTERVAL(1000L)) {
      LOG_INFO("wait conditions satisfied", K(ret), KPC(tmp_param));
    }
  } else {
    consumer_group_id_ = tmp_param->consumer_group_id_;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletForkDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> src_sstables;
  ObTabletForkPrepareTask *prepare_task = nullptr;
  ObTabletForkMergeTask *merge_task = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObTableStoreIterator table_store_iterator;
    if (OB_FAIL(context_.src_tablet_handle_.get_obj()->get_all_tables(table_store_iterator))) {
      LOG_WARN("fail to fetch table store", K(ret));
    } else if (OB_FAIL(ObTabletForkUtil::get_participants(
        table_store_iterator, param_.fork_snapshot_version_, src_sstables))) {
      LOG_WARN("get all sstables failed", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(alloc_task(prepare_task))) {
      LOG_WARN("allocate task failed", K(ret));
    } else if (OB_ISNULL(prepare_task)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr task", K(ret));
    } else if (OB_FAIL(prepare_task->init(param_, context_))) {
      LOG_WARN("init prepare task failed", K(ret));
    } else if (OB_FAIL(add_task(*prepare_task))) {
      LOG_WARN("add task failed", K(ret));
    } else if (OB_FAIL(alloc_task(merge_task))) {
      LOG_WARN("alloc task failed", K(ret));
    } else if (OB_ISNULL(merge_task)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr task", K(ret));
    } else if (OB_FAIL(merge_task->init(param_, context_))) {
      LOG_WARN("init merge task failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < src_sstables.count(); i++) {
        blocksstable::ObSSTable *sstable = static_cast<blocksstable::ObSSTable *>(src_sstables.at(i));
        if (OB_ISNULL(sstable)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected nullptr sstable", K(ret), K(param_));
        } else if (sstable->get_upper_trans_version() <= param_.fork_snapshot_version_) {
          ObTabletForkReuseTask *reuse_task = nullptr;
          if (OB_FAIL(alloc_task(reuse_task))) {
            LOG_WARN("alloc reuse task failed", K(ret));
          } else if (OB_ISNULL(reuse_task)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected nullptr task", K(ret));
          } else if (OB_FAIL(reuse_task->init(param_, context_, sstable))) {
            LOG_WARN("init reuse task failed", K(ret));
          } else if (OB_FAIL(prepare_task->add_child(*reuse_task))) {
            LOG_WARN("add child task failed", K(ret));
          } else if (OB_FAIL(add_task(*reuse_task))) {
            LOG_WARN("add task failed", K(ret));
          } else if (OB_FAIL(reuse_task->add_child(*merge_task))) {
            LOG_WARN("add child task failed", K(ret));
          }
        } else {
          ObTabletForkRewriteTask *rewrite_task = nullptr;
          if (OB_FAIL(alloc_task(rewrite_task))) {
            LOG_WARN("alloc rewrite task failed", K(ret));
          } else if (OB_ISNULL(rewrite_task)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected nullptr task", K(ret));
          } else if (OB_FAIL(rewrite_task->init(param_, context_, sstable))) {
            LOG_WARN("init rewrite task failed", K(ret));
          } else if (OB_FAIL(prepare_task->add_child(*rewrite_task))) {
            LOG_WARN("add child task failed", K(ret));
          } else if (OB_FAIL(add_task(*rewrite_task))) {
            LOG_WARN("add task failed", K(ret));
          } else if (OB_FAIL(rewrite_task->add_child(*merge_task))) {
            LOG_WARN("add child task failed", K(ret));
          }
        }
      }

      if (FAILEDx(add_task(*merge_task))) {
        LOG_WARN("add task failed", K(ret));
      }
    }
  }

  FLOG_INFO("create first task finish", K(ret), K(src_sstables.count()), K(param_), K(context_));
  return ret;
}

uint64_t ObTabletForkDag::hash() const
{
  int ret = OB_SUCCESS;
  uint64_t hash_val = 0;
  if (OB_UNLIKELY(!is_inited_ || !param_.is_valid())) {
    ret = OB_ERR_SYS;
    LOG_ERROR("invalid argument", K(ret), K(is_inited_), K(param_));
  } else {
    hash_val = param_.tenant_id_ + param_.ls_id_.hash()
             + param_.table_id_ + param_.schema_version_
             + param_.source_tablet_id_.hash() + param_.dest_tablet_id_.hash()
             + static_cast<uint64_t>(param_.fork_snapshot_version_)
             + ObDagType::DAG_TYPE_FORK_TABLE;
  }
  return hash_val;
}

bool ObTabletForkDag::operator==(const ObIDag &other) const
{
  int ret = OB_SUCCESS;
  bool is_equal = false;
  if (OB_UNLIKELY(this == &other)) {
    is_equal = true;
  } else if (get_type() == other.get_type()) {
    const ObTabletForkDag &dag = static_cast<const ObTabletForkDag &>(other);
    if (OB_UNLIKELY(!param_.is_valid() || !dag.param_.is_valid())) {
      ret = OB_ERR_SYS;
      LOG_WARN("invalid argument", K(ret), K(param_), K(dag.param_));
    } else {
      is_equal = param_.tenant_id_ == dag.param_.tenant_id_
              && param_.ls_id_ == dag.param_.ls_id_
              && param_.schema_version_ == dag.param_.schema_version_
              && param_.source_tablet_id_ == dag.param_.source_tablet_id_
              && param_.dest_tablet_id_ == dag.param_.dest_tablet_id_;
    }
  }
  return is_equal;
}

int ObTabletForkDag::fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletForkDag has not been initialized", K(ret));
  } else if (OB_FAIL(ADD_DAG_WARN_INFO_PARAM(out_param, allocator, get_type(),
      static_cast<int64_t>(param_.ls_id_.id()), static_cast<int64_t>(param_.source_tablet_id_.id())))) {
    LOG_WARN("failed to fill info param", K(ret));
  }
  return ret;
}

int ObTabletForkDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObForkTableDag has not been initialized", K(ret));
  } else if (OB_UNLIKELY(!param_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param", K(ret), K(param_));
  } else if (OB_FAIL(databuff_printf(buf, buf_len,
      "Fork table: src_tablet_id=%ld, dst_tablet_id=%ld, fork_snapshot_version=%ld, tenant_id=%lu, ls_id=%ld, schema_version=%ld",
      param_.source_tablet_id_.id(), param_.dest_tablet_id_.id(), param_.fork_snapshot_version_,
      param_.tenant_id_, param_.ls_id_.id(), param_.schema_version_))) {
    LOG_WARN("fail to fill dag key", K(ret), K(param_));
  }
  return ret;
}

int ObTabletForkDag::calc_total_row_count()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("has not been inited", K(ret));
  } else if (OB_UNLIKELY(!param_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(param_));
  }
  LOG_INFO("calc row count of the src tablet", K(ret), K(context_));
  return ret;
}

int ObTabletForkPrepareTask::init(ObTabletForkParam &param, ObTabletForkCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid() || !ctx.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(param), K(ctx));
  } else {
    param_ = &param;
    context_ = &ctx;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletForkPrepareTask::prepare_context()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(context_->prepare_index_builder(*param_))) {
    LOG_WARN("prepare index builder failed", K(ret), KPC_(param));
  }
  return ret;
}

int ObTabletForkPrepareTask::process()
{
  int ret = OB_SUCCESS;
  bool is_fork_data_complete = false;
  ObIDag *tmp_dag = get_dag();
  ObTabletForkDag *dag = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(tmp_dag)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err, dag is null", K(ret), KP(tmp_dag));
  } else if (OB_ISNULL(dag = static_cast<ObTabletForkDag *>(tmp_dag))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err, dag type mismatch", K(ret), KP(tmp_dag), KP(dag));
  } else if (OB_SUCCESS != (context_->complement_data_ret_)) {
    LOG_WARN("complement data has already failed", KPC(context_));
  } else if (OB_FAIL(ObTabletForkUtil::check_fork_data_complete(param_->dest_tablet_id_, is_fork_data_complete))) {
    LOG_WARN("check fork data complete failed", K(ret));
  } else if (is_fork_data_complete) {
    LOG_INFO("fork table task has already finished", KPC(param_));
  } else if (OB_FAIL(prepare_context())) {
    LOG_WARN("prepare index builder map failed", K(ret), KPC(param_));
  } else if (OB_FAIL(dag->calc_total_row_count())) {
    LOG_WARN("failed to calc task row count", K(ret));
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(context_)) {
    context_->complement_data_ret_ = ret;
    ret = OB_SUCCESS;
  }
  return ret;
}

ObTabletForkReuseTask::ObTabletForkReuseTask()
  : ObITask(TASK_TYPE_DDL_FORK_REUSE),
    is_inited_(false),
    param_(nullptr),
    context_(nullptr),
    sstable_(nullptr),
    allocator_("ForkReuseTask", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID())
{
}

ObTabletForkReuseTask::~ObTabletForkReuseTask()
{
  allocator_.reset();
}

int ObTabletForkReuseTask::init(
    ObTabletForkParam &param,
    ObTabletForkCtx &ctx,
    storage::ObITable *sstable)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid() || !ctx.is_valid() || nullptr == sstable)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(param), K(ctx), KP(sstable));
  } else {
    param_ = &param;
    context_ = &ctx;
    sstable_ = static_cast<blocksstable::ObSSTable *>(sstable);
    is_inited_ = true;
  }
  return ret;
}

int ObTabletForkReuseTask::process()
{
  int ret = OB_SUCCESS;
  bool is_fork_data_complete = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_SUCCESS != (context_->complement_data_ret_)) {
    LOG_WARN("complement data has already failed", KPC(context_));
  } else if (OB_FAIL(ObTabletForkUtil::check_fork_data_complete(param_->dest_tablet_id_, is_fork_data_complete))) {
    LOG_WARN("check fork data complete failed", K(ret));
  } else if (is_fork_data_complete) {
    LOG_INFO("fork table task has already finished", KPC(param_));
  } else if (OB_FAIL(process_reuse_sstable())) {
    LOG_WARN("process reuse sstable failed", K(ret), KPC(sstable_));
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(context_)) {
    context_->complement_data_ret_ = ret;
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTabletForkReuseTask::process_reuse_sstable()
{
  int ret = OB_SUCCESS;
  ObTabletCreateSSTableParam param;
  ObTableHandleV2 table_handle;
  blocksstable::ObMigrationSSTableParam mig_sstable_param;
  share::SCN fork_snapshot_scn;

  if (OB_ISNULL(sstable_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("sstable is null", K(ret));
  } else if (OB_FAIL(fork_snapshot_scn.convert_for_tx(param_->fork_snapshot_version_))) {
    LOG_WARN("failed to convert fork snapshot version to scn", K(ret), K(param_->fork_snapshot_version_));
  } else {
    ObITable::TableKey src_table_key = sstable_->get_key();
    ObSSTableMetaHandle meta_handle;
    if (OB_FAIL(sstable_->get_meta(meta_handle))) {
      LOG_WARN("failed to get sstable meta", K(ret));
    } else if (OB_FAIL(context_->src_tablet_handle_.get_obj()->build_migration_sstable_param(
        src_table_key, mig_sstable_param, true/*is_fork_table*/))) {
      LOG_WARN("failed to build migration sstable param", K(ret), K(src_table_key));
    } else if (OB_FAIL(param.init_for_fork(mig_sstable_param, param_->dest_tablet_id_, src_table_key, meta_handle.get_sstable_meta(), fork_snapshot_scn))) {
      LOG_WARN("init for fork failed", K(ret), K(param_->dest_tablet_id_), K(src_table_key), K(fork_snapshot_scn));
    } else if (OB_FAIL(context_->create_sstable(param, table_handle))) {
      LOG_WARN("failed to create sstable with reused blocks", K(ret));
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(context_->add_created_sstable(table_handle))) {
        LOG_WARN("failed to add table handle", K(ret));
      } else {
        LOG_DEBUG("fork reuse: successfully reused sstable", K(sstable_->get_key()), K(param_->dest_tablet_id_));
        (void) ATOMIC_AAFx(&context_->row_inserted_, sstable_->get_row_count(), 0);
      }
    }
  }

  return ret;
}

ObTabletForkRewriteTask::ObTabletForkRewriteTask()
  : ObITask(TASK_TYPE_DDL_FORK_REWRITE),
    is_inited_(false),
    param_(nullptr),
    context_(nullptr),
    sstable_(nullptr),
    allocator_("ForkRewriteTask", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID())
{
}

ObTabletForkRewriteTask::~ObTabletForkRewriteTask()
{
  allocator_.reset();
}

int ObTabletForkRewriteTask::init(
    ObTabletForkParam &param,
    ObTabletForkCtx &ctx,
    storage::ObITable *sstable)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid() || !ctx.is_valid() || nullptr == sstable)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(param), K(ctx), KP(sstable));
  } else {
    param_ = &param;
    context_ = &ctx;
    sstable_ = static_cast<blocksstable::ObSSTable *>(sstable);
    is_inited_ = true;
  }
  return ret;
}

int ObTabletForkRewriteTask::process()
{
  int ret = OB_SUCCESS;
  const ObStorageSchema *clipped_storage_schema = nullptr;
  bool is_fork_data_complete = false;
  ObWholeDataStoreDesc data_desc;
  ObMacroBlockWriter *macro_block_writer = nullptr;
  ObSSTableIndexBuilder *sst_idx_builder = nullptr;
  ObITable::TableKey src_table_key = sstable_->get_key();
  ObForkSSTableTaskKey task_key(src_table_key, param_->dest_tablet_id_);
  ObSSTableMergeRes merge_res;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_SUCCESS != (context_->complement_data_ret_)) {
    LOG_WARN("complement data has already failed", KPC(context_));
  } else if (OB_FAIL(ObTabletForkUtil::check_fork_data_complete(param_->dest_tablet_id_, is_fork_data_complete))) {
    LOG_WARN("check fork data complete failed", K(ret));
  } else if (is_fork_data_complete) {
    LOG_DEBUG("fork table task has already finished", KPC(param_));
  } else if (OB_FAIL(prepare_context(clipped_storage_schema))) {
    LOG_WARN("prepare context failed", K(ret));
  } else if (OB_FAIL(prepare_macro_block_writer(*clipped_storage_schema, data_desc, macro_block_writer))) {
    LOG_WARN("prepare macro block writer failed", K(ret));
  } else if (OB_FAIL(process_rewrite_sstable_task(macro_block_writer, *clipped_storage_schema))) {
    LOG_WARN("process rewrite sstable task failed", K(ret));
  } else if (OB_NOT_NULL(macro_block_writer)) {
    if (OB_FAIL(macro_block_writer->close())) {
      LOG_WARN("close macro block writer failed", K(ret));
    } else if (OB_FAIL(context_->index_builder_map_.get_refactored(task_key, sst_idx_builder))) {
      LOG_WARN("get index builder failed", K(ret), K(task_key));
    } else if (OB_ISNULL(sst_idx_builder)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("index builder is null", K(ret));
    } else if (OB_FAIL(sst_idx_builder->close(merge_res))) {
      LOG_WARN("close index builder failed", K(ret));
    } else {
      ObTabletCreateSSTableParam create_param;
      ObSSTableMetaHandle meta_handle;
      ObTableHandleV2 table_handle;
      if (OB_FAIL(sstable_->get_meta(meta_handle))) {
        LOG_WARN("get sstable meta failed", K(ret));
      } else {
        const ObSSTableBasicMeta &basic_meta = meta_handle.get_sstable_meta().get_basic_meta();
        // For fork rewrite: limit end_scn to fork_snapshot_version
        // This ensures the rewritten SSTable's end_scn does not exceed fork_snapshot_version
        share::SCN max_end_scn;
        if (OB_FAIL(max_end_scn.convert_for_tx(param_->fork_snapshot_version_))) {
          LOG_WARN("failed to convert fork snapshot version to scn", K(ret), KPC(param_), K(param_->fork_snapshot_version_));
        } else if (OB_FAIL(create_param.init_for_split(param_->dest_tablet_id_, src_table_key, basic_meta,
            basic_meta.schema_version_, merge_res, max_end_scn))) {
          LOG_WARN("init create param failed", K(ret), K(max_end_scn));
        } else if (OB_FAIL(context_->create_sstable(create_param, table_handle))) {
          LOG_WARN("failed to create sstable", K(ret));
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(context_->add_created_sstable(table_handle))) {
            LOG_WARN("failed to add table handle", K(ret));
          } else {
            LOG_DEBUG("fork rewrite: successfully created sstable",
                K(sstable_->get_key()), K(param_->dest_tablet_id_), K(max_end_scn), K(param_->fork_snapshot_version_));
          }
        }
      }
    }
  }

  if (OB_NOT_NULL(macro_block_writer)) {
    macro_block_writer->~ObMacroBlockWriter();
    allocator_.free(macro_block_writer);
    macro_block_writer = nullptr;
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(context_)) {
    context_->complement_data_ret_ = ret;
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTabletForkRewriteTask::prepare_context(const ObStorageSchema *&clipped_storage_schema)
{
  int ret = OB_SUCCESS;
  clipped_storage_schema = nullptr;
  ObStorageSchema *storage_schema = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(context_->src_tablet_handle_.get_obj()->load_storage_schema(
      allocator_, storage_schema))) {
    LOG_WARN("failed to load storage schema", K(ret));
  } else if (OB_ISNULL(storage_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("storage schema is null", K(ret));
  } else if (OB_FAIL(ObTabletRebuildUtil::get_clipped_storage_schema_on_demand(
      context_->get_allocator(), context_->clipped_schemas_map_,
      param_->source_tablet_id_, *sstable_, *storage_schema,
      false/*try_create*/, clipped_storage_schema))) {
    LOG_WARN("get storage schema via sstable failed", K(ret));
  }

  return ret;
}

int ObTabletForkRewriteTask::prepare_macro_block_writer(
    const ObStorageSchema &clipped_storage_schema,
    ObWholeDataStoreDesc &data_desc,
    ObMacroBlockWriter *&macro_block_writer)
{
  int ret = OB_SUCCESS;
  macro_block_writer = nullptr;
  ObSSTableMetaHandle meta_handle;
  ObMacroDataSeq macro_start_seq(0);
  ObForkSSTableTaskKey task_key(sstable_->get_key(), param_->dest_tablet_id_);
  ObSSTableIndexBuilder *sst_idx_builder = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(sstable_->get_meta(meta_handle))) {
    LOG_WARN("get sstable meta failed", K(ret));
  } else if (OB_FAIL(macro_start_seq.set_sstable_seq(meta_handle.get_sstable_meta().get_sstable_seq()))) {
    LOG_WARN("set sstable logical seq failed", K(ret));
  } else if (OB_FAIL(context_->index_builder_map_.get_refactored(task_key, sst_idx_builder))) {
    LOG_WARN("get index builder failed", K(ret), K(task_key));
  } else {
    const ObMergeType merge_type = sstable_->is_major_sstable() ? MAJOR_MERGE : MINOR_MERGE;
    // For fork table, use fork_snapshot_version instead of sstable's snapshot_version
    const int64_t snapshot_version = param_->fork_snapshot_version_;
    const bool micro_index_clustered = context_->dst_tablet_handle_.get_obj()->get_tablet_meta().micro_index_clustered_;
    compaction::ObExecMode exec_mode = ObExecMode::EXEC_MODE_LOCAL;

    if (OB_FAIL(data_desc.init(
        true/*is_ddl*/, clipped_storage_schema,
        param_->ls_id_,
        param_->dest_tablet_id_,
        merge_type,
        snapshot_version,
        param_->data_format_version_,
        micro_index_clustered,
        context_->dst_tablet_handle_.get_obj()->get_transfer_seq(),
        0/*concurrent_cnt*/,
        sstable_->get_end_scn(),
        nullptr/* cg_schema */,
        0/* table_cg_idx */,
        exec_mode))) {
      LOG_WARN("fail to init data store desc", K(ret), K(param_->dest_tablet_id_), KPC(param_));
    } else if (FALSE_IT(data_desc.get_desc().sstable_index_builder_ = sst_idx_builder)) {
    } else if (FALSE_IT(data_desc.get_static_desc().is_ddl_ = true)) {
    } else {
      void *buf = nullptr;
      ObPreWarmerParam pre_warm_param;
      ObSSTablePrivateObjectCleaner *object_cleaner = nullptr;

      if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObMacroBlockWriter)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc mem failed", K(ret));
      } else if (FALSE_IT(macro_block_writer = new (buf) ObMacroBlockWriter())) {
      } else if (OB_FAIL(pre_warm_param.init(param_->ls_id_, param_->dest_tablet_id_))) {
        LOG_WARN("failed to init pre warm param", K(ret), K(param_->dest_tablet_id_), KPC(param_));
      } else if (OB_FAIL(ObSSTablePrivateObjectCleaner::get_cleaner_from_data_store_desc(
                             data_desc.get_desc(),
                             object_cleaner))) {
        LOG_WARN("failed to get cleaner from data store desc", K(ret));
      } else {
        ObMacroSeqParam macro_seq_param;
        macro_seq_param.seq_type_ = ObMacroSeqParam::SEQ_TYPE_INC;
        macro_seq_param.start_ = macro_start_seq.macro_data_seq_;

        if (OB_FAIL(macro_block_writer->open(data_desc.get_desc(),
             macro_start_seq.get_parallel_idx(), macro_seq_param, pre_warm_param, *object_cleaner))) {
          LOG_WARN("open macro_block_writer failed", K(ret), K(data_desc));
        }
      }

      if (OB_FAIL(ret) && nullptr != macro_block_writer) {
        macro_block_writer->~ObMacroBlockWriter();
        allocator_.free(macro_block_writer);
        macro_block_writer = nullptr;
      }
    }
  }

  return ret;
}

int ObTabletForkRewriteTask::process_rewrite_sstable_task(
    ObMacroBlockWriter *macro_block_writer,
    const ObStorageSchema &clipped_storage_schema)
{
  int ret = OB_SUCCESS;
  ObForkSnapshotRowScan row_scan;
  ObDatumRange whole_range;
  whole_range.set_whole_range();

  if (OB_UNLIKELY(!is_inited_ || nullptr == macro_block_writer)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), KP(macro_block_writer));
  } else {
    ObForkScanParam scan_param(
        param_->table_id_,
        context_->src_tablet_handle_,
        whole_range,
        clipped_storage_schema);

    if (OB_FAIL(row_scan.init(scan_param, *sstable_, param_->ls_id_, param_->fork_snapshot_version_))) {
      LOG_WARN("init fork snapshot row scan failed", K(ret));
    } else {
      const ObDatumRow *row = nullptr;
      int64_t row_count = 0;

      while (OB_SUCC(ret)) {
        if (OB_FAIL(row_scan.get_next_row(row))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_WARN("get next row failed", K(ret));
          }
        } else if (OB_ISNULL(row)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("row is null", K(ret));
        } else {
          row_count++;
          if (OB_FAIL(macro_block_writer->append_row(*row, nullptr))) {
            LOG_WARN("append row failed", K(ret));
          }

          if (row_count % 10000 == 0) {
            (void) ATOMIC_AAFx(&context_->row_inserted_, 10000, 0);
            LOG_DEBUG("fork rewrite: processed rows", K(row_count), K(sstable_->get_key()));
          }
        }
      }

      if (OB_SUCC(ret)) {
        (void) ATOMIC_AAFx(&context_->row_inserted_, row_count % 10000, 0);
        LOG_DEBUG("fork rewrite: finished processing sstable", K(row_count), K(sstable_->get_key()));
      }
    }
  }

  return ret;
}

// ==================== ObForkTableMergeTask Implementation ====================

int ObTabletForkMergeTask::init(ObTabletForkParam &param, ObTabletForkCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid() || !ctx.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(param), K(ctx));
  } else {
    param_ = &param;
    context_ = &ctx;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletForkMergeTask::process()
{
  int ret = OB_SUCCESS;
  bool is_fork_data_complete = false;
  ObIDag *tmp_dag = get_dag();
  ObTabletForkDag *dag = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(tmp_dag)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err, dag is null", K(ret), KP(tmp_dag));
  } else if (OB_ISNULL(dag = static_cast<ObTabletForkDag *>(tmp_dag))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err, dag type mismatch", K(ret), KP(tmp_dag), KP(dag));
  } else if (OB_SUCCESS != (context_->complement_data_ret_)) {
    LOG_WARN("complement data has already failed", KPC(context_));
  } else if (OB_FAIL(ObTabletForkUtil::check_fork_data_complete(param_->dest_tablet_id_, is_fork_data_complete))) {
    LOG_WARN("check fork data complete failed", K(ret));
  } else if (is_fork_data_complete) {
    LOG_DEBUG("fork table task has already finished", KPC(param_));
  } else if (OB_FAIL(create_sstables())) {
    LOG_WARN("create sstables failed", K(ret));
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(context_)) {
    context_->complement_data_ret_ = ret;
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTabletForkMergeTask::create_sstables()
{
  int ret = OB_SUCCESS;
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> created_sstables;
  bool is_fork_data_complete = false;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ObTabletForkUtil::check_fork_data_complete(param_->dest_tablet_id_, is_fork_data_complete))) {
    LOG_WARN("check fork data complete failed", K(ret));
  } else if (is_fork_data_complete) {
    LOG_INFO("fork table task has already finished", KPC(param_));
  } else if (OB_FAIL(context_->get_created_sstables(created_sstables))) {
    LOG_WARN("get created sstables failed", K(ret));
  } else {
    const int64_t src_table_cnt = created_sstables.count();
    ObTablesHandleArray batch_sstables_handle;
    const compaction::ObMergeType merge_type = compaction::ObMergeType::MAJOR_MERGE;

    // Find the minor sstable with the maximum end_scn (since sstables may not be in fixed order)
    const ObITable *max_end_scn_minor_sstable = nullptr;
    share::SCN max_end_scn;
    max_end_scn.set_min();
    for (int64_t i = 0; i < created_sstables.count(); ++i) {
      const ObITable *table = created_sstables.at(i);
      if (OB_NOT_NULL(table) && table->is_minor_sstable()) {
        const share::SCN end_scn = table->get_end_scn();
        if (end_scn > max_end_scn) {
          max_end_scn = end_scn;
          max_end_scn_minor_sstable = table;
        }
      }
    }

    // Collect all created sstables into batch handle
    for (int64_t j = 0; OB_SUCC(ret) && j < src_table_cnt; j++) {
      const ObITable *table = created_sstables.at(j);
      ObTableHandleV2 table_handle;
      if (OB_ISNULL(table)) {
        ret = OB_ERR_SYS;
        LOG_WARN("Error sys", K(ret), K(param_->dest_tablet_id_), K(created_sstables));
      } else if (OB_FAIL(context_->get_created_sstable(j, table_handle))) {
        LOG_WARN("get table handle failed", K(ret), K(j));
      } else {
            LOG_DEBUG("fork merge: adding sstable to batch", K(j), "table_key", table->get_key(),
                 "batch_count", batch_sstables_handle.get_count());
        if (OB_FAIL(batch_sstables_handle.add_table(table_handle))) {
          LOG_WARN("add table failed", K(ret));
        }
      }
    }

    // Check if need to fill empty minor sstable after processing all sstables
    // Use the minor sstable with maximum end_scn for checking
    if (OB_SUCC(ret) && OB_NOT_NULL(max_end_scn_minor_sstable)) {
      bool need_fill_empty_sstable = false;
      share::SCN end_scn;
      if (OB_FAIL(ObTabletRebuildUtil::check_need_fill_empty_sstable(
              context_->ls_handle_,
              max_end_scn_minor_sstable->is_minor_sstable(),
              max_end_scn_minor_sstable->get_key(),
              param_->dest_tablet_id_,
              need_fill_empty_sstable,
              end_scn))) {
        LOG_WARN("failed to check need fill empty sstable", K(ret));
      } else if (need_fill_empty_sstable) {
        ObTabletCreateSSTableParam create_sstable_param;
        ObTableHandleV2 table_handle;
        table_handle.reset();
        ObSSTableMetaHandle meta_handle;
        const blocksstable::ObSSTable *sstable = static_cast<const blocksstable::ObSSTable *>(max_end_scn_minor_sstable);
        if (OB_ISNULL(sstable)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("sstable is null", K(ret));
        } else if (OB_FAIL(sstable->get_meta(meta_handle))) {
          LOG_WARN("get meta failed", K(ret));
        } else if (OB_FAIL(ObTabletRebuildUtil::build_create_empty_sstable_param(
                meta_handle.get_sstable_meta().get_basic_meta(),
                max_end_scn_minor_sstable->get_key(),
                param_->dest_tablet_id_,
                end_scn,
                create_sstable_param))) {
          LOG_WARN("failed to build create empty sstable param", K(ret));
        } else if (OB_FAIL(context_->create_sstable(create_sstable_param, table_handle))) {
          LOG_WARN("create empty sstable failed", K(ret), K(create_sstable_param));
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(batch_sstables_handle.add_table(table_handle))) {
            LOG_WARN("add empty sstable failed", K(ret));
          } else {
            LOG_DEBUG("fork merge: added empty minor sstable", K(end_scn),
                K(max_end_scn_minor_sstable->get_key()), K(max_end_scn));
          }
        }
      }
    }

    // Batch update table store with all created sstables
    if (OB_SUCC(ret) && src_table_cnt > 0) {
      if (OB_FAIL(ObTabletForkMergeTask::update_table_store_with_batch_tables(
              context_->ls_rebuild_seq_,
              context_->ls_handle_,
              context_->dst_tablet_handle_,
              param_->dest_tablet_id_,
              batch_sstables_handle,
              merge_type))) {
        LOG_WARN("update table store with batch tables failed", K(ret), K(batch_sstables_handle));
      }
    }
  }

  return ret;
}

int ObTabletForkMergeTask::update_table_store_with_batch_tables(
    const int64_t ls_rebuild_seq,
    const ObLSHandle &ls_handle,
    const ObTabletHandle &dst_tablet_handle,
    const ObTabletID &dst_tablet_id,
    const ObTablesHandleArray &tables_handle,
    const compaction::ObMergeType &merge_type)
{
  int ret = OB_SUCCESS;
  ObBatchUpdateTableStoreParam param;
  param.reset();
  ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> batch_tables;

  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(ls_rebuild_seq == -1
      || !ls_handle.is_valid()
      || !dst_tablet_handle.is_valid()
      || !dst_tablet_id.is_valid()
      || !is_valid_merge_type(merge_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(ls_rebuild_seq), K(ls_handle), K(dst_tablet_handle),
      K(dst_tablet_id), K(merge_type));
  } else if (OB_FAIL(tables_handle.get_tables(batch_tables))) {
    LOG_WARN("fork table: get batch sstables failed", KR(ret), KPC_(param), K(dst_tablet_id));
  } else if (OB_FAIL(param.tables_handle_.assign(tables_handle))) {
    LOG_WARN("fork table: assign tables handle failed", KR(ret), KPC_(param), K(dst_tablet_id));
    // TODO(fankun.fan): meta major sstable
  } else {
    param.rebuild_seq_ = ls_rebuild_seq;
    param.release_mds_scn_.set_min();
    param.tablet_fork_param_.snapshot_version_ = param_->fork_snapshot_version_;
    param.tablet_fork_param_.multi_version_start_ = param_->fork_snapshot_version_;
    param.tablet_fork_param_.merge_type_ = merge_type;
    share::SCN clog_checkpoint_scn = dst_tablet_handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_;
    share::SCN mds_checkpoint_scn = dst_tablet_handle.get_obj()->get_tablet_meta().mds_checkpoint_scn_;

    bool found_minor_sstable = false;
    int64_t minor_cnt = 0;
    int64_t major_cnt = 0;
    int64_t min_start_scn_val = INT64_MAX;
    int64_t max_end_scn_val = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_tables.count(); ++i) {
      const ObITable *table = batch_tables.at(i);
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fork table: table is null in batch_tables", KR(ret), KPC_(param), K(dst_tablet_id), K(i));
      } else if (table->is_minor_sstable()) {
        found_minor_sstable = true;
        ++minor_cnt;
        const share::SCN end_scn = table->get_end_scn();
        if (end_scn > clog_checkpoint_scn) {
          clog_checkpoint_scn = end_scn;
        }
      } else if (table->is_major_sstable()) {
        ++major_cnt;
      }
      if (OB_NOT_NULL(table)) {
        const int64_t start_scn_val = table->get_start_scn().get_val_for_tx();
        const int64_t end_scn_val = table->get_end_scn().get_val_for_tx();
        min_start_scn_val = std::min(min_start_scn_val, start_scn_val);
        max_end_scn_val = std::max(max_end_scn_val, end_scn_val);
      }
    }
    param.tablet_fork_param_.clog_checkpoint_scn_ = clog_checkpoint_scn;
    param.tablet_fork_param_.mds_checkpoint_scn_ = mds_checkpoint_scn;

    if (OB_FAIL(ls_handle.get_ls()->build_tablet_with_batch_tables(dst_tablet_id, param))) {
      LOG_WARN("fork table: update tablet table store failed", KR(ret), KPC_(param), K(dst_tablet_id),
          "batch_cnt", batch_tables.count(), K(minor_cnt), K(major_cnt),
          "min_start_scn", min_start_scn_val, "max_end_scn", max_end_scn_val,
          K(clog_checkpoint_scn), K(mds_checkpoint_scn), K(param));
      LOG_DEBUG("fork table: batch tables detail", KPC_(param), K(dst_tablet_id), K(batch_tables));
    } else {
      LOG_INFO("fork table: updated tablet table store with batch sstables", KPC_(param), K(dst_tablet_id),
          "batch_cnt", batch_tables.count(), K(minor_cnt), K(major_cnt),
          "min_start_scn", min_start_scn_val, "max_end_scn", max_end_scn_val,
          K(clog_checkpoint_scn), K(mds_checkpoint_scn),
          "found_minor_sstable", found_minor_sstable);
    }
  }

  return ret;
}

int ObTabletForkUtil::check_satisfy_fork_condition(
  const ObTabletForkParam &param,
  bool &is_satisfied)
{
  int ret = OB_SUCCESS;
  is_satisfied = false;
  bool need_freeze = false;
  ObLSHandle ls_handle;
  ObTabletHandle tablet_handle;
  ObArray<ObTableHandleV2> memtable_handles;

  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(param));
  } else if (OB_FAIL(MTL(ObLSService *)->get_ls(param.ls_id_, ls_handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("failed to get ls", K(ret), K(param.ls_id_));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls_handle, param.source_tablet_id_, tablet_handle, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    LOG_WARN("failed to get tablet", K(ret), K(param.source_tablet_id_));
  } else if (OB_UNLIKELY(nullptr == tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet is null", K(ret), K(param.source_tablet_id_));
  } else {
    const share::ObForkTabletInfo &fork_info = tablet_handle.get_obj()->get_tablet_meta().fork_info_;
    if (fork_info.get_fork_src_tablet_id().is_valid() && !fork_info.is_complete()) {
      // Current tablet still carries an incomplete fork mark, wait and retry later.
      is_satisfied = false;
      if (REACH_TIME_INTERVAL(5L * 1000L * 1000L)) { // 5s
        LOG_INFO("fork condition not satisfied: tablet fork info not complete", K(param), K(fork_info));
      } else {
        LOG_DEBUG("fork condition not satisfied: tablet fork info not complete", K(param), K(fork_info));
      }
    } else if (OB_FAIL(tablet_handle.get_obj()->get_all_memtables_from_memtable_mgr(memtable_handles))) {
      LOG_WARN("failed to get all memtables from memtable_mgr", K(ret), K(param.source_tablet_id_));
    } else {
      is_satisfied = true;

      for (int64_t i = 0; OB_SUCC(ret) && i < memtable_handles.count(); ++i) {
        const ObTableHandleV2 &memtable_handle = memtable_handles.at(i);
        if (OB_ISNULL(memtable_handle.get_table())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("memtable is null", K(ret), K(i));
        } else {
          const int64_t memtable_start_scn = memtable_handle.get_table()->get_start_scn().get_val_for_tx();
          if (memtable_start_scn < param.fork_snapshot_version_) {
            is_satisfied = false;
            need_freeze = true;
            if (REACH_COUNT_INTERVAL(1000L)) {
              LOG_INFO("memtable with start_scn < fork_snapshot_version exists, need wait dump",
                      K(param), K(memtable_start_scn));
            }
            break;
          }
        }
      }
    }

    if (OB_SUCC(ret) && need_freeze) {
      if (OB_FAIL(ObTabletForkUtil::freeze_tablet(param.ls_id_, param.source_tablet_id_))) {
        LOG_WARN("failed to freeze tablet", K(ret), K(param.ls_id_), K(param.source_tablet_id_));
      }
      ob_usleep(100 * 1000L); // 100ms
    }
  }

  return ret;
}

int ObTabletForkUtil::check_fork_data_complete(
  const ObTabletID &dest_tablet_id,
  bool &is_complete)
{
  int ret = OB_SUCCESS;
  is_complete = false;
  ObLSHandle ls_handle;
  ObTabletHandle tablet_handle;
  share::ObForkTabletInfo fork_info;

  if (OB_UNLIKELY(!dest_tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(SYS_LS), K(dest_tablet_id));
  } else if (OB_FAIL(MTL(ObLSService *)->get_ls(SYS_LS, ls_handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("failed to get log stream", K(ret), K(SYS_LS));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls_handle, dest_tablet_id,
      tablet_handle, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    LOG_WARN("failed to get tablet handle", K(ret), K(SYS_LS), K(dest_tablet_id));
  } else if (OB_UNLIKELY(nullptr == tablet_handle.get_obj())) {
    ret = OB_ERR_SYS;
    LOG_WARN("tablet handle is null", K(ret), K(SYS_LS), K(dest_tablet_id));
  } else if (OB_FAIL(tablet_handle.get_obj()->get_fork_info(fork_info))) {
    LOG_WARN("failed to get fork_info from tablet", K(ret), K(SYS_LS), K(dest_tablet_id));
  } else if (fork_info.get_fork_src_tablet_id().is_valid() && fork_info.is_complete()) {
    is_complete = true;
    LOG_INFO("tablet fork data complement complete", K(dest_tablet_id), K(fork_info));
  } else {
    is_complete = false;
    if (REACH_TIME_INTERVAL(5L * 1000L * 1000L)) { // 5s
      LOG_INFO("tablet fork data not complete yet", K(dest_tablet_id), K(fork_info));
    } else {
      LOG_DEBUG("tablet fork data not complete yet", K(dest_tablet_id), K(fork_info));
    }
  }
  return ret;
}

int ObTabletForkUtil::get_participants(
  const ObTableStoreIterator &table_store_iter,
  const int64_t fork_snapshot_version,
  ObIArray<ObITable *> &participants)
{
  int ret = OB_SUCCESS;
  participants.reset();
  ObTableStoreIterator iter;
  int64_t total_cnt = 0;
  int64_t skip_mem_cnt = 0;
  int64_t skip_mds_cnt = 0;
  int64_t skip_future_cnt = 0;
  int64_t add_cnt = 0;

  if (OB_FAIL(iter.assign(table_store_iter))) {
    LOG_WARN("failed to assign table store iter", K(ret));
  } else {
    while (OB_SUCC(ret)) {
      ObITable *table = nullptr;
      if (OB_FAIL(iter.get_next(table))) {
        if (OB_UNLIKELY(OB_ITER_END == ret)) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("get next table failed", K(ret), K(iter));
        }
      } else if (OB_UNLIKELY(nullptr == table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table is null", KR(ret), K(iter));
      } else if (table->is_memtable()) {
        ++skip_mem_cnt;
        LOG_DEBUG("fork table: skip memtable", KPC(table));
      } else if (table->is_mds_sstable()) {
        ++skip_mds_cnt;
        LOG_DEBUG("fork table: skip mds sstable", KPC(table));
      } else if (OB_UNLIKELY(!table->is_sstable() || (!table->is_minor_sstable() && !table->is_major_sstable()))) {
        ret = OB_STATE_NOT_MATCH;
        LOG_WARN("unexpected table type when collecting fork participants", KR(ret), KPC(table));
      } else {
        // Filter by fork_snapshot_version: only include sstables with start_scn <= fork_snapshot_version
        const int64_t table_start_scn = table->get_start_scn().get_val_for_tx();
        if (table_start_scn > fork_snapshot_version) {
          ++skip_future_cnt;
          LOG_DEBUG("fork table: skip sstable with start_scn > fork_snapshot_version",
              K(table_start_scn), K(fork_snapshot_version), KPC(table));
        } else if (OB_FAIL(participants.push_back(table))) {
          LOG_WARN("failed to push back sstable", KR(ret), KPC(table));
        } else {
          ++add_cnt;
        }
      }
      ++total_cnt;
    }
  }
  if (OB_SUCC(ret) && (skip_future_cnt > 0 || participants.empty())) {
    LOG_INFO("fork table: participants filtered", K(fork_snapshot_version),
        K(total_cnt), K(add_cnt), K(skip_mem_cnt), K(skip_mds_cnt), K(skip_future_cnt));
  } else if (OB_SUCC(ret)) {
    LOG_DEBUG("fork table: participants filtered", K(fork_snapshot_version),
        K(total_cnt), K(add_cnt), K(skip_mem_cnt), K(skip_mds_cnt), K(skip_future_cnt));
  }
  return ret;
}

int ObTabletForkUtil::try_schedule_fork_dags(const ObTableForkInfo &fork_info)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObTabletForkParam, 4> fork_params;

  if (OB_UNLIKELY(!fork_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fork info", K(ret), K(fork_info));
  } else if (OB_FAIL(fork_info.generate_fork_params(fork_params))) {
    LOG_WARN("failed to generate fork params from fork info", K(ret), K(fork_info));
  } else {
    int64_t scheduled_cnt = 0;
    int64_t exist_cnt = 0;
    int64_t fail_cnt = 0;
    LOG_INFO("fork table: schedule fork dags begin",
        "task_id", fork_info.task_id_,
        "tenant_id", fork_info.tenant_id_,
        K(fork_info.ls_id_),
        "table_id", fork_info.table_id_,
        "schema_version", fork_info.schema_version_,
        "fork_snapshot_version", fork_info.fork_snapshot_version_,
        "tablet_cnt", fork_params.count());
    for (int64_t i = 0; OB_SUCC(ret) && i < fork_params.count(); ++i) {
      ObTabletForkParam &fork_param = fork_params.at(i);
      if (OB_FAIL(compaction::ObScheduleDagFunc::schedule_tablet_fork_dag(fork_param, false /* is_emergency */))) {
        if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
          ++fail_cnt;
          LOG_WARN("failed to schedule tablet fork dag", K(ret), K(fork_param));
        } else if (OB_EAGAIN == ret) {
          ++exist_cnt;
          LOG_DEBUG("exists same dag, wait the dag to finish", K(ret), K(fork_param));
          ret = OB_SUCCESS;
        }
      } else {
        ++scheduled_cnt;
        LOG_DEBUG("scheduled fork dag", K(fork_param.task_id_), K(fork_param.source_tablet_id_),
            K(fork_param.dest_tablet_id_), K(fork_param.fork_snapshot_version_));
      }
    }
    LOG_INFO("fork table: schedule fork dags finish",
        "task_id", fork_info.task_id_,
        "tenant_id", fork_info.tenant_id_,
        K(fork_info.ls_id_),
        "tablet_cnt", fork_params.count(),
        K(scheduled_cnt), K(exist_cnt), K(fail_cnt),
        KR(ret));
  }
  return ret;
}

int ObTabletForkUtil::freeze_tablet(
    const share::ObLSID &ls_id,
    const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObTenantFreezer *tenant_freezer = MTL(ObTenantFreezer*);

  if (OB_ISNULL(tenant_freezer)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObTenantFreezer is null", K(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid()) || OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(ls_id), K(tablet_id));
  } else {
    // Freeze the tablet
    const bool is_sync = true;
    const int64_t max_retry_time_us = 0; // Not used for sync freeze
    const bool need_rewrite_tablet_meta = false;
    const ObFreezeSourceFlag source = ObFreezeSourceFlag::FREEZE_TRIGGER;

    if (OB_FAIL(tenant_freezer->tablet_freeze(ls_id,
                                              tablet_id,
                                              is_sync,
                                              max_retry_time_us,
                                              need_rewrite_tablet_meta,
                                              source))) {
      if (OB_EAGAIN == ret) {
        ret = OB_SUCCESS;
        LOG_INFO("tablet freeze in progress", K(tablet_id));
      } else {
        LOG_WARN("failed to freeze tablet", K(ret), K(tablet_id));
      }
    } else {
      LOG_INFO("tablet freeze completed", K(tablet_id));
    }
  }
  return ret;
}

int ObTabletForkUtil::freeze_tablets(
    const share::ObLSID &ls_id,
    const ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid()) || OB_UNLIKELY(tablet_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(ls_id), K(tablet_ids.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
      const ObTabletID &tablet_id = tablet_ids.at(i);
      if (OB_FAIL(ObTabletForkUtil::freeze_tablet(ls_id, tablet_id))) {
        LOG_WARN("failed to freeze tablet", K(ret), K(tablet_id));
      }
    }
  }
  return ret;
}

}  // end namespace storage
}  // end namespace oceanbase
