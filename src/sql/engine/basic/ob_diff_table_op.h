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

#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_DIFF_TABLE_OP_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_DIFF_TABLE_OP_H_

#include "sql/engine/ob_operator.h"
#include "lib/container/ob_fixed_array.h"
#include "lib/container/ob_se_array.h"
#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include "common/object/ob_object.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace sql
{

// Per-output-column descriptor frozen at compile time (CG). Drives both
// the runtime storage-column lookup and the ObObj cell rendering.
// Collection columns are returned to clients with their true SQL type
// (e.g. VECTOR / ARRAY) — the subschema id is resolved at CG time and
// stored here so the op can attach it to emitted cells; the protocol
// then renders via the standard UDT helper path.
struct ObDiffOutColMeta
{
  enum Kind { K_TABLE = 0, K_FLAG = 1, K_PK = 2, K_VAL = 3 };
  ObDiffOutColMeta()
    : kind_(K_TABLE),
      col_id_(common::OB_INVALID_ID),
      obj_type_(common::ObNullType),
      collation_type_(common::CS_TYPE_INVALID),
      length_(0),
      subschema_id_(UINT16_MAX) {}
  int32_t kind_;             // ObDiffOutColMeta::Kind
  uint64_t col_id_;          // schema column id for K_PK / K_VAL
  common::ObObjType obj_type_;
  common::ObCollationType collation_type_;
  int32_t length_;
  uint16_t subschema_id_;    // valid only for collection columns
  TO_STRING_KV(K_(kind), K_(col_id), K_(obj_type), K_(subschema_id));
  OB_UNIS_VERSION(1);
};

class ObDiffTableSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObDiffTableSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
      tenant_id_(common::OB_INVALID_TENANT_ID),
      cur_table_id_(common::OB_INVALID_ID),
      inc_table_id_(common::OB_INVALID_ID),
      cur_db_name_(),
      cur_table_name_(),
      inc_db_name_(),
      inc_table_name_(),
      pk_col_ids_(alloc),
      val_col_ids_(alloc),
      out_col_metas_(alloc)
  {}

  uint64_t tenant_id_;
  uint64_t cur_table_id_;
  uint64_t inc_table_id_;
  common::ObString cur_db_name_;
  common::ObString cur_table_name_;
  common::ObString inc_db_name_;
  common::ObString inc_table_name_;
  common::ObFixedArray<uint64_t, common::ObIAllocator> pk_col_ids_;
  common::ObFixedArray<uint64_t, common::ObIAllocator> val_col_ids_;
  common::ObFixedArray<ObDiffOutColMeta, common::ObIAllocator> out_col_metas_;
};

class ObDiffTableOp : public ObOperator
{
public:
  ObDiffTableOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);
  virtual ~ObDiffTableOp() {}

  virtual int inner_open() override;
  virtual int inner_get_next_row() override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual void destroy() override;

private:
  // Compute the full diff result into rows_ (bulk-at-open).
  int compute_();
  // Project rows_[cursor_] into MY_SPEC.output_ datums.
  int project_current_row_();

private:
  common::ObArenaAllocator op_alloc_;
  common::ObSEArray<common::ObNewRow *, 64> rows_;
  int64_t cursor_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_BASIC_OB_DIFF_TABLE_OP_H_
