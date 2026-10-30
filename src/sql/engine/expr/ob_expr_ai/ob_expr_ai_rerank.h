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

#ifndef OCEANBASE_SQL_OB_EXPR_AI_RERANK_H_
#define OCEANBASE_SQL_OB_EXPR_AI_RERANK_H_
 
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "ob_ai_func_client.h"
#include "ob_ai_func.h"
#include "ob_ai_func_utils.h"
 
namespace oceanbase 
{
namespace sql 
{

// One element of the model response, kept with its position inside the
// response array so that the sorted order can be applied afterwards.
struct ObRerankSortItem
{
  int64_t pos_;
  int64_t index_;
  double score_;
  bool operator<(const ObRerankSortItem &other) const
  {
    if (score_ != other.score_) {
      return score_ > other.score_;
    }
    // stable tie break: keep the original document order
    return index_ < other.index_;
  }
};

class ObExprAIRerank : public ObFuncExprOperator 
{
public:
  explicit ObExprAIRerank(common::ObIAllocator &alloc);
  virtual ~ObExprAIRerank();
  virtual int calc_result_typeN(ObExprResType &type, ObExprResType *types_array,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_ai_rerank(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  virtual bool need_rt_ctx() const override { return true; }
 
private:
  static int inner_eval_ai_rerank(common::ObIAllocator &allocator, 
                                  const common::ObAIFuncExprInfo &info, 
                                  const share::ObAiModelEndpointInfo &endpoint_info,
                                  common::ObArray<common::ObString> &header_array, 
                                  common::ObString &query, 
                                  common::ObJsonArray *document_array, 
                                  common::ObJsonArray *&result_array);
  
  static int eval_ai_rerank_with_doc_key(const ObExpr &expr, ObEvalCtx &ctx, common::ObIAllocator &allocator,
                                         common::ObString &model_id, common::ObString &query, common::ObJsonArray *document_array,
                                         common::ObString &doc_key, const common::ObAIFuncExprInfo &info, const share::ObAiModelEndpointInfo &endpoint_info, ObDatum &res);
  static int get_doc_array_from_documents_array_with_key(ObIAllocator &allocator, ObJsonArray *document_object_array, ObString &doc_key, ObJsonArray *&doc_array);
  static int construct_config_json(common::ObIAllocator &allocator, int64_t top_k, int64_t return_doc, common::ObJsonObject *&config_json);
  static int construct_batch_document_array(common::ObIAllocator &allocator, common::ObJsonArray *document_array, int64_t start_idx, int64_t end_idx, common::ObJsonArray *&batch_document_array);
  static int batch_result_add_base(common::ObIAllocator &allocator, common::ObJsonArray *array, int64_t start_idx);
  // Send the documents batch by batch and merge every response into one array,
  // where each element keeps its global index inside the original documents.
  static int collect_rerank_result(common::ObIAllocator &allocator,
                                   const common::ObAIFuncExprInfo &info,
                                   const share::ObAiModelEndpointInfo &endpoint_info,
                                   common::ObArray<common::ObString> &header_array,
                                   common::ObString &query,
                                   common::ObJsonArray *document_array,
                                   common::ObJsonArray *&result_array);
  static int sort_model_result_by_score(common::ObIAllocator &allocator,
                                        common::ObJsonArray *model_result_array,
                                        const int64_t document_count,
                                        common::ObArray<ObRerankSortItem> &sorted_items);
  static int build_sorted_document_array(common::ObIAllocator &allocator,
                                         common::ObJsonArray *document_array,
                                         common::ObJsonArray *model_result_array,
                                         common::ObArray<ObRerankSortItem> &sorted_items,
                                         common::ObJsonArray *&sorted_document_array);
  static constexpr int RERANK_BATCH_SIZE = 20;
  static constexpr int MODEL_IDX = 0;
  static constexpr int QUERY_IDX = 1;
  static constexpr int DOCUMENTS_IDX = 2;
  static constexpr int DOC_KEY_IDX = 3;
  static constexpr char MODEL_SCORE_KEY[20] = "_model_score";
  static constexpr char INDEX_KEY[20] = "index";
  static constexpr char SCORE_KEY[20] = "relevance_score";
  DISALLOW_COPY_AND_ASSIGN(ObExprAIRerank);
};

} // namespace sql
} // namespace oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_AI_RERANK_H_
  
