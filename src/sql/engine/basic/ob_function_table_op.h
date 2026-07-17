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

#ifndef OCEANBASE_BASIC_OB_FUNCTION_TABLE_OP_H_
#define OCEANBASE_BASIC_OB_FUNCTION_TABLE_OP_H_

#include "sql/engine/ob_operator.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "lib/charset/ob_charset.h"

namespace oceanbase
{
namespace sql
{

class ObExpr;
class ObFunctionTableSpec : public ObOpSpec
{
OB_UNIS_VERSION_V(1);
public:
  ObFunctionTableSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type), value_expr_(nullptr), column_exprs_(alloc), has_correlated_expr_(false)
  {}
  ObExpr *value_expr_;
  common::ObFixedArray<ObExpr*, common::ObIAllocator> column_exprs_;
  bool has_correlated_expr_;
};

class ObFunctionTableOp : public ObOperator
{
public:
  ObFunctionTableOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input),
    node_idx_(0),
    already_calc_(false),
    row_count_(0),
    col_count_(0),
    value_table_(NULL),
    split_chunk_idx_(0),
    split_initialized_(false),
    split_chunks_()
  {}

  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int switch_iterator() override;
  virtual int inner_get_next_row() override;
  //virtual int inner_get_next_batch(int64_t max_row_cnt) override;
  virtual int inner_close() override;
  virtual void destroy() override;
private:
  struct SplitDocumentOptions
  {
    SplitDocumentOptions()
      : is_markdown_(true), by_sentence_(false), max_units_(256), overlap_(0)
    {}
    bool is_markdown_;
    bool by_sentence_;
    int64_t max_units_;
    int64_t overlap_;
  };
  struct DocumentUnit
  {
    DocumentUnit() : start_(0), end_(0) {}
    DocumentUnit(int64_t start, int64_t end) : start_(start), end_(end) {}
    int64_t start_;
    int64_t end_;
    TO_STRING_KV(K_(start), K_(end));
  };
  struct SplitDocumentChunk
  {
    SplitDocumentChunk() : id_(0), offset_(0), length_(0), text_() {}
    int64_t id_;
    int64_t offset_;
    int64_t length_;
    common::ObString text_;
    TO_STRING_KV(K_(id), K_(offset), K_(length), K_(text));
  };
  int inner_get_next_row_udf();
  int inner_get_next_row_sys_func();
  int inner_get_next_row_ai_split_document();
  int init_split_document();
  int parse_split_options(const common::ObString &json_text,
                          SplitDocumentOptions &options);
  int split_document(const common::ObString &content,
                     const SplitDocumentOptions &options);
  int split_document_range(const common::ObString &content,
                           int64_t range_start,
                           int64_t range_end,
                           const common::ObString &heading,
                           const SplitDocumentOptions &options);
  int build_document_units(const common::ObString &content,
                           int64_t range_start,
                           int64_t range_end,
                           bool by_sentence,
                           common::ObIArray<DocumentUnit> &units);
  int append_split_chunk(const common::ObString &content,
                         const common::ObString &heading,
                         const DocumentUnit &first_unit,
                         const DocumentUnit &last_unit);
  int get_current_result(common::ObObj &result);
  int64_t node_idx_;
  bool already_calc_;
  int64_t row_count_;
  int64_t col_count_;
  common::ObObj value_;
  pl::ObPLCollection *value_table_;
  int64_t split_chunk_idx_;
  bool split_initialized_;
  common::ObSEArray<SplitDocumentChunk, 16> split_chunks_;
  int (ObFunctionTableOp::*next_row_func_)();
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_BASIC_OB_FUNCTION_TABLE_OP_H_ */
