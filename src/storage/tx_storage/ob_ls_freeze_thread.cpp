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

#define USING_LOG_PREFIX STORAGE

#include "storage/tx_storage/ob_ls_freeze_thread.h"
#include "storage/checkpoint/ob_data_checkpoint.h"

namespace oceanbase
{
namespace storage
{

using namespace checkpoint;
using namespace share;

void ObLSFreezeTask::set_task(ObLSFreezeThread *host,
                              ObDataCheckpoint *data_checkpoint,
                              SCN rec_scn)
{
  host_ = host;
  rec_scn_ = rec_scn;
  data_checkpoint_ = data_checkpoint;
}

void ObLSFreezeTask::handle()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(data_checkpoint_)) {
    data_checkpoint_->road_to_flush(rec_scn_);
  }
  if (OB_NOT_NULL(host_)) {
    if (OB_FAIL(host_->push_back_(this))) {
      STORAGE_LOG(WARN, "push back ls free task failed", K(ret));
    }
  }
}

ObLSFreezeThread::ObLSFreezeThread()
    : inited_(false), available_index_(-1), lock_(common::ObLatchIds::THREAD_POOL_LOCK)
{
  for (int64_t i = 0; i < MAX_FREE_TASK_NUM; i++) {
    task_array_[i] = NULL;
  }
}

ObLSFreezeThread::~ObLSFreezeThread()
{
  destroy();
}

void ObLSFreezeThread::destroy()
{
  if (inited_) {
    common::ObSimpleThreadPool::stop();
    common::ObSimpleThreadPool::wait();
    common::ObSimpleThreadPool::destroy();

    while (available_index_ >= 0) {
      task_array_[available_index_]->~ObLSFreezeTask();
      ob_free(task_array_[available_index_]);
      task_array_[available_index_] = NULL;
      available_index_--;
    }

    inited_ = false;
    STORAGE_LOG(INFO, "ls freeze thread destroy", KP(this));
  }
}

int ObLSFreezeThread::init()
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObLSFreezeThread has already been inited", K(ret));
  } else if (OB_FAIL(common::ObSimpleThreadPool::init(get_thread_num_(),
                                                      MAX_FREE_TASK_NUM,
                                                      "LSFreeze"))) {
    STORAGE_LOG(WARN, "ObSimpleThreadPool inited error.", K(ret));
  } else {
    inited_ = true;
    ObMemAttr memattr("FreezeTask");
    for (int64_t i = 0; OB_SUCC(ret) && i < MAX_FREE_TASK_NUM; i++) {
      ObLSFreezeTask *ptr
        = (ObLSFreezeTask *)ob_malloc(sizeof(ObLSFreezeTask), memattr);
      if (NULL == ptr) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        new (ptr) ObLSFreezeTask();
        task_array_[i] = ptr;
        available_index_ = i;
      }
    }
  }
  if (OB_FAIL(ret)) {
    destroy();
  }
  STORAGE_LOG(INFO, "ObLSFreezeThread init finished", K(ret));
  return ret;
}

void ObLSFreezeThread::stop()
{
  if (inited_) {
    common::ObSimpleThreadPool::stop();
  }
}

void ObLSFreezeThread::wait()
{
  if (inited_) {
    common::ObSimpleThreadPool::wait();
  }
}

int ObLSFreezeThread::add_task(ObDataCheckpoint *data_checkpoint,
                               SCN rec_scn)
{
  int ret = OB_SUCCESS;
  ObLSFreezeTask *task = NULL;
  {
    ObSpinLockGuard guard(lock_);
    if (available_index_ >= 0) {
      task = task_array_[available_index_];
      task_array_[available_index_] = NULL;
      available_index_--;
    } else {
      ret = OB_EAGAIN;
    }
  }
  if (OB_SUCC(ret)) {
    task->set_task(this, data_checkpoint, rec_scn);
    if (OB_FAIL(common::ObSimpleThreadPool::push(task))) {
      STORAGE_LOG(WARN, "schedule timer task failed", K(ret));
    }
  }
  return ret;
}

void ObLSFreezeThread::handle(void *task)
{
  if (NULL == task) {
    STORAGE_LOG_RET(WARN, OB_ERR_UNEXPECTED, "task is null", KP(task));
  } else {
    ObLSFreezeTask *freeze_task = static_cast<ObLSFreezeTask *>(task);
    freeze_task->handle();
  }
}

int ObLSFreezeThread::push_back_(ObLSFreezeTask *task)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(task)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), KP(task));
  } else {
    ObSpinLockGuard guard(lock_);
    task_array_[++available_index_] = task;
  }
  return ret;
}

int64_t ObLSFreezeThread::get_thread_num_() const
{
  return QUEUE_THREAD_NUM;
}

}  // namespace storage
}  // namespace oceanbase
