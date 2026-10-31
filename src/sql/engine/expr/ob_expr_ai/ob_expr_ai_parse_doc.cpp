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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_expr_ai_parse_doc.h"
#include "share/rc/ob_module_provider.h"
#include "observer/omt/ob_tenant_ai_service.h"
#include "lib/encode/ob_base64_encode.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprAIParseDoc::ObExprAIParseDoc(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_AI_PARSE_DOC,
                         N_AI_PARSE_DOC,
                         2, /* 最小参数个数：model_name + context，params_json 可选 */
                         NOT_VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{
}

ObExprAIParseDoc::~ObExprAIParseDoc()
{
}

int ObExprAIParseDoc::calc_result_typeN(ObExprResType &type,
                                        ObExprResType *types_stack,
                                        int64_t param_num,
                                        common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num < 2 || param_num > 3)) {
    ObString func_name_(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    types_stack[MODEL_IDX].set_calc_type(ObVarcharType);
    types_stack[MODEL_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (ob_is_string_type(types_stack[CONTEXT_IDX].get_type())) {
      // context 保持原类型（load_file 返回 ObLongTextType，可能为大文档），
      // eval 阶段用 read_real_string_data 读取真实数据
      types_stack[CONTEXT_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("invalid param type for AI_PARSE_DOC", K(ret), K(types_stack[CONTEXT_IDX]));
    }
    if (OB_FAIL(ret)) {
    } else if (param_num == 3) {
      if (OB_UNLIKELY(!ob_is_string_type(types_stack[PARAMS_IDX].get_type()))) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
        LOG_WARN("invalid params type for AI_PARSE_DOC", K(ret), K(types_stack[PARAMS_IDX]));
      } else {
        types_stack[PARAMS_IDX].set_calc_type(ObVarcharType);
        types_stack[PARAMS_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
    }
    if (OB_SUCC(ret)) {
      type.set_type(ObLongTextType);
      type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      type.set_collation_level(CS_LEVEL_IMPLICIT);
      type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
    }
  }
  return ret;
}

int ObExprAIParseDoc::parse_input_type(const common::ObString &params_json,
                                       common::ObString &input_type)
{
  int ret = OB_SUCCESS;
  // 缺省按 "pdf" 处理
  input_type.assign_ptr("pdf", 3);
  const char *p = params_json.ptr();
  int64_t len = params_json.length();
  const char *key = "input_type";
  int64_t key_len = static_cast<int64_t>(strlen(key));
  // 仅支持 {"key":"value"} 扁平对象（与 ai_split_document 的 params 解析同风格）
  for (int64_t i = 0; OB_SUCC(ret) && i + key_len < len; ++i) {
    if (p[i] == '"' && 0 == strncmp(p + i + 1, key, key_len) && p[i + key_len + 1] == '"') {
      int64_t j = i + key_len + 1;
      if (j < len && p[j] == '"') {
        ++j; // 跳过 key 后的引号
      }
      while (j < len && (p[j] == ' ' || p[j] == '\t' || p[j] == ':')) {
        ++j;
      }
      if (j < len && p[j] == '"') {
        int64_t start = ++j;
        while (j < len && p[j] != '"') {
          ++j;
        }
        if (OB_UNLIKELY(j == len)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid params json, unterminated string", K(ret), K(params_json));
        } else {
          input_type.assign_ptr(p + start, static_cast<int32_t>(j - start));
        }
      }
      break;
    }
  }
  return ret;
}

// 将字节 base64 编码为 data URI（OpenAI vision 格式：data:<mime>;base64,...）
static int base64_encode_to_data_uri(common::ObIAllocator &allocator,
                                     const char *mime,
                                     const common::ObString &bytes,
                                     common::ObString &data_uri)
{
  int ret = OB_SUCCESS;
  int64_t enc_len = ObBase64Encoder::needed_encoded_length(bytes.length());
  char *enc_buf = static_cast<char *>(allocator.alloc(enc_len + 1));
  if (OB_ISNULL(enc_buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc base64 buffer", K(ret), K(enc_len));
  } else {
    int64_t pos = 0;
    if (OB_FAIL(ObBase64Encoder::encode(reinterpret_cast<const uint8_t *>(bytes.ptr()),
                                        bytes.length(), enc_buf, enc_len, pos, 0))) {
      LOG_WARN("fail to base64 encode", K(ret));
    } else {
      enc_buf[pos] = '\0';
      ObStringBuffer buf(&allocator);
      if (OB_FAIL(buf.append("data:"))) {
        LOG_WARN("fail to append data uri", K(ret));
      } else if (OB_FAIL(buf.append(mime))) {
        LOG_WARN("fail to append mime", K(ret));
      } else if (OB_FAIL(buf.append(";base64,"))) {
        LOG_WARN("fail to append base64 prefix", K(ret));
      } else if (OB_FAIL(buf.append(enc_buf, pos))) {
        LOG_WARN("fail to append base64 data", K(ret));
      } else if (OB_ISNULL(data_uri = buf.string())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get data uri string", K(ret));
      }
    }
  }
  return ret;
}

#ifdef OB_AI_PARSE_DOC_HAVE_PDFIUM
// pdfium 光栅化实现（仅构建定义 OB_AI_PARSE_DOC_HAVE_PDFIUM 时编译）。
// 接入步骤：链接 pdfium（deps/init/*.deps + CMake），在此把 PDF 字节光栅化为
// PNG 位图字节，再调用 base64_encode_to_data_uri 生成 data URI。
static int rasterize_pdf_with_pdfium(common::ObIAllocator &allocator,
                                     const common::ObString &pdf_bytes,
                                     common::ObString &data_uri)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("pdfium rasterization is not implemented yet", K(ret));
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "ai_parse_doc, pdfium rasterization not implemented");
  return ret;
}
#endif

int ObExprAIParseDoc::rasterize_doc_to_base64(common::ObIAllocator &allocator,
                                              const common::ObString &input_type,
                                              const common::ObString &doc_bytes,
                                              common::ObString &data_uri)
{
  int ret = OB_SUCCESS;
  bool is_pdf = (input_type.length() >= 3 && 0 == input_type.case_compare("pdf"));
  if (is_pdf) {
#ifdef OB_AI_PARSE_DOC_HAVE_PDFIUM
    if (OB_FAIL(rasterize_pdf_with_pdfium(allocator, doc_bytes, data_uri))) {
      LOG_WARN("fail to rasterize pdf", K(ret));
    }
#else
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("pdfium is not linked, cannot parse pdf input", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED,
                   "ai_parse_doc, pdf input requires pdfium rasterization; "
                   "build with OB_AI_PARSE_DOC_HAVE_PDFIUM to enable, "
                   "or pass png/jpg image bytes as context");
#endif
  } else {
    // 图片输入：直接 base64 编码为 data URI（无需 pdfium）
    const char *mime = NULL;
    if (input_type.empty() || 0 == input_type.case_compare("png")) {
      mime = "image/png";
    } else if (0 == input_type.case_compare("jpg") || 0 == input_type.case_compare("jpeg")) {
      mime = "image/jpeg";
    } else if (0 == input_type.case_compare("webp")) {
      mime = "image/webp";
    } else if (0 == input_type.case_compare("bmp")) {
      mime = "image/bmp";
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("unsupported input_type", K(ret), K(input_type));
      LOG_USER_ERROR(OB_NOT_SUPPORTED,
                     "ai_parse_doc, unsupported input_type, only pdf/png/jpg/jpeg/webp/bmp");
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(base64_encode_to_data_uri(allocator, mime, doc_bytes, data_uri))) {
        LOG_WARN("fail to encode image data uri", K(ret));
      }
    }
  }
  return ret;
}

int ObExprAIParseDoc::build_vision_body(common::ObIAllocator &allocator,
                                        const common::ObString &request_model_name,
                                        const common::ObString &image_data_uri,
                                        common::ObJsonObject *&body)
{
  int ret = OB_SUCCESS;
  // 目标结构（OpenAI vision 兼容格式）：
  // {"model":"*","messages":[{"role":"user","content":[
  //    {"type":"text","text":"..."},
  //    {"type":"image_url","image_url":{"url":"data:image/png;base64,..."}}
  // ]}]}
  ObJsonObject *body_obj = nullptr;
  ObJsonString *model_str = nullptr;
  ObJsonArray *messages_array = nullptr;
  ObJsonObject *user_msg = nullptr;
  ObJsonString *role_str = nullptr;
  ObJsonArray *content_array = nullptr;
  ObJsonObject *text_part = nullptr;
  ObJsonString *text_type_str = nullptr;
  ObJsonString *text_str = nullptr;
  ObJsonObject *image_part = nullptr;
  ObJsonString *image_type_str = nullptr;
  ObJsonObject *image_url_obj = nullptr;
  ObJsonString *url_str = nullptr;
  ObString role_user("user");
  ObString type_text("text");
  ObString type_image_url("image_url");
  ObString prompt(ObExprAIParseDoc::DEFAULT_DOC_PROMPT);
  ObString model(request_model_name);
  ObString url(image_data_uri);
  if (request_model_name.empty() || image_data_uri.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("model or image data uri is empty", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    LOG_WARN("Failed to get json object", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    LOG_WARN("Failed to get json string", K(ret));
  } else if (OB_FAIL(body_obj->add("model", model_str))) {
    LOG_WARN("Failed to add model", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, messages_array))) {
    LOG_WARN("Failed to get json array", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, user_msg))) {
    LOG_WARN("Failed to get json object", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, role_user, role_str))) {
    LOG_WARN("Failed to get json string", K(ret));
  } else if (OB_FAIL(user_msg->add("role", role_str))) {
    LOG_WARN("Failed to add role", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, content_array))) {
    LOG_WARN("Failed to get json array", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, text_part))) {
    LOG_WARN("Failed to get json object", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, type_text, text_type_str))) {
    LOG_WARN("Failed to get json string", K(ret));
  } else if (OB_FAIL(text_part->add("type", text_type_str))) {
    LOG_WARN("Failed to add type", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, prompt, text_str))) {
    LOG_WARN("Failed to get json string", K(ret));
  } else if (OB_FAIL(text_part->add("text", text_str))) {
    LOG_WARN("Failed to add text", K(ret));
  } else if (OB_FAIL(content_array->append(text_part))) {
    LOG_WARN("Failed to append text part", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, image_part))) {
    LOG_WARN("Failed to get json object", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, type_image_url, image_type_str))) {
    LOG_WARN("Failed to get json string", K(ret));
  } else if (OB_FAIL(image_part->add("type", image_type_str))) {
    LOG_WARN("Failed to add type", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, image_url_obj))) {
    LOG_WARN("Failed to get json object", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, url, url_str))) {
    LOG_WARN("Failed to get json string", K(ret));
  } else if (OB_FAIL(image_url_obj->add("url", url_str))) {
    LOG_WARN("Failed to add url", K(ret));
  } else if (OB_FAIL(image_part->add("image_url", image_url_obj))) {
    LOG_WARN("Failed to add image_url", K(ret));
  } else if (OB_FAIL(content_array->append(image_part))) {
    LOG_WARN("Failed to append image part", K(ret));
  } else if (OB_FAIL(user_msg->add("content", content_array))) {
    LOG_WARN("Failed to add content", K(ret));
  } else if (OB_FAIL(messages_array->append(user_msg))) {
    LOG_WARN("Failed to append user message", K(ret));
  } else if (OB_FAIL(body_obj->add("messages", messages_array))) {
    LOG_WARN("Failed to add messages", K(ret));
  } else {
    body = body_obj;
  }
  return ret;
}

