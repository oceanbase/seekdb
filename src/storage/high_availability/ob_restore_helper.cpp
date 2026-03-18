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
#include "ob_restore_helper.h"
#include "storage/high_availability/ob_storage_ha_utils.h"
#include "storage/high_availability/ob_restore_helper_ctx.h"
#include "storage/ob_storage_grpc.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "share/ob_io_device_helper.h"
#include "observer/ob_server_struct.h"
#include "share/ob_cluster_version.h"

using namespace oceanbase::obgrpc;
using namespace storageservice;
using namespace oceanbase::blocksstable;

namespace oceanbase
{
namespace restore
{
static const int64_t RPC_TIMEOUT_US = 60L * 1000L * 1000L; // 60 seconds
static const int64_t MACRO_RANGE_MAX_MACRO_COUNT = 128;

static int64_t calc_ls_view_stream_timeout_us(const int64_t tablet_count)
{
  const int64_t LS_VIEW_TIMEOUT_TABLET_BATCH = 1000L;
  const int64_t LS_VIEW_TIMEOUT_PER_BATCH = 30L * 1000L * 1000L; // 30 seconds
  int64_t timeout_us = RPC_TIMEOUT_US;
  if (tablet_count > 0) {
    const int64_t batch_count = (tablet_count + LS_VIEW_TIMEOUT_TABLET_BATCH - 1) / LS_VIEW_TIMEOUT_TABLET_BATCH;
    timeout_us += batch_count * LS_VIEW_TIMEOUT_PER_BATCH;
  }
  return timeout_us;
}

static const char *restore_task_type_strs[] = {
  "STANDBY_RESTORE_TASK",
};

const char *ObRestoreTaskType::get_str(const TYPE &type)
{
  STATIC_ASSERT(static_cast<int64_t>(MAX_RESTORE_TASK_TYPE) == ARRAYSIZEOF(restore_task_type_strs),
                    "restore task type str len is mismatch");
  const char *str = nullptr;
  if (type < 0 || type >= MAX_RESTORE_TASK_TYPE) {
    str = "UNKNOWN_TYPE";
  } else {
    str = restore_task_type_strs[type];
  }
  return str;
}

ObRestoreTask::ObRestoreTask()
  : task_id_(),
    type_(ObRestoreTaskType::MAX_RESTORE_TASK_TYPE),
    src_info_()
{
}

ObRestoreTask::~ObRestoreTask()
{
}

void ObRestoreTask::reset()
{
  task_id_.reset();
  type_ = ObRestoreTaskType::MAX_RESTORE_TASK_TYPE;
  src_info_.reset();
}

bool ObRestoreTask::is_valid() const
{
  return task_id_.is_valid()
      && ObRestoreTaskType::is_valid(type_)
      && src_info_.is_valid();
}

DEF_TO_STRING(ObIRestoreHelper)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV("type", "ObIRestoreHelper");
  J_OBJ_END();
  return pos;
}

ObStandbyRestoreHelper::ObStandbyRestoreHelper()
  : is_inited_(false),
    task_id_(),
    src_(),
    bandwidth_throttle_(nullptr),
    ctx_(nullptr),
    ctx_allocator_("HaHelperCtx")
{
}

ObStandbyRestoreHelper::~ObStandbyRestoreHelper()
{
  destroy();
}

bool ObStandbyRestoreHelper::is_valid() const
{
  return is_inited_
      && src_.is_valid()
      && task_id_.is_valid()
      && OB_NOT_NULL(bandwidth_throttle_);
}

void ObStandbyRestoreHelper::destroy()
{
  if (OB_NOT_NULL(ctx_)) {
    ctx_->destroy();
    ctx_ = nullptr;
  }
  bandwidth_throttle_ = nullptr;
}

int ObStandbyRestoreHelper::init(
    const common::ObAddr &src,
    const share::ObTaskId &task_id,
    common::ObInOutBandwidthThrottle *bandwidth_throttle)
{
  int ret = OB_SUCCESS;
  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("standby restore helper init twice", K(ret), KPC(this));
  } else if (!src.is_valid() || !task_id.is_valid() || OB_ISNULL(bandwidth_throttle)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(src), K(task_id), KP(bandwidth_throttle));
  } else {
    src_ = src;
    task_id_ = task_id;
    bandwidth_throttle_ = bandwidth_throttle;
    is_inited_ = true;
  }
  return ret;
}

