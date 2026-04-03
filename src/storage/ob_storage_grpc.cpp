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

#include "storage/ob_storage_grpc.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/time/ob_time_utility.h"
#include "storage/high_availability/ob_storage_ha_reader.h"
#include "storage/high_availability/ob_storage_ha_utils.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/ob_cluster_version.h"
#include "storage/high_availability/ob_restore_helper_ctx.h"
#include <string>

using namespace oceanbase::common;
using namespace oceanbase::obrpc;
using namespace oceanbase::obgrpc;
using namespace oceanbase::storage;
using namespace storageservice;

namespace oceanbase
{
namespace storage
{

ObGetLSViewTabletCountResult::ObGetLSViewTabletCountResult()
  : tablet_count_(0)
{
}

OB_SERIALIZE_MEMBER(ObGetLSViewTabletCountResult, tablet_count_);

grpc::Status ObStorageGrpcServiceImpl::fetch_ls_view(
    grpc::ServerContext* context,
    const FetchLSViewReq* request,
    grpc::ServerWriter<FetchLSViewRes>* writer)
{
  int ret = OB_SUCCESS;
  const int64_t start_ts = ObTimeUtil::current_time();
  share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
  UNUSED(request);
  MTL_SWITCH(OB_SYS_TENANT_ID) {
    ObLSHandle ls_handle;
    ObLSService *ls_service = nullptr;
    ObLS *ls = nullptr;
    logservice::ObLogHandler *log_handler = nullptr;
    ObRole role;
    int64_t proposal_id = 0;
    ObDeviceHealthStatus dhs = DEVICE_HEALTH_NORMAL;
    int64_t disk_abnormal_time = 0;
    ObMigrationStatus migration_status;
    bool is_need_rebuild = false;
    bool is_log_sync = false;
    bool ls_meta_sent = false;
    int64_t total_tablet_count = 0;
    LOG_INFO("start to fetch ls view", K(ls_id));
    auto fill_ls_meta_f = [&context, &writer, &ls_meta_sent](const ObLSMetaPackage &ls_meta)->int {
      int ret = OB_SUCCESS;
      if (context->IsCancelled()) {
        ret = OB_CANCELED;
        LOG_WARN("client cancelled fetch_ls_view request", K(ret));
      } else {
        FetchLSViewRes response;
        response.set_entry_type(storageservice::LS_META_PACKAGE);
        if (OB_FAIL(serialize_ob_to_proto(ls_meta, &response))) {
          LOG_WARN("failed to serialize ObLSMetaPackage", K(ret));
        } else if (!writer->Write(response)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to write ls meta package to stream", K(ret));
        } else {
          ls_meta_sent = true;
        }
      }
      return ret;
    };

    auto fill_tablet_meta_f = [&context,
                               &writer,
                               &total_tablet_count]
        (const obrpc::ObCopyTabletInfo &tablet_info, const ObTabletHandle &tablet_handle)->int {
      int ret = OB_SUCCESS;
      UNUSED(tablet_handle);
      if (context->IsCancelled()) {
        ret = OB_CANCELED;
        LOG_WARN("client cancelled fetch_ls_view request", K(ret));
      } else {
        FetchLSViewRes response;
        response.set_entry_type(storageservice::TABLET_INFO);
        if (OB_FAIL(serialize_ob_to_proto(tablet_info, &response))) {
          LOG_WARN("failed to serialize ObCopyTabletInfo", K(ret), K(tablet_info));
        } else if (!writer->Write(response)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to write tablet info to stream", K(ret));
        } else {
          ++total_tablet_count;
          LOG_INFO("fetch_ls_view batch sent", K(total_tablet_count));
        }
      }
      return ret;
    };

    LOG_INFO("start to fetch ls view", K(ls_id));
    if (OB_FAIL(ObIOManager::get_instance().get_device_health_status(dhs, disk_abnormal_time))) {
      STORAGE_LOG(WARN, "failed to check is disk error", KR(ret));
    } else if (DEVICE_HEALTH_ERROR == dhs) {
      ret = OB_DISK_ERROR;
      STORAGE_LOG(ERROR, "observer has disk error, cannot be migrate src", KR(ret),
          "disk_health_status", device_health_status_to_str(dhs), K(disk_abnormal_time));
    } else if (OB_ISNULL(ls_service = MTL(ObLSService *))) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "ls service should not be null", K(ret), KP(ls_service));
    } else if (OB_FAIL(ls_service->get_ls(ls_id, ls_handle, ObLSGetMod::STORAGE_MOD))) {
      LOG_WARN("failed to get log stream", K(ret), K(ls_id));
    } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(ls_id));
    } else if (OB_ISNULL(log_handler = ls->get_log_handler())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log handler should not be NULL", K(ret), KP(log_handler), K(ls_id));
    } else if (OB_FAIL(ls->get_migration_status(migration_status))) {
      LOG_WARN("failed to get migration status", K(ret), K(ls_id));
    } else if (!ObMigrationStatusHelper::check_can_migrate_out(migration_status) || ls->is_stopped()
        || ls->is_offline()) {
      ret = OB_SRC_DO_NOT_ALLOWED_MIGRATE;
      STORAGE_LOG(WARN, "src migration status do not allow to migrate out", K(ret), "src migration status",
          migration_status, KPC(ls));
    } else if (OB_FAIL(log_handler->get_role(role, proposal_id))) {
      LOG_WARN("failed to get role", K(ret), K(ls_id));
    } else if (!is_strong_leader(role) && OB_FAIL(log_handler->is_in_sync(is_log_sync, is_need_rebuild))) {
      LOG_WARN("failed to check is in sync", K(ret), K(ls_id));
    } else if (!is_strong_leader(role) && (!is_log_sync || is_need_rebuild)) {
      ret = OB_SRC_DO_NOT_ALLOWED_MIGRATE;
      LOG_WARN("log is not in sync, cannot fetch ls view", K(ret), K(ls_id), K(is_log_sync), K(is_need_rebuild));
    } else if (OB_FAIL(ls->get_ls_meta_package_and_tablet_metas(
                           false /*check_archive*/,
                           fill_ls_meta_f,
                           false /*need_sorted_tablet_id*/,
                           fill_tablet_meta_f))) {
      LOG_WARN("failed to stream ls view", K(ret), K(ls_id));
    } else if (!ls_meta_sent) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls meta package is not sent in fetch_ls_view stream", K(ret), K(ls_id));
    } else {
      LOG_INFO("fetch_ls_view stream finished", K(ls_id), K(total_tablet_count),
          "cost_ts", ObTimeUtil::current_time() - start_ts);
    }
  }
  LOG_INFO("fetch_ls_view stream finished", K(ls_id), "cost_ts", ObTimeUtil::current_time() - start_ts);

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status ObStorageGrpcServiceImpl::get_ls_view_tablet_count(
    grpc::ServerContext* context,
    const storageservice::GetLSViewTabletCountReq* request,
    storageservice::GetLSViewTabletCountRes* response)
{
  int ret = OB_SUCCESS;
  UNUSED(context);
  UNUSED(request);
  ObGetLSViewTabletCountResult result;

  MTL_SWITCH(OB_SYS_TENANT_ID) {
    ObLSHandle ls_handle;
    ObLS *ls = nullptr;
    ObLSVTInfo ls_info;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    if (OB_FAIL(MTL(ObLSService*)->get_ls(ls_id, ls_handle, ObLSGetMod::STORAGE_MOD))) {
      LOG_WARN("failed to get ls", K(ret), K(ls_id));
    } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls is null", K(ret), K(ls_id));
    } else if (OB_FAIL(ls->get_ls_info(ls_info))) {
      LOG_WARN("failed to get ls info", K(ret), K(ls_id));
    } else {
      result.tablet_count_ = ls_info.tablet_count_;
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialize_ob_to_proto(result, response))) {
      LOG_WARN("failed to serialize ObGetLSViewTabletCountResult", K(ret));
    } else {
      share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
      LOG_INFO("get_ls_view_tablet_count RPC handled successfully", K(ls_id), K(result.tablet_count_));
    }
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status ObStorageGrpcServiceImpl::fetch_tablet_info(
    grpc::ServerContext* context,
    const FetchTabletInfoReq* request,
    grpc::ServerWriter<FetchTabletInfoRes>* writer)
{
  int ret = OB_SUCCESS;

  const std::string& req_buf = request->buf();
  const uint64_t req_size = request->size();

  ObCopyTabletInfoArg arg;

  if (OB_FAIL(deserialize_proto_to_ob(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopyTabletInfoArg", K(ret));
  }

  MTL_SWITCH(arg.tenant_id_) {
    ObLSHandle ls_handle;
    ObLSService *ls_service = nullptr;
    ObLS *ls = nullptr;
    ObCopyTabletInfoObProducer producer;
    if (OB_ISNULL(ls_service = MTL(ObLSService *))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls service should not be null", K(ret));
    } else if (OB_FAIL(ls_service->get_ls(arg.ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
      LOG_WARN("failed to get log stream", K(ret), K(arg));
    } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(arg));
    } else if (OB_FAIL(producer.init(arg.tenant_id_, arg.ls_id_, arg.tablet_id_list_))) {
      LOG_WARN("failed to init copy tablet info producer", K(ret), K(arg));
    } else {
      ObCopyTabletInfo tablet_info;
      while (OB_SUCC(ret)) {
        tablet_info.reset();

        if (context->IsCancelled()) {
          ret = OB_CANCELED;
          LOG_WARN("client cancelled the request", K(ret));
          break;
        }

        if (OB_FAIL(producer.get_next_tablet_info(tablet_info))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            STORAGE_LOG(WARN, "failed to get next tablet meta info", K(ret));
          }
        }

        if (OB_SUCC(ret)) {
          FetchTabletInfoRes response;
          if (OB_FAIL(serialize_ob_to_proto(tablet_info, &response))) {
            LOG_WARN("failed to serialize ObCopyTabletInfo", K(ret));
          } else {
            if (!writer->Write(response)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("failed to write tablet info to stream", K(ret));
            }
          }
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    LOG_WARN("fetch_tablet_info stream failed", K(ret), K(arg));
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status ObStorageGrpcServiceImpl::fetch_tablet_sstable_info(
    grpc::ServerContext* context,
    const FetchTabletSSTableInfoReq* request,
    grpc::ServerWriter<FetchTabletSSTableInfoRes>* writer)
{
  int ret = OB_SUCCESS;

  ObCopyTabletsSSTableInfoArg arg;
  const int64_t start_ts = ObTimeUtil::current_time();
  if (OB_FAIL(deserialize_proto_to_ob(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopyTabletsSSTableInfoArg", K(ret));
  } else {
    MTL_SWITCH(arg.tenant_id_) {
      ObCopyTabletsSSTableInfoObProducer tablets_producer;
      ObLSHandle ls_handle;
      ObLSService *ls_service = nullptr;
      obrpc::ObCopyTabletSSTableInfoArg tablet_arg;
      ObLS *ls = nullptr;
      if (OB_FAIL(tablets_producer.init(arg.tenant_id_, arg.ls_id_, arg.tablet_sstable_info_arg_list_))) {
        LOG_WARN("failed to init copy tablets sstable info ob producer", K(ret), K(arg));
      } else if (OB_ISNULL(ls_service = MTL(ObLSService *))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls service should not be null", K(ret));
      } else if (OB_FAIL(ls_service->get_ls(arg.ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
        LOG_WARN("failed to get log stream", K(ret), K(arg));
      } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(arg));
      }
      while (OB_SUCC(ret)) {
        tablet_arg.reset();
        if (context->IsCancelled()) {
          ret = OB_CANCELED;
          LOG_WARN("client cancelled the request", K(ret));
          break;
        }
        if (OB_FAIL(tablets_producer.get_next_tablet_sstable_info(tablet_arg))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_WARN("failed to get next tablet sstable info", K(ret), K(arg));
          }
        } else if (OB_FAIL(build_tablet_sstable_info_(context, tablet_arg, ls, writer))) {
          LOG_WARN("failed to build tablet sstable info", K(ret), K(tablet_arg));
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    LOG_WARN("fetch_tablet_sstable_info stream failed", K(ret), K(arg),"cost_ts", ObTimeUtil::current_time() - start_ts);
  } else {
    LOG_INFO("fetch_tablet_sstable_info stream completed", K(arg),"cost_ts", ObTimeUtil::current_time() - start_ts);
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status ObStorageGrpcServiceImpl::fetch_sstable_macro_info(
    grpc::ServerContext* context,
    const FetchSSTableMacroInfoReq* request,
    grpc::ServerWriter<FetchSSTableMacroInfoRes>* writer)
{
  int ret = OB_SUCCESS;

  ObCopySSTableMacroRangeInfoArg arg;
  const int64_t start_ts = ObTimeUtil::current_time();
  if (OB_FAIL(deserialize_proto_to_ob(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopySSTableMacroRangeInfoArg", K(ret));
  } else {
    MTL_SWITCH(arg.tenant_id_) {
      ObCopySSTableMacroObProducer producer;
      obrpc::ObCopySSTableMacroRangeInfoHeader header;
      if (OB_FAIL(producer.init(arg.tenant_id_, arg.ls_id_, arg.tablet_id_,
          arg.copy_table_key_array_, arg.macro_range_max_marco_count_))) {
        LOG_WARN("failed to init copy sstable macro ob producer", K(ret), K(arg));
      } else {
        while (OB_SUCC(ret)) {
          if (context->IsCancelled()) {
            ret = OB_CANCELED;
            LOG_WARN("client cancelled the request", K(ret));
            break;
          }

          header.reset();
          if (OB_FAIL(producer.get_next_sstable_macro_range_info(header))) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            } else {
              LOG_WARN("failed to get next sstable macro range info", K(ret));
            }
          } else if (OB_FAIL(build_sstable_macro_info_(context, header, arg, writer))) {
            LOG_WARN("failed to build sstable macro info", K(ret), K(header));
          }
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    LOG_WARN("fetch_sstable_macro_info stream failed", K(ret), K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  } else {
    LOG_INFO("fetch_sstable_macro_info stream completed", K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status ObStorageGrpcServiceImpl::fetch_macro_block(
    grpc::ServerContext* context,
    const FetchMacroBlockReq* request,
    grpc::ServerWriter<FetchMacroBlockRes>* writer)
{
  int ret = OB_SUCCESS;

  ObCopyMacroBlockRangeArg arg;
  const int64_t start_ts = ObTimeUtil::current_time();
  if (OB_FAIL(deserialize_proto_to_ob(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopyMacroBlockRangeArg", K(ret));
  } else {
    MTL_SWITCH(arg.tenant_id_) {
      ObCopyMacroBlockObProducer *producer = nullptr;
      void *producer_buf = nullptr;
      blocksstable::ObBufferReader data;
      obrpc::ObCopyMacroBlockHeader header;
      FetchMacroBlockRes header_response;
      FetchMacroBlockRes data_response;
      if (OB_ISNULL(producer_buf = mtl_malloc(sizeof(ObCopyMacroBlockObProducer), "MacroBlockProd"))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory", K(ret));
      } else if (FALSE_IT(producer = new (producer_buf) ObCopyMacroBlockObProducer())) {
      } else if (OB_FAIL(producer->init(arg.tenant_id_, arg.ls_id_, arg.table_key_,
          arg.copy_macro_range_info_, arg.data_version_, arg.backfill_tx_scn_))) {
        LOG_WARN("failed to init copy macro block ob producer", K(ret), K(arg));
      }
      common::ObArenaAllocator header_allocator("MacroBlkHeader");
      while (OB_SUCC(ret)) {
        if (context->IsCancelled()) {
          ret = OB_CANCELED;
          LOG_WARN("client cancelled the request", K(ret));
          break;
        }
        header.reset();
        if (OB_FAIL(producer->get_next_macro_block(data, header))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_WARN("failed to get next macro block", K(ret));
          }
        } else {
          int64_t header_size = serialization::encoded_length(header);
          char *header_buf = nullptr;
          int64_t header_pos = 0;
          header_allocator.reuse();
          if (OB_ISNULL(header_buf = static_cast<char*>(header_allocator.alloc(header_size)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to alloc header buffer", K(ret), K(header_size));
          } else if (OB_FAIL(serialization::encode(header_buf, header_size, header_pos, header))) {
            LOG_WARN("failed to encode header", K(ret));
          } else if (FALSE_IT(header_response.set_buf(header_buf, header_pos))) {
          } else if (FALSE_IT(header_response.set_size(header_pos))) {
          } else if (!writer->Write(header_response)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to write header to grpc stream", K(ret));
          } else if (FALSE_IT(data_response.set_buf(data.data(), data.length()))) {
          } else if (FALSE_IT(data_response.set_size(data.length()))) {
          } else if (!writer->Write(data_response)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to write data to grpc stream", K(ret));
          }
        }
      }
      if (OB_NOT_NULL(producer)) {
        producer->~ObCopyMacroBlockObProducer();
        mtl_free(producer);
        producer = nullptr;
      }
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("fetch_macro_block stream failed", K(ret), K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  } else {
    LOG_INFO("fetch_macro_block stream completed", K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status ObStorageGrpcServiceImpl::check_restore_precondition(
    grpc::ServerContext* context,
    const storageservice::CheckRestorePreconditionReq* request,
    storageservice::CheckRestorePreconditionRes* response)
{
  int ret = OB_SUCCESS;
  obrpc::ObCheckRestorePreconditionResult result;

  MTL_SWITCH(OB_SYS_TENANT_ID) {
    ObLSHandle ls_handle;
    ObLS *ls = nullptr;
    ObLSVTInfo ls_info;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    if (OB_FAIL(MTL(ObLSService*)->get_ls(ls_id, ls_handle, ObLSGetMod::STORAGE_MOD))) {
      LOG_WARN("failed to get ls", K(ret), K(ls_id));
    } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls is null", K(ret), K(ls_id));
    } else if (OB_FAIL(ls->get_ls_info(ls_info))) {
      LOG_WARN("failed to get ls info", K(ret), K(ls_id));
    } else {
      result.required_disk_size_ = ls_info.required_data_disk_size_;
      //check standby and primary node version match
      result.cluster_version_ = GET_MIN_CLUSTER_VERSION();
      ObLSTabletService *tablet_service = ls->get_tablet_svr();
      if (OB_ISNULL(tablet_service)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet service is null", K(ret), K(ls_id));
      } else if (OB_FAIL(tablet_service->get_ls_migration_required_size(result.total_tablet_size_))) {
        LOG_WARN("failed to get ls migration required size", K(ret), K(ls_id));
      } else {
        LOG_INFO("calculated total tablet size for validation",
                 "ls_info_size", result.required_disk_size_,
                 "total_tablet_size", result.total_tablet_size_);
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialize_ob_to_proto(result, response))) {
      LOG_WARN("failed to serialize ObCheckRestorePreconditionResult", K(ret));
    } else {
      share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
      LOG_INFO("check_restore_precondition RPC handled successfully",
               K(ls_id), K(result.required_disk_size_), K(result.cluster_version_));
    }
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

int ObStorageGrpcServiceImpl::build_tablet_sstable_info_(
    grpc::ServerContext* context,
    const obrpc::ObCopyTabletSSTableInfoArg &tablet_arg,
    ObLS *ls,
    grpc::ServerWriter<FetchTabletSSTableInfoRes>* writer)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ls) || OB_ISNULL(writer)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls), KP(writer));
  } else {
    ObCopySSTableInfoObProducer producer;
    ObCopyTabletSSTableHeader header;
    FetchTabletSSTableInfoRes header_response;
    if (OB_FAIL(producer.init(tablet_arg, ls))) {
      LOG_WARN("failed to init copy sstable info ob producer", K(ret), K(tablet_arg));
    } else if (OB_FAIL(producer.get_copy_tablet_sstable_header(header))) {
      LOG_WARN("failed to get copy tablet sstable header", K(ret), K(tablet_arg));
    } else if (OB_FAIL(serialize_ob_to_proto(header, &header_response))) {
      LOG_WARN("failed to serialize ObCopyTabletSSTableHeader", K(ret));
    } else if (!writer->Write(header_response)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to write tablet sstable header to stream", K(ret));
    } else {
      int64_t sstable_count = header.sstable_count_;
      for (int64_t i = 0; OB_SUCC(ret) && i < sstable_count; ++i) {
        if (context->IsCancelled()) {
          ret = OB_CANCELED;
          LOG_WARN("client cancelled the request", K(ret));
          break;
        }

        ObCopyTabletSSTableInfo sstable_info;
        if (OB_FAIL(producer.get_next_sstable_info(sstable_info))) {
          if (OB_ITER_END == ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected end of sstable info stream", K(ret), K(i), K(sstable_count), K(tablet_arg));
          } else {
            LOG_WARN("failed to get next sstable info", K(ret), K(tablet_arg));
          }
        } else {
          FetchTabletSSTableInfoRes sstable_response;
          if (OB_FAIL(serialize_ob_to_proto(sstable_info, &sstable_response))) {
            LOG_WARN("failed to serialize ObCopyTabletSSTableInfo", K(ret));
          } else if (!writer->Write(sstable_response)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to write sstable info to stream", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObStorageGrpcServiceImpl::build_sstable_macro_info_(
    grpc::ServerContext* context,
    const obrpc::ObCopySSTableMacroRangeInfoHeader &header,
    const obrpc::ObCopySSTableMacroRangeInfoArg &arg,
    grpc::ServerWriter<FetchSSTableMacroInfoRes>* writer)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(writer)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(writer));
  } else if (!header.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid header", K(ret), K(header));
  } else {
    // Send header first
    FetchSSTableMacroInfoRes header_response;
    if (OB_FAIL(serialize_ob_to_proto(header, &header_response))) {
      LOG_WARN("failed to serialize ObCopySSTableMacroRangeInfoHeader", K(ret));
    } else if (!writer->Write(header_response)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to write sstable macro range header to stream", K(ret));
    } else if (header.macro_range_count_ > 0) {
      ObICopySSTableMacroRangeObProducer *producer = nullptr;
      void *buf = nullptr;
      ObCopySSTableMacroRangeObProducer *sstable_producer = nullptr;
      if (OB_ISNULL(buf = mtl_malloc(sizeof(ObCopySSTableMacroRangeObProducer), "SSTMacroRange"))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory", K(ret));
      } else if (FALSE_IT(sstable_producer = new (buf) ObCopySSTableMacroRangeObProducer())) {
      } else if (OB_FAIL(sstable_producer->init(arg.tenant_id_, arg.ls_id_, arg.tablet_id_,
              header, arg.macro_range_max_marco_count_))) {
        LOG_WARN("failed to init sstable macro range producer", K(ret), K(arg), K(header));
      } else {
        producer = sstable_producer;
        sstable_producer = nullptr;
      }
      if (OB_FAIL(ret) && OB_NOT_NULL(sstable_producer)) {
        sstable_producer->~ObCopySSTableMacroRangeObProducer();
        mtl_free(sstable_producer);
        sstable_producer = nullptr;
      }
      if (OB_SUCC(ret) && OB_NOT_NULL(producer)) {
        SMART_VAR(ObCopyMacroRangeInfo, macro_range_info) {
          for (int64_t i = 0; OB_SUCC(ret) && i < header.macro_range_count_; ++i) {
            if (context->IsCancelled()) {
              ret = OB_CANCELED;
              LOG_WARN("client cancelled the request", K(ret));
              break;
            }

            macro_range_info.reuse();
            if (OB_FAIL(producer->get_next_macro_range_info(macro_range_info))) {
              if (OB_ITER_END == ret) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("unexpected end of macro range info stream", K(ret), K(i), K(header.macro_range_count_), K(header.copy_table_key_));
              } else {
                LOG_WARN("failed to get next macro range info", K(ret), K(header.copy_table_key_));
              }
            } else {
              FetchSSTableMacroInfoRes macro_response;
              if (OB_FAIL(serialize_ob_to_proto(macro_range_info, &macro_response))) {
                LOG_WARN("failed to serialize ObCopyMacroRangeInfo", K(ret));
              } else if (!writer->Write(macro_response)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("failed to write macro range info to stream", K(ret));
              }
            }
          }
        }
      }
      if (OB_NOT_NULL(producer)) {
        producer->~ObICopySSTableMacroRangeObProducer();
        mtl_free(producer);
        producer = nullptr;
      }
    }
  }
  return ret;
}

int ObStorageGrpcClient::init_tablet_sstable_info_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    const obrpc::ObCopyTabletsSSTableInfoArg &arg,
    common::ObIAllocator &allocator,
    restore::ObRestoreHelperSSTableInfoCtx &sstable_info_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStorageGrpcClient *grpc_client = nullptr;

  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStorageGrpcClient)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc grpc client", K(ret));
  } else {
    void *ctx_buf = nullptr;
    grpc_client = new (buf) ObStorageGrpcClient();
    if (OB_FAIL(grpc_client->init(src_addr, timeout))) {
      LOG_WARN("failed to init storage grpc client", K(ret), K(src_addr), K(timeout));
    } else if (OB_ISNULL(ctx_buf = allocator.alloc(sizeof(grpc::ClientContext)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc grpc client context", K(ret));
    } else if (FALSE_IT(sstable_info_ctx.sstable_info_context_ = new (ctx_buf) grpc::ClientContext())) {
    } else if (OB_FAIL(grpc_client->create_tablet_sstable_info_stream(arg, *sstable_info_ctx.sstable_info_context_,
                                                                          sstable_info_ctx.sstable_info_reader_))) {
        LOG_WARN("failed to create tablet sstable info stream", K(ret), K(arg), K(src_addr));
    } else {
      sstable_info_ctx.grpc_client_ = grpc_client;
      grpc_client = nullptr;
    }
  }
  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(grpc_client)) {
      grpc_client->~ObStorageGrpcClient();
      allocator.free(grpc_client);
      grpc_client = nullptr;
    }
    if (OB_NOT_NULL(sstable_info_ctx.sstable_info_context_)) {
      sstable_info_ctx.sstable_info_context_->~ClientContext();
      allocator.free(sstable_info_ctx.sstable_info_context_);
      sstable_info_ctx.sstable_info_context_ = nullptr;
    }
    sstable_info_ctx.grpc_client_ = nullptr;
    sstable_info_ctx.sstable_info_reader_.reset();
  }

  return ret;
}

int ObStorageGrpcClient::init_sstable_macro_info_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    const obrpc::ObCopySSTableMacroRangeInfoArg &arg,
    common::ObIAllocator &allocator,
    restore::ObRestoreHelperSSTableMacroRangeCtx &macro_range_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStorageGrpcClient *grpc_client = nullptr;

  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStorageGrpcClient)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc grpc client", K(ret));
  } else {
    void *ctx_buf = nullptr;
    grpc_client = new (buf) ObStorageGrpcClient();
    if (OB_FAIL(grpc_client->init(src_addr, timeout))) {
      LOG_WARN("failed to init storage grpc client", K(ret), K(src_addr), K(timeout));
    } else if (OB_ISNULL(ctx_buf = allocator.alloc(sizeof(grpc::ClientContext)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc grpc client context", K(ret));
    } else if (FALSE_IT(macro_range_ctx.macro_info_context_ = new (ctx_buf) grpc::ClientContext())) {
    } else if (OB_FAIL(grpc_client->create_sstable_macro_info_stream(arg, *macro_range_ctx.macro_info_context_,
                                                                          macro_range_ctx.macro_info_reader_))) {
        LOG_WARN("failed to create sstable macro info stream", K(ret), K(arg), K(src_addr));
    } else {
      macro_range_ctx.grpc_client_ = grpc_client;
      grpc_client = nullptr;
    }
  }
  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(grpc_client)) {
      grpc_client->~ObStorageGrpcClient();
      allocator.free(grpc_client);
      grpc_client = nullptr;
    }
    if (OB_NOT_NULL(macro_range_ctx.macro_info_context_)) {
      macro_range_ctx.macro_info_context_->~ClientContext();
      allocator.free(macro_range_ctx.macro_info_context_);
      macro_range_ctx.macro_info_context_ = nullptr;
    }
    macro_range_ctx.grpc_client_ = nullptr;
    macro_range_ctx.macro_info_reader_.reset();
  }

  return ret;
}

int ObStorageGrpcClient::init_macro_block_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    const obrpc::ObCopyMacroBlockRangeArg &arg,
    common::ObIAllocator &allocator,
    restore::ObRestoreHelperMacroBlockCtx &macro_block_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStorageGrpcClient *grpc_client = nullptr;

  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStorageGrpcClient)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc grpc client", K(ret));
  } else {
    void *ctx_buf = nullptr;
    grpc_client = new (buf) ObStorageGrpcClient();
    if (OB_FAIL(grpc_client->init(src_addr, timeout))) {
      LOG_WARN("failed to init storage grpc client", K(ret), K(src_addr), K(timeout));
    } else if (OB_ISNULL(ctx_buf = allocator.alloc(sizeof(grpc::ClientContext)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc grpc client context", K(ret));
    } else if (FALSE_IT(macro_block_ctx.macro_block_context_ = new (ctx_buf) grpc::ClientContext())) {
    } else if (OB_FAIL(grpc_client->create_macro_block_stream(arg, *macro_block_ctx.macro_block_context_,
                                                                  macro_block_ctx.macro_block_reader_))) {
        LOG_WARN("failed to create macro block stream", K(ret), K(arg), K(src_addr));
    } else {
      macro_block_ctx.grpc_client_ = grpc_client;
      grpc_client = nullptr;
    }
  }
  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(grpc_client)) {
      grpc_client->~ObStorageGrpcClient();
      allocator.free(grpc_client);
      grpc_client = nullptr;
    }
    if (OB_NOT_NULL(macro_block_ctx.macro_block_context_)) {
      macro_block_ctx.macro_block_context_->~ClientContext();
      allocator.free(macro_block_ctx.macro_block_context_);
      macro_block_ctx.macro_block_context_ = nullptr;
    }
    macro_block_ctx.grpc_client_ = nullptr;
    macro_block_ctx.macro_block_reader_.reset();
  }

  return ret;
}

int ObStorageGrpcClient::init_tablet_info_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    const obrpc::ObCopyTabletInfoArg &arg,
    common::ObIAllocator &allocator,
    restore::ObRestoreHelperTabletInfoCtx &tablet_info_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStorageGrpcClient *grpc_client = nullptr;

  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStorageGrpcClient)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc grpc client", K(ret));
  } else {
    void *ctx_buf = nullptr;
    grpc_client = new (buf) ObStorageGrpcClient();
    if (OB_FAIL(grpc_client->init(src_addr, timeout))) {
      LOG_WARN("failed to init storage grpc client", K(ret), K(src_addr), K(timeout));
    } else if (OB_ISNULL(ctx_buf = allocator.alloc(sizeof(grpc::ClientContext)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc grpc client context", K(ret));
    } else if (FALSE_IT(tablet_info_ctx.tablet_info_context_ = new (ctx_buf) grpc::ClientContext())) {
    } else if (OB_FAIL(grpc_client->create_tablet_info_stream(arg, *tablet_info_ctx.tablet_info_context_,
                                                                  tablet_info_ctx.tablet_info_reader_))) {
        LOG_WARN("failed to create tablet info stream", K(ret), K(arg), K(src_addr));
    } else {
      tablet_info_ctx.grpc_client_ = grpc_client;
      grpc_client = nullptr;
    }
  }
  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(grpc_client)) {
      grpc_client->~ObStorageGrpcClient();
      allocator.free(grpc_client);
      grpc_client = nullptr;
    }
    if (OB_NOT_NULL(tablet_info_ctx.tablet_info_context_)) {
      tablet_info_ctx.tablet_info_context_->~ClientContext();
      allocator.free(tablet_info_ctx.tablet_info_context_);
      tablet_info_ctx.tablet_info_context_ = nullptr;
    }
    tablet_info_ctx.grpc_client_ = nullptr;
    tablet_info_ctx.tablet_info_reader_.reset();
  }

  return ret;
}

// ==================== ObStorageGrpcClient ====================
ObStorageGrpcClient::ObStorageGrpcClient()
  : is_inited_(false),
    grpc_client_()
{
}

ObStorageGrpcClient::~ObStorageGrpcClient()
{
}

int ObStorageGrpcClient::init(const common::ObAddr& addr, int64_t timeout)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObStorageGrpcClient already inited", K(ret));
  } else if (!addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid addr", K(ret), K(addr));
  } else {
    ret = grpc_client_.init(addr, timeout);
    if (OB_SUCC(ret)) {
      is_inited_ = true;
      LOG_INFO("ObStorageGrpcClient init success", K(addr), K(timeout));
    } else {
      LOG_WARN("failed to init grpc client", K(ret), K(addr));
    }
  }

  return ret;
}

int ObStorageGrpcClient::get_ls_view_tablet_count(ObGetLSViewTabletCountResult& result)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStorageGrpcClient not inited", K(ret));
  } else {
    GetLSViewTabletCountReq req;
    GetLSViewTabletCountRes res;
    grpc::ClientContext context;
    GRPC_SET_CONTEXT(grpc_client_, context);
    auto status = grpc_client_.stub_->get_ls_view_tablet_count(&context, req, &res);
    if (OB_FAIL(grpc_client_.translate_error(status))) {
      LOG_WARN("failed to call get_ls_view_tablet_count RPC", K(ret));
    } else if (OB_FAIL(deserialize_proto_to_ob(res, result))) {
      LOG_WARN("failed to deserialize ObGetLSViewTabletCountResult", K(ret));
    }
  }

  return ret;
}

int ObStorageGrpcClient::check_restore_precondition(
    obrpc::ObCheckRestorePreconditionResult& result)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStorageGrpcClient not inited", K(ret));
  } else {
    storageservice::CheckRestorePreconditionReq req;
    storageservice::CheckRestorePreconditionRes res;
    grpc::ClientContext context;
    GRPC_SET_CONTEXT(grpc_client_, context);
    auto status = grpc_client_.stub_->check_restore_precondition(&context, req, &res);
    if (OB_FAIL(grpc_client_.translate_error(status))) {
      LOG_WARN("failed to call check_restore_precondition RPC", K(ret));
    } else if (OB_FAIL(deserialize_proto_to_ob(res, result))) {
      LOG_WARN("failed to deserialize ObCheckRestorePreconditionResult", K(ret));
    }
  }

  return ret;
}

int ObStorageGrpcClient::fetch_tablet_info(
    const obrpc::ObCopyTabletInfoArg& arg,
    std::function<int(const obrpc::ObCopyTabletInfo&)> callback)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStorageGrpcClient not inited", K(ret));
  } else {
    FetchTabletInfoReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyTabletInfoArg", K(ret));
    } else {
      grpc::ClientContext context;
      GRPC_SET_CONTEXT(grpc_client_, context);
      auto reader = grpc_client_.stub_->fetch_tablet_info(&context, req);

      if (OB_ISNULL(reader)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create stream reader", K(ret));
      } else {
        FetchTabletInfoRes response;
        while (reader->Read(&response)) {
          const std::string& res_buf = response.buf();
          const uint64_t res_size = response.size();

          if (res_size > 0) {
            ObCopyTabletInfo tablet_info;
            if (OB_FAIL(deserialize_proto_to_ob(response, tablet_info))) {
              LOG_WARN("failed to deserialize ObCopyTabletInfo", K(ret), K(res_size));
              break;
            } else {
              if (OB_FAIL(callback(tablet_info))) {
                if (OB_ITER_END == ret) {
                  ret = OB_SUCCESS;
                  LOG_INFO("callback requested to stop");
                } else {
                  LOG_WARN("callback failed", K(ret));
                }
                break;
              }
            }
          }
        }

        grpc::Status status = reader->Finish();
        int grpc_ret = grpc_client_.translate_error(status);

        if (OB_FAIL(grpc_ret)) {
          LOG_WARN("fetch_tablet_info stream failed", K(ret));
        } else {
          LOG_INFO("fetch_tablet_info stream completed", K(arg));
        }
      }
    }
  }

  return ret;
}

int ObStorageGrpcClient::create_tablet_info_stream(
    const obrpc::ObCopyTabletInfoArg &arg,
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<storageservice::FetchTabletInfoRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStorageGrpcClient not inited", K(ret));
  } else {
    FetchTabletInfoReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyTabletInfoArg", K(ret));
    } else {
      GRPC_SET_CONTEXT(grpc_client_, context);
      reader = grpc_client_.stub_->fetch_tablet_info(&context, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create tablet info stream reader", K(ret));
      }
    }
  }

  return ret;
}

int ObStorageGrpcClient::create_ls_view_stream(
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<storageservice::FetchLSViewRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStorageGrpcClient not inited", K(ret));
  } else {
    FetchLSViewReq req;
    GRPC_SET_CONTEXT(grpc_client_, context);
    reader = grpc_client_.stub_->fetch_ls_view(&context, req);
    if (OB_ISNULL(reader.get())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to create ls view stream reader", K(ret));
    }
  }
  return ret;
}

