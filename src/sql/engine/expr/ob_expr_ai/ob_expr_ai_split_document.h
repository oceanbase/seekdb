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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_SPLIT_DOCUMENT_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_SPLIT_DOCUMENT_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
// AI_SPLIT_DOCUMENT 表函数运行期上下文：
// 首次 eval 时解析 content 与 params_json，切分得到全部 chunk 数组；
// 之后每次 eval 推进一行，将当前行的 4 列写入 cur_* 字段供执行器读取。
class ObExprAISplitDocumentCtx : public ObExprOperatorCtx
{
public:
  ObExprAISplitDocumentCtx()
    : inited_(false), row_idx_(0), chunk_cnt_(0),
      chunk_ids_(NULL), chunk_offsets_(NULL), chunk_lengths_(NULL), chunk_texts_(NULL),
      cur_id_(0), cur_offset_(0), cur_length_(0)
  {
  }
  virtual ~ObExprAISplitDocumentCtx() {}
  bool inited_;
  int64_t row_idx_;
  int64_t chunk_cnt_;
  int64_t *chunk_ids_;
  int64_t *chunk_offsets_;
  int64_t *chunk_lengths_;
  ObString *chunk_texts_;
  // 当前行输出（由 eval 填充，执行器读取）
  int64_t cur_id_;
  int64_t cur_offset_;
  int64_t cur_length_;
  ObString cur_text_;
  DISALLOW_COPY_AND_ASSIGN(ObExprAISplitDocumentCtx);
};

class ObExprAISplitDocument : public ObFuncExprOperator
{
public:
  explicit ObExprAISplitDocument(common::ObIAllocator &alloc);
  virtual ~ObExprAISplitDocument();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &content,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &content,
                                ObExprResType &params,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual bool need_rt_ctx() const override { return true; }
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_split_document(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  // 将 content 按参数切分为 chunk 数组并写入 ctx
  static int do_split(common::ObIAllocator &allocator,
                      const common::ObString &content,
                      bool is_markdown,
                      bool is_word,
                      int64_t max_cnt,
                      int64_t overlap,
                      ObExprAISplitDocumentCtx &out);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprAISplitDocument);
};
}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_SPLIT_DOCUMENT_ */
