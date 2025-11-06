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

#pragma once

#include "lib/allocator/page_arena.h"
#include "lib/profile/ob_trace_id.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace observer
{
class ObITableLoadTaskProcessor;
class ObITableLoadTaskCallback;

class ObTableLoadTask
{
  friend class ObITableLoadTaskProcessor;
  friend class ObITableLoadTaskCallback;
public:
  ObTableLoadTask(uint64_t tenant_id);
  ~ObTableLoadTask();
  template<typename Processor, typename... Args>
  int set_processor(Args&&... args);
  template<typename Callback, typename... Args>
  int set_callback(Args&&... args);
  const common::ObCurTraceId::TraceId get_trace_id() const { return trace_id_; }
  ObITableLoadTaskProcessor *get_processor() const { return processor_; }
  ObITableLoadTaskCallback *get_callback() const { return callback_; }
  bool is_valid() const { return nullptr != processor_ && nullptr != callback_; }
  int do_work();
  void callback(int ret_code);
  TO_STRING_KV(KPC_(processor), KP_(callback));
protected:
  common::ObCurTraceId::TraceId trace_id_;
  common::ObArenaAllocator allocator_;
  ObITableLoadTaskProcessor *processor_;
  ObITableLoadTaskCallback *callback_;
private:
  DISABLE_COPY_ASSIGN(ObTableLoadTask);
};

template<typename Processor, typename... Args>
int ObTableLoadTask::set_processor(Args&&... args)
{
  int ret = common::OB_SUCCESS;
  if (OB_NOT_NULL(processor_)) {
    ret = common::OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "unexpected alloc processor", KR(ret));
  } else {
    if (OB_ISNULL(processor_ = OB_NEWx(Processor, (&allocator_), *this, args...))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      SERVER_LOG(WARN, "fail to new processor", KR(ret));
    }
  }
  return ret;
}

template<typename Callback, typename... Args>
int ObTableLoadTask::set_callback(Args&&... args)
{
  int ret = common::OB_SUCCESS;
  if (OB_NOT_NULL(callback_)) {
    ret = common::OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "unexpected alloc callback", KR(ret));
  } else {
    if (OB_ISNULL(callback_ = OB_NEWx(Callback, (&allocator_), args...))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      SERVER_LOG(WARN, "fail to new callback", KR(ret));
    }
  }
  return ret;
}

class ObITableLoadTaskProcessor
{
public:
  ObITableLoadTaskProcessor(ObTableLoadTask &task)
    : parent_(task), allocator_(task.allocator_) {}
  virtual ~ObITableLoadTaskProcessor() = default;
  virtual int process() = 0;
  VIRTUAL_TO_STRING_KV(KP(this));
protected:
  ObTableLoadTask &parent_;
  common::ObIAllocator &allocator_;
private:
  DISABLE_COPY_ASSIGN(ObITableLoadTaskProcessor);
};

class ObITableLoadTaskCallback
{
public:
  ObITableLoadTaskCallback() = default;
  virtual ~ObITableLoadTaskCallback() = default;
  virtual void callback(int ret_code, ObTableLoadTask *task) = 0;
private:
  DISABLE_COPY_ASSIGN(ObITableLoadTaskCallback);
};

}  // namespace observer
}  // namespace oceanbase
