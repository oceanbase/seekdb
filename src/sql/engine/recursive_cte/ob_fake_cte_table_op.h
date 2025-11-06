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

#ifndef _OB_FAKE_CTE_TABLE_OP_H
#define _OB_FAKE_CTE_TABLE_OP_H 1

#include "lib/container/ob_array.h"
#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include "ob_search_method_op.h"
#include "sql/engine/ob_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"

namespace oceanbase
{
namespace sql
{

class ObExecContext;
class ObFakeCTETableSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  explicit ObFakeCTETableSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
      : ObOpSpec(alloc, type), column_involved_offset_(alloc), column_involved_exprs_(alloc),
        is_bulk_search_(false), is_union_distinct_(false)
  {
  }

  INHERIT_TO_STRING_KV("op_spec", ObOpSpec,
      K_(column_involved_offset), K_(column_involved_exprs),
      K_(is_bulk_search), K_(is_union_distinct));

  virtual ~ObFakeCTETableSpec() {}
  // Array index refers to the index inside output_, array content is the offset position of the original column in the cte table
  common::ObFixedArray<int64_t, common::ObIAllocator> column_involved_offset_;
  common::ObFixedArray<ObExpr *, common::ObIAllocator> column_involved_exprs_;
  //for breadth search first
  bool is_bulk_search_;
  bool is_union_distinct_;
};

class ObFakeCTETableOp : public ObOperator
{
public:
  explicit ObFakeCTETableOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
      : ObOperator(exec_ctx, spec, input),
        empty_(false),
        pump_row_(nullptr),
        allocator_(nullptr),
        read_bluk_cnt_(0),
        bulk_rows_(),
        mem_context_(nullptr)
    {
    }
  common::ObArray<ObChunkDatumStore::StoredRow *> &get_bulk_rows() { return bulk_rows_; };
  void reuse();
  virtual void destroy();
  virtual int inner_rescan() override;
  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  inline void set_empty() { empty_ = true; }
  inline void update_status();
  int get_next_single_row();
  int get_next_bulk_row();
  int get_next_single_batch(const int64_t max_row_cnt);
  int get_next_bulk_batch(const int64_t max_row_cnt);
  int add_single_row(ObChunkDatumStore::StoredRow *row);
  int copy_datums(ObChunkDatumStore::StoredRow *row, common::ObDatum *datums,
                  int64_t cnt, const common::ObIArray<int64_t> &chosen_datums,
                  char *buf, const int64_t size, const int64_t row_size,
                  const uint32_t row_extend_size);
  // Copy cells included in chosen_index from src_row to dst_row
  int deep_copy_row(const ObChunkDatumStore::StoredRow *src_row,
                    const ObChunkDatumStore::StoredRow *&dst_row,
                    const common::ObIArray<int64_t> &chosen_index,
                    int64_t extra_size,
                    common::ObIAllocator &allocator);
  int to_expr(const common::ObIArray<ObExpr*> &exprs,
              const common::ObIArray<int64_t> &chosen_datums,
              ObChunkDatumStore::StoredRow *row, ObEvalCtx &ctx);
  int attach_rows(const common::ObIArray<ObExpr*> &exprs,
                  const common::ObIArray<int64_t > &chosen_index,
                  const common::ObArray<ObChunkDatumStore::StoredRow *> &srows,
                  const int64_t rows_offset, ObEvalCtx &ctxm, const int64_t read_rows);
  const static int64_t ROW_EXTRA_SIZE = 0;

private:
  bool empty_;
  const ObChunkDatumStore::StoredRow* pump_row_;
  ObIAllocator *allocator_;
  // for batch search recursive cte
  int64_t read_bluk_cnt_;
  common::ObArray<ObChunkDatumStore::StoredRow *> bulk_rows_;
  lib::MemoryContext mem_context_;
};

void ObFakeCTETableOp::update_status()
{
  read_bluk_cnt_ = 0;
  if (MY_SPEC.column_involved_offset_.empty()) {
    empty_ = false;
  } else if (!bulk_rows_.empty()) {
    empty_ = false;
  }
}


} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_FAKE_CTE_TABLE_OP_H */
