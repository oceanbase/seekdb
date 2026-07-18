/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_ai_split_document.h"
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/string/ob_sql_string.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

namespace
{
struct SplitOptions
{
  SplitOptions() : markdown_(true), by_sentence_(false), max_(256), overlap_(0) {}
  bool markdown_;
  bool by_sentence_;
  int64_t max_;
  int64_t overlap_;
};

struct SplitUnit
{
  SplitUnit() : begin_(0), end_(0) {}
  SplitUnit(int64_t begin, int64_t end) : begin_(begin), end_(end) {}
  int64_t begin_;
  int64_t end_;
  TO_STRING_KV(K_(begin), K_(end));
};

bool is_space(const char c)
{
  return ' ' == c || '\t' == c || '\r' == c || '\n' == c || '\f' == c;
}

int append_json_string(ObSqlString &json, const ObString &str)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(json.append("\""))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < str.length(); ++i) {
      const unsigned char c = static_cast<unsigned char>(str.ptr()[i]);
      switch (c) {
        case '\"': ret = json.append("\\\""); break;
        case '\\': ret = json.append("\\\\"); break;
        case '\b': ret = json.append("\\b"); break;
        case '\f': ret = json.append("\\f"); break;
        case '\n': ret = json.append("\\n"); break;
        case '\r': ret = json.append("\\r"); break;
        case '\t': ret = json.append("\\t"); break;
        default:
          if (c < 0x20) {
            ret = json.append_fmt("\\u%04x", static_cast<unsigned int>(c));
          } else {
            ret = json.append(ObString(1, str.ptr() + i));
          }
      }
    }
    if (OB_SUCC(ret)) {
      ret = json.append("\"");
    }
  }
  return ret;
}

int get_string_option(ObJsonObject &obj, const char *key, ObString &value, bool &exists)
{
  int ret = OB_SUCCESS;
  ObJsonNode *node = obj.get_value(key);
  exists = OB_NOT_NULL(node);
  if (exists) {
    if (ObJsonNodeType::J_STRING != node->json_type()) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      value = static_cast<ObJsonString *>(node)->value();
    }
  }
  return ret;
}

