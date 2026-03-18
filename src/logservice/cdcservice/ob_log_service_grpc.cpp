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

#define USING_LOG_PREFIX EXTLOG

#include "logservice/cdcservice/ob_log_service_grpc.h"
#include "logservice/ob_log_service.h"
#include "logservice/cdcservice/ob_cdc_service.h"
#include "logservice/cdcservice/ob_cdc_req.h"
#include "logservice/cdcservice/ob_cdc_raw_log_req.h"
#include "grpc/ob_grpc_context.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/time/ob_time_utility.h"
#include "share/rc/ob_tenant_base.h"

using namespace oceanbase::common;
using namespace oceanbase::obrpc;
using namespace oceanbase::obgrpc;
using namespace oceanbase::logservice;

namespace oceanbase
{
namespace cdc
{

static int __get_cdc_service(uint64_t tenant_id, ObCdcService *&cdc_service)
{
  int ret = OB_SUCCESS;
  logservice::ObLogService *log_service = nullptr;

  if (OB_ISNULL(log_service = MTL(logservice::ObLogService*))) {
    ret = OB_ERR_UNEXPECTED;
    EXTLOG_LOG(WARN, "get_log_service failed", KR(ret));
  } else if (OB_ISNULL(cdc_service = log_service->get_cdc_service())) {
    ret = OB_ERR_UNEXPECTED;
    EXTLOG_LOG(WARN, "cdc_service is NULL", KR(ret), K(tenant_id));
  } else {
    // get CdcService succ
  }

  return ret;
}

::grpc::Status ObLogServiceGrpcServiceImpl::fetch_log(
    ::grpc::ServerContext* context,
    const ::logservice::FetchLogReq* request,
    ::logservice::FetchLogRes* response)
{
  int ret = OB_SUCCESS;
  ObCdcLSFetchLogReq req;
  auto resp = std::make_unique<ObCdcLSFetchLogResp>();
  ObCdcService *cdc_service = nullptr;
  uint64_t tenant_id = OB_INVALID_TENANT_ID;

  // Deserialize proto request to OB object
  if (OB_FAIL(deserialize_proto_to_ob(*request, req))) {
    EXTLOG_LOG(WARN, "failed to deserialize FetchLogReq", KR(ret));
  } else {
    tenant_id = req.get_tenant_id();
    if (OB_INVALID_TENANT_ID == tenant_id) {
      ret = OB_INVALID_ARGUMENT;
      EXTLOG_LOG(WARN, "invalid tenant_id in request", KR(ret), K(req));
    } else {
      MTL_SWITCH(tenant_id) {
        if (OB_FAIL(__get_cdc_service(tenant_id, cdc_service))) {
          EXTLOG_LOG(ERROR, "__get_cdc_service failed", KR(ret), K(tenant_id));
        } else if (OB_ISNULL(cdc_service)) {
          ret = OB_ERR_UNEXPECTED;
          EXTLOG_LOG(ERROR, "cdc_service is null", KR(ret), K(tenant_id));
        } else {
          int64_t send_timestamp = ObTimeUtility::current_time();
          int64_t receive_timestamp = send_timestamp;
          ret = cdc_service->fetch_log(req, *resp, send_timestamp, receive_timestamp);
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    // Serialize OB response to proto
    if (OB_FAIL(serialize_ob_to_proto(*resp, response))) {
      EXTLOG_LOG(WARN, "failed to serialize ObCdcLSFetchLogResp", KR(ret));
    }
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

::grpc::Status ObLogServiceGrpcServiceImpl::fetch_missing_log(
    ::grpc::ServerContext* context,
    const ::logservice::FetchMissingLogReq* request,
    ::logservice::FetchMissingLogRes* response)
{
  int ret = OB_SUCCESS;
  ObCdcLSFetchMissLogReq req;
  auto resp = std::make_unique<ObCdcLSFetchLogResp>();
  ObCdcService *cdc_service = nullptr;
  uint64_t tenant_id = OB_INVALID_TENANT_ID;

  // Deserialize proto request to OB object
  if (OB_FAIL(deserialize_proto_to_ob(*request, req))) {
    EXTLOG_LOG(WARN, "failed to deserialize FetchMissingLogReq", KR(ret));
  } else {
    tenant_id = req.get_tenant_id();
    if (OB_INVALID_TENANT_ID == tenant_id) {
      ret = OB_INVALID_ARGUMENT;
      EXTLOG_LOG(WARN, "invalid tenant_id in request", KR(ret), K(req));
    } else {
      MTL_SWITCH(tenant_id) {
        if (OB_FAIL(__get_cdc_service(tenant_id, cdc_service))) {
          EXTLOG_LOG(ERROR, "__get_cdc_service failed", KR(ret), K(tenant_id));
        } else if (OB_ISNULL(cdc_service)) {
          ret = OB_ERR_UNEXPECTED;
          EXTLOG_LOG(ERROR, "cdc_service is null", KR(ret), K(tenant_id));
        } else {
          int64_t send_timestamp = ObTimeUtility::current_time();
          int64_t receive_timestamp = send_timestamp;
          ret = cdc_service->fetch_missing_log(req, *resp, send_timestamp, receive_timestamp);
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    // Serialize OB response to proto
    if (OB_FAIL(serialize_ob_to_proto(*resp, response))) {
      EXTLOG_LOG(WARN, "failed to serialize ObCdcLSFetchLogResp", KR(ret));
    }
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

::grpc::Status ObLogServiceGrpcServiceImpl::fetch_raw_log(
    ::grpc::ServerContext* context,
    const ::logservice::FetchRawLogReq* request,
    ::logservice::FetchRawLogRes* response)
{
  int ret = OB_SUCCESS;
  ObCdcFetchRawLogReq req;
  auto resp = std::make_unique<ObCdcFetchRawLogResp>();
  ObCdcService *cdc_service = nullptr;
  uint64_t tenant_id = OB_INVALID_TENANT_ID;

  // Deserialize proto request to OB object
  if (OB_FAIL(deserialize_proto_to_ob(*request, req))) {
    EXTLOG_LOG(WARN, "failed to deserialize FetchRawLogReq", KR(ret));
  } else {
    tenant_id = req.get_tenant_id();
    if (OB_INVALID_TENANT_ID == tenant_id) {
      ret = OB_INVALID_ARGUMENT;
      EXTLOG_LOG(WARN, "invalid tenant_id in request", KR(ret), K(req));
    } else {
      MTL_SWITCH(tenant_id) {
        if (OB_FAIL(__get_cdc_service(tenant_id, cdc_service))) {
          EXTLOG_LOG(ERROR, "__get_cdc_service failed", KR(ret), K(tenant_id));
        } else if (OB_ISNULL(cdc_service)) {
          ret = OB_ERR_UNEXPECTED;
          EXTLOG_LOG(ERROR, "cdc_service is null", KR(ret), K(tenant_id));
        } else {
          int64_t send_timestamp = ObTimeUtility::current_time();
          int64_t receive_timestamp = send_timestamp;
          ret = cdc_service->fetch_raw_log(req, *resp, send_timestamp, receive_timestamp);
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    // Serialize OB response to proto
    if (OB_FAIL(serialize_ob_to_proto(*resp, response))) {
      EXTLOG_LOG(WARN, "failed to serialize ObCdcFetchRawLogResp", KR(ret));
    }
  }

  return obgrpc::ob_error_to_grpc_status(ret);
}

} // namespace cdc
} // namespace oceanbase
