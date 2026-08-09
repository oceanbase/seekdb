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

#include "standby/ob_standby_grpc.h"
#include "standby/ob_standby_grpc_service.h"
#include "grpc/ob_grpc_server.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/time/ob_time_utility.h"
#include "standby/ob_standby_palf_base_info.h"
#include "standby/ob_standby_grpc_stream_util.h"
#include "standby/restore/ob_standby_restore_reader.h"
#include "standby/standby_host.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "common/ob_version_def.h"
#include "share/rc/ob_server_runtime.h"
#include "standby/restore/ob_restore_helper_ctx.h"
#include "logservice/ob_log_handler.h"
#include <string>

using namespace oceanbase::common;
using namespace oceanbase::obcall;
using namespace oceanbase::obgrpc;
using namespace oceanbase::storage;
using namespace standbyservice;

namespace oceanbase
{
namespace standby
{

class StandbyGrpcService final : public standbyservice::StandbyService::Service
{
public:
  explicit StandbyGrpcService(const StandbyConfig &config)
    : io_timeout_ms_(config.io_timeout_ms_)
  {}
  virtual ~StandbyGrpcService() {}

  grpc::Status fetch_ls_view(grpc::ServerContext* context,
                             const standbyservice::FetchLSViewReq* request,
                             grpc::ServerWriter<standbyservice::FetchLSViewRes>* writer) override;

  grpc::Status fetch_tablet_info(grpc::ServerContext* context,
                                    const standbyservice::FetchTabletInfoReq* request,
                                    grpc::ServerWriter<standbyservice::FetchTabletInfoRes>* writer) override;

  grpc::Status fetch_tablet_sstable_info(grpc::ServerContext* context,
                                            const standbyservice::FetchTabletSSTableInfoReq* request,
                                            grpc::ServerWriter<standbyservice::FetchTabletSSTableInfoRes>* writer) override;

  grpc::Status fetch_sstable_macro_info(grpc::ServerContext* context,
                                            const standbyservice::FetchSSTableMacroInfoReq* request,
                                            grpc::ServerWriter<standbyservice::FetchSSTableMacroInfoRes>* writer) override;

  grpc::Status fetch_macro_block(grpc::ServerContext* context,
                                    const standbyservice::FetchMacroBlockReq* request,
                                    grpc::ServerWriter<standbyservice::FetchMacroBlockRes>* writer) override;

  grpc::Status get_ls_view_tablet_count(grpc::ServerContext* context,
                                            const standbyservice::GetLSViewTabletCountReq* request,
                                            standbyservice::GetLSViewTabletCountRes* response) override;

  grpc::Status check_restore_precondition(grpc::ServerContext* context,
                                            const standbyservice::CheckRestorePreconditionReq* request,
                                            standbyservice::CheckRestorePreconditionRes* response) override;

  grpc::Status fetch_standby_palf_base_info(grpc::ServerContext* context,
                                            const standbyservice::FetchStandbyPalfBaseInfoReq* request,
                                            standbyservice::FetchStandbyPalfBaseInfoRes* response) override;