int ObStandbyRestoreHelper::copy_for_task(common::ObIAllocator &allocator, ObIRestoreHelper *&helper) const
{
  int ret = OB_SUCCESS;
  helper = nullptr;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else {
    void *buf = nullptr;
    ObStandbyRestoreHelper *task_helper = nullptr;
    if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStandbyRestoreHelper)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory for task helper", K(ret));
    } else if (FALSE_IT(task_helper = new (buf) ObStandbyRestoreHelper())) {
    } else {
      task_helper->task_id_ = task_id_;
      task_helper->src_ = src_;
      task_helper->bandwidth_throttle_ = bandwidth_throttle_;
      task_helper->is_inited_ = true;
      task_helper->ctx_ = nullptr;
      task_helper->ctx_allocator_.reset();
      helper = task_helper;
      task_helper = nullptr;
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::check_restore_precondition()
{
  int ret = OB_SUCCESS;
  int64_t required_size = 0;

  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else {
    obrpc::ObCheckRestorePreconditionResult result;
    ObStorageGrpcClient grpc_client;
    if (OB_FAIL(grpc_client.init(src_, RPC_TIMEOUT_US))) {
      LOG_WARN("failed to init grpc client", K(ret), K_(src));
    } else if (OB_FAIL(grpc_client.check_restore_precondition(result))) {
      LOG_WARN("failed to check restore precondition from source", K(ret), K_(src));
    } else {
      uint64_t local_cluster_version = GET_MIN_CLUSTER_VERSION();
      if (result.cluster_version_ != local_cluster_version) {
        ret = OB_MIGRATE_NOT_COMPATIBLE;
        LOG_ERROR("cluster version mismatch between primary and standby, cannot restore",
            KR(ret), "primary_version", result.cluster_version_, "standby_version", local_cluster_version);
      } else {
        result.required_disk_size_ = std::max(result.required_disk_size_, result.total_tablet_size_);
        if (OB_FAIL(LOCAL_DEVICE_INSTANCE.check_space_full(result.required_disk_size_))) {
          LOG_ERROR("failed to check_is_disk_full, cannot restore", KR(ret), K(result));
        } else {
          LOG_INFO("disk space and cluster version check passed",
              "required_size", result.required_disk_size_,
              "total_tablet_size", result.total_tablet_size_,
              "cluster_version", result.cluster_version_);
        }
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::init_for_ls_view()
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (OB_FAIL(create_ctx_(ObRestoreHelperCtxType::RESTORE_HELPER_CTX_LS_VIEW))) {
    LOG_WARN("failed to init ls view ctx", K(ret), KPC(this));
  }
  return ret;
}

int ObStandbyRestoreHelper::create_ctx_(const ObRestoreHelperCtxType ctx_type)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore helper ctx already exists, init streaming rpc repeatedly is not allowed",
        K(ret), K(ctx_type), "cur_ctx_type", ctx_->get_type(), KP(ctx_));
  } else if (OB_FAIL(ObRestoreHelperCtxUtil::create_ctx(ctx_type, ctx_allocator_, ctx_))) {
    LOG_WARN("failed to create restore helper ctx", K(ret), K(ctx_type));
  } else if (OB_ISNULL(ctx_) || ctx_type != ctx_->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore helper ctx is null or ctx type mismatch", K(ret), K(ctx_type), KP(ctx_));
  }
  return ret;
}

int ObStandbyRestoreHelper::get_ls_view_rpc_timeout_(int64_t &rpc_timeout_us)
{
  int ret = OB_SUCCESS;
  storage::ObGetLSViewTabletCountResult tablet_count_result;
  storage::ObStorageGrpcClient grpc_client;

  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (OB_FAIL(grpc_client.init(src_, RPC_TIMEOUT_US))) {
    LOG_WARN("failed to init grpc client for ls view tablet count", K(ret), K_(src));
  } else if (OB_FAIL(grpc_client.get_ls_view_tablet_count(tablet_count_result))) {
    LOG_WARN("failed to get ls view tablet count", K(ret), K_(src), K(rpc_timeout_us));
  } else if (!tablet_count_result.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls view tablet count result is invalid", K(ret), K(tablet_count_result));
  } else {
    rpc_timeout_us = calc_ls_view_stream_timeout_us(tablet_count_result.tablet_count_);
    LOG_INFO("calculated ls view stream timeout", K_(src),  "tablet_count", tablet_count_result.tablet_count_, K(rpc_timeout_us));
  }

  return ret;
}

int ObStandbyRestoreHelper::fetch_ls_meta(ObLSMetaPackage &ls_meta)
{
  int ret = OB_SUCCESS;
  int64_t rpc_timeout_us = 0;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx is null, ls view streaming rpc is not inited", K(ret), KPC(this));
  } else if (ObRestoreHelperCtxType::RESTORE_HELPER_CTX_LS_VIEW != ctx_->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx type mismatch", K(ret), "cur_ctx_type", ctx_->get_type(), KPC(this));
  } else {
    ObRestoreHelperLSViewCtx *ls_view_ctx = static_cast<ObRestoreHelperLSViewCtx *>(ctx_);
    if (ls_view_ctx->ls_meta_fetched_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls meta already fetched", K(ret), KPC(this));
    } else if (OB_FAIL(get_ls_view_rpc_timeout_(rpc_timeout_us))) {
      LOG_WARN("failed to get ls view rpc timeout", K(ret), K_(src));
    } else if (OB_FAIL(oceanbase::storage::ObStorageGrpcClient::init_ls_view_stream(
                          src_, rpc_timeout_us, ctx_allocator_, ls_meta, *ls_view_ctx))) {
      LOG_WARN("failed to init ls view stream", K(ret), K_(src), K(rpc_timeout_us));
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::fetch_next_tablet_info(obrpc::ObCopyTabletInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  tablet_info.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx is null, ls view streaming rpc is not inited", K(ret), KPC(this));
  } else if (ObRestoreHelperCtxType::RESTORE_HELPER_CTX_LS_VIEW != ctx_->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx type mismatch", K(ret), "cur_ctx_type", ctx_->get_type(), KPC(this));
  } else {
    ObRestoreHelperLSViewCtx *ls_view_ctx = static_cast<ObRestoreHelperLSViewCtx *>(ctx_);
    if (!ls_view_ctx->ls_meta_fetched_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls meta not fetched yet, fetch_ls_meta should be called first", K(ret), KPC(this));
    } else if (OB_ISNULL(ls_view_ctx->grpc_client_) || !ls_view_ctx->ls_view_reader_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls view stream is not initialized", K(ret), KP(ls_view_ctx));
    } else {
      storageservice::FetchLSViewRes response;
      if (!ls_view_ctx->ls_view_reader_->Read(&response)) {
        if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(ls_view_ctx->ls_view_reader_, ls_view_ctx->grpc_client_))) {
          LOG_WARN("failed to close reader", K(ret));
        } else {
          ret = OB_ITER_END;
        }
      } else if (storageservice::TABLET_INFO == response.entry_type()) {
        if (OB_FAIL(obgrpc::deserialize_proto_to_ob(response, tablet_info))) {
          LOG_WARN("failed to deserialize ObCopyTabletInfo", K(ret));
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid fetch ls view entry type", K(ret), "entry_type", response.entry_type());
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::init_for_fetch_tablet_meta(const common::ObIArray<common::ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (tablet_id_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id_array));
  } else if (OB_FAIL(create_ctx_(ObRestoreHelperCtxType::RESTORE_HELPER_CTX_TABLET_INFO))) {
    LOG_WARN("failed to init tablet info ctx", K(ret), KPC(this));
  } else {
    ObRestoreHelperTabletInfoCtx *tablet_info_ctx = static_cast<ObRestoreHelperTabletInfoCtx *>(ctx_);
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    obrpc::ObCopyTabletInfoArg arg;
    arg.tenant_id_ = OB_SYS_TENANT_ID;
    arg.ls_id_ = ls_id;
    if (OB_FAIL(arg.tablet_id_list_.assign(tablet_id_array))) {
      LOG_WARN("failed to assign tablet id list", K(ret), K(tablet_id_array));
    } else if (OB_FAIL(storage::ObStorageGrpcClient::init_tablet_info_stream(src_, RPC_TIMEOUT_US,
                                                                              arg, ctx_allocator_, *tablet_info_ctx))) {
       LOG_WARN("failed to create tablet info stream", K(ret), K(arg));
    }
  }
  return ret;
}


int ObStandbyRestoreHelper::fetch_tablet_meta(obrpc::ObCopyTabletInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  tablet_info.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx is null, tablet info streaming rpc is not inited", K(ret), KPC(this));
  } else if (ObRestoreHelperCtxType::RESTORE_HELPER_CTX_TABLET_INFO != ctx_->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx type mismatch", K(ret), "cur_ctx_type", ctx_->get_type(), KPC(this));
  } else {
    ObRestoreHelperTabletInfoCtx *tablet_info_ctx = static_cast<ObRestoreHelperTabletInfoCtx *>(ctx_);
    if (OB_ISNULL(tablet_info_ctx->grpc_client_) || !tablet_info_ctx->tablet_info_reader_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet info stream is not initialized", K(ret), KP(tablet_info_ctx));
    } else {
      storageservice::FetchTabletInfoRes response;
      if (!tablet_info_ctx->tablet_info_reader_->Read(&response)) {
        if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(
                tablet_info_ctx->tablet_info_reader_, tablet_info_ctx->grpc_client_))) {
          LOG_WARN("failed to close reader", K(ret));
        } else {
          ret = OB_ITER_END;
        }
      } else if (OB_FAIL(obgrpc::deserialize_proto_to_ob(response, tablet_info))) {
        LOG_WARN("failed to deserialize ObCopyTabletInfo", K(ret));
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::build_copy_tablet_sstable_info_arg_for_restore_(
    const ObTabletHandle &tablet_handle,
    obrpc::ObCopyTabletSSTableInfoArg &arg)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  arg.reset();
  ObTabletID tablet_id;

  if (!tablet_handle.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build copy tablet sstable info arg get invalid argument", K(ret), K(tablet_handle));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(tablet_handle));
  } else if (FALSE_IT(tablet_id = tablet->get_tablet_meta().tablet_id_)) {
  } else if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fail to fetch table store", K(ret), K(tablet_id));
  } else {
    arg.tablet_id_ = tablet_id;
    const ObSSTableArray &major_sstable_array = table_store_wrapper.get_member()->get_major_sstables();
    const ObSSTableArray &ddl_sstable_array = table_store_wrapper.get_member()->get_ddl_sstables();
    arg.minor_sstable_scn_range_.start_scn_.set_base();
    arg.minor_sstable_scn_range_.end_scn_.set_max();

    if (OB_FAIL(get_major_sstable_max_snapshot_for_restore_(major_sstable_array, arg.max_major_sstable_snapshot_))) {
      LOG_WARN("failed to get sstable max snapshot", K(ret), K(tablet_id));
    } else if (OB_FAIL(get_need_copy_ddl_sstable_range_for_restore_(tablet, ddl_sstable_array, arg.ddl_sstable_scn_range_))) {
      LOG_WARN("failed to get need copy ddl sstable range", K(ret));
    } else {
      LOG_INFO("succeed build copy sstable arg for restore", K(tablet_id), K(arg));
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::get_major_sstable_max_snapshot_for_restore_(
    const ObSSTableArray &major_sstable_array,
    int64_t &max_snapshot_version)
{
  int ret = OB_SUCCESS;
  ObArray<ObSSTableWrapper> sstables;

  max_snapshot_version = 0;
  if (major_sstable_array.count() > 0 && OB_FAIL(major_sstable_array.get_all_table_wrappers(sstables))) {
    LOG_WARN("failed to get all tables", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < sstables.count(); ++i) {
      const ObITable *table = sstables.at(i).get_sstable();
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("sstable should not be NULL", K(ret), KP(table));
      } else if (!table->is_major_sstable()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("sstable type is unexpected", K(ret), KP(table));
      } else {
        max_snapshot_version = std::max(max_snapshot_version, table->get_key().get_snapshot_version());
      }
    }
  }
  return ret;
}

// the tablet meta if the one copied from the source server
// ddl_sstable_array is the sstable of the destination server
// the first ddl sstable is an empty one with scn range: (ddl_start_scn - 1, ddl_start_scn]
// the scn range of ddl_sstable_array is continuous, so get the min ddl start scn as the end scn of need_copy_scn_range
int ObStandbyRestoreHelper::get_need_copy_ddl_sstable_range_for_restore_(
    const ObTablet *tablet,
    const ObSSTableArray &ddl_sstable_array,
    share::ObScnRange &need_copy_scn_range)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tablet)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be null", K(ret));
  } else if (tablet->get_tablet_meta().table_store_flag_.with_major_sstable()) {
    need_copy_scn_range.start_scn_.set_min();
    need_copy_scn_range.end_scn_.set_min();
  } else {
    const SCN ddl_start_scn = tablet->get_tablet_meta().ddl_start_scn_;
    const SCN ddl_checkpoint_scn = tablet->get_tablet_meta().ddl_checkpoint_scn_;
    need_copy_scn_range.start_scn_ = tablet->get_tablet_meta().get_ddl_sstable_start_scn();
    if (ddl_start_scn > ddl_checkpoint_scn) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("checkpoint ts should be greater than start ts",
        K(ret), "tablet_meta", tablet->get_tablet_meta());
    } else {
      if (!ddl_sstable_array.empty()) {
        ObArray<ObSSTableWrapper> sstables;
        if (OB_FAIL(ddl_sstable_array.get_all_table_wrappers(sstables))) {
          LOG_WARN("failed to get all ddl sstables", K(ret));
        } else {
          SCN min_start_scn = SCN::max_scn();
          for (int64_t i = 0; OB_SUCC(ret) && i < sstables.count(); ++i) {
            const ObITable *table = sstables.at(i).get_sstable();
            if (OB_ISNULL(table) || !table->is_ddl_dump_sstable()) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("sstable should not be NULL or not ddl dump sstable", K(ret), KP(table));
            } else if (table->get_key().scn_range_.start_scn_.is_valid()) {
              min_start_scn = std::min(min_start_scn, table->get_key().scn_range_.start_scn_);
            }
          }
        }
      } else {
        need_copy_scn_range.end_scn_ = ddl_checkpoint_scn;
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::init_for_build_tablets_sstable_info(
    const common::ObIArray<ObTabletHandle> &tablet_handle_array)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret));
  } else if (tablet_handle_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_handle_array));
  } else if (OB_FAIL(create_ctx_(ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_INFO))) {
    LOG_WARN("failed to init sstable info ctx", K(ret), KPC(this));
  } else {
    ObRestoreHelperSSTableInfoCtx *sstable_info_ctx = static_cast<ObRestoreHelperSSTableInfoCtx *>(ctx_);
    obrpc::ObCopyTabletsSSTableInfoArg arg;
    const uint64_t tenant_id = MTL_ID();
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    // TODO(xingzhi): remove ls_rebuild_seq_ need_check_seq_ ls_id_ tenant_id_ of ObCopyTabletsSSTableInfoArg
    arg.tenant_id_ = tenant_id;
    arg.ls_id_ = ls_id;
    arg.need_check_seq_ = false;
    arg.ls_rebuild_seq_ = 0;
    arg.is_only_copy_major_ = false;
    arg.version_ = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_handle_array.count(); ++i) {
      obrpc::ObCopyTabletSSTableInfoArg tablet_arg;
      if (OB_FAIL(build_copy_tablet_sstable_info_arg_for_restore_(tablet_handle_array.at(i), tablet_arg))) {
        LOG_WARN("failed to build copy tablet sstable info arg", K(ret), K(i), K(tablet_handle_array.at(i)));
      } else if (OB_FAIL(arg.tablet_sstable_info_arg_list_.push_back(tablet_arg))) {
        LOG_WARN("failed to push back tablet sstable info arg", K(ret), K(tablet_arg));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(storage::ObStorageGrpcClient::init_tablet_sstable_info_stream(
              src_, RPC_TIMEOUT_US, arg, ctx_allocator_, *sstable_info_ctx))) {
        LOG_WARN("failed to init tablet sstable info stream", K(ret), K(arg), K_(src));
      } else {
        sstable_info_ctx->pending_sstable_count_ = 0;
        sstable_info_ctx->cur_tablet_id_.reset();
      }
    }

    if (OB_FAIL(ret)) {
      // Cleanup on error
      if (OB_NOT_NULL(sstable_info_ctx)) {
        sstable_info_ctx->reset();
        if (OB_NOT_NULL(ctx_)) {
          ctx_->destroy();
          ctx_allocator_.free(ctx_);
          ctx_ = nullptr;
        }
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::fetch_next_tablet_sstable_header(obrpc::ObCopyTabletSSTableHeader &copy_header)
{
  int ret = OB_SUCCESS;
  copy_header.reset();
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx is null, sstable info streaming rpc is not inited", K(ret), KPC(this));
  } else if (ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_INFO != ctx_->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx type mismatch", K(ret), "cur_ctx_type", ctx_->get_type(), KPC(this));
  } else {
    ObRestoreHelperSSTableInfoCtx *sstable_info_ctx = static_cast<ObRestoreHelperSSTableInfoCtx *>(ctx_);
    if (OB_ISNULL(sstable_info_ctx->grpc_client_) || !sstable_info_ctx->sstable_info_reader_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sstable info stream is not initialized", K(ret), KP(sstable_info_ctx));
    } else if (sstable_info_ctx->pending_sstable_count_ > 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot fetch next header when there are pending sstable metas", K(ret), "pending_count", sstable_info_ctx->pending_sstable_count_);
    } else {
      storageservice::FetchTabletSSTableInfoRes response;
      if (!sstable_info_ctx->sstable_info_reader_->Read(&response)) {
        if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(
                        sstable_info_ctx->sstable_info_reader_, sstable_info_ctx->grpc_client_))) {
          LOG_WARN("failed to close reader", K(ret));
        } else {
          ret = OB_ITER_END;
        }
      } else if (OB_FAIL(obgrpc::deserialize_proto_to_ob(response, copy_header))) {
        LOG_WARN("failed to deserialize ObCopyTabletSSTableHeader", K(ret));
      } else {
        sstable_info_ctx->pending_sstable_count_ = copy_header.sstable_count_;
        sstable_info_ctx->cur_tablet_id_ = copy_header.tablet_id_;
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::fetch_next_sstable_meta(obrpc::ObCopyTabletSSTableInfo &sstable_info)
{
  int ret = OB_SUCCESS;
  sstable_info.reset();
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx is null, sstable info streaming rpc is not inited", K(ret), KPC(this));
  } else if (ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_INFO != ctx_->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx type mismatch", K(ret), "cur_ctx_type", ctx_->get_type(), KPC(this));
  } else {
    ObRestoreHelperSSTableInfoCtx *sstable_info_ctx = static_cast<ObRestoreHelperSSTableInfoCtx *>(ctx_);
    if (OB_ISNULL(sstable_info_ctx->grpc_client_) || !sstable_info_ctx->sstable_info_reader_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sstable info stream is not initialized", K(ret), KP(sstable_info_ctx->grpc_client_));
    } else if (sstable_info_ctx->pending_sstable_count_ <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot fetch sstable meta when pending count is zero, should fetch header first", K(ret), "pending_count", sstable_info_ctx->pending_sstable_count_);
    } else {
      storageservice::FetchTabletSSTableInfoRes response;
      if (!sstable_info_ctx->sstable_info_reader_->Read(&response)) {
        if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(
                        sstable_info_ctx->sstable_info_reader_, sstable_info_ctx->grpc_client_))) {
          LOG_WARN("failed to close reader", K(ret));
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected end of stream when expecting sstable meta",
              K(ret), "pending_count", sstable_info_ctx->pending_sstable_count_);
        }
      } else if (OB_FAIL(obgrpc::deserialize_proto_to_ob(response, sstable_info))) {
        LOG_WARN("failed to deserialize ObCopyTabletSSTableInfo", K(ret));
      } else {
        sstable_info_ctx->pending_sstable_count_--;
        if (sstable_info_ctx->pending_sstable_count_ == 0) {
          sstable_info_ctx->cur_tablet_id_.reset();
        }
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::init_for_sstable_macro_range(const common::ObIArray<storage::ObITable::TableKey> &copy_table_key_array)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (copy_table_key_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument, copy table key array is empty", K(ret), KPC(this));
  } else if (OB_FAIL(create_ctx_(ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_MACRO_RANGE))) {
    LOG_WARN("failed to init sstable macro range ctx", K(ret), KPC(this));
  } else {
    ObRestoreHelperSSTableMacroRangeCtx *macro_range_ctx = static_cast<ObRestoreHelperSSTableMacroRangeCtx *>(ctx_);
    obrpc::ObCopySSTableMacroRangeInfoArg arg;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    arg.tenant_id_ = OB_SYS_TENANT_ID;
    arg.ls_id_ = ls_id;
    arg.tablet_id_ = copy_table_key_array.at(0).tablet_id_;
    arg.macro_range_max_marco_count_ = MACRO_RANGE_MAX_MACRO_COUNT;
    arg.need_check_seq_ = false;
    arg.ls_rebuild_seq_ = 0;

    if (OB_FAIL(arg.copy_table_key_array_.assign(copy_table_key_array))) {
      LOG_WARN("failed to assign copy table key array", K(ret), "key_cnt", copy_table_key_array.count());
    } else if (OB_FAIL(storage::ObStorageGrpcClient::init_sstable_macro_info_stream(src_, RPC_TIMEOUT_US,
                                                                                arg, ctx_allocator_, *macro_range_ctx))) {
      LOG_WARN("failed to init sstable macro info stream", K(ret), K(arg), K_(src));
    }

    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(macro_range_ctx)) {
        macro_range_ctx->reset();
        if (OB_NOT_NULL(ctx_)) {
          ctx_->destroy();
          ctx_allocator_.free(ctx_);
          ctx_ = nullptr;
        }
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::fetch_next_sstable_macro_range_info(storage::ObCopySSTableMacroRangeInfo &sstable_macro_range_info)
{
  int ret = OB_SUCCESS;
  sstable_macro_range_info.reset();

  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx is null, sstable macro range streaming rpc is not inited", K(ret), KPC(this));
  } else if (ObRestoreHelperCtxType::RESTORE_HELPER_CTX_SSTABLE_MACRO_RANGE != ctx_->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx type mismatch", K(ret), "cur_ctx_type", ctx_->get_type(), KPC(this));
  } else {
    ObRestoreHelperSSTableMacroRangeCtx *macro_range_ctx = static_cast<ObRestoreHelperSSTableMacroRangeCtx *>(ctx_);
    storage::ObCopyMacroRangeInfo macro_range_info;
    //TODO(xingzhi): add valid function formacro_range_ctx
    if (OB_ISNULL(macro_range_ctx->grpc_client_) || !macro_range_ctx->macro_info_reader_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sstable macro info stream is not initialized", K(ret));
    } else {
      storageservice::FetchSSTableMacroInfoRes response;
      obrpc::ObCopySSTableMacroRangeInfoHeader header;
      if (!macro_range_ctx->macro_info_reader_->Read(&response)) {
        if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(
                        macro_range_ctx->macro_info_reader_, macro_range_ctx->grpc_client_))) {
          LOG_WARN("failed to close reader", K(ret));
        } else {
          ret = OB_ITER_END;
        }
      } else if (OB_FAIL(obgrpc::deserialize_proto_to_ob(response, header))) {
        LOG_WARN("failed to deserialize ObCopySSTableMacroRangeInfoHeader", K(ret));
      } else {
        sstable_macro_range_info.copy_table_key_ = header.copy_table_key_;

        // Read macro range infos
        for (int64_t i = 0; OB_SUCC(ret) && i < header.macro_range_count_; ++i) {
          storageservice::FetchSSTableMacroInfoRes response;
          macro_range_info.reuse();
          if (!macro_range_ctx->macro_info_reader_->Read(&response)) {
            if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(
                            macro_range_ctx->macro_info_reader_, macro_range_ctx->grpc_client_))) {
              LOG_WARN("failed to close reader", K(ret));
            } else {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected end of stream when expecting macro range info", K(ret),
                  "expected_count", header.macro_range_count_, "read_count", i);
            }
          } else if (OB_FAIL(obgrpc::deserialize_proto_to_ob(response, macro_range_info))) {
            LOG_WARN("failed to deserialize ObCopyMacroRangeInfo", K(ret));
          } else if (OB_FAIL(sstable_macro_range_info.copy_macro_range_array_.push_back(macro_range_info))) {
            LOG_WARN("failed to push back macro range info", K(ret), K(macro_range_info));
          }
        }
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::init_for_macro_block_copy(
    const storage::ObITable::TableKey &copy_table_key,
    const storage::ObCopyMacroRangeInfo &macro_range_info,
    const share::SCN &backfill_tx_scn,
    const int64_t data_version,
    storage::ObMacroBlockReuseMgr *macro_block_reuse_mgr)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (!copy_table_key.is_valid() || !macro_range_info.is_valid()
                  || data_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(copy_table_key), K(macro_range_info), K(data_version));
  } else if (OB_FAIL(create_ctx_(ObRestoreHelperCtxType::RESTORE_HELPER_CTX_MACRO_BLOCK))) {
    LOG_WARN("failed to init macro block ctx", K(ret), KPC(this));
  } else {
    ObRestoreHelperMacroBlockCtx *macro_block_ctx = static_cast<ObRestoreHelperMacroBlockCtx *>(ctx_);
    obrpc::ObCopyMacroBlockRangeArg arg;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    arg.tenant_id_ = OB_SYS_TENANT_ID;
    arg.ls_id_ = ls_id;
    arg.table_key_ = copy_table_key;
    arg.backfill_tx_scn_ = backfill_tx_scn;
    arg.data_version_ = data_version;
    arg.need_check_seq_ = false;
    arg.ls_rebuild_seq_ = 0;
    macro_block_ctx->macro_block_reuse_mgr_ = macro_block_reuse_mgr;
    macro_block_ctx->data_version_ = data_version;
    macro_block_ctx->copy_table_key_ = copy_table_key;
    if (OB_FAIL(arg.copy_macro_range_info_.assign(macro_range_info))) {
      LOG_WARN("failed to assign macro range info", K(ret), K(macro_range_info));
    } else if (OB_FAIL(storage::ObStorageGrpcClient::init_macro_block_stream(src_, RPC_TIMEOUT_US,
                                                                        arg, ctx_allocator_, *macro_block_ctx))) {
      LOG_WARN("failed to init macro block stream", K(ret), K(arg), K_(src));
    } else if (OB_FAIL(macro_block_ctx->data_buffer_.ensure_space(common::OB_DEFAULT_MACRO_BLOCK_SIZE))) {
      LOG_WARN("failed to ensure space for macro block data buffer", K(ret),
                  "buffer_size", common::OB_DEFAULT_MACRO_BLOCK_SIZE);
    }

    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(macro_block_ctx)) {
        macro_block_ctx->reset();
        if (OB_NOT_NULL(ctx_)) {
          ctx_->destroy();
          ctx_allocator_.free(ctx_);
          ctx_ = nullptr;
        }
      }
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::fetch_next_macro_block(storage::ObICopyMacroBlockReader::CopyMacroBlockReadData &read_data)
{
  int ret = OB_SUCCESS;
  read_data.reset();

  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby restore helper not init", K(ret), KPC(this));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx is null, macro block streaming rpc is not inited", K(ret), KPC(this));
  } else if (ObRestoreHelperCtxType::RESTORE_HELPER_CTX_MACRO_BLOCK != ctx_->get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx type mismatch", K(ret), "cur_ctx_type", ctx_->get_type(), KPC(this));
  } else {
    ObRestoreHelperMacroBlockCtx *macro_block_ctx = static_cast<ObRestoreHelperMacroBlockCtx *>(ctx_);
    obrpc::ObCopyMacroBlockHeader header;
    blocksstable::ObBufferReader data_reader;
    if (OB_ISNULL(macro_block_ctx->grpc_client_) || !macro_block_ctx->macro_block_reader_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("macro block stream is not initialized", K(ret));
    } else if (OB_FAIL(fetch_macro_block_header_(macro_block_ctx, header))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("failed to fetch macro block header", K(ret));
      }
    } else if (OB_FAIL(fetch_macro_block_data_(macro_block_ctx, header, data_reader))) {
      LOG_WARN("failed to fetch macro block data", K(ret), K(header));
    } else if (obrpc::ObCopyMacroBlockDataType::MACRO_DATA == header.data_type_) {
      if (OB_FAIL(read_data.set_macro_data(data_reader, header.is_reuse_macro_block_))) {
        LOG_WARN("failed to set macro data", K(ret), K(header));
      }
    } else if (obrpc::ObCopyMacroBlockDataType::MACRO_META_ROW == header.data_type_) {
      ObDatumRow macro_meta_row;
      blocksstable::ObDataMacroBlockMeta macro_meta;
      blocksstable::MacroBlockId macro_id;
      int64_t data_checksum = 0;
      int64_t pos = 0;

      if (OB_FAIL(macro_meta_row.init(OB_MAX_ROWKEY_COLUMN_NUMBER + 1))) {
        LOG_WARN("failed to init macro meta row", K(ret));
      } else if (OB_FAIL(macro_meta_row.deserialize(data_reader.data(), header.occupy_size_, pos))) {
        LOG_WARN("failed to deserialize macro meta row", K(ret), K(header.occupy_size_), K(pos));
      } else if (OB_FAIL(macro_meta.parse_row(macro_meta_row))) {
        LOG_WARN("failed to parse macro meta row", K(ret), K(macro_meta_row));
      } else if (OB_ISNULL(macro_block_ctx->macro_block_reuse_mgr_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("macro block reuse mgr is NULL", K(ret), KP(macro_block_ctx->macro_block_reuse_mgr_));
      } else if (OB_FAIL(macro_block_ctx->macro_block_reuse_mgr_->get_macro_block_reuse_info(
          macro_block_ctx->copy_table_key_, macro_meta.get_logic_id(), macro_id, data_checksum))) {
        LOG_WARN("failed to get macro block reuse info",
                    K(ret), K(macro_block_ctx->copy_table_key_), K(macro_meta.get_logic_id()));
      } else if (macro_meta.get_meta_val().data_checksum_ != data_checksum) {
        ret = OB_CHECKSUM_ERROR;
        LOG_WARN("data checksum not match", K(ret), K(macro_meta.get_meta_val().data_checksum_), K(data_checksum));
      } else {
        macro_meta.val_.macro_id_ = macro_id;
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(read_data.set_macro_meta(macro_meta, header.is_reuse_macro_block_))) {
          LOG_WARN("failed to set macro meta", K(ret), K(macro_meta), K(header.is_reuse_macro_block_));
        }
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid data type", K(ret), K(header.data_type_));
    }
  }
  return ret;
}

