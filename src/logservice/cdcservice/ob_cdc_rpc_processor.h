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

#ifndef OCEANBASE_LOGSERVICE_OB_CDC_RPC_PROCESSOR
#define OCEANBASE_LOGSERVICE_OB_CDC_RPC_PROCESSOR

#include "ob_cdc_rpc_proxy.h"
#include "ob_cdc_service.h"
#include "rpc/obrpc/ob_rpc_processor.h"

namespace oceanbase
{
namespace obrpc
{
int __get_cdc_service(uint64_t tenant_id, cdc::ObCdcService *&cdc_service);

class ObCdcLSReqStartLSNByTsP : public
  obrpc::ObRpcProcessor<obrpc::ObCdcProxy::ObRpc<obrpc::OB_LOG_REQ_START_LSN_BY_TS> >
{
public:
  ObCdcLSReqStartLSNByTsP() {}
  ~ObCdcLSReqStartLSNByTsP() {}
protected:
  int process();
};

class ObCdcLSFetchLogP : public
  obrpc::ObRpcProcessor<obrpc::ObCdcProxy::ObRpc<obrpc::OB_LS_FETCH_LOG2> >
{
public:
  ObCdcLSFetchLogP() {}
  ~ObCdcLSFetchLogP() {}
protected:
  int process();
};

class ObCdcLSFetchMissingLogP : public
  obrpc::ObRpcProcessor<obrpc::ObCdcProxy::ObRpc<obrpc::OB_LS_FETCH_MISSING_LOG> >
{
public:
  ObCdcLSFetchMissingLogP() {}
  ~ObCdcLSFetchMissingLogP() {}
protected:
  int process();
};

class ObCdcFetchRawLogP : public
  obrpc::ObRpcProcessor<obrpc::ObCdcProxy::ObRpc<obrpc::OB_CDC_FETCH_RAW_LOG> >
{
public:
  ObCdcFetchRawLogP() {}
  ~ObCdcFetchRawLogP() {}
protected:
  int process();
};

} // namespace obrpc
} // namespace oceanbase

#endif
