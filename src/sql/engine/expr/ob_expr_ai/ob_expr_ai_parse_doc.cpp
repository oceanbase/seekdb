/**
 * Copyright (c) 2025 OceanBase
 * SPDX-License-Identifier: Apache-2.0
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_parse_doc.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "sql/engine/expr/ob_expr_multi_mode_func_helper.h"
#include "observer/omt/ob_tenant_ai_service.h"
#include "share/ob_lob_access_utils.h"
#include "lib/ai_parse_doc/ob_pdf_render.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprAIParseDoc::ObExprAIParseDoc(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_AI_PARSE_DOC,
                         N_AI_PARSE_DOC,
                         MORE_THAN_ZERO,
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
                                        ObExprTypeCtx &type_ctx) const
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
    // context: a url string (phase 1) or a pdf blob (phase 2). Read as bytes at eval;
    // only normalize collation for a non-binary string input.
    if (ob_is_string_type(types_stack[CONTEXT_IDX].get_type())
        && types_stack[CONTEXT_IDX].get_collation_type() != CS_TYPE_BINARY) {
      types_stack[CONTEXT_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
    if (OB_FAIL(ret)) {
    } else if (param_num == 3) {
      if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, PARAMS_IDX, N_AI_PARSE_DOC))) {
        LOG_WARN("wrong type for json params", K(ret), K(types_stack[PARAMS_IDX].get_type()));
      } else if (ob_is_string_type(types_stack[PARAMS_IDX].get_type())
                 && types_stack[PARAMS_IDX].get_collation_type() != CS_TYPE_BINARY
                 && types_stack[PARAMS_IDX].get_charset_type() != CHARSET_UTF8MB4) {
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

int ObExprAIParseDoc::parse_params(ObIAllocator &allocator,
                                   ObJsonObject *params_obj,
                                   ObString &input_type,
                                   ObString &output_format,
                                   ObString &prompt)
{
  UNUSED(allocator);
  int ret = OB_SUCCESS;
  input_type.reset();
  output_format.reset();
  prompt.reset();
  if (OB_NOT_NULL(params_obj)) {
    ObIJsonBase *v = nullptr;
    if (OB_NOT_NULL(v = params_obj->get_value(ObString("input_type")))
        && v->json_type() == ObJsonNodeType::J_STRING) {
      input_type = static_cast<ObJsonString *>(v)->get_str();
    }
    if (OB_NOT_NULL(v = params_obj->get_value(ObString("output_format")))
        && v->json_type() == ObJsonNodeType::J_STRING) {
      output_format = static_cast<ObJsonString *>(v)->get_str();
    }
    if (OB_NOT_NULL(v = params_obj->get_value(ObString("prompt")))
        && v->json_type() == ObJsonNodeType::J_STRING) {
      prompt = static_cast<ObJsonString *>(v)->get_str();
    }
  }
  if (output_format.empty()) {
    output_format = ObString("markdown");
  }
  return ret;
}

int ObExprAIParseDoc::get_default_prompt(const ObString &request_model_name,
                                         const ObString &output_format,
                                         ObString &prompt)
{
  // Phase 1: generic instructions that work for OpenAI-compatible VL/OCR models.
  // Vendor-specific fixed prompts (e.g. deepseek-ocr "<|grounding|>...") are Phase 3.
  UNUSED(request_model_name);
  int ret = OB_SUCCESS;
  if (0 == output_format.case_compare("text")) {
    prompt = ObString("Extract all the plain text content from the document.");
  } else if (0 == output_format.case_compare("html")) {
    prompt = ObString("Convert the document to HTML.");
  } else {
    prompt = ObString("Convert the document to markdown.");
  }
  return ret;
}

int ObExprAIParseDoc::eval_ai_parse_doc(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *arg_model = nullptr;
  ObDatum *arg_context = nullptr;
  ObDatum *arg_params = nullptr;
  if (OB_FAIL(expr.eval_param_value(ctx, arg_model, arg_context, arg_params))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (arg_model->is_null() || arg_context->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_parse_doc, model or context is null");
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    uint64_t tenant_id = ObMultiModeExprHelper::get_tenant_id(ctx.exec_ctx_.get_my_session());
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator(), expr.type_, tenant_id, ret);
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(tenant_id, N_AI_PARSE_DOC));
    ObString model_id = arg_model->get_string();
    ObString context;
    ObString input_type;
    ObString output_format;
    ObString prompt;
    ObJsonObject *params_obj = nullptr;
    ObAIFuncExprInfo *info = nullptr;
    omt::ObAiServiceGuard ai_service_guard;
    omt::ObTenantAiService *ai_service = MTL(omt::ObTenantAiService *);
    const share::ObAiModelEndpointInfo *endpoint_info = nullptr;
    // read the context argument (a url string in phase 1, a blob later)
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(temp_allocator, *arg_context,
            expr.args_[CONTEXT_IDX]->datum_meta_, expr.args_[CONTEXT_IDX]->obj_meta_.has_lob_header(), context))) {
      LOG_WARN("fail to read context", K(ret));
    }
    // optional params json (json arg, or a json-string arg)
    if (OB_SUCC(ret) && OB_NOT_NULL(arg_params) && !arg_params->is_null()) {
      ObExpr *params_expr = expr.args_[PARAMS_IDX];
      if (params_expr->datum_meta_.type_ == ObJsonType) {
        ObIJsonBase *j_base = nullptr;
        bool is_null = false;
        if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, PARAMS_IDX, j_base, is_null))) {
          LOG_WARN("get_json_doc failed", K(ret));
        } else if (!is_null && j_base->json_type() == ObJsonNodeType::J_OBJECT) {
          params_obj = static_cast<ObJsonObject *>(j_base);
        }
      } else {
        ObString params_str;
        if (OB_FAIL(ObTextStringHelper::read_real_string_data(temp_allocator, *arg_params,
                params_expr->datum_meta_, params_expr->obj_meta_.has_lob_header(), params_str))) {
          LOG_WARN("fail to read params", K(ret));
        } else if (!params_str.empty()
                   && OB_FAIL(ObAIFuncJsonUtils::get_json_object_form_str(temp_allocator, params_str, params_obj))) {
          LOG_WARN("fail to parse params json", K(ret));
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(parse_params(temp_allocator, params_obj, input_type, output_format, prompt))) {
      LOG_WARN("fail to parse params", K(ret));
    }
    if (OB_FAIL(ret)) {
    } else if (model_id.empty() || context.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_parse_doc, model or context is empty");
      res.set_null();
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(temp_allocator, model_id, info))) {
      LOG_WARN("fail to get ai func info", K(ret), K(model_id));
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
    } else if (!input_type.empty()
               && 0 != input_type.case_compare("pdf")
               && 0 != input_type.case_compare("url")) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_parse_doc, input_type must be 'pdf' or 'url'");
    } else {
      ObString request_model = endpoint_info->get_request_model_name().empty()
                                   ? info->model_ : endpoint_info->get_request_model_name();
      ObSEArray<ObString, 8> image_urls;
      const bool is_url = (0 == input_type.case_compare("url"));
      if (is_url) {
        // url path: the context itself is the image URL (or data:image;base64 URL)
        if (OB_FAIL(image_urls.push_back(context))) {
          LOG_WARN("fail to collect image url", K(ret));
        }
      } else {
        // pdf path (default): raster each page to a PNG and pass them as images
        if (OB_FAIL(ObPdfRender::render_to_png_base64_urls(
                context.ptr(), context.length(), 2.0 /*scale ~144dpi*/, 0 /*all pages*/,
                temp_allocator, image_urls))) {
          LOG_WARN("fail to render pdf pages", K(ret));
          if (OB_INVALID_ARGUMENT == ret) {
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_parse_doc, context is not a valid PDF");
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (prompt.empty()
                 && OB_FAIL(get_default_prompt(request_model, output_format, prompt))) {
        LOG_WARN("fail to get default prompt", K(ret));
      } else {
        ObAIFuncModel model(temp_allocator, *info, *endpoint_info);
        ObString result;
        if (OB_FAIL(model.call_parse_doc(prompt, image_urls, nullptr, result))) {
          LOG_WARN("fail to call parse_doc", K(ret));
        } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, res, result))) {
          LOG_WARN("fail to set string result", K(ret));
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
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  int ret = OB_SUCCESS;
  rt_expr.eval_func_ = ObExprAIParseDoc::eval_ai_parse_doc;
  return ret;
}

} // namespace sql
} // namespace oceanbase
