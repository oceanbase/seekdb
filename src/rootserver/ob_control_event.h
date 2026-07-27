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

#ifndef OCEANBASE_ROOTSERVER_OB_CONTROL_EVENT_H_
#define OCEANBASE_ROOTSERVER_OB_CONTROL_EVENT_H_

#include "lib/utility/ob_print_kv.h"
#include "lib/profile/ob_trace_id.h"
#include "share/ob_structured_event_logger.h"

#define CONTROL_EVENT_ADD(level, error_code, event, args...) \
    do { \
      const int64_t MAX_VALUE_LENGTH = 512; \
      char VALUE[MAX_VALUE_LENGTH]; \
      int64_t pos = 0; \
      ::oceanbase::common::ObCurTraceId::TraceId *trace_id = ObCurTraceId::get_trace_id();\
      ::oceanbase::common::databuff_print_kv(VALUE, MAX_VALUE_LENGTH, pos, ##args, KPC(trace_id)); \
      MANAGEMENT_EVENT_ADD("control_event", event, "event_type", #level, "ret", error_code, NULL, "", NULL, "", NULL, "", "message", "", ObHexEscapeSqlStr(VALUE)); \
    } while (0)

#define CONTROL_EVENT_ADD_COMMAND(error_code, event, args...) \
    CONTROL_EVENT_ADD(CONTROL, error_code, event, ##args)

#define CONTROL_EVENT_ADD_COMMAND_START(error_code, event, args...) \
    CONTROL_EVENT_ADD(CONTROL, error_code, event, "flag", "start", ##args)

#define CONTROL_EVENT_ADD_COMMAND_FINISH(error_code, event, args...) \
    CONTROL_EVENT_ADD(CONTROL, error_code, event,  "flag", "finish", ##args)

#define CONTROL_EVENT_ADD_LOG(error_code, event, args...)\
    do {\
      if (OB_SUCCESS == error_code) {\
        CONTROL_EVENT_ADD(INFO, error_code, event, ##args);\
      } else {\
        CONTROL_EVENT_ADD(WARN, error_code, event, ##args);\
      }\
    } while (0)

#endif
