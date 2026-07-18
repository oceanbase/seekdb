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
#include "lib/container/ob_se_array.h"

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
    next_row_func_(nullptr),
    ai_split_alloc_("AISplitDocument"),
    ai_split_chunks_(),
    ai_split_content_(),
    ai_split_next_idx_(0),
    ai_split_inited_(false)
  {}

  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int switch_iterator() override;
  virtual int inner_get_next_row() override;
  //virtual int inner_get_next_batch(int64_t max_row_cnt) override;
  virtual int inner_close() override;
  virtual void destroy() override;
private:
  struct AISplitRange
  {
    AISplitRange() : start_(0), end_(0) {}
    AISplitRange(int64_t start, int64_t end) : start_(start), end_(end) {}
    int64_t start_;
    int64_t end_;
  };

  struct AISplitChunk
  {
    AISplitChunk() : chunk_id_(0), chunk_offset_(0), chunk_length_(0), chunk_text_() {}
    int64_t chunk_id_;
    int64_t chunk_offset_;
    int64_t chunk_length_;
    common::ObString chunk_text_;
  };

  struct AISplitParams
  {
    AISplitParams()
      : type_(common::ObString::make_string("markdown")),
        by_(common::ObString::make_string("word")),
        max_(256),
        overlap_(0)
    {}
    common::ObString type_;
    common::ObString by_;
    int64_t max_;
    int64_t overlap_;
  };

  int inner_get_next_row_udf();
  int inner_get_next_row_sys_func();
  int inner_get_next_row_ai_split_document();
  int get_current_result(common::ObObj &result);
  bool is_ai_split_document() const;
  void reset_ai_split_document_state();
  int init_ai_split_document();
  int parse_ai_split_document_params(common::ObIAllocator &allocator,
                                     const ObExpr &expr,
                                     AISplitParams &params);
  int build_ai_split_document_chunks(const AISplitParams &params);
  int build_ai_split_document_word_chunks(const AISplitParams &params);
  int build_ai_split_document_sentence_chunks(const AISplitParams &params);
  int build_ai_split_document_markdown_chunks(const AISplitParams &params);
  int build_ai_split_document_markdown_word_chunks(const AISplitParams &params,
                                                   const common::ObString &heading,
                                                   const int64_t body_start,
                                                   const int64_t body_end);
  int build_ai_split_document_markdown_sentence_chunks(const AISplitParams &params,
                                                       const common::ObString &heading,
                                                       const int64_t body_start,
                                                       const int64_t body_end);
  int add_ai_split_document_chunk(const int64_t start, const int64_t end);
  int add_ai_split_document_chunk(const int64_t start,
                                  const int64_t end,
                                  const common::ObString &chunk_text);
  int add_ai_split_document_markdown_chunk(const common::ObString &heading,
                                           const int64_t start,
                                           const int64_t end);
  static bool is_ai_split_ascii_space(const char c);
  static bool is_ai_split_markdown_heading(const char *line,
                                           const int64_t len,
                                           common::ObString &heading);
  static bool is_ai_split_sentence_terminator(const char *ptr, const int64_t len);
  static int64_t get_ai_split_utf8_char_len(const char *ptr, const int64_t remain);
  int64_t node_idx_;
  bool already_calc_;
  int64_t row_count_;
  int64_t col_count_;
  common::ObObj value_;
  pl::ObPLCollection *value_table_;
  int (ObFunctionTableOp::*next_row_func_)();
  common::ObArenaAllocator ai_split_alloc_;
  common::ObSEArray<AISplitChunk, 16> ai_split_chunks_;
  common::ObString ai_split_content_;
  int64_t ai_split_next_idx_;
  bool ai_split_inited_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_BASIC_OB_FUNCTION_TABLE_OP_H_ */
