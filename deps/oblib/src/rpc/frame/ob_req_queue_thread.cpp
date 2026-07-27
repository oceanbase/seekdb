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

#include "rpc/frame/ob_req_queue_thread.h"

#include "lib/oblog/ob_warning_buffer.h"
#include "rpc/ob_request.h"

using namespace oceanbase::rpc::frame;
using namespace oceanbase::common;
using namespace oceanbase::lib;

ObReqQueue::ObReqQueue(int capacity)
    : queue_(),
      qhandler_(NULL),
      host_()
{
  queue_.set_limit(capacity);
}

ObReqQueue::~ObReqQueue()
{
  LOG_INFO("begin to destroy queue", K(queue_.size()));
}

void ObReqQueue::set_qhandler(ObiReqQHandler *qhandler)
{
  if (OB_ISNULL(qhandler)) {
    LOG_ERROR_RET(common::OB_INVALID_ARGUMENT, "invalid argument", K(qhandler));
  }
  qhandler_ = qhandler;
}

bool ObReqQueue::push(ObRequest *req, int max_queue_len, bool block)
{
  bool bret = true;
  if (max_queue_len > 0 && queue_.size() >= max_queue_len) {
    if (!block) {
      bret =  false;
    }
  }

  if (!OB_ISNULL(req)) {
    req->set_enqueue_timestamp(ObTimeUtility::current_time());
  }

  if (bret) {
    bret = OB_LIKELY(OB_SUCCESS == queue_.push(req, 0));
  }
  return bret;
}



int ObReqQueue::process_task(ObLink *task)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(task) || OB_ISNULL(qhandler_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("queue pop NULL task", K(task), K(ret), K(qhandler_));
  } else {
    lib::ContextParam param;
    param.set_mem_attr(ObModIds::OB_ROOT_CONTEXT)
      .set_properties(USE_TL_PAGE_OPTIONAL);
    CREATE_WITH_TEMP_CONTEXT(param) {
      ObRequest *req = static_cast<ObRequest *>(task);

      ObCurTraceId::init(host_);

      // setup and init warning buffer
      ob_setup_default_tsi_warning_buffer();
      ob_reset_tsi_warning_buffer();
      qhandler_->handle_request(req);
      ObCurTraceId::reset();
      ObThreadLogLevelUtils::clear();
    }
  }

  return ret;
}

void ObReqQueue::loop()
{
  int ret = OB_SUCCESS;
  int64_t timeout = 3000 * 1000;
  ObLink *task = NULL;
  if (OB_ISNULL(qhandler_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(qhandler_));
  } else if (OB_FAIL(qhandler_->onThreadCreated(nullptr))) {
    LOG_ERROR("do thread created fail, thread will exit", K(ret));
  } else {
    // The main loop threads process tasks.
    while (!Thread::current().has_set_stop()) {
      if (OB_FAIL(queue_.pop(task, timeout))) {
        LOG_DEBUG("queue pop task fail", K(&queue_));
      } else if (NULL != task) {
        process_task(task);  // ignore return code.
      } else {
        // unexpected
        LOG_ERROR("queue pop successfully but task is NULL");
      }
    }  // main loop

    LOG_INFO("exiting queue thread and wait remain finish", K(queue_.size()));
    // Process remains if we should wait until all task has been
    // processed before exiting this thread. Previous return code
    // isn't significant, we just ignore it to make progress. When
    // queue pop a normal task we process it until pop fails.
    ret = OB_SUCCESS;
    while (queue_.size() > 0 && OB_SUCC(ret)) {
      if (OB_FAIL(queue_.pop(task, timeout))) {
        LOG_DEBUG("queue pop task fail", K(&queue_));
        if(OB_ENTRY_NOT_EXIST == ret) {
          // lightyqueue may return OB_ENTRY_NOT_EXIST when tasks existing
          ret = OB_SUCCESS;
        }
      } else if (NULL != task) {
        process_task(task);  // ignore return code.
      } else {
        // unexpected
        LOG_ERROR("queue pop successfully but task is NULL");
      }
    }

    // No matter error occurred before or not.
    if (OB_FAIL(qhandler_->onThreadDestroy(nullptr))) {
      OB_LOG(ERROR, "handle thread destroy fail", K(ret));
    }
  }
}
