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
#include "ob_transfer_struct.h"
#include "common/ob_version_def.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "src/storage/tx_storage/ob_ls_map.h"
#include "storage/compaction/ob_medium_compaction_func.h"
#include "storage/ob_storage_schema_util.h"
#include "share/schema/ob_tenant_schema_service.h"

using namespace oceanbase;
using namespace share;
using namespace storage;
using namespace transaction;

ObTXStartTransferOutInfo::ObTXStartTransferOutInfo()
  : src_ls_id_(),
    dest_ls_id_(),
    tablet_list_(),
    task_id_(),
    data_end_scn_(),
    transfer_epoch_(0),
    data_version_(DEFAULT_MIN_DATA_VERSION),
    filter_tx_need_transfer_(false),
    move_tx_ids_()
{
}


bool ObTXStartTransferOutInfo::is_valid() const
{
  return src_ls_id_.is_valid()
    && dest_ls_id_.is_valid()
    && !tablet_list_.empty()
    && data_version_ > 0;
}


int64_t ObTXStartTransferOutInfo::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t save_pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
      // do nothing
  } else {
    ObClusterVersion data_version;
    if (OB_FAIL(data_version.init(data_version_))) {
      LOG_WARN("failed to init data version", K(ret), K(data_version_));
    } else {
      J_OBJ_START();
      J_KV(K_(src_ls_id), K_(dest_ls_id), K_(tablet_list), K_(task_id),
           K_(data_end_scn), K_(transfer_epoch), K(data_version),
           K_(filter_tx_need_transfer), K_(move_tx_ids));
      J_OBJ_END();
    }
  }
  return pos;
}

OB_SERIALIZE_MEMBER(ObTXStartTransferOutInfo, src_ls_id_, dest_ls_id_, tablet_list_, task_id_,
    data_end_scn_, transfer_epoch_, data_version_, filter_tx_need_transfer_, move_tx_ids_);


ObTXStartTransferInInfo::ObTXStartTransferInInfo()
  : src_ls_id_(),
    dest_ls_id_(),
    start_scn_(),
    tablet_meta_list_(),
    task_id_(),
    data_version_(DEFAULT_MIN_DATA_VERSION)
{
}


bool ObTXStartTransferInInfo::is_valid() const
{
  return src_ls_id_.is_valid()
      && dest_ls_id_.is_valid()
      && start_scn_.is_valid()
      && !tablet_meta_list_.empty()
      && data_version_ > 0;
}


int ObTXStartTransferInInfo::get_tablet_id_list(common::ObIArray<common::ObTabletID> &tablet_id_list) const
{
  int ret = OB_SUCCESS;
  tablet_id_list.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_meta_list_.count(); ++i) {
    const ObMigrationTabletParam &tablet_meta = tablet_meta_list_.at(i);
    if (OB_FAIL(tablet_id_list.push_back(tablet_meta.tablet_id_))) {
      LOG_WARN("failed to push tablet id into array", K(ret), K(tablet_meta));
    }
  }
  return ret;
}

int64_t ObTXStartTransferInInfo::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t save_pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
      // do nothing
  } else {
    ObClusterVersion data_version;
    if (OB_FAIL(data_version.init(data_version_))) {
      LOG_WARN("failed to init data version", K(ret), K(data_version_));
    } else {
      J_OBJ_START();
      J_KV(K_(src_ls_id), K_(dest_ls_id), K_(start_scn), K_(tablet_meta_list), K_(task_id), K(data_version));
      J_OBJ_END();
    }
  }
  return pos;
}

OB_SERIALIZE_MEMBER(ObTXStartTransferInInfo, src_ls_id_, dest_ls_id_, start_scn_, tablet_meta_list_, task_id_, data_version_);

/* ObTXFinishTransferInInfo */

