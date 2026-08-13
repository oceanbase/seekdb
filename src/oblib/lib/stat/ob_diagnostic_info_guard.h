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

#ifndef OB_DIAGNOSTIC_INFO_GUARD_H_
#define OB_DIAGNOSTIC_INFO_GUARD_H_

#include "lib/stat/ob_diagnose_info.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/wait_event/ob_inner_sql_wait_type.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <typeinfo>

namespace oceanbase
{
namespace common
{

static const int64_t DI_TRACE_TEXT_LENGTH = 64;

struct ObDiagnoseRuntimeTraceContext
{
  ObDiagnoseRuntimeTraceContext()
      : in_parse_(false),
        in_pl_parse_(false),
        in_get_plan_cache_(false),
        in_sql_optimize_(false),
        in_sql_execution_(false),
        in_px_execution_(false),
        in_sequence_load_(false),
        in_committing_(false),
        in_storage_read_(false),
        in_storage_write_(false),
        in_das_remote_exec_(false),
        in_filter_rows_(false),
        in_rpc_encode_(false),
        in_rpc_decode_(false),
        in_connection_mgr_(false),
        in_check_row_confliction_(false),
        in_deadlock_row_register_(false),
        in_check_tx_status_(false),
        in_resolve_(false),
        in_rewrite_(false),
        in_foreign_key_cascading_(false),
        in_extract_query_range_(false),
        plan_line_id_(0),
        tablet_id_(0),
        inner_sql_wait_type_id_(ObInnerSqlWaitTypeId::NULL_INNER_SQL),
        ls_id_(0),
        dop_(0),
        required_px_workers_number_(0),
        admitted_px_workers_number_(0),
        table_id_(0),
        table_schema_version_(0)
  {
    program_[0] = '\0';
    module_[0] = '\0';
    action_[0] = '\0';
    thread_name_[0] = '\0';
    service_name_[0] = '\0';
  }

  static void set_text(char *dest, const char *src,
      const int64_t src_len = DI_TRACE_TEXT_LENGTH - 1)
  {
    if (OB_NOT_NULL(dest)) {
      const int64_t copy_len = OB_ISNULL(src)
          ? 0 : std::min<int64_t>(src_len, DI_TRACE_TEXT_LENGTH - 1);
      if (copy_len > 0) {
        MEMCPY(dest, src, copy_len);
      }
      dest[copy_len] = '\0';
    }
  }

