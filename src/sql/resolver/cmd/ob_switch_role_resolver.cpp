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
#include "sql/resolver/cmd/ob_switch_role_resolver.h"
#include "sql/session/ob_sql_session_info.h"  // ObSQLSessionInfo

namespace oceanbase
{
namespace sql
{
using namespace common;
using namespace obrpc;
using namespace share;
using namespace share::schema;

ObSwitchRoleResolver::~ObSwitchRoleResolver()
{
}

int ObSwitchRoleResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (T_SWITCHOVER_TO_STANDBY == parse_tree.type_ || T_SWITCHOVER_TO_PRIMARY == parse_tree.type_ || T_ACTIVATE_STANDBY == parse_tree.type_) {
    if (OB_FAIL(resolve_switch_role(parse_tree))) {
      LOG_WARN("failed to resolve switch cluster", KR(ret));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse node", KR(ret),
             "type", get_type_name(parse_tree.type_));
  }
  return ret;
}

int ObSwitchRoleResolver::resolve_switch_role(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObSwitchRoleStmt *stmt = create_stmt<ObSwitchRoleStmt>();
  obrpc::ObSwitchRoleArg::OpType op_type = obrpc::ObSwitchRoleArg::OpType::INVALID;
  bool is_verify = false;

  if (OB_ISNULL(stmt)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create stmt fail", KR(ret));
  } else {
    switch(parse_tree.type_) {
      case T_SWITCHOVER_TO_PRIMARY: {
        op_type = ObSwitchRoleArg::SWITCH_TO_PRIMARY;
        // Check for VERIFY keyword (optional child node)
        if (parse_tree.num_child_ > 0 && OB_NOT_NULL(parse_tree.children_) && OB_NOT_NULL(parse_tree.children_[0])) {
          if (T_INT == parse_tree.children_[0]->type_ && parse_tree.children_[0]->value_ == 1) {
            is_verify = true;
          }
        }
        break;
      }
      case T_SWITCHOVER_TO_STANDBY: {
        op_type = ObSwitchRoleArg::SWITCH_TO_STANDBY;
        // Check for VERIFY keyword (optional child node)
        if (parse_tree.num_child_ > 0 && OB_NOT_NULL(parse_tree.children_) && OB_NOT_NULL(parse_tree.children_[0])) {
          if (T_INT == parse_tree.children_[0]->type_ && parse_tree.children_[0]->value_ == 1) {
            is_verify = true;
          }
        }
        break;
      }
      case T_ACTIVATE_STANDBY: {
        op_type = ObSwitchRoleArg::FAILOVER_TO_PRIMARY;
        // Check for VERIFY keyword (optional child node)
        if (parse_tree.num_child_ > 0 && OB_NOT_NULL(parse_tree.children_) && OB_NOT_NULL(parse_tree.children_[0])) {
          if (T_INT == parse_tree.children_[0]->type_ && parse_tree.children_[0]->value_ == 1) {
            is_verify = true;
          }
        }
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid switch node type", KR(ret), "type", get_type_name(parse_tree.type_));
        break;
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(stmt->get_arg().init(op_type, is_verify))) {
      LOG_WARN("fail to init arg", KR(ret), K(stmt->get_arg()), K(op_type), K(is_verify));
    } else {
      stmt_ = stmt;
    }
  }

  return ret;
}


} //end sql
} //end oceanbase
