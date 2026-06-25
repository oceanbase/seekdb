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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_DIFF_TABLE_STMT_H_
#define OCEANBASE_SQL_RESOLVER_CMD_OB_DIFF_TABLE_STMT_H_

#include "lib/string/ob_string.h"
#include "lib/container/ob_se_array.h"
#include "common/object/ob_object.h"
#include "sql/resolver/dml/ob_dml_stmt.h"

namespace oceanbase
{
namespace sql
{

// Per-output-column descriptor frozen at resolve time. Drives both the
// runtime row rendering (in the compute helper) and the CG generation
// of typed output exprs. Collection (vector/array) columns are exposed
// to clients as VARCHAR; the runtime renders them as their string form.
struct ObDiffOutputCol
{
  ObDiffOutputCol() : name_(), obj_type_(common::ObNullType),
      collation_type_(common::CS_TYPE_INVALID), length_(0),
      is_synth_(false), is_pk_(false), col_id_(common::OB_INVALID_ID),
      subschema_id_(UINT16_MAX) {}
  common::ObString name_;
  common::ObObjType obj_type_;
  common::ObCollationType collation_type_;
  int32_t length_;
  bool is_synth_;
  bool is_pk_;
  uint64_t col_id_;
  uint16_t subschema_id_;   // valid only when obj_type_ is a collection type
  TO_STRING_KV(K_(name), K_(obj_type), K_(length), K_(is_pk),
               K_(col_id), K_(subschema_id));
};

// DIFF TABLE statement. Extends ObDMLStmt so it flows through the
// regular plan / SELECT pipeline. No rows are precomputed here: the
// resolver only captures schema-bound metadata; row production happens
// at execute time inside ObDiffTableOp.
class ObDiffTableStmt : public ObDMLStmt
{
public:
  ObDiffTableStmt()
    : ObDMLStmt(stmt::T_DIFF_TABLE),
      tenant_id_(common::OB_INVALID_TENANT_ID),
      cur_db_id_(common::OB_INVALID_ID),
      cur_table_id_(common::OB_INVALID_ID),
      inc_db_id_(common::OB_INVALID_ID),
      inc_table_id_(common::OB_INVALID_ID)
  {}
  virtual ~ObDiffTableStmt() = default;

  void set_tenant_id(uint64_t t) { tenant_id_ = t; }
  uint64_t get_tenant_id() const { return tenant_id_; }
  void set_cur_table_id(uint64_t v) { cur_table_id_ = v; }
  uint64_t get_cur_table_id() const { return cur_table_id_; }
  void set_inc_table_id(uint64_t v) { inc_table_id_ = v; }
  uint64_t get_inc_table_id() const { return inc_table_id_; }
  void set_cur_db_id(uint64_t v) { cur_db_id_ = v; }
  uint64_t get_cur_db_id() const { return cur_db_id_; }
  void set_inc_db_id(uint64_t v) { inc_db_id_ = v; }
  uint64_t get_inc_db_id() const { return inc_db_id_; }
  void set_cur_db(const common::ObString &s) { cur_db_ = s; }
  const common::ObString &get_cur_db() const { return cur_db_; }
  void set_cur_table(const common::ObString &s) { cur_table_ = s; }
  const common::ObString &get_cur_table() const { return cur_table_; }
  void set_inc_db(const common::ObString &s) { inc_db_ = s; }
  const common::ObString &get_inc_db() const { return inc_db_; }
  void set_inc_table(const common::ObString &s) { inc_table_ = s; }
  const common::ObString &get_inc_table() const { return inc_table_; }
  common::ObIArray<common::ObString> &pk_cols() { return pk_cols_; }
  const common::ObIArray<common::ObString> &pk_cols() const { return pk_cols_; }
  common::ObIArray<common::ObString> &val_cols() { return val_cols_; }
  const common::ObIArray<common::ObString> &val_cols() const { return val_cols_; }
  common::ObIArray<ObDiffOutputCol> &out_cols() { return out_cols_; }
  const common::ObIArray<ObDiffOutputCol> &out_cols() const { return out_cols_; }

  TO_STRING_KV(K_(stmt_type), K_(tenant_id), K_(cur_table_id), K_(inc_table_id),
               K_(cur_db), K_(cur_table), K_(inc_db), K_(inc_table),
               K_(pk_cols), K_(val_cols));

private:
  uint64_t tenant_id_;
  uint64_t cur_db_id_;
  uint64_t cur_table_id_;
  uint64_t inc_db_id_;
  uint64_t inc_table_id_;
  common::ObString cur_db_;
  common::ObString cur_table_;
  common::ObString inc_db_;
  common::ObString inc_table_;
  common::ObSEArray<common::ObString, 4>  pk_cols_;
  common::ObSEArray<common::ObString, 16> val_cols_;
  common::ObSEArray<ObDiffOutputCol, 20>  out_cols_;
  DISALLOW_COPY_AND_ASSIGN(ObDiffTableStmt);
};

} // namespace sql
} // namespace oceanbase
#endif