  grpc::Status fetch_log(grpc::ServerContext* context,
                         const standbyservice::FetchLogReq* request,
                         grpc::ServerWriter<standbyservice::FetchLogRes>* writer) override;

private:
  int64_t io_timeout_ms_;
};

int create_and_register_standby_grpc_service(
    obgrpc::ObGrpcServer &grpc_server,
    const StandbyConfig &config,
    StandbyGrpcService *&service)
{
  int ret = OB_SUCCESS;
  service = nullptr;
  StandbyGrpcService *new_service = nullptr;
  if (!config.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standby gRPC service config", KR(ret));
  } else if (OB_ISNULL(new_service = OB_NEW(StandbyGrpcService, "StandbyGrpc", config))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate standby gRPC service", KR(ret));
  } else if (OB_FAIL(grpc_server.register_service(new_service))) {
    LOG_WARN("failed to register standby gRPC service", KR(ret));
    OB_DELETE(StandbyGrpcService, "StandbyGrpc", new_service);
  } else {
    service = new_service;
  }
  return ret;
}

void destroy_standby_grpc_service(StandbyGrpcService *&service)
{
  if (nullptr != service) {
    OB_DELETE(StandbyGrpcService, "StandbyGrpc", service);
    service = nullptr;
  }
}

ObStandbyLSViewTabletCountResult::ObStandbyLSViewTabletCountResult()
  : tablet_count_(0)
{
}

OB_SERIALIZE_MEMBER(ObStandbyLSViewTabletCountResult, tablet_count_);

ObStandbyLSViewMeta::ObStandbyLSViewMeta()
  : ls_meta_(), physical_checkpoint_scn_()
{
}

OB_SERIALIZE_MEMBER(ObStandbyLSViewMeta, ls_meta_, physical_checkpoint_scn_);

grpc::Status StandbyGrpcService::fetch_ls_view(
    grpc::ServerContext* context,
    const FetchLSViewReq* request,
    grpc::ServerWriter<FetchLSViewRes>* writer)
{
  int ret = OB_SUCCESS;
  const int64_t start_ts = ObTimeUtil::current_time();
  share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
  UNUSED(request);
  SERVER_MODULE_SCOPE {
    ObLSService *ls_service = nullptr;
    ObLS *ls = nullptr;
    bool ls_meta_sent = false;
    int64_t total_tablet_count = 0;
    LOG_INFO("start to fetch ls view", K(ls_id));
    auto fill_ls_meta_f = [&context, &writer, &ls_meta_sent](const ObStandbyLSViewMeta &ls_view_meta)->int {
      int ret = OB_SUCCESS;
      if (context->IsCancelled()) {
        ret = OB_CANCELED;
        LOG_WARN("client cancelled fetch_ls_view request", K(ret));
      } else {
        FetchLSViewRes response;
        response.set_entry_type(standbyservice::LS_META_PACKAGE);
        if (OB_FAIL(serialize_ob_to_proto(ls_view_meta, &response))) {
          LOG_ERROR("failed to serialize standby ls view meta", K(ret));
        } else if (!writer->Write(response)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("failed to write ls meta package to stream", K(ret));
        } else {
          ls_meta_sent = true;
        }
      }
      return ret;
    };

    auto fill_tablet_meta_f = [&context,
                               &writer,
                               &total_tablet_count]
        (const obcall::ObCopyTabletInfo &tablet_info, const ObTabletHandle &tablet_handle)->int {
      int ret = OB_SUCCESS;
      UNUSED(tablet_handle);
      if (context->IsCancelled()) {
        ret = OB_CANCELED;
        LOG_WARN("client cancelled fetch_ls_view request", K(ret));
      } else {
        FetchLSViewRes response;
        response.set_entry_type(standbyservice::TABLET_INFO);
        if (OB_FAIL(serialize_ob_to_proto(tablet_info, &response))) {
          LOG_ERROR("failed to serialize ObCopyTabletInfo", K(ret), K(tablet_info));
        } else if (!writer->Write(response)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("failed to write tablet info to stream", K(ret), K(tablet_info));
        } else {
          ++total_tablet_count;
          LOG_INFO("fetch_ls_view batch sent", K(total_tablet_count));
        }
      }
      return ret;
    };

    LOG_INFO("start to fetch ls view", K(ls_id));
    if (OB_ISNULL(ls_service = share::server_service<storage::ObLSService>())) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "ls service should not be null", K(ret), KP(ls_service));
    } else if (OB_FAIL(ls_service->get_ls(ls))) {
      LOG_WARN("failed to get log stream", K(ret), K(ls_id));
    } else if (OB_ISNULL(ls)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(ls_id));
    } else {
      ObStandbyLSViewMeta ls_view_meta;
      ObLSTabletIterator tablet_iter(ObMDSGetTabletMode::READ_WITHOUT_CHECK);
      if (OB_FAIL(ls->get_physical_restore_base(
              ls_view_meta.ls_meta_, ls_view_meta.physical_checkpoint_scn_))) {
        LOG_WARN("failed to get physical restore base", K(ret), K(ls_id));
      } else if (!ls_view_meta.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid standby ls view meta", K(ret), K(ls_view_meta));
      } else if (OB_FAIL(fill_ls_meta_f(ls_view_meta))) {
        LOG_WARN("failed to stream ls meta", K(ret), K(ls_id));
      } else if (OB_FAIL(ls->build_tablet_iter(tablet_iter))) {
        LOG_WARN("failed to build tablet iterator", K(ret), K(ls_id));
      }

      while (OB_SUCC(ret)) {
        ObTabletHandle tablet_handle;
        ObTablet *tablet = nullptr;
        obcall::ObCopyTabletInfo tablet_info;
        if (OB_FAIL(tablet_iter.get_next_tablet(tablet_handle))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to iterate source tablets", K(ret), K(ls_id));
          }
          break;
        } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("source tablet is null", K(ret), K(tablet_handle));
        } else if (FALSE_IT(tablet_info.tablet_id_ = tablet->get_tablet_meta().tablet_id_)) {
        } else if (FALSE_IT(tablet_info.status_ = ObCopyTabletStatus::TABLET_EXIST)) {
        } else if (FALSE_IT(tablet_info.version_ = DATA_CURRENT_VERSION)) {
        } else if (OB_FAIL(tablet_info.param_.build_from_tablet(*tablet))) {
          LOG_WARN("failed to build source tablet meta", K(ret), KPC(tablet));
        } else if (OB_FAIL(fill_tablet_meta_f(tablet_info, tablet_handle))) {
          LOG_WARN("failed to stream source tablet meta", K(ret), K(tablet_info));
        }
      }
    }

    if (OB_SUCC(ret) && !ls_meta_sent) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("ls meta is not sent in fetch_ls_view stream", K(ret), K(ls_id));
    }
    if (OB_SUCC(ret)) {
      LOG_INFO("fetch_ls_view stream finished", K(ls_id), K(total_tablet_count),
          "cost_ts", ObTimeUtil::current_time() - start_ts);
    }
  }
  LOG_INFO("fetch_ls_view stream finished", K(ls_id), "cost_ts", ObTimeUtil::current_time() - start_ts);

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status StandbyGrpcService::get_ls_view_tablet_count(
    grpc::ServerContext* context,
    const standbyservice::GetLSViewTabletCountReq* request,
    standbyservice::GetLSViewTabletCountRes* response)
{
  int ret = OB_SUCCESS;
  UNUSED(context);
  UNUSED(request);
  ObStandbyLSViewTabletCountResult result;

  SERVER_MODULE_SCOPE {
    ObLS *ls = nullptr;
    ObLSVTInfo ls_info;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    ObLSService *ls_service = share::server_service<ObLSService>();
    if (OB_ISNULL(ls_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls service should not be null", K(ret));
    } else if (OB_FAIL(ls_service->get_ls(ls))) {
      LOG_WARN("failed to get ls", K(ret), K(ls_id));
    } else if (OB_ISNULL(ls)) {
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
      LOG_WARN("failed to serialize ObStandbyLSViewTabletCountResult", K(ret));
    } else {
      share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
      LOG_INFO("get_ls_view_tablet_count RPC handled successfully", K(ls_id), K(result.tablet_count_));
    }
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status StandbyGrpcService::fetch_tablet_info(
    grpc::ServerContext* context,
    const FetchTabletInfoReq* request,
    grpc::ServerWriter<FetchTabletInfoRes>* writer)
{
  int ret = OB_SUCCESS;

  ObCopyTabletInfoArg arg;

  if (OB_FAIL(deserialize_proto_to_ob(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopyTabletInfoArg", K(ret));
  }

  SERVER_MODULE_SCOPE {
    ObLSService *ls_service = nullptr;
    ObLS *ls = nullptr;
    ObCopyTabletInfoObProducer producer;
    if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls service should not be null", K(ret));
    } else if (OB_FAIL(ls_service->get_ls(ls))) {
      LOG_WARN("failed to get log stream", K(ret), K(arg));
    } else if (OB_ISNULL(ls)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(arg));
    } else if (OB_FAIL(producer.init(arg.ls_id_, arg.tablet_id_list_))) {
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

grpc::Status StandbyGrpcService::fetch_tablet_sstable_info(
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
    SERVER_MODULE_SCOPE {
      ObCopyTabletsSSTableInfoObProducer tablets_producer;
      ObLSService *ls_service = nullptr;
      obcall::ObCopyTabletSSTableInfoArg tablet_arg;
      ObLS *ls = nullptr;
      if (OB_FAIL(tablets_producer.init(arg.ls_id_, arg.tablet_sstable_info_arg_list_))) {
        LOG_WARN("failed to init copy tablets sstable info ob producer", K(ret), K(arg));
      } else if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls service should not be null", K(ret));
      } else if (OB_FAIL(ls_service->get_ls(ls))) {
        LOG_WARN("failed to get log stream", K(ret), K(arg));
      } else if (OB_ISNULL(ls)) {
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
        } else if (OB_FAIL(ObStandbyGrpcStreamUtil::build_tablet_sstable_info(
                       context, tablet_arg, ls, writer))) {
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

grpc::Status StandbyGrpcService::fetch_sstable_macro_info(
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
    SERVER_MODULE_SCOPE {
      ObCopySSTableMacroObProducer producer;
      obcall::ObCopySSTableMacroRangeInfoHeader header;
      if (OB_FAIL(producer.init(arg.ls_id_, arg.tablet_id_,
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
          } else if (OB_FAIL(ObStandbyGrpcStreamUtil::build_sstable_macro_info(
                         context, header, arg, writer))) {
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

grpc::Status StandbyGrpcService::fetch_macro_block(
    grpc::ServerContext* context,
    const FetchMacroBlockReq* request,
    grpc::ServerWriter<FetchMacroBlockRes>* writer)
{
  int ret = OB_SUCCESS;

  ObCopyMacroBlockRangeArg arg;
  const int64_t start_ts = ObTimeUtil::current_time();
  LOG_INFO("fetch_macro_block stream begin");
  if (OB_FAIL(deserialize_proto_to_ob(*request, arg))) {
    LOG_WARN("failed to deserialize ObCopyMacroBlockRangeArg", K(ret));
  } else {
    LOG_INFO("fetch_macro_block arg decoded", K(arg));
    SERVER_MODULE_SCOPE {
      ObCopyMacroBlockObProducer producer;
      blocksstable::ObBufferReader data;
      obcall::ObCopyMacroBlockHeader header;
      FetchMacroBlockRes header_response;
      FetchMacroBlockRes data_response;
      if (OB_FAIL(producer.init(arg.ls_id_, arg.table_key_,
          arg.copy_macro_range_info_, arg.data_version_, arg.backfill_tx_scn_, io_timeout_ms_))) {
        LOG_ERROR("failed to init copy macro block ob producer", K(ret), K(arg));
      }
      common::ObArenaAllocator header_allocator("MacroBlkHeader");
      while (OB_SUCC(ret)) {
        if (context->IsCancelled()) {
          ret = OB_CANCELED;
          LOG_WARN("client cancelled the request", K(ret));
          break;
        }
        header.reset();
        if (OB_FAIL(producer.get_next_macro_block(data, header))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_ERROR("failed to get next macro block", K(ret), K(arg));
          }
        } else {
          LOG_INFO("fetch_macro_block send one block", K(header), "data_size", data.length());
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
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("fetch_macro_block stream failed", K(ret), K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  } else {
    LOG_INFO("fetch_macro_block stream completed", K(arg), "cost_ts", ObTimeUtil::current_time() - start_ts);
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status StandbyGrpcService::check_restore_precondition(
    grpc::ServerContext* context,
    const standbyservice::CheckRestorePreconditionReq* request,
    standbyservice::CheckRestorePreconditionRes* response)
{
  int ret = OB_SUCCESS;
  obcall::ObCheckRestorePreconditionResult result;

  SERVER_MODULE_SCOPE {
    ObLS *ls = nullptr;
    share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    ObLSService *ls_service = share::server_service<ObLSService>();
    if (OB_ISNULL(ls_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls service should not be null", K(ret));
    } else if (OB_FAIL(ls_service->get_ls(ls))) {
      LOG_WARN("failed to get ls", K(ret), K(ls_id));
    } else if (OB_ISNULL(ls)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls is null", K(ret), K(ls_id));
    } else {
      ObLSTabletIterator tablet_iter(ObMDSGetTabletMode::READ_WITHOUT_CHECK);
      result.data_version_ = DATA_CURRENT_VERSION;
      if (OB_FAIL(ls->build_tablet_iter(tablet_iter))) {
        LOG_WARN("failed to build tablet iterator for restore sizing", K(ret), K(ls_id));
      }
      while (OB_SUCC(ret)) {
        ObTabletHandle tablet_handle;
        if (OB_FAIL(tablet_iter.get_next_tablet(tablet_handle))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to iterate tablet for restore sizing", K(ret), K(ls_id));
          }
          break;
        } else if (OB_ISNULL(tablet_handle.get_obj())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("tablet is null while calculating restore size", K(ret), K(ls_id));
        } else {
          const ObTabletSpaceUsage &usage = tablet_handle.get_obj()->get_tablet_meta().space_usage_;
          const int64_t data_size = std::max(
              usage.all_sstable_data_required_size_, usage.all_sstable_data_occupy_size_);
          if (data_size < 0 || usage.all_sstable_meta_size_ < 0
              || usage.tablet_clustered_meta_size_ < 0
              || data_size > INT64_MAX - usage.all_sstable_meta_size_
              || data_size + usage.all_sstable_meta_size_
                  > INT64_MAX - usage.tablet_clustered_meta_size_) {
            ret = OB_SIZE_OVERFLOW;
            LOG_WARN("invalid tablet restore size", K(ret), K(usage));
          } else {
            const int64_t tablet_size = data_size + usage.all_sstable_meta_size_
                + usage.tablet_clustered_meta_size_;
            if (result.total_tablet_size_ > INT64_MAX - tablet_size) {
              ret = OB_SIZE_OVERFLOW;
              LOG_WARN("restore size overflow", K(ret), K_(result.total_tablet_size), K(tablet_size));
            } else {
              result.total_tablet_size_ += tablet_size;
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        result.required_disk_size_ = result.total_tablet_size_;
        LOG_INFO("calculated restore disk requirement", K(ls_id), K(result.total_tablet_size_));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialize_ob_to_proto(result, response))) {
      LOG_WARN("failed to serialize ObCheckRestorePreconditionResult", K(ret));
    } else {
      share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
      LOG_INFO("check_restore_precondition RPC handled successfully",
               K(ls_id), K(result.required_disk_size_), K(result.data_version_));
    }
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status StandbyGrpcService::fetch_standby_palf_base_info(
    grpc::ServerContext* context,
    const standbyservice::FetchStandbyPalfBaseInfoReq* request,
    standbyservice::FetchStandbyPalfBaseInfoRes* response)
{
  int ret = OB_SUCCESS;
  standby::ObFetchStandbyPalfBaseInfoArg arg;
  standby::ObFetchStandbyPalfBaseInfoResult result;
  UNUSED(context);

  if (OB_ISNULL(request) || OB_ISNULL(response)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch standby palf base info request", K(ret), KP(request), KP(response));
  } else if (OB_FAIL(deserialize_proto_to_ob(*request, arg))) {
    LOG_WARN("failed to deserialize ObFetchStandbyPalfBaseInfoArg", K(ret));
  } else if (OB_FAIL(standby::ObStandbyPalfBaseInfoBuilder::build(arg, result))) {
    LOG_WARN("failed to build standby palf base info", K(ret), K(arg));
  } else if (OB_FAIL(serialize_ob_to_proto(result, response))) {
    LOG_WARN("failed to serialize ObFetchStandbyPalfBaseInfoResult", K(ret), K(result));
  } else {
    LOG_INFO("fetch standby palf base info RPC handled successfully", K(arg), K(result));
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

grpc::Status StandbyGrpcService::fetch_log(
    grpc::ServerContext *context,
    const standbyservice::FetchLogReq *request,
    grpc::ServerWriter<standbyservice::FetchLogRes> *writer)
{
  int ret = OB_SUCCESS;
  static const int64_t MAX_FETCH_BYTES = 64L * 1024L * 1024L;
  if (OB_ISNULL(context) || OB_ISNULL(request) || OB_ISNULL(writer)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch log request", K(ret), KP(context), KP(request), KP(writer));
    return obgrpc::ob_error_to_grpc_status(ret);
  }

  const palf::LSN start_lsn(request->start_lsn());
  int64_t sent_bytes = 0;
  if (request->max_bytes() > MAX_FETCH_BYTES) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fetch log request is too large", K(ret), "max_bytes", request->max_bytes());
  } else if (request->max_bytes() > 0 && !start_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch log start lsn", K(ret), K(start_lsn));
  } else {
    SERVER_MODULE_SCOPE {
      ObLSService *ls_service = share::server_service<ObLSService>();
      ObLS *ls = nullptr;
      logservice::ObLogHandler *log_handler = nullptr;
      palf::LSN begin_lsn;
      palf::LSN end_lsn;
      palf::PalfGroupBufferIterator iterator;
      if (OB_ISNULL(ls_service)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls service is null", K(ret));
      } else if (OB_FAIL(ls_service->get_ls(ls))) {
        LOG_WARN("failed to get log stream", K(ret));
      } else if (OB_ISNULL(ls) || OB_ISNULL(log_handler = ls->get_log_handler())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("log stream or handler is null", K(ret), KP(ls), KP(log_handler));
      } else if (request->max_bytes() > 0
                 && OB_FAIL(log_handler->get_begin_lsn(begin_lsn))) {
        LOG_WARN("failed to get source log begin lsn", K(ret), K(start_lsn));
      } else if (request->max_bytes() > 0
                 && OB_FAIL(log_handler->get_end_lsn(end_lsn))) {
        LOG_WARN("failed to get source log end lsn", K(ret), K(start_lsn));
      } else if (request->max_bytes() > 0 && start_lsn < begin_lsn) {
        ret = OB_ERR_OUT_OF_LOWER_BOUND;
        LOG_ERROR("standby requested logs older than the retained source history",
            K(ret), K(start_lsn), K(begin_lsn));
      } else if (request->max_bytes() > 0 && start_lsn > end_lsn) {
        ret = OB_ERR_OUT_OF_UPPER_BOUND;
        LOG_ERROR("standby requested logs beyond the source end",
            K(ret), K(start_lsn), K(end_lsn));
      }
      if (OB_SUCC(ret) && request->max_bytes() > 0 && start_lsn < end_lsn
          && OB_FAIL(log_handler->seek(start_lsn, iterator))) {
        LOG_ERROR("source log range contains an unreadable gap",
            K(ret), K(start_lsn), K(begin_lsn), K(end_lsn));
      }
      while (OB_SUCC(ret) && iterator.is_inited()
             && sent_bytes < static_cast<int64_t>(request->max_bytes())) {
        const char *buf = nullptr;
        int64_t size = 0;
        share::SCN log_scn;
        palf::LSN log_lsn;
        palf::LogGroupEntry group_entry;
        if (context->IsCancelled()) {
          ret = OB_CANCELED;
        } else if (OB_FAIL(iterator.next())) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          }
          break;
        } else if (OB_FAIL(iterator.get_entry(buf, group_entry, log_lsn))) {
          LOG_WARN("failed to read source log group", K(ret));
        } else if (FALSE_IT(size = group_entry.get_group_entry_size())) {
        } else if (FALSE_IT(log_scn = group_entry.get_scn())) {
        } else {
          standbyservice::FetchLogRes response;
          response.set_buf(buf, size);
          response.set_size(size);
          response.set_source_lsn(log_lsn.val_);
          response.set_end_scn(log_scn.get_val_for_logservice());
          if (!writer->Write(response)) {
            ret = OB_CANCELED;
          } else {
            sent_bytes += size;
          }
        }
      }
      if (OB_SUCC(ret)) {
        share::SCN source_end_scn;
        standbyservice::FetchLogRes response;
        if (OB_FAIL(ls->get_end_scn(source_end_scn))) {
          LOG_WARN("failed to get source log end scn", K(ret));
        } else {
          response.set_size(0);
          response.set_end_scn(source_end_scn.get_val_for_logservice());
          if (!writer->Write(response)) {
            ret = OB_CANCELED;
          }
        }
      }
    }
  }
  return obgrpc::ob_error_to_grpc_status(ret);
}

int ObStandbyGrpcClient::init_tablet_sstable_info_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    const obcall::ObCopyTabletsSSTableInfoArg &arg,
    common::ObIAllocator &allocator,
    restore::ObRestoreHelperSSTableInfoCtx &sstable_info_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStandbyGrpcClient *grpc_client = nullptr;

  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStandbyGrpcClient)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc grpc client", K(ret));
  } else {
    void *ctx_buf = nullptr;
    grpc_client = new (buf) ObStandbyGrpcClient();
    if (OB_FAIL(grpc_client->init(src_addr, timeout, rpc_tls_enabled))) {
      LOG_WARN("failed to init standby grpc client", K(ret), K(src_addr), K(timeout));
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
      grpc_client->~ObStandbyGrpcClient();
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

int ObStandbyGrpcClient::init_sstable_macro_info_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    const obcall::ObCopySSTableMacroRangeInfoArg &arg,
    common::ObIAllocator &allocator,
    restore::ObRestoreHelperSSTableMacroRangeCtx &macro_range_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStandbyGrpcClient *grpc_client = nullptr;

  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStandbyGrpcClient)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc grpc client", K(ret));
  } else {
    void *ctx_buf = nullptr;
    grpc_client = new (buf) ObStandbyGrpcClient();
    if (OB_FAIL(grpc_client->init(src_addr, timeout, rpc_tls_enabled))) {
      LOG_WARN("failed to init standby grpc client", K(ret), K(src_addr), K(timeout));
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
      grpc_client->~ObStandbyGrpcClient();
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

int ObStandbyGrpcClient::init_macro_block_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    const obcall::ObCopyMacroBlockRangeArg &arg,
    common::ObIAllocator &allocator,
    restore::ObRestoreHelperMacroBlockCtx &macro_block_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStandbyGrpcClient *grpc_client = nullptr;

  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStandbyGrpcClient)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc grpc client", K(ret));
  } else {
    void *ctx_buf = nullptr;
    grpc_client = new (buf) ObStandbyGrpcClient();
    if (OB_FAIL(grpc_client->init(src_addr, timeout, rpc_tls_enabled))) {
      LOG_WARN("failed to init standby grpc client", K(ret), K(src_addr), K(timeout));
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
      grpc_client->~ObStandbyGrpcClient();
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

int ObStandbyGrpcClient::init_tablet_info_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    const obcall::ObCopyTabletInfoArg &arg,
    common::ObIAllocator &allocator,
    restore::ObRestoreHelperTabletInfoCtx &tablet_info_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStandbyGrpcClient *grpc_client = nullptr;

  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStandbyGrpcClient)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc grpc client", K(ret));
  } else {
    void *ctx_buf = nullptr;
    grpc_client = new (buf) ObStandbyGrpcClient();
    if (OB_FAIL(grpc_client->init(src_addr, timeout, rpc_tls_enabled))) {
      LOG_WARN("failed to init standby grpc client", K(ret), K(src_addr), K(timeout));
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
      grpc_client->~ObStandbyGrpcClient();
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

// ==================== ObStandbyGrpcClient ====================
ObStandbyGrpcClient::ObStandbyGrpcClient()
  : is_inited_(false),
    grpc_client_()
{
}

ObStandbyGrpcClient::~ObStandbyGrpcClient()
{
}

int ObStandbyGrpcClient::init(
    const common::ObAddr& addr,
    int64_t timeout,
    bool rpc_tls_enabled)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObStandbyGrpcClient already inited", K(ret));
  } else if (!addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid addr", K(ret), K(addr));
  } else {
    ret = grpc_client_.init(addr, timeout, rpc_tls_enabled);
    if (OB_SUCC(ret)) {
      is_inited_ = true;
      LOG_INFO("ObStandbyGrpcClient init success", K(addr), K(timeout));
    } else {
      LOG_WARN("failed to init grpc client", K(ret), K(addr));
    }
  }

  return ret;
}

int ObStandbyGrpcClient::get_ls_view_tablet_count(ObStandbyLSViewTabletCountResult& result)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStandbyGrpcClient not inited", K(ret));
  } else {
    GetLSViewTabletCountReq req;
    GetLSViewTabletCountRes res;
    grpc::ClientContext context;
    grpc_client_.ctx_.set_grpc_context(context);
    auto status = grpc_client_.stub_->get_ls_view_tablet_count(&context, req, &res);
    if (OB_FAIL(grpc_client_.translate_error(status))) {
      LOG_WARN("failed to call get_ls_view_tablet_count RPC", K(ret));
    } else if (OB_FAIL(deserialize_proto_to_ob(res, result))) {
      LOG_WARN("failed to deserialize ObStandbyLSViewTabletCountResult", K(ret));
    }
  }

  return ret;
}

int ObStandbyGrpcClient::check_restore_precondition(
    obcall::ObCheckRestorePreconditionResult& result)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStandbyGrpcClient not inited", K(ret));
  } else {
    standbyservice::CheckRestorePreconditionReq req;
    standbyservice::CheckRestorePreconditionRes res;
    grpc::ClientContext context;
    grpc_client_.ctx_.set_grpc_context(context);
    auto status = grpc_client_.stub_->check_restore_precondition(&context, req, &res);
    if (OB_FAIL(grpc_client_.translate_error(status))) {
      LOG_WARN("failed to call check_restore_precondition RPC", K(ret));
    } else if (OB_FAIL(deserialize_proto_to_ob(res, result))) {
      LOG_WARN("failed to deserialize ObCheckRestorePreconditionResult", K(ret));
    }
  }

  return ret;
}

int ObStandbyGrpcClient::fetch_standby_palf_base_info(
    const standby::ObFetchStandbyPalfBaseInfoArg &arg,
    standby::ObFetchStandbyPalfBaseInfoResult &result)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStandbyGrpcClient not inited", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch standby palf base info arg", K(ret), K(arg));
  } else {
    standbyservice::FetchStandbyPalfBaseInfoReq req;
    standbyservice::FetchStandbyPalfBaseInfoRes res;
    grpc::ClientContext context;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObFetchStandbyPalfBaseInfoArg", K(ret), K(arg));
    } else {
      grpc_client_.ctx_.set_grpc_context(context);
      auto status = grpc_client_.stub_->fetch_standby_palf_base_info(&context, req, &res);
      if (OB_FAIL(grpc_client_.translate_error(status))) {
        LOG_WARN("failed to call fetch_standby_palf_base_info RPC", K(ret), K(arg));
      } else if (OB_FAIL(deserialize_proto_to_ob(res, result))) {
        LOG_WARN("failed to deserialize ObFetchStandbyPalfBaseInfoResult", K(ret), K(arg));
      } else if (!result.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid fetch standby palf base info result", K(ret), K(arg), K(result));
      } else {
        LOG_INFO("fetch standby palf base info completed", K(arg), K(result));
      }
    }
  }

  return ret;
}

int ObStandbyGrpcClient::fetch_log(
    const palf::LSN &start_lsn,
    const int64_t max_bytes,
    const std::function<int(const char *, int64_t, const palf::LSN &,
                            const share::SCN &)> &consume_log)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!start_lsn.is_valid() || max_bytes <= 0 || !consume_log) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fetch log argument", K(ret), K(start_lsn), K(max_bytes));
  } else {
    standbyservice::FetchLogReq request;
    standbyservice::FetchLogRes response;
    grpc::ClientContext context;
    request.set_start_lsn(start_lsn.val_);
    request.set_max_bytes(max_bytes);
    grpc_client_.ctx_.set_grpc_context(context);
    std::unique_ptr<grpc::ClientReader<standbyservice::FetchLogRes>> reader(
        grpc_client_.stub_->fetch_log(&context, request));
    if (OB_ISNULL(reader)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      while (OB_SUCC(ret) && reader->Read(&response)) {
        share::SCN log_scn;
        const palf::LSN source_lsn(response.source_lsn());
        if (response.size() == 0) {
          // The terminal frame carries the source stream boundary.
        } else if (!source_lsn.is_valid()
                   || response.size() != static_cast<uint64_t>(response.buf().size())) {
          ret = OB_INVALID_DATA;
          LOG_WARN("invalid fetch log response", K(ret), "size", response.size(),
              "buf_size", response.buf().size(), K(source_lsn));
        } else if (OB_FAIL(log_scn.convert_for_logservice(response.end_scn()))) {
          LOG_WARN("invalid source log scn", K(ret), "source_scn", response.end_scn());
        } else if (OB_FAIL(consume_log(
            response.buf().data(), static_cast<int64_t>(response.size()), source_lsn, log_scn))) {
          LOG_WARN("failed to consume source log group", K(ret), K(source_lsn),
              K(log_scn), "size", response.size());
        }
      }
      if (OB_FAIL(ret)) {
        context.TryCancel();
      }
      const grpc::Status status = reader->Finish();
      if (OB_SUCC(ret) && OB_FAIL(grpc_client_.translate_error(status))) {
        LOG_WARN("fetch log stream failed", K(ret));
      }
    }
  }
  return ret;
}

int ObStandbyGrpcClient::get_log_end_scn(share::SCN &end_scn)
{
  int ret = OB_SUCCESS;
  end_scn.reset();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    standbyservice::FetchLogReq request;
    standbyservice::FetchLogRes response;
    grpc::ClientContext context;
    request.set_start_lsn(0);
    request.set_max_bytes(0);
    grpc_client_.ctx_.set_grpc_context(context);
    std::unique_ptr<grpc::ClientReader<standbyservice::FetchLogRes>> reader(
        grpc_client_.stub_->fetch_log(&context, request));
    if (OB_ISNULL(reader)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!reader->Read(&response) || response.size() != 0) {
      ret = OB_INVALID_DATA;
      LOG_WARN("missing source log boundary frame", K(ret));
    } else if (OB_FAIL(end_scn.convert_for_logservice(response.end_scn()))) {
      LOG_WARN("invalid source log end scn", K(ret), "end_scn", response.end_scn());
    }
    const grpc::Status status = reader->Finish();
    if (OB_SUCC(ret) && OB_FAIL(grpc_client_.translate_error(status))) {
      LOG_WARN("get source log end scn failed", K(ret));
    }
  }
  return ret;
}

int ObStandbyGrpcClient::fetch_tablet_info(
    const obcall::ObCopyTabletInfoArg& arg,
    std::function<int(const obcall::ObCopyTabletInfo&)> callback)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStandbyGrpcClient not inited", K(ret));
  } else {
    FetchTabletInfoReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyTabletInfoArg", K(ret));
    } else {
      grpc::ClientContext context;
      grpc_client_.ctx_.set_grpc_context(context);
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

int ObStandbyGrpcClient::create_tablet_info_stream(
    const obcall::ObCopyTabletInfoArg &arg,
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<standbyservice::FetchTabletInfoRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStandbyGrpcClient not inited", K(ret));
  } else {
    FetchTabletInfoReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyTabletInfoArg", K(ret));
    } else {
      grpc_client_.ctx_.set_grpc_context(context);
      reader = grpc_client_.stub_->fetch_tablet_info(&context, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create tablet info stream reader", K(ret));
      }
    }
  }

  return ret;
}

int ObStandbyGrpcClient::create_ls_view_stream(
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<standbyservice::FetchLSViewRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStandbyGrpcClient not inited", K(ret));
  } else {
    FetchLSViewReq req;
    grpc_client_.ctx_.set_grpc_context(context);
    reader = grpc_client_.stub_->fetch_ls_view(&context, req);
    if (OB_ISNULL(reader.get())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to create ls view stream reader", K(ret));
    }
  }
  return ret;
}

int ObStandbyGrpcClient::create_tablet_sstable_info_stream(
    const obcall::ObCopyTabletsSSTableInfoArg &arg,
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<standbyservice::FetchTabletSSTableInfoRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStandbyGrpcClient not inited", K(ret));
  } else {
    FetchTabletSSTableInfoReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyTabletsSSTableInfoArg", K(ret));
    } else {
      grpc_client_.ctx_.set_grpc_context(context);
      reader = grpc_client_.stub_->fetch_tablet_sstable_info(&context, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create tablet sstable info stream reader", K(ret));
      }
    }
  }

  return ret;
}

int ObStandbyGrpcClient::create_sstable_macro_info_stream(
    const obcall::ObCopySSTableMacroRangeInfoArg &arg,
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<standbyservice::FetchSSTableMacroInfoRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStandbyGrpcClient not inited", K(ret));
  } else {
    FetchSSTableMacroInfoReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopySSTableMacroRangeInfoArg", K(ret));
    } else {
      grpc_client_.ctx_.set_grpc_context(context);
      reader = grpc_client_.stub_->fetch_sstable_macro_info(&context, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create sstable macro info stream reader", K(ret));
      }
    }
  }

  return ret;
}

int ObStandbyGrpcClient::create_macro_block_stream(
    const obcall::ObCopyMacroBlockRangeArg &arg,
    grpc::ClientContext &context,
    std::unique_ptr<grpc::ClientReader<standbyservice::FetchMacroBlockRes>> &reader)
{
  int ret = OB_SUCCESS;
  reader.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStandbyGrpcClient not inited", K(ret));
  } else {
    FetchMacroBlockReq req;
    if (OB_FAIL(serialize_ob_to_proto(arg, &req))) {
      LOG_WARN("failed to serialize ObCopyMacroBlockRangeArg", K(ret));
    } else {
      grpc_client_.ctx_.set_grpc_context(context);
      reader = grpc_client_.stub_->fetch_macro_block(&context, req);
      if (OB_ISNULL(reader.get())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create macro block stream reader", K(ret));
      }
    }
  }

  return ret;
}

