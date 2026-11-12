/*
 * Copyright (c) 2026 OceanBase.
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

#pragma once

#include "rust/sql_parser.h"
#include "sql/resolver/ob_resolver_define.h"
#include "sql/resolver/ob_stmt.h"
#include "sql/resolver/statement_factory.h"

namespace oceanbase::sql::access_control
{
enum class AccountNameMode { Schema, Verbatim };

int copy_text(rust::Slice<const uint8_t> text, ObResolverParams &params, common::ObString &copy);
int resolve_account(const peg::Node &spec, ObResolverParams &params, AccountNameMode mode,
                    common::ObString &name, common::ObString &host);
int resolve_role_account(const peg::Node &spec, ObResolverParams &params,
                         common::ObString &name, common::ObString &host);
int resolve_dropped_account(const peg::Node &spec, ObResolverParams &params, bool protect_root,
                            common::ObString &name, common::ObString &host);
int check_plugin(const peg::Node &spec, ObResolverParams &params, bool case_sensitive);
int check_reserved_account(ObResolverParams &params, uint64_t user_id);
int check_reserved_account(ObResolverParams &params, const common::ObString &name,
                            const common::ObString &host);
int check_password_strength(common::ObString &password, ObResolverParams &params);
bool valid_encoded_password(const common::ObString &password);
int resolve_privileges(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt);
int resolve_roles(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt);
}
