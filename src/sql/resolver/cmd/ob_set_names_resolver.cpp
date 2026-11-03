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
#include "sql/resolver/cmd/ob_set_names_resolver.h"
#include "sql/resolver/cmd/ob_set_names_stmt.h"

namespace oceanbase
{
using namespace oceanbase::common;
namespace sql
{
ObSetNamesResolver::ObSetNamesResolver(ObResolverParams &params)
    :ObCMDResolver(params)
{}

ObSetNamesResolver::~ObSetNamesResolver()
{}

int ObSetNamesResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObSetNamesStmt *names = create_stmt<ObSetNamesStmt>();
  if (names == nullptr) return OB_ALLOCATE_MEMORY_FAILED;
  names->set_is_set_names(statement.kind() == peg::NodeKind::SetNames);
  const auto &charset = peg::setting_charset(statement);
  if (charset.kind() == peg::NodeKind::Default) {
    names->set_is_default_charset(true);
  } else {
    ObString name;
    const auto charset_text = charset.text();
    OZ (ob_write_string(*allocator_, syntax_text(charset_text), name));
    if (OB_SUCC(ret) && names->is_set_names()) {
      const auto collation = ObCharset::get_default_collation(ObCharset::charset_type(name));
      if (!ObCharset::is_valid_collation(collation)) {
        ret = OB_ERR_UNKNOWN_CHARSET;
        LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, name.length(), name.ptr());
      } else if (ObCharset::get_charset(collation)->mbminlen > 1) {
        ret = OB_ERR_WRONG_VALUE_FOR_VAR;
        LOG_USER_ERROR(OB_ERR_WRONG_VALUE_FOR_VAR, static_cast<int>(strlen("character_set_client")), "character_set_client", name.length(), name.ptr());
      }
    }
    if (OB_SUCC(ret)) names->set_charset(name);
  }
  if (OB_SUCC(ret) && peg::setting_has_collation(statement)) {
    const auto &collation = peg::setting_collation(statement);
    if (collation.kind() == peg::NodeKind::Default) {
      names->set_is_default_collation(true);
    } else {
      ObString name;
      const auto collation_text = collation.text();
      OZ (ob_write_string(*allocator_, syntax_text(collation_text), name));
      if (OB_SUCC(ret)) names->set_collation(name);
    }
  }
  return ret;
}
} // sql
} // oceanbase