int ObStandbyRestoreHelper::fetch_macro_block_header_(
    ObRestoreHelperMacroBlockCtx *macro_block_ctx,
    obrpc::ObCopyMacroBlockHeader &header)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(macro_block_ctx) || !macro_block_ctx->macro_block_reader_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(macro_block_ctx));
  } else {
    storageservice::FetchMacroBlockRes response;
    if (!macro_block_ctx->macro_block_reader_->Read(&response)) {
      if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(
                      macro_block_ctx->macro_block_reader_, macro_block_ctx->grpc_client_))) {
        LOG_WARN("failed to close reader", K(ret));
      } else {
        ret = OB_ITER_END;
      }
    } else if (OB_FAIL(obgrpc::deserialize_proto_to_ob(response, header))) {
      LOG_WARN("failed to deserialize ObCopyMacroBlockHeader", K(ret));
    }
  }

  return ret;
}

int ObStandbyRestoreHelper::fetch_macro_block_data_(
    ObRestoreHelperMacroBlockCtx *macro_block_ctx,
    const obrpc::ObCopyMacroBlockHeader &header,
    blocksstable::ObBufferReader &data_reader)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(macro_block_ctx) || !macro_block_ctx->macro_block_reader_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(macro_block_ctx));
  } else {
    int64_t occupy_size = header.occupy_size_;
    if (occupy_size > macro_block_ctx->data_buffer_.capacity()) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("occupy size exceeds buffer capacity", K(ret),
               K(occupy_size), "buffer_capacity", macro_block_ctx->data_buffer_.capacity());
    } else {
      storageservice::FetchMacroBlockRes response;
      if (!macro_block_ctx->macro_block_reader_->Read(&response)) {
        if (OB_FAIL(ObRestoreHelperCtxUtil::close_reader(
                        macro_block_ctx->macro_block_reader_, macro_block_ctx->grpc_client_))) {
          LOG_WARN("failed to close reader", K(ret));
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected end of stream when expecting macro block data", K(ret), K(occupy_size));
        }
      } else if (static_cast<int64_t>(response.size()) != occupy_size) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("data size mismatch", K(ret), "expected_size", occupy_size, "actual_size", response.size());
      } else {
        macro_block_ctx->data_buffer_.reuse();
        if (OB_FAIL(macro_block_ctx->data_buffer_.write(response.buf().data(), occupy_size))) {
          LOG_WARN("failed to write data to buffer", K(ret), K(occupy_size));
        } else {
          data_reader.assign(macro_block_ctx->data_buffer_.data(), occupy_size, occupy_size);
        }
      }
    }
  }

  return ret;
}

} // namespace restore
} // namespace oceanbase