OB_SERIALIZE_MEMBER(ObTXFinishTransferInInfo, src_ls_id_, dest_ls_id_, start_scn_, tablet_list_, task_id_, data_version_);
ObTXFinishTransferInInfo::ObTXFinishTransferInInfo()
  : src_ls_id_(),
    dest_ls_id_(),
    start_scn_(),
    tablet_list_(),
    task_id_(),
    data_version_(DEFAULT_MIN_DATA_VERSION)
{
}


bool ObTXFinishTransferInInfo::is_valid() const
{
  return src_ls_id_.is_valid()
      && dest_ls_id_.is_valid()
      && start_scn_.is_valid()
      && !tablet_list_.empty()
      && data_version_ > 0;
}


int64_t ObTXFinishTransferInInfo::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t save_pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
      // do nothing
  } else {
    ObClusterVersion data_version;
    if (OB_FAIL(data_version.init(data_version_))) {
      LOG_WARN("failed to init data version", K(ret), K(data_version_));
    } else {
      J_OBJ_START();
      J_KV(K_(src_ls_id), K_(dest_ls_id), K_(start_scn), K_(tablet_list), K_(task_id), K(data_version));
      J_OBJ_END();
    }
  }
  return pos;
}

/* ObTXFinishTransferOutInfo */
OB_SERIALIZE_MEMBER(ObTXFinishTransferOutInfo, src_ls_id_, dest_ls_id_, finish_scn_, tablet_list_, task_id_, data_version_);
ObTXFinishTransferOutInfo::ObTXFinishTransferOutInfo()
  : src_ls_id_(),
    dest_ls_id_(),
    finish_scn_(),
    tablet_list_(),
    task_id_(),
    data_version_(DEFAULT_MIN_DATA_VERSION)
{
}


bool ObTXFinishTransferOutInfo::is_valid() const
{
  return src_ls_id_.is_valid()
      && dest_ls_id_.is_valid()
      && finish_scn_.is_valid()
      && !tablet_list_.empty()
      && data_version_ > 0;
}


int64_t ObTXFinishTransferOutInfo::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t save_pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
      // do nothing
  } else {
    ObClusterVersion data_version;
    if (OB_FAIL(data_version.init(data_version_))) {
      LOG_WARN("failed to init data version", K(ret), K(data_version_));
    } else {
      J_OBJ_START();
      J_KV(K_(src_ls_id), K_(dest_ls_id), K_(finish_scn), K_(tablet_list), K_(task_id), K(data_version));
      J_OBJ_END();
    }
  }
  return pos;
}

ObTXTransferInAbortedInfo::ObTXTransferInAbortedInfo()
  : dest_ls_id_(),
    tablet_list_(),
    data_version_(CLUSTER_VERSION_1_0_0_0)
{
}


bool ObTXTransferInAbortedInfo::is_valid() const
{
  return dest_ls_id_.is_valid()
      && !tablet_list_.empty()
      && data_version_ > 0;
}


int64_t ObTXTransferInAbortedInfo::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t save_pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
      // do nothing
  } else {
    ObClusterVersion data_version;
    if (OB_FAIL(data_version.init(data_version_))) {
      LOG_WARN("failed to init data version", K(ret), K(data_version_));
    } else {
      J_OBJ_START();
      J_KV(K_(dest_ls_id), K_(tablet_list), K(data_version));
      J_OBJ_END();
    }
  }
  return pos;
}

OB_SERIALIZE_MEMBER(ObTXTransferInAbortedInfo, dest_ls_id_, tablet_list_, data_version_);








int ObTXTransferUtils::get_tablet_status_(
   const bool get_commit,
   const ObTablet *tablet,
   ObTabletCreateDeleteMdsUserData &user_data)
{
  int ret = OB_SUCCESS;
  if (get_commit) {
    if (OB_FAIL(tablet->get_latest_committed(user_data))) {
      LOG_WARN("failed to get committed tablet status", K(ret), KPC(tablet), K(user_data));
    }
  } else {
    mds::MdsWriter unused_writer;// will be removed later
    mds::TwoPhaseCommitState unused_trans_stat;// will be removed later
    share::SCN unused_trans_version;// will be removed later
    if (OB_FAIL(tablet->get_latest(user_data,
        unused_writer, unused_trans_stat, unused_trans_version))) {
      LOG_WARN("failed to get latest tablet status", K(ret), KPC(tablet), K(user_data));
    }
  }
  return ret;
}