  char program_[DI_TRACE_TEXT_LENGTH];
  char module_[DI_TRACE_TEXT_LENGTH];
  char action_[DI_TRACE_TEXT_LENGTH];
  char thread_name_[DI_TRACE_TEXT_LENGTH];
  char service_name_[DI_TRACE_TEXT_LENGTH];
  bool in_parse_;
  bool in_pl_parse_;
  bool in_get_plan_cache_;
  bool in_sql_optimize_;
  bool in_sql_execution_;
  bool in_px_execution_;
  bool in_sequence_load_;
  bool in_committing_;
  bool in_storage_read_;
  bool in_storage_write_;
  bool in_das_remote_exec_;
  bool in_filter_rows_;
  bool in_rpc_encode_;
  bool in_rpc_decode_;
  bool in_connection_mgr_;
  bool in_check_row_confliction_;
  bool in_deadlock_row_register_;
  bool in_check_tx_status_;
  bool in_resolve_;
  bool in_rewrite_;
  bool in_foreign_key_cascading_;
  bool in_extract_query_range_;
  int32_t plan_line_id_;
  uint64_t tablet_id_;
  ObInnerSqlWaitTypeId inner_sql_wait_type_id_;
  int64_t ls_id_;
  int64_t dop_;
  int64_t required_px_workers_number_;
  int64_t admitted_px_workers_number_;
  uint64_t table_id_;
  int64_t table_schema_version_;
};

class ObLocalDiagnosticInfo
{
public:
  DISABLE_COPY_ASSIGN(ObLocalDiagnosticInfo);
  static inline ObDiagnoseRuntimeTraceContext *get()
  {
    return lib::is_diagnose_info_enabled() ? &get_instance() : NULL;
  }
  static inline void add_stat(
      const ObStatEventIds::ObStatEventIdEnum stat_no, const int64_t value)
  {
    if (stat_no >= 0 && stat_no < ObStatEventIds::STAT_EVENT_ADD_END) {
      ObDiagnoseSessionInfo *session_info = ObDiagnoseSessionInfo::get_local_diagnose_info();
      if (OB_STAT_EVENTS[stat_no].summary_in_session_ && OB_NOT_NULL(session_info)) {
        (void)session_info->update_stat(stat_no, value);
      }
      (void)ObDIGlobalRuntimeCache::get_instance().update_stat(stat_no, value);
    }
  }
  static inline void set_stat(
      const ObStatEventIds::ObStatEventIdEnum stat_no, const int64_t value)
  {
    (void)ObDIGlobalRuntimeCache::get_instance().set_stat(stat_no, value);
  }
  static void set_thread_name(const char *name)
  {
    ObDiagnoseRuntimeTraceContext *ctx = get();
    if (OB_NOT_NULL(ctx)) {
      ObDiagnoseRuntimeTraceContext::set_text(ctx->thread_name_, name);
    }
  }
  static void set_thread_name(uint64_t runtime_id, const char *name)
  {
    UNUSED(runtime_id);
    set_thread_name(name);
  }
  static void set_service_name(uint64_t runtime_id, const char *name)
  {
    UNUSED(runtime_id);
    ObDiagnoseRuntimeTraceContext *ctx = get();
    if (OB_NOT_NULL(ctx)) {
      ObDiagnoseRuntimeTraceContext::set_text(ctx->service_name_, name);
    }
  }
  static void set_service_module(const char *module)
  {
    ObDiagnoseRuntimeTraceContext *ctx = get();
    if (OB_NOT_NULL(ctx)) {
      ObDiagnoseRuntimeTraceContext::set_text(ctx->module_, module);
    }
  }
  static void set_service_action(const char *service, const char *module, const char *action)
  {
    ObDiagnoseRuntimeTraceContext *ctx = get();
    if (OB_NOT_NULL(ctx)) {
      ObDiagnoseRuntimeTraceContext::set_text(ctx->service_name_, service);
      ObDiagnoseRuntimeTraceContext::set_text(ctx->module_, module);
      ObDiagnoseRuntimeTraceContext::set_text(ctx->action_, action);
    }
  }
  static void set_service_action(const char *action)
  {
    ObDiagnoseRuntimeTraceContext *ctx = get();
    if (OB_NOT_NULL(ctx)) {
      ObDiagnoseRuntimeTraceContext::set_text(ctx->action_, action);
    }
  }
  static ObInnerSqlWaitTypeId get_inner_sql_wait_type()
  {
    ObDiagnoseRuntimeTraceContext *ctx = get();
    return OB_ISNULL(ctx) ? ObInnerSqlWaitTypeId::NULL_INNER_SQL : ctx->inner_sql_wait_type_id_;
  }
private:
  static ObDiagnoseRuntimeTraceContext &get_instance()
  {
    static thread_local ObDiagnoseRuntimeTraceContext context;
    return context;
  }
  ObLocalDiagnosticInfo() = delete;
  ~ObLocalDiagnosticInfo() = delete;
};

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

