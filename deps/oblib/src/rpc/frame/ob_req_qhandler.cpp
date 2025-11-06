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

#define USING_LOG_PREFIX RPC_FRAME

#include "rpc/frame/ob_req_qhandler.h"

#include "rpc/frame/ob_req_translator.h"
#include "rpc/frame/ob_req_processor.h"

using namespace oceanbase::rpc;
using namespace oceanbase::obrpc;
using namespace oceanbase::rpc::frame;
using namespace oceanbase::common;

ObReqQHandler::ObReqQHandler(ObReqTranslator &translator)
    : translator_(translator)
{
  // empty
}

ObReqQHandler::~ObReqQHandler()
{
  // empty
}

int ObReqQHandler::init()
{
  return translator_.init();
}

int ObReqQHandler::onThreadCreated(obsys::CThread *th)
{
  UNUSED(th);
  LOG_INFO("new task thread create", K(&translator_));
  return translator_.th_init();
}

int ObReqQHandler::onThreadDestroy(obsys::CThread *th)
{
  UNUSED(th);
  return translator_.th_destroy();
}

bool ObReqQHandler::handlePacketQueue(ObRequest *req, void */* arg */)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(req), K(ret));
  }

  ObReqProcessor *processor = NULL;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(translator_.translate(*req, processor))) {
      LOG_WARN("translate reqeust fail", K(*req), K(ret));
      processor = NULL;
    }
  }

  // We just test processor is created correctly, but ignore the
  // returning code before.
  if (NULL == processor) {
    if (NULL != req) {
      on_translate_fail(req, ret);
    }
  } else {
    req->set_trace_point(ObRequest::OB_EASY_REQUEST_QHANDLER_PROCESSOR_RUN);
    if (OB_FAIL(processor->run())) {
      LOG_WARN("process rpc fail", K(ret));
    }
    translator_.release(processor);
  }

  return OB_SUCC(ret);
}