int ObTXTransferUtils::set_tablet_freeze_flag(storage::ObLS &ls, ObTablet *tablet)
{
  MDS_TG(10_ms);
  int ret = OB_SUCCESS;
  ObArray<ObTableHandleV2> memtables;
  ObTabletID tablet_id = tablet->get_tablet_meta().tablet_id_;
  SCN weak_read_scn;
  share::ObLSID ls_id = ls.get_ls_id();

  if (OB_ISNULL(tablet) ) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), KP(tablet));
  } else if (FALSE_IT(weak_read_scn = ls.get_ls_wrs_handler()->get_ls_weak_read_ts())) {
  } else if (!weak_read_scn.is_valid()
      || ObScnRange::MAX_SCN == weak_read_scn) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("weak read scn is invalid", K(ret), K(ls_id), K(weak_read_scn));
  } else if (ObScnRange::MIN_SCN == weak_read_scn) {
    ret = OB_EAGAIN;
    LOG_WARN("weak read service not inited, need to wait for weak read scn to advance", K(ret), K(ls_id), K(weak_read_scn));
  } else if (OB_FAIL(tablet->get_all_memtables_from_memtable_mgr(memtables))) {
    LOG_WARN("failed to get_memtable_mgr for get all memtable", K(ret), KPC(tablet));
  } else {
    CLICK();
    ObITabletMemtable *mt = nullptr;
    for (int64_t i = 0; OB_SUCC(ret) && i < memtables.count(); ++i) {
      if (OB_UNLIKELY(memtables.at(i).get_tablet_memtable(mt))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table in tables_handle is not memtable", K(ret), K(memtables.at(i)));
      } else if (!mt->is_active_memtable()) {
        // skip
      } else if (OB_UNLIKELY(!mt->is_data_memtable())) {
        // incremental direct load hold table lock will block transfer scheduling, so there will be no active direct load memtable
        ret = OB_TRANSFER_SYS_ERROR;
        LOG_WARN("memtable is not data memtable", K(ret), KPC(mt));
      } else {
        memtable::ObMemtable *memtable = static_cast<memtable::ObMemtable *>(mt);
        memtable->set_transfer_freeze(weak_read_scn);
      }
    }
    if (OB_SUCC(ret)) {
      LOG_INFO("succ set transfer freeze", K(tablet_id), K(ls_id));
    }
  }

  return ret;
}



int ObTXTransferUtils::build_empty_minor_sstable_param_(
    const SCN start_scn,
    const SCN end_scn,
    const ObStorageSchema &table_schema,
    const common::ObTabletID &tablet_id,
    ObTabletCreateSSTableParam &param)
{
  int ret = OB_SUCCESS;

  if (!start_scn.is_valid() || !end_scn.is_valid() || start_scn == end_scn
      || !table_schema.is_valid() || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build empty minor sstable param get invalid argument", K(ret), K(table_schema), K(tablet_id), K(start_scn), K(end_scn));
  } else if (OB_FAIL(param.init_for_transfer_empty_minor_sstable(tablet_id, start_scn, end_scn, table_schema))) {
    LOG_WARN("fail to init sstable param", K(ret), K(tablet_id), K(start_scn), K(end_scn), K(table_schema));
  }
  return ret;
}

/* ObTransferLockStatus */


const char *ObTransferLockStatus::str() const
{
  const char *str = "INVALID_STATUS";
  switch (status_) {
  case START: {
    str = "START";
    break;
  }
  case DOING: {
    str = "DOING";
    break;
  }
  case ABORTED: {
    str = "ABORTED";
    break;
  }
  default: {
    str = "INVALID_STATUS";
  }
  }
  return str;
}


