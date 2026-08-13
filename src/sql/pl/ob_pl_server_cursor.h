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

#ifndef OCEANBASE_SQL_PL_OB_PL_SERVER_CURSOR_H_
#define OCEANBASE_SQL_PL_OB_PL_SERVER_CURSOR_H_

#include "sql/pl/ob_pl_type.h"

namespace oceanbase
{
namespace pl
{

// SQL state owned by a MySQL prepared-statement server cursor.
class ObPLServerCursorInfo : public ObPLCursorInfo
{
public:
  ObPLServerCursorInfo()
    : ObPLCursorInfo(true),
      stmt_type_(sql::stmt::T_NONE),
      sql_entity_(nullptr),
      ps_sql_(),
      exec_params_(),
      fields_()
  {}
  ~ObPLServerCursorInfo() override { reset(); }

  int close(sql::ObSQLSessionInfo &session, bool is_reuse = false) override;
  void reset();
  int prepare_entity(sql::ObSQLSessionInfo &session);
  int init_params(int64_t param_count);

  lib::MemoryContext &get_sql_entity() { return sql_entity_; }

  const common::ObString &get_ps_sql() const { return ps_sql_; }
  void set_ps_sql(const common::ObString &sql) { ps_sql_ = sql; }
  sql::stmt::StmtType get_stmt_type() const { return stmt_type_; }
  void set_stmt_type(sql::stmt::StmtType type) { stmt_type_ = type; }
  common::ParamStore &get_exec_params() { return exec_params_; }
  common::ColumnsFieldArray &get_field_columns() { return fields_; }

  static int deep_copy_field_columns(
      common::ObIAllocator &allocator,
      const common::ColumnsFieldIArray *src_fields,
      common::ColumnsFieldArray &dst_fields);

private:
  sql::stmt::StmtType stmt_type_;
  lib::MemoryContext sql_entity_;
  common::ObString ps_sql_;
  common::ParamStore exec_params_;
  common::ColumnsFieldArray fields_;
};

} // namespace pl
} // namespace oceanbase

#endif // OCEANBASE_SQL_PL_OB_PL_SERVER_CURSOR_H_
