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

#define USING_LOG_PREFIX SERVER

#include "ob_table_load_resource_rpc_executor.h"

namespace oceanbase
{
namespace observer
{

// apply_resource
int ObDirectLoadResourceApplyExecutor::check_args()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(arg_));
  } else if (OB_UNLIKELY(arg_.tenant_id_ != MTL_ID())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mtl_id not match", KR(ret), "mtl_id", MTL_ID());
  }

  return ret;
}

int ObDirectLoadResourceApplyExecutor::process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTableLoadResourceService::local_apply_resource(arg_, res_))) {
    LOG_WARN("fail to apply resource", KR(ret));
  }

  return ret;
}

// release_resource
int ObDirectLoadResourceReleaseExecutor::check_args()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(arg_));
  } else if (OB_UNLIKELY(arg_.tenant_id_ != MTL_ID())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mtl_id not match", KR(ret), "mtl_id", MTL_ID());
  }

  return ret;
}

int ObDirectLoadResourceReleaseExecutor::process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTableLoadResourceService::local_release_resource(arg_))) {
    LOG_WARN("fail to release resource", KR(ret));
  }
  
  return ret;
}

// update_resource
int ObDirectLoadResourceUpdateExecutor::check_args()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(arg_));
  } else if (OB_UNLIKELY(arg_.tenant_id_ != MTL_ID())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mtl_id not match", KR(ret), "mtl_id", MTL_ID());
  }

  return ret;
}

int ObDirectLoadResourceUpdateExecutor::process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTableLoadResourceService::local_update_resource(arg_))) {
    LOG_WARN("fail to update resource", KR(ret));
  }
  
  return ret;
}

// check_resource
int ObDirectLoadResourceCheckExecutor::check_args()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(arg_));
  } else if (OB_UNLIKELY(arg_.tenant_id_ != MTL_ID())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mtl_id not match", KR(ret), "mtl_id", MTL_ID());
  }

  return ret;
}

int ObDirectLoadResourceCheckExecutor::process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTableLoadService::refresh_and_check_resource(arg_, res_))) {
    LOG_WARN("fail to refresh_and_check_resource", KR(ret));
  }
  
  return ret;
}

} // namespace observer
} // namespace oceanbase