/* ObTransferLockInfoRowKey */

ObTransferLockInfoRowKey::ObTransferLockInfoRowKey() : tenant_id_(), ls_id_()
{}


/* ObTransferTaskLockInfo */

ObTransferTaskLockInfo::ObTransferTaskLockInfo()
    : tenant_id_(), ls_id_(), task_id_(), status_(), lock_owner_(), comment_()
{}


bool ObTransferTaskLockInfo::is_valid() const
{
  return OB_INVALID_ID != tenant_id_ && ls_id_.is_valid() && task_id_ >= 0 && status_.is_valid()
      && lock_owner_ > 0;
}



/******************ObTransferTabletInfoMgr*********************/
ObTransferBuildTabletInfoCtx::ObTransferTabletInfoMgr::ObTransferTabletInfoMgr()
  : lock_(),
    tablet_info_array_(),
    storage_schema_mgr_()

{
}

ObTransferBuildTabletInfoCtx::ObTransferTabletInfoMgr::~ObTransferTabletInfoMgr()
{
  common::SpinWLockGuard guard(lock_);
  tablet_info_array_.reset();
  storage_schema_mgr_.reset();
}

int ObTransferBuildTabletInfoCtx::ObTransferTabletInfoMgr::add_tablet_info(
    const ObMigrationTabletParam &param)
{
  int ret = OB_SUCCESS;
  if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("add tablet info get invalid argument", K(ret), K(param));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (OB_FAIL(tablet_info_array_.push_back(param))) {
      LOG_WARN("failed to add tablet info", K(ret), K(param));
    }
  }
  return ret;
}

int64_t ObTransferBuildTabletInfoCtx::ObTransferTabletInfoMgr::get_tablet_info_num() const
{
  common::SpinRLockGuard guard(lock_);
  return tablet_info_array_.count();
}

