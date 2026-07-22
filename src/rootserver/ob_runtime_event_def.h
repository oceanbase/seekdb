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
// Runtime events are written asynchronously with up to six name/value pairs.
#ifdef DEF_MODULE
#ifdef DEF_EVENT
  class DBMS_PARTITION {
    public:
      DEF_MODULE(DBMS_PARTITION, "DBMS_PARTITION");

      DEF_EVENT(DBMS_PARTITION, MANAGE_DYNAMIC_PARTITION, "MANAGE_DYNAMIC_PARTITION",
                SUCCESS_TABLE_ID_LIST,
                FAILED_TABLE_ID_LIST);
  };
#endif
#endif
////////////////////////////////////////////////////////////////
#ifndef OCEANBASE_ROOTSERVER_OB_RUNTIME_EVENT_DEF_H_
#define OCEANBASE_ROOTSERVER_OB_RUNTIME_EVENT_DEF_H_
#include <stdint.h>
#include "share/ob_structured_event_logger.h" // TENANT_EVENT_ADD
namespace oceanbase
{
namespace runtime_event
{
#define DEF_MODULE(MODULE, MODULE_STR) \
  static constexpr const char* const MODULE##_NAME = #MODULE; \
  static constexpr const char* const MODULE##_STR = MODULE_STR;
#define DEF_EVENT_COMMON(EVENT, EVENT_STR) \
  static constexpr const char* const EVENT##_NAME = #EVENT; \
  static constexpr const char* const EVENT##_STR = EVENT_STR;
#define OneArguments(MODULE)
#define TwoArguments(MODULE, EVENT)
#define ThreeArguments(MODULE, EVENT, EVENT_STR) \
  DEF_EVENT_COMMON(EVENT, EVENT_STR) \
  template<> \
  static void MODULE##_##EVENT##_func(const char * const module, const char * const event, \
                               const int64_t event_timestamp, const int user_ret, const int64_t cost_us) \
  { \
    TENANT_EVENT_ADD(module, event, event_timestamp, user_ret, cost_us); \
    return ;\
  }
#define FourArguments(MODULE, EVENT, EVENT_STR, NAME1) \
  DEF_EVENT_COMMON(EVENT, EVENT_STR) \
  template<typename T1> \
  static void MODULE##_##EVENT##_func(const char * const module, const char * const event, \
                               const int64_t event_timestamp, const int user_ret, const int64_t cost_us, \
                               const T1 &value1) \
  { \
    TENANT_EVENT_ADD(module, event, event_timestamp, user_ret, cost_us, #NAME1, value1); \
    return ;\
  }
#define FiveArguments(MODULE, EVENT, EVENT_STR, NAME1, NAME2) \
  DEF_EVENT_COMMON(EVENT, EVENT_STR) \
  template<typename T1, typename T2> \
  static void MODULE##_##EVENT##_func(const char * const module, const char * const event, \
                               const int64_t event_timestamp, const int user_ret, const int64_t cost_us, \
                               const T1 &value1, const T2 &value2) \
  { \
    TENANT_EVENT_ADD(module, event, event_timestamp, user_ret, cost_us, #NAME1, value1, #NAME2, value2); \
    return ;\
  }
#define SixArguments(MODULE, EVENT, EVENT_STR, NAME1, NAME2, NAME3) \
  DEF_EVENT_COMMON(EVENT, EVENT_STR) \
  template<typename T1, typename T2, typename T3> \
  static void MODULE##_##EVENT##_func(const char * const module, const char * const event, \
                               const int64_t event_timestamp, const int user_ret, const int64_t cost_us, \
                               const T1 &value1, const T2 &value2, \
                               const T3 &value3) \
  { \
    TENANT_EVENT_ADD(module, event, event_timestamp, user_ret, cost_us, #NAME1, value1, #NAME2, value2, \
                     #NAME3, value3); \
    return ;\
  }
#define SevenArguments(MODULE, EVENT, EVENT_STR, NAME1, NAME2, NAME3, NAME4) \
  DEF_EVENT_COMMON(EVENT, EVENT_STR) \
  template<typename T1, typename T2, typename T3, typename T4> \
  static void MODULE##_##EVENT##_func(const char * const module, const char * const event, \
                               const int64_t event_timestamp, const int user_ret, const int64_t cost_us, \
                               const T1 &value1, const T2 &value2, \
                               const T3 &value3, const T4 &value4) \
  { \
    TENANT_EVENT_ADD(module, event, event_timestamp, user_ret, cost_us, #NAME1, value1, #NAME2, value2, \
                     #NAME3, value3, #NAME4, value4); \
    return ;\
  }
#define EightArguments(MODULE, EVENT, EVENT_STR, NAME1, NAME2, NAME3, NAME4, NAME5) \
  DEF_EVENT_COMMON(EVENT, EVENT_STR) \
  template<typename T1, typename T2, typename T3, typename T4, \
      typename T5> \
  static void MODULE##_##EVENT##_func(const char * const module, const char * const event, \
                               const int64_t event_timestamp, const int user_ret, const int64_t cost_us, \
                               const T1 &value1, const T2 &value2, \
                               const T3 &value3, const T4 &value4, \
                               const T5 &value5) \
  { \
    TENANT_EVENT_ADD(module, event, event_timestamp, user_ret, cost_us, #NAME1, value1, #NAME2, value2, \
                     #NAME3, value3, #NAME4, value4, #NAME5, value5); \
    return ;\
  }
#define NineArguments(MODULE, EVENT, EVENT_STR, NAME1, NAME2, NAME3, NAME4, NAME5, NAME6) \
  DEF_EVENT_COMMON(EVENT, EVENT_STR) \
  template<typename T1, typename T2, typename T3, typename T4, \
      typename T5, typename T6> \
  static void MODULE##_##EVENT##_func(const char * const module, const char * const event, \
                               const int64_t event_timestamp, const int user_ret, const int64_t cost_us, \
                               const T1 &value1, const T2 &value2, \
                               const T3 &value3, const T4 &value4, \
                               const T5 &value5, const T6 &value6) \
  { \
    TENANT_EVENT_ADD(module, event, event_timestamp, user_ret, cost_us, #NAME1, value1, #NAME2, value2, \
                     #NAME3, value3, #NAME4, value4, #NAME5, value5, #NAME6, value6); \
    return ;\
  }

#define GetMacro(_1, _2, _3, _4, _5, _6, _7, _8, _9, NAME, ...) NAME
#define DEF_EVENT(...) \
  GetMacro(__VA_ARGS__, NineArguments, EightArguments, SevenArguments, SixArguments, FiveArguments, FourArguments, ThreeArguments, TwoArguments, OneArgument, ...)(__VA_ARGS__)

#define RUNTIME_EVENT(MODULE, EVENT, event_timestamp, user_ret, cost_us, args...) \
  MODULE::MODULE##_##EVENT##_func(MODULE::MODULE##_STR, MODULE::EVENT##_STR, event_timestamp, user_ret, cost_us, args)

#include "ob_runtime_event_def.h"
#undef DEF_MODULE
#undef DEF_EVENT
#undef DEF_EVENT_COMMON
#undef OneArguments
#undef TwoArguments
#undef ThreeArguments
#undef FourArguments
#undef FiveArguments
#undef SixArguments
#undef SevenArguments
#undef EightArguments
#undef NineArguments
#undef GetMacro
} // end namespace runtime_event
} // end namespace oceanbase
#endif /* OCEANBASE_ROOTSERVER_OB_RUNTIME_EVENT_DEF_H_ */
