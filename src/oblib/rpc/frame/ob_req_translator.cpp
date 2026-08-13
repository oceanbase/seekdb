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

#define USING_LOG_PREFIX RPC
#include "rpc/frame/ob_req_translator.h"

#include "rpc/frame/ob_req_processor.h"

using namespace oceanbase::common;
using namespace oceanbase::rpc;
using namespace oceanbase::rpc::frame;

int ObReqTranslator::init()
{
  return OB_SUCCESS;
}

int ObReqTranslator::th_init()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObReqTranslator::th_destroy()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObReqTranslator::translate(ObRequest &req, ObReqProcessor *&processor)
{
  int ret = OB_SUCCESS;
  processor = get_processor(req);
  if (NULL == processor) {
    RPC_LOG(WARN, "can't translate packet", K(req), K(ret));
    ret = OB_NOT_SUPPORTED;
  } else {
    //processor->reuse();
    processor->set_ob_request(req);
  }
  return ret;
}

int ObReqTranslator::release(ObReqProcessor *processor)
{
  int ret = OB_SUCCESS;
  UNUSED(processor);
  return ret;
}