int ObTransferBuildTabletInfoCtx::ObTransferTabletInfoMgr::get_tablet_info(
    const int64_t index, const ObMigrationTabletParam *&param)
{
  int ret = OB_SUCCESS;
  param = nullptr;
  common::SpinRLockGuard guard(lock_);
  if (index < 0 || index >= tablet_info_array_.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get tablet info get invalid argument", K(ret), K(index));
  } else {
    ObMigrationTabletParam &tmp_param = tablet_info_array_.at(index);
    ObStorageSchema *storage_schema = nullptr;
    if (OB_FAIL(storage_schema_mgr_.get_storage_schema(tmp_param.tablet_id_, storage_schema))) {
      LOG_WARN("failed to get storage schema", K(tmp_param));
    } else if (OB_ISNULL(storage_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("storage schema should not be NULL", K(ret), K(tmp_param));
    } else if (storage_schema->column_cnt_ > tmp_param.storage_schema_.column_cnt_) {
      LOG_INFO("modified storage schema", "new_storage_schema", *storage_schema,
          "old_storage_schema", tmp_param.storage_schema_);
      if (OB_FAIL(tmp_param.storage_schema_.assign(tmp_param.allocator_, *storage_schema))) {
        LOG_WARN("failed to assign storage schema", K(ret), K(tmp_param), KPC(storage_schema));
      }
    }

    if (OB_SUCC(ret)) {
      param = &tablet_info_array_.at(index);
    }
  }
  return ret;
}

void ObTransferBuildTabletInfoCtx::ObTransferTabletInfoMgr::reuse()
{
  common::SpinWLockGuard guard(lock_);
  tablet_info_array_.reset();
  storage_schema_mgr_.reset();
}

int ObTransferBuildTabletInfoCtx::ObTransferTabletInfoMgr::build_storage_schema(
    const share::ObTransferTaskInfo &task_info,
    ObTimeoutCtx &timeout_ctx)
{
  int ret = OB_SUCCESS;
  if (!task_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build storage schema info get invalid argument", K(ret), K(task_info));
  } else if (OB_FAIL(storage_schema_mgr_.init(task_info.tablet_list_.count()))) {
    LOG_WARN("failed to init storage schema mgr", K(ret), K(task_info));
  } else if (OB_FAIL(storage_schema_mgr_.build_storage_schema(task_info, timeout_ctx))) {
    LOG_WARN("failed to build storage schema", K(ret), K(task_info));
  }
  return ret;
}

/******************ObTransferBuildTabletInfoCtx*********************/
ObTransferBuildTabletInfoCtx::ObTransferBuildTabletInfoCtx()
  : lock_(),
    dest_ls_id_(),
    index_(0),
    tablet_info_array_(),
    child_task_num_(0),
    total_tablet_count_(0),
    result_(OB_SUCCESS),
    data_version_(0),
    task_id_(),
    mgr_()
{
}

ObTransferBuildTabletInfoCtx::~ObTransferBuildTabletInfoCtx()
{
}




bool ObTransferBuildTabletInfoCtx::is_valid_() const
{
  return index_ >= 0 && tablet_info_array_.count() >= 0 && index_ <= tablet_info_array_.count() && data_version_ > 0 && dest_ls_id_.is_valid();
}














/******************ObTransferStorageSchemaMgr*********************/
ObTransferBuildTabletInfoCtx::ObTransferStorageSchemaMgr::ObTransferStorageSchemaMgr()
  : is_inited_(false),
    allocator_(),
    storage_schema_map_()
{
  ObMemAttr attr(MTL_ID(), "TransferSchema");
  allocator_.set_attr(attr);
}

ObTransferBuildTabletInfoCtx::ObTransferStorageSchemaMgr::~ObTransferStorageSchemaMgr()
{
  reset();
}

void ObTransferBuildTabletInfoCtx::ObTransferStorageSchemaMgr::reset()
{
  FOREACH(iter, storage_schema_map_) {
    ObStorageSchema *storage_schema = iter->second;
    storage_schema->~ObStorageSchema();
  }
  storage_schema_map_.destroy();
  allocator_.reset();
  is_inited_ = false;
}

int ObTransferBuildTabletInfoCtx::ObTransferStorageSchemaMgr::init(const int64_t bucket_num)
{
  int ret = OB_SUCCESS;
  ObMemAttr attr(MTL_ID(), "TransferSchema");
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("transfer storage schema mgr is already init", K(ret));
  } else if (bucket_num <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("transfer storage schema mgr init get invalid argument", K(ret), K(bucket_num));
  } else if (OB_FAIL(storage_schema_map_.create(bucket_num, attr))) {
    LOG_WARN("failed to create storage schema map", K(ret), K(bucket_num));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObTransferBuildTabletInfoCtx::ObTransferStorageSchemaMgr::build_storage_schema(
    const share::ObTransferTaskInfo &task_info,
    ObTimeoutCtx &timeout_ctx)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("transfer storage schema mgr do not init", K(ret));
  } else if (!task_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build storage schema get invalid argument", K(ret), K(task_info));
  } else if (OB_FAIL(build_latest_storage_schema_(task_info, timeout_ctx))) {
    LOG_WARN("failed to build latest storage schema", K(ret));
  }
  return ret;
}

