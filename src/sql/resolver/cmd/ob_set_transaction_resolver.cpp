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

#include "data_plane/transaction/ob_transaction_isolation.h"
#include "sql/resolver/cmd/ob_set_transaction_resolver.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"
#include "src/sql/resolver/ob_resolver_utils.h"
#include "sql/ob_trans_character.h"
using namespace oceanbase::common;
using namespace oceanbase::transaction;
namespace oceanbase
{
using namespace share;
namespace sql
{
ObSetTransactionResolver::ObSetTransactionResolver(ObResolverParams &params)
    :ObCMDResolver(params)
{}

ObSetTransactionResolver::~ObSetTransactionResolver()
{}

int ObSetTransactionResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObVariableSetStmt *stmt = create_stmt<ObVariableSetStmt>();
  if (OB_ISNULL(stmt)) ret = OB_ALLOCATE_MEMORY_FAILED;
  else if (OB_ISNULL(allocator_) || OB_ISNULL(session_info_)) ret = OB_ERR_UNEXPECTED;
  else {
    const auto options = peg::transaction_setting(statement);
    ObSetVar::SetScopeType scope = options.scope == peg::VariableScope::Global ? ObSetVar::SET_SCOPE_GLOBAL
        : options.scope == peg::VariableScope::Session ? ObSetVar::SET_SCOPE_SESSION : ObSetVar::SET_SCOPE_NEXT_TRANS;
    if (options.isolation != peg::TransactionIsolation::Default) {
      ObVariableSetStmt::VariableSetNode variable;
      variable.set_scope_ = scope;
      variable.is_system_variable_ = true;
      OZ(ob_write_string(*allocator_, OB_SV_TX_ISOLATION, variable.variable_name_));
      int32_t level = ObTransIsolation::UNKNOWN;
      switch (options.isolation) {
        case peg::TransactionIsolation::ReadUncommitted: level = ObTransIsolation::READ_UNCOMMITTED; break;
        case peg::TransactionIsolation::ReadCommitted: level = ObTransIsolation::READ_COMMITED; break;
        case peg::TransactionIsolation::RepeatableRead: level = ObTransIsolation::REPEATABLE_READ; break;
        case peg::TransactionIsolation::Serializable: level = ObTransIsolation::SERIALIZABLE; break;
        default: break;
      }
      if (OB_SUCC(ret) && !ObTransIsolation::is_valid(level)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "isolation level is");
      }
      OZ(build_isolation_expr(variable.value_expr_, level));
      OZ(stmt->add_variable_node(variable));
    }
    if (options.access != peg::TransactionAccess::Default) {
      ObVariableSetStmt::VariableSetNode variable;
      variable.set_scope_ = scope;
      variable.is_system_variable_ = true;
      OZ(ob_write_string(*allocator_, OB_SV_TX_READ_ONLY, variable.variable_name_));
      OZ(build_access_expr(variable.value_expr_, options.access == peg::TransactionAccess::ReadOnly));
      OZ(stmt->add_variable_node(variable));
    }
  }
  return ret;
}

int ObSetTransactionResolver::build_isolation_expr(ObRawExpr *&expr, int32_t level)
{
  int ret = OB_SUCCESS;
  ObObjParam val;
  ObConstRawExpr *c_expr = NULL;
  const ObString &level_name = ObTransIsolation::get_name(level);
  if (OB_ISNULL(params_.expr_factory_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("invalid stmt", K_(stmt));
  } else if (level_name.empty()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_VARCHAR, c_expr))) {
  } else {
    // we use int type to represent isolation level, except for system variable tx_isolation,
    // which use varchar type, so we need cast int to varchar here.
    val.set_varchar(level_name);
    val.set_collation_type(ObCharset::get_system_collation());
    val.set_param_meta();
    c_expr->set_value(val);
    c_expr->set_data_type(ObVarcharType);
    expr = c_expr;
  }
  return ret;
}

int ObSetTransactionResolver::build_access_expr(ObRawExpr *&expr, const bool is_read_only)
{
  int ret = OB_SUCCESS;
  ObObjParam val;
  ObConstRawExpr *c_expr = NULL;
  if (is_read_only) {
    val.set_int(1);
  } else {
    val.set_int(0);
  }
  val.set_scale(0);
  val.set_param_meta();
  if (OB_ISNULL(params_.expr_factory_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("invalid stmt", K_(stmt));
  } else if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_INT, c_expr))) {
  } else {
    c_expr->set_value(val);
    c_expr->set_data_type(ObIntType);
    expr = c_expr;
  }
  return ret;
}

}
}
