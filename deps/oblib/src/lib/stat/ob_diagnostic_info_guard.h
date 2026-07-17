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

// ObDiagnosticInfo subsystem has been fully removed.
// This header provides no-op stubs so that existing callers compile without
// functional changes.

#ifndef OB_DIAGNOSTIC_INFO_GUARD_H_
#define OB_DIAGNOSTIC_INFO_GUARD_H_

#include "lib/utility/ob_macro_utils.h"
#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include <typeinfo>
#include "lib/wait_event/ob_inner_sql_wait_type.h"

namespace oceanbase
{

namespace common
{

class ObDiagnosticInfo;
class ObDiagnosticInfoContainer;
class ObDiagnosticInfoSwitchGuard;

// No-op stub: ObDIActionGuard was used for ASH program/module/action tracking
class ObDIActionGuard
{
public:
  enum ActionNameSpace
  {
    NS_INVALID = 0,
    NS_PROGRAM = 1,
    NS_MODULE = 2,
    NS_ACTION = 3,
    NS_MAX
  };
  ObDIActionGuard(const char *, const char *, const char *) {}
  ObDIActionGuard(const char *, const char *) {}
  ObDIActionGuard(const char *) {}
  ObDIActionGuard(const ObString &) {}
  ObDIActionGuard(const std::type_info &) {}
  ObDIActionGuard(ActionNameSpace, const char *, ...) {}
  ~ObDIActionGuard() {}
};

// No-op stub: ObLocalDiagnosticInfo
class ObLocalDiagnosticInfo
{
public:
  DISABLE_COPY_ASSIGN(ObLocalDiagnosticInfo);
  static inline ObDiagnosticInfo *get() { return nullptr; }
  static inline int inc_ref(ObDiagnosticInfo *) { return OB_SUCCESS; }
  static inline int dec_ref(ObDiagnosticInfo *&di) { di = nullptr; return OB_SUCCESS; }
  static void set_thread_name(const char *) {}
  static void set_thread_name(uint64_t, const char *) {}
  static void set_service_name(uint64_t, const char *) {}
  static void set_service_module(const char *) {}
  static void set_service_action(const char *, const char *, const char *) {}
  static void set_service_action(const char *) {}
private:
  ObLocalDiagnosticInfo() = default;
  ~ObLocalDiagnosticInfo() = default;
};

// No-op stub: ObTenantDiagnosticInfoSummaryGuard
class ObTenantDiagnosticInfoSummaryGuard
{
public:
  ObTenantDiagnosticInfoSummaryGuard() {}
  explicit ObTenantDiagnosticInfoSummaryGuard(int64_t, int64_t = 0, bool = false) {}
  explicit ObTenantDiagnosticInfoSummaryGuard(void *) {}
  ~ObTenantDiagnosticInfoSummaryGuard() {}
  DISABLE_COPY_ASSIGN(ObTenantDiagnosticInfoSummaryGuard);
};

// No-op stub: ObDiagnosticInfoSwitchGuard
class ObDiagnosticInfoSwitchGuard
{
public:
  explicit ObDiagnosticInfoSwitchGuard(ObDiagnosticInfo *) {}
  ~ObDiagnosticInfoSwitchGuard() {}
  DISABLE_COPY_ASSIGN(ObDiagnosticInfoSwitchGuard);
};

// No-op stub: ASH item attach guard
#define DEF_ASH_ITEM_ATTACH_GUARD(item_name, item_type)                           \
class ObAshStat_##item_name##_AttachGuard                                         \
{                                                                                 \
public:                                                                           \
  ObAshStat_##item_name##_AttachGuard(const item_type &) {}                       \
  ~ObAshStat_##item_name##_AttachGuard() {}                                       \
private:                                                                          \
  DISALLOW_COPY_AND_ASSIGN(ObAshStat_##item_name##_AttachGuard);                  \
};

DEF_ASH_ITEM_ATTACH_GUARD(plan_line_id, int32_t);

#define ASH_ITEM_ATTACH_GUARD(item_name, item_val)                                \
  ObAshStat_##item_name##_AttachGuard _ash_item_attach_guard(item_val);

// No-op stub: ASH flag setter guards
#define DEF_ASH_FLAGS_SETTER_GUARD(ash_flag_type)                                \
  class ObActiveSession_##ash_flag_type##_FlagSetterGuard                        \
  {                                                                              \
  public:                                                                        \
    ObActiveSession_##ash_flag_type##_FlagSetterGuard() {}                       \
    ~ObActiveSession_##ash_flag_type##_FlagSetterGuard() {}                      \
  private:                                                                       \
    DISALLOW_COPY_AND_ASSIGN(ObActiveSession_##ash_flag_type##_FlagSetterGuard); \
  };

DEF_ASH_FLAGS_SETTER_GUARD(in_parse)
DEF_ASH_FLAGS_SETTER_GUARD(in_pl_parse)
DEF_ASH_FLAGS_SETTER_GUARD(in_get_plan_cache)
DEF_ASH_FLAGS_SETTER_GUARD(in_sql_optimize)
DEF_ASH_FLAGS_SETTER_GUARD(in_sql_execution)
DEF_ASH_FLAGS_SETTER_GUARD(in_px_execution)
DEF_ASH_FLAGS_SETTER_GUARD(in_sequence_load)
DEF_ASH_FLAGS_SETTER_GUARD(in_committing)
DEF_ASH_FLAGS_SETTER_GUARD(in_storage_read)
DEF_ASH_FLAGS_SETTER_GUARD(in_storage_write)
DEF_ASH_FLAGS_SETTER_GUARD(in_das_remote_exec)
DEF_ASH_FLAGS_SETTER_GUARD(in_filter_rows)
DEF_ASH_FLAGS_SETTER_GUARD(in_rpc_encode)
DEF_ASH_FLAGS_SETTER_GUARD(in_rpc_decode)
DEF_ASH_FLAGS_SETTER_GUARD(in_connection_mgr)
DEF_ASH_FLAGS_SETTER_GUARD(in_check_row_confliction)
DEF_ASH_FLAGS_SETTER_GUARD(in_deadlock_row_register)
DEF_ASH_FLAGS_SETTER_GUARD(in_check_tx_status)
DEF_ASH_FLAGS_SETTER_GUARD(in_resolve)
DEF_ASH_FLAGS_SETTER_GUARD(in_rewrite)
DEF_ASH_FLAGS_SETTER_GUARD(in_foreign_key_cascading)
DEF_ASH_FLAGS_SETTER_GUARD(in_extract_query_range)

#undef DEF_ASH_FLAGS_SETTER_GUARD

#define ACTIVE_SESSION_FLAG_SETTER_GUARD(ash_flag_type)                                            \
  ObActiveSession_##ash_flag_type##_FlagSetterGuard _ash_flag_setter_guard;

// No-op stub: ObASHSetInnerSqlWaitGuard
class ObASHSetInnerSqlWaitGuard {
public:
  ObASHSetInnerSqlWaitGuard(ObInnerSqlWaitTypeId) {};
  ~ObASHSetInnerSqlWaitGuard() {};
};

// No-op stub: ObASHTabletIdSetterGuard
class ObASHTabletIdSetterGuard {
public:
  ObASHTabletIdSetterGuard(uint64_t) {}
  ~ObASHTabletIdSetterGuard() {}
};

} /* namespace common */
} /* namespace oceanbase */

#define EVENT_ADD(stat_no, value)
#define EVENT_TENANT_ADD(stat_no, value)
#define EVENT_INC(stat_no)
#define EVENT_TENANT_INC(stat_no)
#define EVENT_DEC(stat_no)

// No-op stub: ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER
#define ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(field, value)

#define WAIT_BEGIN(event_no, timeout_ms, p1, p2, p3, is_atomic)  \
  do {                                                           \
    need_record_ = false;                                        \
  } while (0)

#define WAIT_END(event_no)                                       \
  do {                                                           \
  } while (0)

#endif /* OB_DIAGNOSTIC_INFO_GUARD_H_ */