int ObStandbyGrpcClient::translate_error(const grpc::Status &status)
{
  return grpc_client_.translate_error(status);
}

int ObStandbyGrpcClient::init_ls_view_stream(
    const common::ObAddr &src_addr,
    int64_t timeout,
    bool rpc_tls_enabled,
    common::ObIAllocator &allocator,
    ObLSMeta &ls_meta,
    share::SCN &physical_checkpoint_scn,
    restore::ObRestoreHelperLSViewCtx &ls_view_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObStandbyGrpcClient *grpc_client = nullptr;

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStandbyGrpcClient)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc grpc client", K(ret));
    } else {
      grpc_client = new (buf) ObStandbyGrpcClient();
      if (OB_FAIL(grpc_client->init(src_addr, timeout, rpc_tls_enabled))) {
        LOG_WARN("failed to init standby grpc client", K(ret), K(src_addr), K(timeout));
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
            } else if (standbyservice::LS_META_PACKAGE != first_res.entry_type()) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("first fetch_ls_view response is not ls meta package", K(ret),
                  "entry_type", first_res.entry_type());
            } else {
              ObStandbyLSViewMeta ls_view_meta;
              if (OB_FAIL(deserialize_proto_to_ob(first_res, ls_view_meta))) {
                LOG_WARN("failed to deserialize standby ls view meta", K(ret));
              } else if (!ls_view_meta.is_valid()) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("invalid standby ls view meta", K(ret), K(ls_view_meta));
              } else {
                ls_meta = ls_view_meta.ls_meta_;
                physical_checkpoint_scn = ls_view_meta.physical_checkpoint_scn_;
              }
            }
            if (OB_SUCC(ret)) {
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
      grpc_client->~ObStandbyGrpcClient();
      allocator.free(grpc_client);
      grpc_client = nullptr;
    }
    ls_view_ctx.ls_meta_fetched_ = false;
    ls_view_ctx.grpc_client_ = nullptr;
    ls_view_ctx.ls_view_reader_.reset();
  }

  return ret;
}

} // namespace standby
} // oceanbase