  ObDIActionGuard(const char *program, const char *module, const char *action)
      : context_(ObLocalDiagnosticInfo::get()),
        reset_program_(false), reset_module_(false), reset_action_(false)
  {
    set_program(program);
    set_module(module);
    set_action(action);
  }
  ObDIActionGuard(const char *module, const char *action)
      : context_(ObLocalDiagnosticInfo::get()),
        reset_program_(false), reset_module_(false), reset_action_(false)
  {
    set_module(module);
    set_action(action);
  }
  explicit ObDIActionGuard(const char *action)
      : context_(ObLocalDiagnosticInfo::get()),
        reset_program_(false), reset_module_(false), reset_action_(false)
  {
    set_action(action);
  }
  explicit ObDIActionGuard(const ObString &action)
      : context_(ObLocalDiagnosticInfo::get()),
        reset_program_(false), reset_module_(false), reset_action_(false)
  {
    set_action(action.ptr(), action.length());
  }
  explicit ObDIActionGuard(const std::type_info &type_info)
      : context_(ObLocalDiagnosticInfo::get()),
        reset_program_(false), reset_module_(false), reset_action_(false)
  {
    set_action(type_info.name());
  }
  ObDIActionGuard(ActionNameSpace action_ns, const char *action_format, ...)
      : context_(ObLocalDiagnosticInfo::get()),
        reset_program_(false), reset_module_(false), reset_action_(false)
  {
    char text[DI_TRACE_TEXT_LENGTH];
    text[0] = '\0';
    if (OB_NOT_NULL(action_format)) {
      va_list args;
      va_start(args, action_format);
      (void)vsnprintf(text, sizeof(text), action_format, args);
      va_end(args);
    }
    switch (action_ns) {
      case NS_PROGRAM:
        set_program(text);
        break;
      case NS_MODULE:
        set_module(text);
        break;
      case NS_ACTION:
        set_action(text);
        break;
      default:
        break;
    }
  }
  ~ObDIActionGuard()
  {
    if (OB_NOT_NULL(context_)) {
      if (reset_program_) {
        MEMCPY(context_->program_, prev_program_, sizeof(prev_program_));
      }
      if (reset_module_) {
        MEMCPY(context_->module_, prev_module_, sizeof(prev_module_));
      }
      if (reset_action_) {
        MEMCPY(context_->action_, prev_action_, sizeof(prev_action_));
      }
    }
  }
private:
  void set_program(const char *value)
  {
    set_program(value, OB_ISNULL(value) ? 0 : strlen(value));
  }
  void set_program(const char *value, const int64_t length)
  {
    if (OB_NOT_NULL(context_) && OB_NOT_NULL(value)) {
      MEMCPY(prev_program_, context_->program_, sizeof(prev_program_));
      ObDiagnoseRuntimeTraceContext::set_text(context_->program_, value, length);
      reset_program_ = true;
    }
  }
  void set_module(const char *value)
  {
    if (OB_NOT_NULL(context_) && OB_NOT_NULL(value)) {
      MEMCPY(prev_module_, context_->module_, sizeof(prev_module_));
      ObDiagnoseRuntimeTraceContext::set_text(context_->module_, value,
          strlen(value));
      reset_module_ = true;
    }
  }
  void set_action(const char *value)
  {
    set_action(value, OB_ISNULL(value) ? 0 : strlen(value));
  }
  void set_action(const char *value, const int64_t length)
  {
    if (OB_NOT_NULL(context_) && OB_NOT_NULL(value)) {
      MEMCPY(prev_action_, context_->action_, sizeof(prev_action_));
      ObDiagnoseRuntimeTraceContext::set_text(context_->action_, value, length);
      reset_action_ = true;
    }
  }
private:
  ObDiagnoseRuntimeTraceContext *context_;
  bool reset_program_;
  bool reset_module_;
  bool reset_action_;
  char prev_program_[DI_TRACE_TEXT_LENGTH];
  char prev_module_[DI_TRACE_TEXT_LENGTH];
  char prev_action_[DI_TRACE_TEXT_LENGTH];
  DISALLOW_COPY_AND_ASSIGN(ObDIActionGuard);
};

#define DEF_ASH_ITEM_ATTACH_GUARD(item_name, item_type)                           \
class ObAshStat_##item_name##_AttachGuard                                         \
{                                                                                 \
public:                                                                           \
  explicit ObAshStat_##item_name##_AttachGuard(const item_type &item_val)         \
      : context_(ObLocalDiagnosticInfo::get()), pre_item_val_()                   \
  {                                                                               \
    if (OB_NOT_NULL(context_)) {                                                   \
      pre_item_val_ = context_->item_name##_;                                     \
      context_->item_name##_ = item_val;                                          \
    }                                                                             \
  }                                                                               \
  ~ObAshStat_##item_name##_AttachGuard()                                           \
  {                                                                               \
    if (OB_NOT_NULL(context_)) {                                                   \
      context_->item_name##_ = pre_item_val_;                                     \
    }                                                                             \
  }                                                                               \
private:                                                                          \
  ObDiagnoseRuntimeTraceContext *context_;                                         \
  item_type pre_item_val_;                                                        \
  DISALLOW_COPY_AND_ASSIGN(ObAshStat_##item_name##_AttachGuard);                  \
};

DEF_ASH_ITEM_ATTACH_GUARD(plan_line_id, int32_t)

#define ASH_ITEM_ATTACH_GUARD(item_name, item_val)                                \
  ObAshStat_##item_name##_AttachGuard _ash_item_attach_guard(item_val)

#define DEF_ASH_FLAGS_SETTER_GUARD(ash_flag_type)                                \
class ObActiveSession_##ash_flag_type##_FlagSetterGuard                          \
{                                                                                \
public:                                                                          \
  ObActiveSession_##ash_flag_type##_FlagSetterGuard()                            \
      : context_(ObLocalDiagnosticInfo::get()), prev_value_(false)               \
  {                                                                              \
    if (OB_NOT_NULL(context_)) {                                                  \
      prev_value_ = context_->ash_flag_type##_;                                  \
      context_->ash_flag_type##_ = true;                                         \
    }                                                                            \
  }                                                                              \
  ~ObActiveSession_##ash_flag_type##_FlagSetterGuard()                           \
  {                                                                              \
    if (OB_NOT_NULL(context_)) {                                                  \
      context_->ash_flag_type##_ = prev_value_;                                  \
    }                                                                            \
  }                                                                              \
private:                                                                         \
  ObDiagnoseRuntimeTraceContext *context_;                                        \
  bool prev_value_;                                                              \
  DISALLOW_COPY_AND_ASSIGN(ObActiveSession_##ash_flag_type##_FlagSetterGuard);   \
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

#define ACTIVE_SESSION_FLAG_SETTER_GUARD(ash_flag_type)                          \
  ObActiveSession_##ash_flag_type##_FlagSetterGuard _ash_flag_setter_guard

class ObASHSetInnerSqlWaitGuard
{
public:
  explicit ObASHSetInnerSqlWaitGuard(ObInnerSqlWaitTypeId id)
      : context_(ObLocalDiagnosticInfo::get()), prev_id_(ObInnerSqlWaitTypeId::NULL_INNER_SQL)
  {
    if (OB_NOT_NULL(context_)) {
      prev_id_ = context_->inner_sql_wait_type_id_;
      context_->inner_sql_wait_type_id_ = id;
    }
  }
  ~ObASHSetInnerSqlWaitGuard()
  {
    if (OB_NOT_NULL(context_)) {
      context_->inner_sql_wait_type_id_ = prev_id_;
    }
  }
private:
  ObDiagnoseRuntimeTraceContext *context_;
  ObInnerSqlWaitTypeId prev_id_;
  DISALLOW_COPY_AND_ASSIGN(ObASHSetInnerSqlWaitGuard);
};

class ObASHTabletIdSetterGuard
{
public:
  explicit ObASHTabletIdSetterGuard(uint64_t tablet_id)
      : context_(ObLocalDiagnosticInfo::get()), prev_tablet_id_(0)
  {
    if (OB_NOT_NULL(context_)) {
      prev_tablet_id_ = context_->tablet_id_;
      context_->tablet_id_ = tablet_id;
    }
  }
  ~ObASHTabletIdSetterGuard()
  {
    if (OB_NOT_NULL(context_)) {
      context_->tablet_id_ = prev_tablet_id_;
    }
  }
private:
  ObDiagnoseRuntimeTraceContext *context_;
  uint64_t prev_tablet_id_;
  DISALLOW_COPY_AND_ASSIGN(ObASHTabletIdSetterGuard);
};

} /* namespace common */
} /* namespace oceanbase */

#define EVENT_ADD(stat_no, value)                                                 \
  do {                                                                            \
    if (::oceanbase::lib::is_diagnose_info_enabled()) {                           \
      ::oceanbase::common::ObLocalDiagnosticInfo::add_stat(                       \
          ::oceanbase::common::ObStatEventIds::stat_no, (value));                 \
    }                                                                             \
  } while (0)

#define EVENT_INC(stat_no) EVENT_ADD(stat_no, 1)
#define EVENT_DEC(stat_no) EVENT_ADD(stat_no, -1)

#define EVENT_SET(stat_no, value)                                                 \
  do {                                                                            \
    if (::oceanbase::lib::is_diagnose_info_enabled()) {                           \
      ::oceanbase::common::ObLocalDiagnosticInfo::set_stat(                       \
          ::oceanbase::common::ObStatEventIds::stat_no, (value));                 \
    }                                                                             \
  } while (0)

#define ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(field, value)                      \
  do {                                                                            \
    ::oceanbase::common::ObDiagnoseRuntimeTraceContext *ctx =                     \
        ::oceanbase::common::ObLocalDiagnosticInfo::get();                        \
    if (OB_NOT_NULL(ctx)) {                                                        \
      ctx->field = (value);                                                        \
    }                                                                             \
  } while (0)

#define WAIT_BEGIN(event_no, timeout_ms, p1, p2, p3, is_atomic)                  \
  do {                                                                            \
    need_record_ = false;                                                         \
    ::oceanbase::common::ObDiagnoseSessionInfo *wait_di =                         \
        ::oceanbase::common::ObDiagnoseSessionInfo::get_local_diagnose_info();    \
    if (OB_NOT_NULL(wait_di)) {                                                    \
      need_record_ = OB_SUCCESS == wait_di->notify_wait_begin(                    \
          event_no, timeout_ms, p1, p2, p3, is_atomic);                           \
    }                                                                             \
  } while (0)

#define WAIT_END(event_no)                                                        \
  do {                                                                            \
    if (need_record_) {                                                           \
      ::oceanbase::common::ObDiagnoseSessionInfo *wait_di =                       \
          ::oceanbase::common::ObDiagnoseSessionInfo::get_local_diagnose_info();  \
      if (OB_NOT_NULL(wait_di)) {                                                  \
        (void)wait_di->notify_wait_end(NULL, false,                               \
            ::oceanbase::common::OB_WAIT_EVENTS[event_no].wait_class_             \
                == ::oceanbase::common::ObWaitClassIds::IDLE);                    \
      }                                                                           \
    }                                                                             \
  } while (0)

#endif /* OB_DIAGNOSTIC_INFO_GUARD_H_ */