int ObStorageGrpcClient::create_tablet_sstable_info_stream(
    const obrpc::ObCopyTabletsSSTableInfoArg &arg,
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<storageservice::FetchTabletSSTableInfoRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStorageGrpcClient not inited", K(ret));
  } else {
    FetchTabletSSTableInfoReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyTabletsSSTableInfoArg", K(ret));
    } else {
      GRPC_SET_CONTEXT(grpc_client_, context);
      reader = grpc_client_.stub_->fetch_tablet_sstable_info(&context, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create tablet sstable info stream reader", K(ret));
      }
    }
  }

  return ret;
}

int ObStorageGrpcClient::create_sstable_macro_info_stream(
    const obrpc::ObCopySSTableMacroRangeInfoArg &arg,
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<storageservice::FetchSSTableMacroInfoRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStorageGrpcClient not inited", K(ret));
  } else {
    FetchSSTableMacroInfoReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopySSTableMacroRangeInfoArg", K(ret));
    } else {
      GRPC_SET_CONTEXT(grpc_client_, context);
      reader = grpc_client_.stub_->fetch_sstable_macro_info(&context, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create sstable macro info stream reader", K(ret));
      }
    }
  }

  return ret;
}

int ObStorageGrpcClient::create_macro_block_stream(
    const obrpc::ObCopyMacroBlockRangeArg &arg,
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<storageservice::FetchMacroBlockRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStorageGrpcClient not inited", K(ret));
  } else {
    FetchMacroBlockReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyMacroBlockRangeArg", K(ret));
    } else {
      GRPC_SET_CONTEXT(grpc_client_, context);
      reader = grpc_client_.stub_->fetch_macro_block(&context, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create macro block stream reader", K(ret));
      }
    }
  }

  return ret;
}

