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

#define USING_LOG_PREFIX RS

#include "lib/ob_define.h"
#include "lib/oblog/ob_log_module.h"
#include "rootserver/standby/ob_service_grpc.h"
#include "share/ob_all_tenant_info.h"
#include "share/ob_server_struct.h"
#include "share/ob_rpc_struct.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "logservice/ob_log_handler.h"

using namespace oceanbase::obgrpc;
using namespace oceanbase::share;
using namespace oceanbase::obrpc;
using namespace oceanbase::storage;
using namespace oceanbase::logservice;

namespace oceanbase
{
namespace rootserver
{
namespace standby
{

grpc::Status ObServerServiceImpl::get_tenant_info(
    grpc::ServerContext* context,
    const serverservice::GetTenantInfoReq* request,
    serverservice::GetTenantInfoRes* response)
{
  int ret = OB_SUCCESS;
  UNUSED(context);
  UNUSED(request); // tenant_id is not needed, always use sys tenant

  // Load tenant info from KV storage, always use sys tenant
  ObAllTenantInfo tenant_info;
  if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(false, tenant_info))) {
    LOG_WARN("failed to load tenant info", KR(ret));
  }

  // Serialize tenant_info to response
  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialize_ob_to_proto(tenant_info, response))) {
      LOG_WARN("failed to serialize tenant_info", KR(ret), K(tenant_info));
    }
  }

  return ob_error_to_grpc_status(ret);
}

grpc::Status ObServerServiceImpl::get_max_log_info(
    grpc::ServerContext* context,
    const serverservice::GetMaxLogInfoReq* request,
    serverservice::GetMaxLogInfoRes* response)
{
  int ret = OB_SUCCESS;
  UNUSED(context);
  UNUSED(request); // Always query sys_ls, no parameters needed

  if (OB_ISNULL(response)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid response", KR(ret), KP(response));
  } else {
    // Always query sys_ls
    const share::ObLSID ls_id = share::SYS_LS;

    // gRPC threads don't have tenant context, need to switch to SYS tenant
    MTL_SWITCH(OB_SYS_TENANT_ID) {
      // Get LS and log handler
      ObLSService *ls_service = MTL(ObLSService*);
      ObLSHandle ls_handle;
      ObLS *ls = nullptr;
      ObLogHandler *log_handler = nullptr;

      if (OB_ISNULL(ls_service)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls_service is null", KR(ret));
      } else if (OB_FAIL(ls_service->get_ls(ls_id, ls_handle, ObLSGetMod::RS_MOD))) {
        LOG_WARN("get sys ls failed", KR(ret), K(ls_id));
      } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("sys ls is null", KR(ret), K(ls_id));
      } else if (OB_ISNULL(log_handler = ls->get_log_handler())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("log_handler is null", KR(ret), K(ls_id));
      } else {
        // Get access_mode and end_scn
        int64_t mode_version = palf::INVALID_PROPOSAL_ID;
        palf::AccessMode access_mode = palf::AccessMode::INVALID_ACCESS_MODE;
        share::SCN end_scn;

        if (OB_FAIL(log_handler->get_access_mode(mode_version, access_mode))) {
          LOG_WARN("get_access_mode failed", KR(ret), K(ls_id));
        } else if (OB_FAIL(log_handler->get_end_scn(end_scn))) {
          LOG_WARN("get_end_scn failed", KR(ret), K(ls_id));
        } else {
          // Set protobuf fields directly
          response->set_access_mode(static_cast<int32_t>(access_mode));
          response->set_scn_val(end_scn.get_val_for_logservice());
        }
      }
    }
  }

  return ob_error_to_grpc_status(ret);
}

// ==================== ObServiceGrpcClient ====================

ObServiceGrpcClient::ObServiceGrpcClient()
  : is_inited_(false),
    grpc_client_()
{
}

ObServiceGrpcClient::~ObServiceGrpcClient()
{
}

int ObServiceGrpcClient::init(const common::ObAddr& addr, int64_t timeout)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObServiceGrpcClient already inited", K(ret));
  } else if (!addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid addr", K(ret), K(addr));
  } else {
    ret = grpc_client_.init(addr, timeout);
    if (OB_SUCC(ret)) {
      is_inited_ = true;
      LOG_INFO("ObServiceGrpcClient init success", K(addr), K(timeout));
    } else {
      LOG_WARN("failed to init grpc client", K(ret), K(addr));
    }
  }

  return ret;
}

int ObServiceGrpcClient::get_tenant_info(share::ObAllTenantInfo& tenant_info)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObServiceGrpcClient not inited", K(ret));
  } else {
    serverservice::GetTenantInfoReq req; // Empty request, no serialization needed
    serverservice::GetTenantInfoRes res;

    grpc::ClientContext context;
    GRPC_SET_CONTEXT(grpc_client_, context);
    auto status = grpc_client_.stub_->get_tenant_info(&context, req, &res);
    if (OB_FAIL(grpc_client_.translate_error(status))) {
      LOG_WARN("failed to call get_tenant_info", K(ret));
    } else if (OB_FAIL(deserialize_proto_to_ob(res, tenant_info))) {
      LOG_WARN("failed to deserialize tenant_info", K(ret));
    }
  }

  return ret;
}

int ObServiceGrpcClient::get_max_log_info(palf::AccessMode& mode, share::SCN& scn)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObServiceGrpcClient not inited", K(ret));
  } else {
    serverservice::GetMaxLogInfoReq req; // Empty request, always query sys_ls
    serverservice::GetMaxLogInfoRes res;

    grpc::ClientContext context;
    GRPC_SET_CONTEXT(grpc_client_, context);
    auto status = grpc_client_.stub_->get_max_log_info(&context, req, &res);
    if (OB_FAIL(grpc_client_.translate_error(status))) {
      LOG_WARN("failed to call get_max_log_info", K(ret));
    } else {
      // Read protobuf fields directly
      mode = static_cast<palf::AccessMode>(res.access_mode());
      if (OB_FAIL(scn.convert_for_logservice(res.scn_val()))) {
        LOG_WARN("failed to convert scn from logservice value", K(ret), K(res.scn_val()));
      }
    }
  }

  return ret;
}

} // namespace standby
} // namespace rootserver
} // namespace oceanbase
