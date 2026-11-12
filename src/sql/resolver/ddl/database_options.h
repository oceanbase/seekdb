// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#pragma once

#include "share/ob_rpc_struct.h"
#include "rust/sql_parser.h"

namespace oceanbase::sql {
template <class DatabaseStatement>
int resolve_database_options(DatabaseStatement &stmt, rust::Slice<const peg::Node> options,
                             common::ObBitSet<> *alter_options = nullptr)
{
  using namespace common;
  int ret = OB_SUCCESS;
  bool collation_set = false;
  for (const auto &option : options) {
    if (OB_FAIL(ret)) { break; }
    if (option.kind() == peg::NodeKind::ReadOnly || option.kind() == peg::NodeKind::ReadWrite) {
      stmt.set_read_only(option.kind() == peg::NodeKind::ReadOnly);
      if (alter_options != nullptr) {
        OZ(alter_options->add_member(obcall::ObAlterDatabaseArg::READ_ONLY));
      }
      continue;
    }
    const auto option_text = option.text();
    ObString name(syntax_text(option_text));
    ObCharsetType charset_type = CHARSET_INVALID;
    ObCollationType collation_type = CS_TYPE_INVALID;
    if (option.kind() == peg::NodeKind::Charset) {
      charset_type = ObCharset::charset_type(name.trim());
      collation_type = ObCharset::get_default_collation(charset_type);
      if (charset_type == CHARSET_INVALID) {
        ret = OB_ERR_UNKNOWN_CHARSET;
        LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, name.length(), name.ptr());
      } else if (collation_type == CS_TYPE_INVALID) {
        ret = OB_ERR_UNEXPECTED;
      } else if (collation_set && stmt.get_charset_type() != charset_type) {
        ret = OB_ERR_CONFLICTING_DECLARATIONS;
        LOG_USER_ERROR(OB_ERR_CONFLICTING_DECLARATIONS,
                       ObCharset::charset_name(stmt.get_charset_type()),
                       ObCharset::charset_name(charset_type));
      }
    } else if (option.kind() == peg::NodeKind::Collation) {
      collation_type = ObCharset::collation_type(name.trim());
      charset_type = ObCharset::charset_type_by_coll(collation_type);
      if (collation_type == CS_TYPE_INVALID) {
        ret = OB_ERR_UNKNOWN_COLLATION;
        LOG_USER_ERROR(OB_ERR_UNKNOWN_COLLATION, name.length(), name.ptr());
      } else if (charset_type == CHARSET_INVALID) {
        ret = OB_ERR_UNEXPECTED;
      } else if (collation_set && stmt.get_charset_type() != charset_type) {
        ret = OB_ERR_COLLATION_MISMATCH;
      }
    }
    if (OB_SUCC(ret)) {
      stmt.set_charset_type(charset_type);
      stmt.set_collation_type(collation_type);
      collation_set = true;
      if (alter_options != nullptr) {
        OZ(alter_options->add_member(obcall::ObAlterDatabaseArg::COLLATION_TYPE));
      }
    }
  }
  return ret;
}
}
