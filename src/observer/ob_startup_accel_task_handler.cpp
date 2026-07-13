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
#include "observer/ob_startup_accel_task_handler.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace observer
{
const int64_t ObStartupAccelTaskHandler::MAX_QUEUED_TASK_NUM = 128;
const int64_t ObStartupAccelTaskHandler::MAX_THREAD_NUM = 64;

ObStartupAccelTaskHandler::ObStartupAccelTaskHandler()
  : is_inited_(false),
    accel_type_(SERVER_ACCEL),
    task_allocator_()
{}

ObStartupAccelTaskHandler::~ObStartupAccelTaskHandler()
{
  destroy();
}

int ObStartupAccelTaskHandler::init(ObStartupAccelType accel_type)
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr = ObMemAttr("StartupTask",
                                 ObCtxIds::DEFAULT_CTX_ID);
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObStartupAccelTaskHandler has already been inited", K(ret));
  } else if (OB_FAIL(task_allocator_.init(lib::ObMallocAllocator::get_instance(),
      OB_MALLOC_NORMAL_BLOCK_SIZE, mem_attr))) {
    LOG_WARN("fail to init tenant tiny allocator", K(ret));
  } else if (FALSE_IT(accel_type_ = accel_type)) {
  } else if (OB_FAIL(common::ObSimpleThreadPool::init(get_thread_cnt(),
                                                      MAX_QUEUED_TASK_NUM,
                                                      "StartupAccel"))) {
    LOG_WARN("fail to init startup accel thread pool", K(ret), K(accel_type), K(get_thread_cnt()));
  } else {
    is_inited_ = true;
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    task_allocator_.reset();
  }
  return ret;
}

int64_t ObStartupAccelTaskHandler::get_thread_cnt()
{
  int64_t thread_cnt = 1;
  if (lib::is_mini_mode()) {
    thread_cnt = 1;
  } else {
    if (SERVER_ACCEL == accel_type_) {
      thread_cnt = common::get_cpu_count();
    } else {
      thread_cnt = MTL_CPU_COUNT();
    }
  }

  return std::min(MAX_THREAD_NUM, thread_cnt);
}

int ObStartupAccelTaskHandler::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStartupAccelTaskHandler not inited", K(ret));
  }
  return ret;
}

void ObStartupAccelTaskHandler::stop()
{
  if (IS_INIT) {
    common::ObSimpleThreadPool::stop();
  }
}

void ObStartupAccelTaskHandler::wait()
{
  if (IS_INIT) {
    common::ObSimpleThreadPool::wait();
  }
}

void ObStartupAccelTaskHandler::destroy()
{
  if (IS_INIT) {
    common::ObSimpleThreadPool::destroy();
    task_allocator_.reset();
    is_inited_ = false;
  }
}

int ObStartupAccelTaskHandler::push_task(ObStartupAccelTask *task)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObStartupAccelTaskHandler not inited", K(ret));
  } else if (OB_FAIL(common::ObSimpleThreadPool::push(task))) {
    LOG_WARN("fail to push startup accel task", K(ret), KPC(task));
  }
  return ret;
}

void ObStartupAccelTaskHandler::handle(void *task)
{
  int ret = OB_SUCCESS;
  if (NULL == task) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task is null", K(ret));
  } else {
    ObStartupAccelTask *startup_task = static_cast<ObStartupAccelTask *>(task);
    if (OB_FAIL(startup_task->execute())) {
      LOG_WARN("fail to execute startup task", K(ret), KPC(startup_task));
    }
    startup_task->~ObStartupAccelTask();
    task_allocator_.free(startup_task);
  }
}

} // observer
} // oceanbase
