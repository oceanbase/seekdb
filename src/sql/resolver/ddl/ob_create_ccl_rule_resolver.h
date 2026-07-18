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

#ifndef OCEANBASE_SQL_OB_CREATE_CCL_RULE_RESOLVER_H_
#define OCEANBASE_SQL_OB_CREATE_CCL_RULE_RESOLVER_H_
#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObCreateCCLRuleResolver: public ObDDLResolver
{
  static const int64_t IF_NOT_EXIST = 0;
  static const int64_t CCL_RULE_NAME = 1;
  static const int64_t CCL_AFFECT_DATABASE_TABLE = 2;
  static const int64_t CCL_AFFECT_USER_HOSTNAME = 3;
  static const int64_t CCL_AFFECT_DML = 4;
  static const int64_t CCL_FILTER_OPTION = 5;
  static const int64_t CCL_WITH_OPTION = 6;
  static const int64_t CCL_AFFECT_SCOPE = 7;
public:
  explicit ObCreateCCLRuleResolver(ObResolverParams &params);
  virtual ~ObCreateCCLRuleResolver();
  virtual int resolve(const ParseNode &parse_tree);
private:
  /**
   * @brief
   * Merge multiple keywords, using the specified delimiter, and escape special characters within the string.
   *
   * @param ccl_filter_option_node ParseNode
   * @param separator Delimiter used to concatenate strings, ';'
   * @param escape_char Character used to escape special characters, '\'
   */
  int merge_strings_with_escape(const ParseNode &ccl_filter_option_node,
                                char separator, char escape_char,
                                ObString &ccl_keyword);
  /**
   * @brief Escape a single string, handling delimiters and the escape character itself.
   *
   * @param original_string Original ObString
   * @param separator Delimiter in the string, ';'
   * @param escape_char Escape character in the string, '\'
   */
  int escape_string(const ObString &original_string, char separator,
                    char escape_char, ObString &after_escape_string);

  DISALLOW_COPY_AND_ASSIGN(ObCreateCCLRuleResolver);
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_OB_CREATE_CCL_RULE_RESOLVER_H_*/
