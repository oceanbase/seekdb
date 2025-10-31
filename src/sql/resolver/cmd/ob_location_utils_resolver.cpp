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

#include "sql/resolver/cmd/ob_location_utils_resolver.h"
#include "sql/resolver/cmd/ob_location_utils_stmt.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace oceanbase::common;
namespace sql
{
ObLocationUtilsResolver::ObLocationUtilsResolver(ObResolverParams &params)
    :ObCMDResolver(params)
{}

ObLocationUtilsResolver::~ObLocationUtilsResolver()
{}

int ObLocationUtilsResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObString location_name;
  if (T_LOCATION_UTILS != parse_tree.type_) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid parse tree type", K(ret));
  } else if (OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "parse_tree.children_ is null.", K(ret));
  } else if (OB_ISNULL(parse_tree.children_[LOCATION_NAME])) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "parse_tree.children_[LOCATION_NAME] is null.", K(ret));
  } else {
    ObLocationUtilsStmt *stmt = NULL;
    if (OB_ISNULL(stmt = create_stmt<ObLocationUtilsStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "create stmt failed", K(ret));
    }
    if (OB_SUCC(ret)) {
      ParseNode *child_node = parse_tree.children_[LOCATION_NAME];
      location_name.assign_ptr(child_node->str_value_, static_cast<int32_t>(child_node->str_len_));
      stmt->set_location_name(location_name);
      if (OB_LOCATION_UTILS_REMOVE == child_node->value_ ) {
        stmt->set_op_type(child_node->value_);
      } else {
        ret = OB_INVALID_ARGUMENT;
        SQL_RESV_LOG(WARN, "invalid util type", K(ret), K(child_node->value_), K(location_name));
      }
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(parse_tree.children_[SUB_PATH])) {
      ObString sub_path;
      ParseNode *child_node = parse_tree.children_[SUB_PATH];
      sub_path.assign_ptr(child_node->str_value_, static_cast<int32_t>(child_node->str_len_));
      stmt->set_sub_path(sub_path);
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(parse_tree.children_[PATTERN])) {
      ParseNode *child_node = parse_tree.children_[PATTERN];
      ObString pattern;
      if (T_EXTERNAL_FILE_PATTERN != child_node->type_) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "invalid file format option", K(ret));
      } else if (child_node->num_child_ != 1 || OB_ISNULL(child_node->children_[0])) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "unexpected child num", K(child_node->num_child_));
      } else if (0 == child_node->children_[0]->str_len_) {
        ObSqlString err_msg;
        err_msg.append_fmt("empty regular expression");
        ret = OB_ERR_REGEXP_ERROR;
        LOG_USER_ERROR(OB_ERR_REGEXP_ERROR, err_msg.ptr());
        SQL_RESV_LOG(WARN, "empty regular expression", K(ret));
      } else {
        pattern = ObString(child_node->children_[0]->str_len_,
                          child_node->children_[0]->str_value_);
        if (OB_FAIL(ObSQLUtils::convert_sql_text_to_schema_for_storing(*allocator_,
                                                                        session_info_->get_dtc_params(),
                                                                        pattern))) {
          SQL_RESV_LOG(WARN, "failed to convert pattern to utf8", K(ret));
        } else {
          stmt->set_pattern(pattern);
        }
      }
    }
  }
  return ret;
}
} // sql
} // oceanbase