int ObTransferBuildTabletInfoCtx::ObTransferStorageSchemaMgr::get_storage_schema(
    const ObTabletID &tablet_id,
    ObStorageSchema *&storage_schema)
{
  int ret = OB_SUCCESS;
  storage_schema = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("transfer storage schema mgr do not init", K(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get storage schema get invalid argument", K(ret), K(tablet_id));
  } else if (OB_FAIL(storage_schema_map_.get_refactored(tablet_id, storage_schema))) {
    LOG_WARN("failed to get storage schema", K(ret), K(tablet_id));
  }
  return ret;
}

int ObTransferBuildTabletInfoCtx::ObTransferStorageSchemaMgr::build_latest_storage_schema_(
    const share::ObTransferTaskInfo &task_info,
    ObTimeoutCtx &timeout_ctx)
{
  int ret = OB_SUCCESS;
  ObSchemaService *server_schema_service = nullptr;
  ObMultiVersionSchemaService *schema_service = nullptr;
  const int64_t start_ts = ObTimeUtil::current_time();
  ObLSService *ls_service = nullptr;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("transfer storage schema mgr do not init", K(ret));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret));
  } else if (OB_ISNULL(server_schema_service = GCTX.schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("server_schema_service is null", KR(ret));
  } else if (OB_ISNULL(schema_service = MTL(ObTenantSchemaService *)->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get schema service from MTL", K(ret));
  } else if (OB_ISNULL(ls_service = MTL(ObLSService*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObLSService from MTL", K(ret), KP(ls_service));
  } else if (OB_FAIL(ls_service->get_ls(task_info.src_ls_id_, ls_handle, ObLSGetMod::HA_MOD))) {
    LOG_WARN("failed to get ls", K(ret), K(task_info));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", K(ret), KP(ls), K(task_info));
  } else {
    ObRefreshSchemaStatus status;
    status.tenant_id_ = task_info.tenant_id_;
    int64_t schema_version = 0;

    if (OB_FAIL(server_schema_service->fetch_schema_version(status, *GCTX.sql_proxy_, schema_version))) {
      LOG_WARN("fail to fetch schema version", KR(ret), K(status));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < task_info.tablet_list_.count(); ++i) {
        const ObTabletID &tablet_id = task_info.tablet_list_.at(i).tablet_id_;
        if (timeout_ctx.is_timeouted()) {
          ret = OB_TIMEOUT;
          LOG_WARN("transfer prepare storage schema timeout", K(ret));
        } else if (OB_FAIL(build_tablet_storage_schema_(task_info, tablet_id, schema_version, ls, *schema_service))) {
          LOG_WARN("failed to build tablet storage schema", K(ret), K(tablet_id));
        }
      }
    }
  }

  LOG_INFO("finish build storage schema", K(ret), "cost_ts", ObTimeUtil::current_time() - start_ts);
  return ret;
}

int ObTransferBuildTabletInfoCtx::ObTransferStorageSchemaMgr::build_tablet_storage_schema_(
    const share::ObTransferTaskInfo &task_info,
    const ObTabletID &tablet_id,
    const int64_t schema_version,
    ObLS *ls,
    ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObStorageSchema *storage_schema = nullptr;
  bool is_skip_merge_index = false;
  uint64_t compat_version = 0;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("transfer storage schema mgr do not init", K(ret));
  } else if (schema_version < 0 || !tablet_id.is_valid() || OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get table storage schema get invalid argument", K(ret), K(schema_version));
  } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle, 0,
      ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    LOG_WARN("failed to get tablet", K(ret), K(task_info));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), KP(tablet));
  } else if (OB_FAIL(GET_MIN_DATA_VERSION(task_info.tenant_id_, compat_version))) {
    LOG_WARN("fail to get data version", KR(ret), K(task_info));
  } else if (OB_FAIL(ObStorageSchemaUtil::alloc_storage_schema(allocator_, storage_schema))) {
    LOG_WARN("failed to alloc storage schema", K(ret));
  } else if (OB_FAIL(compaction::ObMediumCompactionScheduleFunc::get_table_schema_to_merge(
      schema_service, *tablet, schema_version, compat_version, allocator_, *storage_schema, is_skip_merge_index))) {
    LOG_WARN("failed to get table schema to merge", K(ret), KPC(tablet), K(task_info));
  } else if (OB_FAIL(storage_schema_map_.set_refactored(tablet_id, storage_schema))) {
    LOG_WARN("failed to push storage schema into map", K(ret), K(tablet_id), KPC(storage_schema));
  } else {
    storage_schema = nullptr;
  }

  if (OB_NOT_NULL(storage_schema)) {
    storage_schema->~ObStorageSchema();
  }
  return ret;
}
