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

#include "observer/table_load/ob_table_load_task.h"
#include "src/share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace observer
{
using namespace common;

ObTableLoadTask::ObTableLoadTask(uint64_t tenant_id)
  : trace_id_(*ObCurTraceId::get_trace_id()),
    allocator_("TLD_Task"),
    processor_(nullptr),
    callback_(nullptr)
{
  allocator_.set_tenant_id(MTL_ID());
}

ObTableLoadTask::~ObTableLoadTask()
{
  if (nullptr != processor_) {
    processor_->~ObITableLoadTaskProcessor();
    allocator_.free(processor_);
    processor_ = nullptr;
  }
  if (nullptr != callback_) {
    callback_->~ObITableLoadTaskCallback();
    allocator_.free(callback_);
    callback_ = nullptr;
  }
}

int ObTableLoadTask::do_work()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(processor_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null processor", KR(ret));
  } else {
    ret = processor_->process();
  }
  return ret;
}

void ObTableLoadTask::callback(int ret_code)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(callback_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null callback", KR(ret));
  } else {
    callback_->callback(ret_code, this);
  }
}

}  // namespace observer
}  // namespace oceanbase
