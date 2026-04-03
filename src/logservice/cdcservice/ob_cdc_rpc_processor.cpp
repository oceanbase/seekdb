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

#include "ob_cdc_rpc_processor.h"
#include "logservice/ob_log_service.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace obrpc
{
int __get_cdc_service(uint64_t tenant_id, cdc::ObCdcService *&cdc_service)
{
  int ret = OB_SUCCESS;
  logservice::ObLogService *log_service = nullptr;

  if (tenant_id != MTL_ID()) {
    ret = OB_ERR_UNEXPECTED;
    EXTLOG_LOG(ERROR, "mtl id not match", KR(ret), K(tenant_id), K(MTL_ID()));
  } else if (OB_ISNULL(log_service = MTL(logservice::ObLogService*))) {
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

int ObCdcLSReqStartLSNByTsP::process()
{
  int ret = common::OB_SUCCESS;
  const ObCdcReqStartLSNByTsReq &req = arg_;
  ObCdcReqStartLSNByTsResp &resp = result_;
  cdc::ObCdcService *cdc_service = nullptr;

  if (OB_FAIL(__get_cdc_service(rpc_pkt_->get_tenant_id(), cdc_service))) {
    EXTLOG_LOG(ERROR, "__get_cdc_service failed", KR(ret));
  } else if (OB_ISNULL(cdc_service)) {
    ret = OB_ERR_UNEXPECTED;
    EXTLOG_LOG(ERROR, "cdc_service is null", KR(ret));
  } else {
    ret = cdc_service->req_start_lsn_by_ts_ns(req, resp);
  }

  // rewrite ret for rpc framework
  return OB_SUCCESS;
}

int ObCdcLSFetchLogP::process()
{
  int ret = common::OB_SUCCESS;
  const ObCdcLSFetchLogReq &req = arg_;
  ObCdcLSFetchLogResp &resp = result_;
  cdc::ObCdcService *cdc_service = nullptr;

  if (OB_FAIL(__get_cdc_service(rpc_pkt_->get_tenant_id(), cdc_service))) {
    EXTLOG_LOG(ERROR, "__get_cdc_service failed", KR(ret));
  } else if (OB_ISNULL(cdc_service)) {
    ret = OB_ERR_UNEXPECTED;
    EXTLOG_LOG(ERROR, "cdc_service is null", KR(ret));
  } else {
    set_result_compress_type(req.get_compressor_type());
    ret = cdc_service->fetch_log(req, resp, get_send_timestamp(), get_receive_timestamp());
  }

  // rewrite ret for rpc framework
  return OB_SUCCESS;
}

int ObCdcLSFetchMissingLogP::process()
{
  int ret = common::OB_SUCCESS;
  const ObCdcLSFetchMissLogReq &req = arg_;
  ObCdcLSFetchLogResp &resp = result_;
  cdc::ObCdcService *cdc_service = nullptr;

  if (OB_FAIL(__get_cdc_service(rpc_pkt_->get_tenant_id(), cdc_service))) {
    EXTLOG_LOG(ERROR, "__get_cdc_service failed", KR(ret));
  } else if (OB_ISNULL(cdc_service)) {
    ret = OB_ERR_UNEXPECTED;
    EXTLOG_LOG(ERROR, "cdc_service is null", KR(ret));
  } else {
    set_result_compress_type(req.get_compressor_type());
    ret = cdc_service->fetch_missing_log(req, resp, get_send_timestamp(), get_receive_timestamp());
  }

  // rewrite ret for rpc framework
  return OB_SUCCESS;
}

int ObCdcFetchRawLogP::process()
{
  int ret = OB_SUCCESS;

  const ObCdcFetchRawLogReq &req = arg_;
  ObCdcFetchRawLogResp &resp = result_;
  cdc::ObCdcService *cdc_service = nullptr;
  if (OB_FAIL(__get_cdc_service(rpc_pkt_->get_tenant_id(), cdc_service))) {
    EXTLOG_LOG(ERROR, "__get_cdc_service failed", KR(ret));
  } else if (OB_ISNULL(cdc_service)) {
    ret = OB_ERR_UNEXPECTED;
    EXTLOG_LOG(ERROR, "cdc_service is null", KR(ret));
  } else {
    set_result_compress_type(req.get_compressor_type());
    ret = cdc_service->fetch_raw_log(req, resp, get_send_timestamp(), get_receive_timestamp());
  }
  return OB_SUCCESS;
}

} // namespace obrpc
} // namespace oceanbase
