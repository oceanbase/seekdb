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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_PARSE_DOC_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_PARSE_DOC_

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "ob_ai_func_client.h"
#include "ob_ai_func.h"
#include "ob_ai_func_utils.h"

namespace oceanbase
{
namespace sql
{
// AI_PARSE_DOC: ai_parse_doc(model_name, context [, params_json]) -> LONGTEXT
//
// 语义：复用 DBMS_AI_SERVICE 的 completion 通道，把文档字节（PDF / 图片）
// 送入视觉模型，识别并提取文档中的文本内容。
//   - model_name：DBMS_AI_SERVICE.CREATE_AI_MODEL 注册的 completion 模型名
//   - context：load_file() 返回的文档字节（PDF，或 png/jpg 等图片字节）
//   - params_json（可选）：{"input_type":"pdf"}，input_type 缺省按 "pdf" 处理
//
// PDF 光栅化依赖 pdfium（当前仓库未链接）：
//   - 构建时定义 OB_AI_PARSE_DOC_HAVE_PDFIUM 启用 pdfium 光栅化分支；
//   - 未启用时 PDF 输入返回清晰的 OB_NOT_SUPPORTED 错误；
//   - 图片输入（png/jpg/jpeg/webp/bmp）无需 pdfium，直接走 base64 data URI
//     完整链路。
class ObExprAIParseDoc : public ObFuncExprOperator
{
public:
  explicit ObExprAIParseDoc(common::ObIAllocator &alloc);
  virtual ~ObExprAIParseDoc();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_ai_parse_doc(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  virtual bool need_rt_ctx() const override { return true; }
private:
  // 解析 params_json 扁平对象，提取 input_type 字段（缺省 "pdf"）
  static int parse_input_type(const common::ObString &params_json,
                              common::ObString &input_type);
  // 将文档字节转为 OpenAI vision 用的 data URI
  //   - input_type=pdf: 走 pdfium 光栅化（OB_AI_PARSE_DOC_HAVE_PDFIUM）
  //   - 其余图片类型: 直接 base64 编码
  static int rasterize_doc_to_base64(common::ObIAllocator &allocator,
                                     const common::ObString &input_type,
                                     const common::ObString &doc_bytes,
                                     common::ObString &data_uri);
  // 构造 OpenAI vision 格式请求 body（含 image_url + data URI）
  static int build_vision_body(common::ObIAllocator &allocator,
                               const common::ObString &request_model_name,
                               const common::ObString &image_data_uri,
                               common::ObJsonObject *&body);
  static constexpr int MODEL_IDX = 0;
  static constexpr int CONTEXT_IDX = 1;
  static constexpr int PARAMS_IDX = 2;
  static constexpr const char *DEFAULT_DOC_PROMPT =
      "Please recognize and extract all text content from the document/image. "
      "Output the text verbatim preserving the original structure, without any extra explanation.";
  DISALLOW_COPY_AND_ASSIGN(ObExprAIParseDoc);
};
}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_PARSE_DOC_ */
