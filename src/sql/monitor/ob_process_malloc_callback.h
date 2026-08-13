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

#ifndef OCEANBASE_SQL_MONITOR_OB_PROCESS_MALLOC_CALLBACK_H_
#define OCEANBASE_SQL_MONITOR_OB_PROCESS_MALLOC_CALLBACK_H_

#include "lib/alloc/ob_malloc_callback.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/utility/ob_tracepoint.h"

namespace oceanbase
{
namespace sql
{

class ObProcessMallocCallback final : public lib::ObMallocCallback
{
public:
  ObProcessMallocCallback(int64_t cur_used, int64_t &max_used)
    : cur_used_(cur_used), max_used_(max_used)
  {
    max_used_ = cur_used_ > max_used_ ? cur_used_ : max_used_;
  }
  virtual ~ObProcessMallocCallback() {}

  virtual void operator()(const ObMemAttr &attr, int64_t add_size) override
  {
    // Obtain the monitored label from the two tracepoint values.  SqlDtlBuf
    // and memstore allocations are accounted by their owning modules.
    int64_t label_high64 = - EVENT_CODE(EventTable::EN_SQL_MEMORY_LABEL_HIGH64);
    if (OB_UNLIKELY(lib::ObLabel("SqlDtlBuf") == attr.label_
                    || ObCtxIds::MEMSTORE_CTX_ID == attr.ctx_id_)) {
      // do nothing
    } else if (label_high64 != 0) {
      int64_t label_low64 = - EVENT_CODE(EventTable::EN_SQL_MEMORY_LABEL_LOW64);
      char trace_label[16] = {'\0'};
      MEMCPY(trace_label, &label_high64, sizeof(int64_t));
      MEMCPY(trace_label + 8, &label_low64, sizeof(int64_t));
      if (lib::ObLabel(trace_label) == attr.label_) {
        cur_used_ += add_size;
        max_used_ = cur_used_ > max_used_ ? cur_used_ : max_used_;
#ifdef ERRSIM
        int64_t dynamic_leak_size = - EVENT_CODE(EventTable::EN_SQL_MEMORY_DYNAMIC_LEAK_SIZE);
        if (dynamic_leak_size > 0 && max_used_ >= dynamic_leak_size) {
          abort();
        }
#endif // ERRSIM
      }
    } else {
      cur_used_ += add_size;
      max_used_ = cur_used_ > max_used_ ? cur_used_ : max_used_;
    }
  }

private:
  int64_t cur_used_;
  int64_t &max_used_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_MONITOR_OB_PROCESS_MALLOC_CALLBACK_H_
