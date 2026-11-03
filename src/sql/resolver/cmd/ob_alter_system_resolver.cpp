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

#define USING_LOG_PREFIX SQL_RESV

#include "sql/resolver/cmd/ob_alter_system_resolver.h"
#include "share/config/ob_config_helper.h"
#include "sql/resolver/cmd/ob_alter_system_stmt.h"
#include "sql/resolver/ddl/ob_create_table_resolver.h"
#include "sql/resolver/ddl/ob_drop_table_stmt.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "share/ob_server_struct.h"
#include "share/ob_share_util.h"
#include "rust/config.h"
#include "sql/syntax.h"

namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share;
using namespace share::schema;
namespace sql
{
int ObFreezeResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObFreezeStmt *stmt = nullptr;
  if (OB_ISNULL(session_info_)) ret = OB_ERR_UNEXPECTED;
  else if (OB_ISNULL(stmt = create_stmt<ObFreezeStmt>())) ret = OB_ALLOCATE_MEMORY_FAILED;
  else {
    stmt->set_major_freeze(peg::freeze_is_major(statement));
    const auto &tablet = peg::freeze_tablet_id(statement);
    if (tablet.kind() != peg::NodeKind::Missing) {
      uint64_t id = 0;
      OZ(ObResolverUtils::resolve_unsigned_integer(tablet, "TABLET_ID", id));
      OX(stmt->get_tablet_id() = id);
    }
  }
  return ret;
}

int MergeControlResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObAdminMergeStmt *stmt = create_stmt<ObAdminMergeStmt>();
  if (OB_ISNULL(stmt)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    stmt->set_merge_type(statement.kind() == peg::NodeKind::SuspendMerge
        ? ObAdminMergeStmt::MergeType::SUSPEND : ObAdminMergeStmt::MergeType::RESUME);
  }
  return ret;
}

int ClearMergeErrorResolver::resolve(const peg::Node &statement)
{
  return create_stmt<ObClearMergeErrorStmt>() == nullptr ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS;
}

int FlushDagWarningsResolver::resolve(const peg::Node &statement)
{
  return create_stmt<ObFlushDagWarningsStmt>() == nullptr ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS;
}

int RefreshIoCalibrationResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObRefreshIOCalibraitonStmt *stmt = create_stmt<ObRefreshIOCalibraitonStmt>();
  if (OB_ISNULL(stmt)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    const auto syntax = peg::io_calibration_definition(statement);
    auto &parameter = stmt->get_param();
    parameter.only_refresh_ = syntax.only_refresh;
    OZ(ob_write_string(*allocator_, syntax_text(syntax.storage), parameter.storage_name_));
    for (size_t i = 0; OB_SUCC(ret) && i < syntax.entries.size(); ++i) {
      const auto text = peg::expression_literal(syntax.entries[i]).text;
      if (i == 0 && text.empty()) {
        parameter.only_refresh_ = false;
        break;
      }
      common::ObIOBenchResult item;
      OZ(ObIOCalibration::parse_calibration_string(syntax_text(text), item));
      OZ(parameter.calibration_list_.push_back(item));
    }
  }
  return ret;
}

int SwitchRoleResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObSwitchRoleStmt *stmt = create_stmt<ObSwitchRoleStmt>();
  stmt::StmtType type = stmt::T_NONE;
  if (OB_ISNULL(stmt)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    OZ(statement_type(statement, type));
    OX(stmt->set_stmt_type(type));
    OX(stmt->set_verify(peg::role_switch_verify(statement)));
  }
  return ret;
}

int CancelTaskResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObCancelTaskStmt *stmt = create_stmt<ObCancelTaskStmt>();
  if (OB_ISNULL(stmt)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    const auto text = peg::cancel_task_id(statement);
    ObString id;
    if (text.empty()) {
      ret = OB_ERR_PARSER_SYNTAX;
    } else {
      OZ(ob_write_string(*allocator_, syntax_text(text), id));
    }
    OZ(stmt->set_task_id(id));
  }
  return ret;
}

int ObFlushCacheResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObFlushCacheStmt *stmt = nullptr;
  if (OB_ISNULL(session_info_)) ret = OB_ERR_UNEXPECTED;
  else if (OB_ISNULL(stmt = create_stmt<ObFlushCacheStmt>())) ret = OB_ALLOCATE_MEMORY_FAILED;
  else {
    auto &argument = stmt->flush_cache_arg_;
    switch (peg::cache_flush_kind(statement)) {
      case peg::CacheKind::Plan: argument.cache_type_ = CACHE_TYPE_PLAN; break;
      case peg::CacheKind::Procedure: argument.cache_type_ = CACHE_TYPE_PL_OBJ; break;
      case peg::CacheKind::PreparedStatement: argument.cache_type_ = CACHE_TYPE_PS_OBJ; break;
      case peg::CacheKind::Library: argument.cache_type_ = CACHE_TYPE_LIB_CACHE; break;
    }
    if (peg::cache_flush_has_namespace(statement)) {
      if (argument.cache_type_ != CACHE_TYPE_LIB_CACHE) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "only support lib cache's cache evict by namespace, other type");
      } else {
        const auto text = peg::cache_flush_namespace(statement);
        argument.ns_type_ = ObLibCacheRegister::get_ns_type_by_name(syntax_text(text));
        if (text.empty() || argument.ns_type_ <= ObLibCacheNameSpace::NS_INVALID
            || argument.ns_type_ >= ObLibCacheNameSpace::NS_MAX) ret = OB_ERR_UNEXPECTED;
      }
    }
    const auto filter = peg::cache_flush_filter(statement);
    if (OB_SUCC(ret) && filter != peg::CacheFilter::All) {
      if (argument.cache_type_ != CACHE_TYPE_PLAN && argument.cache_type_ != CACHE_TYPE_PL_OBJ) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "only support plan cache's fine-grained cache evict, other type");
      } else {
        const auto &value = peg::cache_flush_filter_value(statement);
        if (filter == peg::CacheFilter::SqlId) {
          const auto text = peg::expression_literal(value).text;
          if (text.size() > OB_MAX_SQL_ID_LENGTH + 1) ret = OB_INVALID_ARGUMENT;
          else OZ(ob_write_string(*allocator_, syntax_text(text), argument.sql_id_));
        } else {
          if (value.kind() == peg::NodeKind::Parameter) {
            argument.schema_id_ = peg::placeholder_index(value);
          } else {
            OZ(ObResolverUtils::resolve_unsigned_integer(value, "SCHEMA_ID", argument.schema_id_));
          }
        }
        OX(argument.is_fine_grained_ = true);
      }
    }
    ObSchemaGetterGuard schema_guard;
    if (OB_SUCC(ret) && OB_ISNULL(GCTX.schema_service_)) ret = OB_ERR_UNEXPECTED;
    OZ(GCTX.schema_service_->get_runtime_schema_guard(schema_guard));
    if (OB_SUCC(ret) && peg::cache_flush_has_databases(statement)) {
      if (!argument.is_fine_grained_) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "flushing cache in database level at coarse flushing");
      } else {
        const auto text = peg::cache_flush_databases(statement);
        ObString databases(syntax_text(text));
        while (OB_SUCC(ret) && !databases.empty()) {
          ObString database = databases.split_on(',').trim();
          if (database.empty() && databases.find(',') == nullptr) {
            database = databases;
            databases.reset();
          }
          if (!database.empty()) {
            uint64_t id = OB_INVALID_ID;
            if (OB_FAIL(schema_guard.get_database_id(database, id)) || id == OB_INVALID_ID) ret = OB_ERR_BAD_DATABASE;
            else OZ(argument.push_database(id));
          }
        }
      }
    }
  }
  return ret;
}

int ObFlushKVCacheResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObFlushKVCacheStmt *stmt = create_stmt<ObFlushKVCacheStmt>();
  if (OB_ISNULL(stmt)) ret = OB_ALLOCATE_MEMORY_FAILED;
  else if (peg::kv_cache_has_name(statement)) {
    const auto name = peg::kv_cache_name(statement);
    if (name.empty()) ret = OB_ERR_UNEXPECTED;
    else OZ(stmt->cache_name_.assign(syntax_text(name)));
  }
  return ret;
}

int ObSetConfigResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObSetConfigStmt *stmt = create_stmt<ObSetConfigStmt>();
  if (OB_ISNULL(stmt)) ret = OB_ALLOCATE_MEMORY_FAILED;
  else {
    HEAP_VAR(ObCreateTableResolver, ddl_resolver, params_) {
      for (const auto &assignment : peg::configuration_assignments(statement)) {
        if (OB_FAIL(ret)) break;
        HEAP_VAR(ObAdminSetConfigItem, item) {
          const auto name = peg::configuration_name(assignment);
          OZ(item.name_.assign(syntax_text(name)));
          if (OB_SUCC(ret)) {
            ObString owned_name(item.name_.size(), item.name_.ptr());
            ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, owned_name);
          }
          ObObjParam value;
          ObDefaultValueRes resolved(value);
          OZ(ddl_resolver.resolve_default_value(peg::configuration_value(assignment), resolved));
          if (OB_SUCC(ret) && !resolved.is_literal_) ret = OB_ERR_ILLEGAL_TYPE;
          ObCollationType collation = ObCharset::get_system_collation();
          if (OB_SUCC(ret) && session_info_ != nullptr
              && OB_SUCCESS != session_info_->get_collation_connection(collation)) {
            collation = ObCharset::get_default_collation(ObCharset::get_default_charset());
          }
          ObArenaAllocator allocator(ObModIds::OB_SQL_COMPILE);
          ObCastCtx cast_ctx(&allocator, nullptr, 0, CM_NONE, collation, nullptr);
          ObString text;
          if (OB_SUCC(ret)) { EXPR_GET_VARCHAR_V2(value, text); }
          OZ(item.value_.assign(text));
          const auto comment = peg::configuration_comment(assignment);
          OZ(item.comment_.assign(syntax_text(comment)));
          if (OB_SUCC(ret) && STRCASECMP(item.name_.ptr(), DEFAULT_TABLE_ORGANIZATION) == 0
              && !ObConfigDefaultTableOrganizationChecker::check(item)) ret = OB_OP_NOT_ALLOW;
          OZ(stmt->get_rpc_arg().items_.push_back(item));
        }
      }
    }
  }
  return ret;
}

int ObResetConfigResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObResetConfigStmt *stmt = create_stmt<ObResetConfigStmt>();
  if (OB_ISNULL(stmt)) ret = OB_ALLOCATE_MEMORY_FAILED;
  else {
    for (const auto &assignment : peg::configuration_assignments(statement)) {
      if (OB_FAIL(ret)) break;
      HEAP_VAR(ObAdminSetConfigItem, item) {
        const auto name = peg::configuration_name(assignment);
        OZ(item.name_.assign(syntax_text(name)));
        if (OB_SUCC(ret)) {
          ObString owned_name(item.name_.size(), item.name_.ptr());
          ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, owned_name);
          if (!config::parameter_exists(rust::Str(item.name_.ptr()))) ret = OB_ERR_SYS_CONFIG_UNKNOWN;
          else {
            const rust::String value = config::parameter_default(rust::Str(item.name_.ptr()));
            OZ(item.value_.assign(ObString(value.size(), value.data())));
            OX(item.is_reset_ = true);
            OZ(stmt->get_rpc_arg().items_.push_back(item));
          }
        }
      }
    }
  }
  return ret;
}

int ObSetTPResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObSetTPStmt *stmt = create_stmt<ObSetTPStmt>();
  if (OB_ISNULL(stmt)) ret = OB_ALLOCATE_MEMORY_FAILED;
  else {
    auto &parameter = stmt->get_param();
    for (const auto &option : peg::tracepoint_options(statement)) {
      if (OB_FAIL(ret)) break;
      const auto &value = peg::tracepoint_value(option);
      if (option.kind() == peg::NodeKind::TracepointName) {
        if (parameter.event_no_ != 0) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "Setting tp_no and tp_name simultaneously is");
        } else {
          const auto value_text = value.text();
          OZ(ob_write_string(*allocator_, syntax_text(value_text), parameter.event_name_));
        }
      } else {
        const int64_t number = static_cast<int64_t>(peg::integer_literal_value(value));
        switch (option.kind()) {
          case peg::NodeKind::TracepointNumber:
            if (!parameter.event_name_.empty()) {
              ret = OB_NOT_SUPPORTED;
              LOG_USER_ERROR(OB_NOT_SUPPORTED, "Setting tp_no and tp_name simultaneously is");
            } else parameter.event_no_ = number;
            break;
          case peg::NodeKind::TracepointOccurrence:
            if (number > 0) parameter.occur_ = number;
            else ret = OB_INVALID_ARGUMENT;
            break;
          case peg::NodeKind::TracepointFrequency:
            if (value.kind() == peg::NodeKind::Int) {
              if (number < 0) ret = OB_INVALID_ARGUMENT;
              else parameter.trigger_freq_ = number;
            }
            break;
          case peg::NodeKind::TracepointError: parameter.error_code_ = number > 0 ? -number : number; break;
          case peg::NodeKind::TracepointMatch: parameter.cond_ = number; break;
          default: ret = OB_ERR_UNEXPECTED; break;
        }
      }
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
