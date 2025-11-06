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

#include "log_rpc_processor.h"
#include "logservice/ob_log_service.h"

namespace oceanbase
{
namespace palf
{

int __get_palf_env_impl(uint64_t tenant_id, IPalfEnvImpl *&palf_env_impl, const bool need_check_tenant_id)
{
  int ret = OB_SUCCESS;
  logservice::ObLogService *log_service = nullptr;
  PalfEnv *palf_env = nullptr;
   if (OB_ISNULL(log_service = MTL(logservice::ObLogService*))) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "get_log_service failed", K(ret));
	} else if (OB_ISNULL(palf_env = log_service->get_palf_env())) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "get_palf_env failed", K(ret));
  } else if (OB_ISNULL(palf_env_impl = palf_env->get_palf_env_impl())) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "get_palf_env_impl failed", K(ret), KP(log_service), KP(palf_env_impl));
  } else if (need_check_tenant_id && tenant_id != palf_env_impl->get_tenant_id()) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(ERROR, "tenant_id is not same as palf_env", K(ret), K(tenant_id), "tenant_id_in_palf", palf_env_impl->get_tenant_id());
  } else {
    // do nothing
  }
	return ret;
}

} // end namespace palf
} // end namespace oceanbase