int get_int_option(ObJsonObject &obj, const char *key, int64_t &value, bool &exists)
{
  int ret = OB_SUCCESS;
  ObJsonNode *node = obj.get_value(key);
  exists = OB_NOT_NULL(node);
  if (exists) {
    if (ObJsonNodeType::J_INT == node->json_type()) {
      value = static_cast<ObJsonInt *>(node)->value();
    } else if (ObJsonNodeType::J_UINT == node->json_type()) {
      const uint64_t unsigned_value = static_cast<ObJsonUint *>(node)->value();
      if (unsigned_value > static_cast<uint64_t>(INT64_MAX)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        value = static_cast<int64_t>(unsigned_value);
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

int parse_options(ObIAllocator &allocator, const ObString &parameters, SplitOptions &options)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *base = NULL;
  ObString value;
  bool exists = false;
  if (parameters.empty()) {
  } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator, parameters,
                  ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, base))) {
    LOG_WARN("failed to parse split document parameters", K(ret));
  } else if (OB_ISNULL(base) || ObJsonNodeType::J_OBJECT != base->json_type()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObJsonObject &obj = *static_cast<ObJsonObject *>(base);
    if (OB_FAIL(get_string_option(obj, "type", value, exists))) {
    } else if (exists && 0 == value.case_compare("text")) {
      options.markdown_ = false;
    } else if (exists && 0 == value.case_compare("markdown")) {
      options.markdown_ = true;
    } else if (exists) {
      ret = OB_INVALID_ARGUMENT;
    }
    if (OB_SUCC(ret) && OB_FAIL(get_string_option(obj, "by", value, exists))) {
    } else if (OB_SUCC(ret) && exists && 0 == value.case_compare("sentence")) {
      options.by_sentence_ = true;
    } else if (OB_SUCC(ret) && exists && 0 == value.case_compare("word")) {
      options.by_sentence_ = false;
    } else if (OB_SUCC(ret) && exists) {
      ret = OB_INVALID_ARGUMENT;
    }
    if (OB_SUCC(ret) && OB_FAIL(get_int_option(obj, "max", options.max_, exists))) {
    }
    if (OB_SUCC(ret) && OB_FAIL(get_int_option(obj, "overlap", options.overlap_, exists))) {
    }
    if (OB_SUCC(ret) && (options.max_ <= 0 || options.overlap_ < 0 ||
                         options.overlap_ >= options.max_)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  if (OB_INVALID_ARGUMENT == ret) {
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "invalid AI_SPLIT_DOCUMENT parameters");
  }
  return ret;
}

int collect_units(const ObString &content, const int64_t begin, const int64_t end,
                  const bool by_sentence, ObIArray<SplitUnit> &units)
{
  int ret = OB_SUCCESS;
  int64_t pos = begin;
  while (OB_SUCC(ret) && pos < end) {
    while (pos < end && is_space(content.ptr()[pos])) { ++pos; }
    if (pos >= end) { break; }
    const int64_t unit_begin = pos;
    if (by_sentence) {
      while (pos < end && '.' != content.ptr()[pos] && '!' != content.ptr()[pos] &&
             '?' != content.ptr()[pos]) { ++pos; }
      if (pos < end) { ++pos; }
    } else {
      while (pos < end && !is_space(content.ptr()[pos])) { ++pos; }
    }
    int64_t unit_end = pos;
    while (unit_end > unit_begin && is_space(content.ptr()[unit_end - 1])) { --unit_end; }
    if (unit_end > unit_begin && OB_FAIL(units.push_back(SplitUnit(unit_begin, unit_end)))) {
      LOG_WARN("failed to add split unit", K(ret));
    }
  }
  return ret;
}

int append_chunks(const ObString &content, const ObString &heading,
                  const ObIArray<SplitUnit> &units, const SplitOptions &options,
                  int64_t &chunk_id, ObSqlString &json, bool &first)
{
  int ret = OB_SUCCESS;
  const int64_t step = options.max_ - options.overlap_;
  for (int64_t i = 0; OB_SUCC(ret) && i < units.count(); i += step) {
    const int64_t last = MIN(i + options.max_, units.count()) - 1;
    const int64_t begin = units.at(i).begin_;
    const int64_t end = units.at(last).end_;
    if (!first && OB_FAIL(json.append(","))) {
    } else if (OB_FAIL(json.append_fmt("{\"chunk_id\":%ld,\"chunk_offset\":%ld,\"chunk_length\":%ld,\"chunk_text\":",
                                       chunk_id, begin, end - begin))) {
    } else {
      ObSqlString text;
      if (!heading.empty() && OB_FAIL(text.append(heading))) {
      } else if (!heading.empty() && OB_FAIL(text.append("\n"))) {
      } else if (OB_FAIL(text.append(ObString(end - begin, content.ptr() + begin)))) {
      } else if (OB_FAIL(append_json_string(json, text.string()))) {
      } else if (OB_FAIL(json.append("}"))) {
      } else {
        first = false;
        ++chunk_id;
      }
    }
  }
  return ret;
}

int split_document(const ObString &content, const SplitOptions &options, ObSqlString &json)
{
  int ret = json.append("[");
  bool first = true;
  int64_t chunk_id = 0;
  if (OB_SUCC(ret) && !options.markdown_) {
    ObSEArray<SplitUnit, 32> units;
    if (OB_FAIL(collect_units(content, 0, content.length(), options.by_sentence_, units))) {
    } else if (OB_FAIL(append_chunks(content, ObString(), units, options, chunk_id, json, first))) {
    }
  } else if (OB_SUCC(ret)) {
    ObString heading;
    int64_t section_begin = 0;
    int64_t line_begin = 0;
    while (OB_SUCC(ret) && line_begin <= content.length()) {
      int64_t line_end = line_begin;
      while (line_end < content.length() && '\n' != content.ptr()[line_end]) { ++line_end; }
      int64_t trimmed_end = line_end;
      if (trimmed_end > line_begin && '\r' == content.ptr()[trimmed_end - 1]) { --trimmed_end; }
      int64_t hashes = 0;
      while (line_begin + hashes < trimmed_end && hashes < 6 && '#' == content.ptr()[line_begin + hashes]) { ++hashes; }
      const bool is_heading = hashes > 0 && line_begin + hashes < trimmed_end &&
                              ' ' == content.ptr()[line_begin + hashes];
      if (is_heading) {
        ObSEArray<SplitUnit, 32> units;
        if (section_begin < line_begin && OB_FAIL(collect_units(content, section_begin, line_begin,
                                                                 options.by_sentence_, units))) {
        } else if (OB_FAIL(append_chunks(content, heading, units, options, chunk_id, json, first))) {
        } else {
          heading.assign_ptr(content.ptr() + line_begin, trimmed_end - line_begin);
          section_begin = line_end < content.length() ? line_end + 1 : line_end;
        }
      }
      if (line_end >= content.length()) { break; }
      line_begin = line_end + 1;
    }
    if (OB_SUCC(ret)) {
      ObSEArray<SplitUnit, 32> units;
      if (OB_FAIL(collect_units(content, section_begin, content.length(), options.by_sentence_, units))) {
      } else if (OB_FAIL(append_chunks(content, heading, units, options, chunk_id, json, first))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    ret = json.append("]");
  }
  return ret;
}
} // namespace

ObExprAISplitDocument::ObExprAISplitDocument(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_AI_SPLIT_DOCUMENT, N_AI_SPLIT_DOCUMENT, 2,
                        NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprAISplitDocument::~ObExprAISplitDocument()
{
}

int ObExprAISplitDocument::calc_result_type2(ObExprResType &type,
                                             ObExprResType &content_type,
                                             ObExprResType &parameters_type,
                                             common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  content_type.set_calc_type(ObVarcharType);
  content_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  parameters_type.set_calc_type(ObVarcharType);
  parameters_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  type.set_varchar();
  type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  type.set_collation_level(CS_LEVEL_COERCIBLE);
  type.set_length(OB_MAX_VARCHAR_LENGTH);
  return OB_SUCCESS;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *content = NULL;
  ObDatum *parameters = NULL;
  if (OB_UNLIKELY(2 != expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, content))) {
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, parameters))) {
  } else if (content->is_null()) {
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard guard(ctx);
    SplitOptions options;
    ObSqlString json;
    const ObString parameter_string = parameters->is_null() ? ObString() : parameters->get_string();
    if (OB_FAIL(parse_options(guard.get_allocator(), parameter_string, options))) {
    } else if (OB_FAIL(split_document(content->get_string(), options, json))) {
    } else {
      char *buffer = expr.get_str_res_mem(ctx, json.length());
      if (OB_ISNULL(buffer)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(buffer, json.ptr(), json.length());
        res.set_string(buffer, json.length());
      }
    }
  }
  return ret;
}

int ObExprAISplitDocument::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprAISplitDocument::eval_ai_split_document;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
