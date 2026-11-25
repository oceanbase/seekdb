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

#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase::sql {

class OutlineResolver : public ObDDLResolver {
public:
  explicit OutlineResolver(ObResolverParams &params) : ObDDLResolver(params) {}
  int resolve(const peg::Node &node);

private:
  int resolve_name(const peg::Node &relation, common::ObString &database,
                   common::ObString &name);
  int resolve_statement(const peg::Node &node, ObStmt *&statement,
                        common::ObString &sql, common::ObString &target);
  int store_source(const peg::Node &node, common::ObString &text);
};

}
