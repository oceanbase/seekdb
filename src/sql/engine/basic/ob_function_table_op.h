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
#include "lib/allocator/page_arena.h"

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
    split_allocator_("AISplitDoc"),
    split_chunks_(),
    split_content_()
  {}

  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int switch_iterator() override;
  virtual int inner_get_next_row() override;
  //virtual int inner_get_next_batch(int64_t max_row_cnt) override;
  virtual int inner_close() override;
  virtual void destroy() override;
private:
  enum class SplitType : int8_t { TEXT, MARKDOWN };
  enum class SplitBy : int8_t { WORD, SENTENCE };
  struct SplitParam
  {
    SplitParam() : type_(SplitType::MARKDOWN), by_(SplitBy::WORD), max_(256), overlap_(0) {}
    SplitType type_;
    SplitBy by_;
    int64_t max_;
    int64_t overlap_;
  };
  struct SplitUnit
  {
    SplitUnit() : offset_(0), length_(0) {}
    SplitUnit(int64_t offset, int64_t length) : offset_(offset), length_(length) {}
    int64_t offset_;
    int64_t length_;
    TO_STRING_KV(K_(offset), K_(length));
  };
  struct SplitChunk
  {
    SplitChunk() : offset_(0), length_(0), text_() {}
    int64_t offset_;
    int64_t length_;
    common::ObString text_;
    TO_STRING_KV(K_(offset), K_(length), K_(text));
  };

  int inner_get_next_row_udf();
  int inner_get_next_row_sys_func();
  int inner_get_next_row_ai_split_document();
  int init_ai_split_document();
  int parse_split_param(const common::ObString &param_str, SplitParam &param);
  int split_text_range(const int64_t begin,
                       const int64_t end,
                       const common::ObString &heading,
                       const SplitParam &param);
  int split_markdown(const SplitParam &param);
  int add_split_chunks(const common::ObIArray<SplitUnit> &units,
                       const common::ObString &heading,
                       const SplitParam &param);
  void reset_ai_split_document();
  static bool is_split_space(const char ch);
  static bool is_markdown_heading(const common::ObString &content,
                                  const int64_t begin,
                                  const int64_t end,
                                  int64_t &heading_begin);
  int get_current_result(common::ObObj &result);
  int64_t node_idx_;
  bool already_calc_;
  int64_t row_count_;
  int64_t col_count_;
  common::ObObj value_;
  pl::ObPLCollection *value_table_;
  int (ObFunctionTableOp::*next_row_func_)();
  common::ObArenaAllocator split_allocator_;
  common::ObSEArray<SplitChunk, 16> split_chunks_;
  common::ObString split_content_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_BASIC_OB_FUNCTION_TABLE_OP_H_ */