int ObStorageGrpcClient::translate_error(const grpc::Status &status)
{
  return grpc_client_.translate_error(status);
}

int ObStorageGrpcClient::init_ls_view_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    common::ObIAllocator &allocator,
    ObLSMetaPackage &ls_meta,
    restore::ObRestoreHelperLSViewCtx &ls_view_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStorageGrpcClient *grpc_client = nullptr;

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStorageGrpcClient)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc grpc client", K(ret));
    } else {
      grpc_client = new (buf) ObStorageGrpcClient();
      if (OB_FAIL(grpc_client->init(src_addr, timeout))) {
        LOG_WARN("failed to init storage grpc client", K(ret), K(src_addr), K(timeout));
      } else {
        void *ctx_buf = nullptr;
        if (OB_ISNULL(ctx_buf = allocator.alloc(sizeof(grpc::ClientContext)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc grpc client context", K(ret));
        } else {
          ls_view_ctx.ls_view_context_ = new (ctx_buf) grpc::ClientContext();
          if (OB_FAIL(grpc_client->create_ls_view_stream(
                  *ls_view_ctx.ls_view_context_, ls_view_ctx.ls_view_reader_))) {
            LOG_WARN("failed to create ls view stream", K(ret), K(src_addr), K(timeout));
          } else {
            FetchLSViewRes first_res;
            if (!ls_view_ctx.ls_view_reader_->Read(&first_res)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("failed to read first fetch_ls_view response", K(ret), K(src_addr), K(timeout));
            } else if (storageservice::LS_META_PACKAGE != first_res.entry_type()) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("first fetch_ls_view response is not ls meta package", K(ret),
                  "entry_type", first_res.entry_type());
            } else if (OB_FAIL(deserialize_proto_to_ob(first_res, ls_meta))) {
              LOG_WARN("failed to deserialize ObLSMetaPackage", K(ret));
            } else {
              ls_view_ctx.ls_meta_fetched_ = true;
              ls_view_ctx.grpc_client_ = grpc_client;
              grpc_client = nullptr;
            }
          }
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    if (ls_view_ctx.ls_view_reader_ && OB_NOT_NULL(grpc_client)) {
      int tmp_ret = OB_SUCCESS;
      if(OB_TMP_FAIL(restore::ObRestoreHelperCtxUtil::close_reader(ls_view_ctx.ls_view_reader_, grpc_client))) {
        LOG_WARN("failed to close ls view reader", KR(tmp_ret));
      }
    }
    if (OB_NOT_NULL(ls_view_ctx.ls_view_context_)) {
      ls_view_ctx.ls_view_context_->~ClientContext();
      allocator.free(ls_view_ctx.ls_view_context_);
      ls_view_ctx.ls_view_context_ = nullptr;
    }
    if (OB_NOT_NULL(grpc_client)) {
      grpc_client->~ObStorageGrpcClient();
      allocator.free(grpc_client);
      grpc_client = nullptr;
    }
    ls_view_ctx.ls_meta_fetched_ = false;
    ls_view_ctx.grpc_client_ = nullptr;
    ls_view_ctx.ls_view_reader_.reset();
  }

  return ret;
}

} // storage
} // oceanbase