int ObExprAIParseDoc::eval_ai_parse_doc(const ObExpr &expr,
                                        ObEvalCtx &ctx,
                                        ObDatum &res)
{
  INIT_SUCC(ret);
  ObDatum *arg_model_id = nullptr;
  ObDatum *arg_context = nullptr;
  ObDatum *arg_params = nullptr;
  if (OB_FAIL(expr.eval_param_value(ctx, arg_model_id, arg_context, arg_params))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (arg_model_id->is_null() || arg_context->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parameters is null", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_parse_doc, parameters is null");
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator(), expr.type_, ret);
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_PARSE_DOC));
    ObString model_id = arg_model_id->get_string();
    ObString context;
    ObString input_type;
    ObString data_uri;
    ObAIFuncExprInfo *info = nullptr;
    omt::ObAiServiceGuard ai_service_guard;
    omt::ObTenantAiService *ai_service = share::g_mp->tenant_ai_service();
    const share::ObAiModelEndpointInfo *endpoint_info = nullptr;
    ObAIFuncClient ai_client;
    ObJsonObject *body = nullptr;
    ObJsonObject *response = nullptr;
    ObIJsonBase *output = nullptr;

    if (OB_FAIL(ObTextStringHelper::read_real_string_data(
            temp_allocator, *arg_context,
            expr.args_[CONTEXT_IDX]->datum_meta_,
            expr.args_[CONTEXT_IDX]->obj_meta_.has_lob_header(), context))) {
      LOG_WARN("fail to get real string data", K(ret));
    } else if (OB_NOT_NULL(arg_params)) {
      ObString params_json;
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(
              temp_allocator, *arg_params,
              expr.args_[PARAMS_IDX]->datum_meta_,
              expr.args_[PARAMS_IDX]->obj_meta_.has_lob_header(), params_json))) {
        LOG_WARN("fail to get real string data", K(ret));
      } else if (OB_FAIL(parse_input_type(params_json, input_type))) {
        LOG_WARN("fail to parse params json", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (model_id.empty() || context.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("model id or context is empty", K(ret));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_parse_doc, model id or context is empty");
      res.set_null();
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(temp_allocator, model_id, info))) {
      LOG_WARN("fail to get ai func info", K(ret));
    } else if (OB_ISNULL(ai_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ai service is null", K(ret));
    } else if (OB_FAIL(ai_service->get_ai_service_guard(ai_service_guard))) {
      LOG_WARN("failed to get ai service guard", K(ret));
    } else if (OB_FAIL(ai_service_guard.get_ai_endpoint_by_ai_model_name(model_id, endpoint_info))) {
      LOG_WARN("failed to get endpoint info", K(ret), K(model_id));
    } else if (OB_ISNULL(endpoint_info)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("endpoint info is null", K(ret));
    } else if (OB_FAIL(ObAIFuncUtils::check_info_type_completion(info))) {
      LOG_WARN("model type must be COMPLETION", K(ret));
    } else {
      // 1) 文档光栅化 / 图片编码为 data URI
      if (OB_FAIL(rasterize_doc_to_base64(temp_allocator, input_type, context, data_uri))) {
        LOG_WARN("fail to rasterize doc", K(ret));
      } else {
        ObString request_model_name = info->model_;
        if (!endpoint_info->get_request_model_name().empty()) {
          request_model_name = endpoint_info->get_request_model_name();
        }
        // 2) 构造 OpenAI vision 格式 body
        if (OB_FAIL(build_vision_body(temp_allocator, request_model_name, data_uri, body))) {
          LOG_WARN("fail to build vision body", K(ret));
        } else {
          ObArray<ObString> header_array;
          // 3) 复用 complete 通道的 header / client / 响应解析
          if (OB_FAIL(ObAIFuncUtils::get_header(temp_allocator, *info, *endpoint_info, header_array))) {
            LOG_WARN("fail to get header", K(ret));
          } else if (OB_FAIL(ai_client.send_post(temp_allocator, endpoint_info->get_url(),
                                                 header_array, body, response))) {
            LOG_WARN("fail to send post", K(ret));
          } else if (OB_FAIL(ObAIFuncUtils::parse_complete_output(temp_allocator, *endpoint_info,
                                                                 response, output))) {
            LOG_WARN("fail to parse output", K(ret));
          } else if (OB_ISNULL(output) || output->json_type() != ObJsonNodeType::J_STRING) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("output is not string", K(ret));
          } else {
            ObString result;
            if (OB_FAIL(ObAIFuncJsonUtils::print_json_to_str(temp_allocator, output, result))) {
              LOG_WARN("fail to print json to str", K(ret));
            } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, res, result))) {
              LOG_WARN("fail to set string result", K(ret));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObExprAIParseDoc::cg_expr(ObExprCGCtx &expr_cg_ctx,
                              const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  INIT_SUCC(ret);
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = ObExprAIParseDoc::eval_ai_parse_doc;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
