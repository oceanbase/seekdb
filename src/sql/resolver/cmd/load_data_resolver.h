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

#include "sql/resolver/cmd/ob_cmd_resolver.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "rust/sql_parser.h"

namespace oceanbase::sql {
class ObLoadDataStmt;
struct ObDataInFileStruct;

class LoadDataResolver : public ObCMDResolver {
public:
  explicit LoadDataResolver(ObResolverParams &params) : ObCMDResolver(params) {}
  int resolve(const peg::Node &node);
private:
  int resolve_filename(const peg::Node &filename, ObLoadDataStmt &statement);
  int resolve_table(const peg::Node &table, ObLoadDataStmt &statement);
  int resolve_formats(rust::Slice<const peg::Node> options, ObDataInFileStruct &formats);
  int resolve_fields(rust::Slice<const peg::Node> fields, common::ObNameCaseMode case_mode,
                     ObLoadDataStmt &statement);
  int resolve_default_fields(ObLoadDataStmt &statement);
  int resolve_assignments(rust::Slice<const peg::Node> assignments, common::ObNameCaseMode case_mode,
                          ObLoadDataStmt &statement);
  int build_column_ref_expr(ObQualifiedName &name, ObRawExpr *&expression);
  int resolve_hints(rust::Slice<const peg::Node> hints, ObLoadDataStmt &statement);
  int resolve_partitions(rust::Slice<const peg::Node> names, ObLoadDataStmt &statement);
  int copy_literal(const peg::Node &node, common::ObString &value);
  int validate_stmt(ObLoadDataStmt &statement);
  int local_infile_enabled(bool &enabled) const;
  int check_trigger_constraint(const share::schema::ObTableSchema *schema);
};

}
