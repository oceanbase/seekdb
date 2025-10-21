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

#define USING_LOG_PREFIX RPC_OBRPC
#include "rpc/obrpc/ob_rpc_translator.h"


using namespace oceanbase::common;
using namespace oceanbase::rpc;
using namespace oceanbase::obrpc;
using namespace oceanbase::rpc::frame;

int ObRpcTranslator::th_init()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObReqTranslator::th_init())) {
    LOG_WARN("init req translator for thread fail", K(ret));
  } else {
    LOG_INFO("Init thread local success");
  }

  return ret;
}

int ObRpcTranslator::th_destroy()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObReqTranslator::th_destroy())) {
    LOG_ERROR("destroy req translator fail", K(ret));
  }

  return ret;
}
