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

#ifndef OCEANBASE_LOG_FETCHER_SRC_OB_LOG_TRACE_ID_
#define OCEANBASE_LOG_FETCHER_SRC_OB_LOG_TRACE_ID_

#include "lib/net/ob_addr.h"
#include "lib/profile/ob_trace_id.h"

namespace oceanbase
{
namespace logfetcher
{
common::ObAddr& get_self_addr();

// trace id: Used to identify rpc requests between libobcdc-observer
inline void init_trace_id()
{
  common::ObCurTraceId::init(get_self_addr());
}

inline void clear_trace_id()
{
  common::ObCurTraceId::reset();
}

inline void set_trace_id(const common::ObCurTraceId::TraceId &trace_id)
{
  common::ObCurTraceId::set(trace_id);
}

class ObLogTraceIdGuard
{
public:
  ObLogTraceIdGuard()
  {
    init_trace_id();
  }

  explicit ObLogTraceIdGuard(const common::ObCurTraceId::TraceId &trace_id)
  {
    set_trace_id(trace_id);
  }

  ~ObLogTraceIdGuard()
  {
    clear_trace_id();
  }
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogTraceIdGuard);
};

}
}
#endif // OCEANBASE_LOG_FETCHER_SRC_OB_LOG_TRACE_ID_
