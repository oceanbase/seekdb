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

#include "sql/engine/basic/ob_function_table_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "common/json_type/ob_json_parse.h"
#include "common/json_type/ob_json_tree.h"
#include <algorithm>


namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER((ObFunctionTableSpec, ObOpSpec), value_expr_, column_exprs_, has_correlated_expr_);

int ObFunctionTableOp::inner_open()
{
  int ret = OB_SUCCESS;
  node_idx_ = 0;
  already_calc_ = false;
  split_chunk_idx_ = 0;
  split_initialized_ = false;
  split_chunks_.reset();
  if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value expr is not init", K(ret));
  } else if (T_FUN_SYS_AI_SPLIT_DOCUMENT == MY_SPEC.value_expr_->type_) {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_ai_split_document;
  } else if (ObExtendType == MY_SPEC.value_expr_->datum_meta_.type_) {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_udf;
  } else {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_sys_func;
  }
  return ret;
}

int ObFunctionTableOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
    LOG_WARN("failed to inner rescan", K(ret));
  } else {
    node_idx_ = 0;
    split_chunk_idx_ = 0;
    if (MY_SPEC.has_correlated_expr_) {
      row_count_ = 0;
      col_count_ = 0;
      value_table_ = NULL;
      already_calc_ = false;
      split_initialized_ = false;
      split_chunks_.reset();
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_close()
{
  int ret = OB_SUCCESS;
  node_idx_ = 0;
  row_count_ = 0;
  col_count_ = 0;
  value_table_ = NULL;
  split_chunk_idx_ = 0;
  split_initialized_ = false;
  split_chunks_.reset();
  return ret;
}

//ObFunctionTableOp has its own switch_iterator
int ObFunctionTableOp::switch_iterator()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_switch_iterator())) {
    LOG_WARN("failed to switch iterator", K(ret));
  } else if (OB_ISNULL(ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get session", K(ret));
  } else if (NULL == ctx_.get_my_session()->get_pl_implicit_cursor()
            || !ctx_.get_my_session()->get_pl_implicit_cursor()->get_in_forall()) {
    ret = OB_ITER_END;
  } else {
    node_idx_ = 0;
    split_chunk_idx_ = 0;
  }
  return ret;
}

void ObFunctionTableOp::destroy()
{
  ObOperator::destroy();
}


int ObFunctionTableOp::get_current_result(ObObj &result)
{
  int ret = OB_SUCCESS;
  void *data = NULL;
  CK (already_calc_);
  if (node_idx_ < 0 || node_idx_ >= row_count_ * col_count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get current result in table function",
              K(node_idx_), K(row_count_), K(col_count_));
  }
  do {
    CK (node_idx_ >= 0);
    if (OB_SUCC(ret) && node_idx_ >= row_count_) {
      ret = OB_ITER_END;
    }
    CK (OB_NOT_NULL(value_table_));
    OX (data = value_table_->get_data());
    CK (OB_NOT_NULL(data));
    OX (result = (static_cast<ObObj*>(data))[node_idx_++]);
  } while (OB_SUCC(ret) && result.get_meta().get_type() == ObMaxType);
  return ret;
}

int ObFunctionTableOp::inner_get_next_row()
{
  return (this->*next_row_func_)();
}

int ObFunctionTableOp::inner_get_next_row_udf()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  clear_evaluated_flag();
  if (OB_ISNULL(plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get plan ctx", K(ret), K(plan_ctx));
  } else if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value expr is not init", K(ret));
  } else if (ObExtendType != MY_SPEC.value_expr_->datum_meta_.type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected value", K(ret), K(MY_SPEC.value_expr_->datum_meta_.type_));
  } else if (FALSE_IT(plan_ctx->set_autoinc_id_tmp(0))) {
  } else if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("failed to check status ", K(ret));
  } else {
    ObDatum *value = nullptr;
    if (!already_calc_) {
      if (OB_FAIL(MY_SPEC.value_expr_->eval(eval_ctx_, value))) {
        LOG_WARN("failed to eval value expr", K(ret));
      } else if (value->is_null()) {
        //do nothing
      } else if (OB_ISNULL(value_table_ 
                 = reinterpret_cast<pl::ObPLCollection*>(value->get_ext()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get value table", K(ret));
      } else {
        row_count_ = value_table_->is_inited() ? value_table_->get_count() : 0;
        col_count_ = value_table_->get_column_count();
      }
      already_calc_ = true;
    }
    if (OB_SUCC(ret) && OB_UNLIKELY(node_idx_ >= row_count_)) {
      ret = OB_ITER_END;
    }
    CK (MY_SPEC.column_exprs_.count() >= col_count_);
    ObObj obj_stack[col_count_];
    if (OB_SUCC(ret)) {
      if (nullptr != value_table_ 
          && ObExtendType == value_table_->get_element_type().get_obj_type()) {
        pl::ObPLComposite *composite = NULL;
        pl::ObPLRecord *record = NULL;
        ObObj record_obj;
        OZ (get_current_result(record_obj));
        if (OB_FAIL(ret)) {
        } else if (ObUserDefinedSQLType == record_obj.get_type()) {
          obj_stack[0] = record_obj;   
        } else if (record_obj.is_pl_extend()) {
          CK (OB_NOT_NULL(composite = reinterpret_cast<pl::ObPLComposite*>(record_obj.get_ext())));
          if (OB_SUCC(ret)) {
            if (composite->is_record()) {
              OX (record = static_cast<pl::ObPLRecord*>(composite));
              CK (record->get_count() == col_count_);
              for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
                OZ (record->get_element(i, obj_stack[i]));
              }
            } else if (composite->is_collection()) {
              OX (obj_stack[0] = record_obj);
            } else {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected composite type", K(ret), K(composite->get_type()));
            }
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("unexpected here", K(ret), K(record_obj), K(record_obj.meta_));
        }
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
          if (OB_FAIL(get_current_result(obj_stack[i]))) {
            LOG_WARN("failed to get current result", K(ret), K(i));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
        if (obj_stack[i].is_null()) {
          MY_SPEC.column_exprs_.at(i)->locate_datum_for_write(eval_ctx_).set_null();
        } else {
          const ObObjDatumMapType &datum_map = MY_SPEC.column_exprs_.at(i)->obj_datum_map_;
          ObExpr * const &expr = MY_SPEC.column_exprs_.at(i);
          ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
          if (OB_FAIL(datum.from_obj(obj_stack[i], datum_map))) {
            LOG_WARN("failed to convert datum", K(ret));
          } else if (is_lob_storage(obj_stack[i].get_type()) &&
                     OB_FAIL(ob_adjust_lob_datum(obj_stack[i], expr->obj_meta_, datum_map,
                                                 get_exec_ctx().get_allocator(), datum))) {
            LOG_WARN("adjust lob datum failed", K(ret), K(obj_stack[i].get_meta()), K(expr->obj_meta_));
          }
        }
        if (OB_SUCC(ret)) {
          MY_SPEC.column_exprs_.at(i)->set_evaluated_projected(eval_ctx_);
        }
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_get_next_row_sys_func()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  ObDatum *value = nullptr;
  clear_evaluated_flag();
  if (OB_ISNULL(plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get plan ctx", K(ret), K(plan_ctx));
  } else if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("failed to check status ", K(ret));
  } else if (OB_FAIL(MY_SPEC.value_expr_->eval(eval_ctx_, value))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("failed to eval value expr", K(ret));
    }
  } else {
    MY_SPEC.column_exprs_.at(0)->locate_datum_for_write(eval_ctx_).set_datum(*value);
    MY_SPEC.column_exprs_.at(0)->set_evaluated_projected(eval_ctx_);
  }
  return ret;
}

static bool is_document_space(const char ch)
{
  return ' ' == ch || '\t' == ch || '\r' == ch || '\n' == ch || '\f' == ch || '\v' == ch;
}

static int64_t sentence_delimiter_length(const ObString &content, const int64_t pos)
{
  int64_t len = 0;
  if (pos >= 0 && pos < content.length()) {
    const unsigned char ch = static_cast<unsigned char>(content[pos]);
    if ('.' == ch || '!' == ch || '?' == ch) {
      len = 1;
    } else if (pos + 2 < content.length()) {
      const unsigned char ch1 = static_cast<unsigned char>(content[pos + 1]);
      const unsigned char ch2 = static_cast<unsigned char>(content[pos + 2]);
      if ((0xE3 == ch && 0x80 == ch1 && 0x82 == ch2)       // Chinese full stop
          || (0xEF == ch && 0xBC == ch1 && 0x81 == ch2)    // full-width exclamation
          || (0xEF == ch && 0xBC == ch1 && 0x9F == ch2)) { // full-width question
        len = 3;
      }
    }
  }
  return len;
}

static int64_t closing_punctuation_length(const ObString &content, const int64_t pos)
{
  int64_t len = 0;
  if (pos >= 0 && pos < content.length()) {
    const unsigned char ch = static_cast<unsigned char>(content[pos]);
    if ('"' == ch || '\'' == ch || ')' == ch || ']' == ch || '}' == ch) {
      len = 1;
    } else if (pos + 2 < content.length()) {
      const unsigned char ch1 = static_cast<unsigned char>(content[pos + 1]);
      const unsigned char ch2 = static_cast<unsigned char>(content[pos + 2]);
      if (0xE2 == ch && 0x80 == ch1 && (0x99 == ch2 || 0x9D == ch2)) {
        len = 3; // right single or double quotation mark
      }
    }
  }
  return len;
}

int ObFunctionTableOp::parse_split_options(const ObString &json_text,
                                           SplitDocumentOptions &options)
{
  int ret = OB_SUCCESS;
  ObJsonNode *root = NULL;
  const char *syntax_error = NULL;
  uint64_t error_offset = 0;
  const uint32_t parse_flags = ObJsonParser::JSN_STRICT_FLAG | ObJsonParser::JSN_UNIQUE_FLAG;
  if (json_text.empty()) {
    // Keep defaults for an empty optional argument.
  } else if (OB_FAIL(ObJsonParser::parse_json_text(&ctx_.get_allocator(),
                                                   json_text.ptr(),
                                                   json_text.length(),
                                                   syntax_error,
                                                   &error_offset,
                                                   root,
                                                   parse_flags))) {
    LOG_WARN("failed to parse ai_split_document parameters", K(ret),
             K(json_text), KCSTRING(syntax_error), K(error_offset));
  } else if (OB_ISNULL(root) || ObJsonNodeType::J_OBJECT != root->json_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ai_split_document parameters must be a JSON object", K(ret));
  } else {
    for (uint64_t i = 0; OB_SUCC(ret) && i < root->element_count(); ++i) {
      ObString key;
      ObIJsonBase *value = NULL;
      if (OB_FAIL(root->get_key(i, key)) || OB_FAIL(root->get_object_value(i, value))) {
        LOG_WARN("failed to read ai_split_document option", K(ret), K(i));
      } else if (OB_ISNULL(value)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null ai_split_document option node", K(ret), K(key));
      } else if (0 == key.case_compare("type") || 0 == key.case_compare("by")) {
        if (ObJsonNodeType::J_STRING != value->json_type()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("ai_split_document string option has invalid type", K(ret), K(key));
        } else {
          const ObString option_value(value->get_data_length(), value->get_data());
          if (0 == key.case_compare("type")) {
            if (0 == option_value.case_compare("markdown")) {
              options.is_markdown_ = true;
            } else if (0 == option_value.case_compare("text")) {
              options.is_markdown_ = false;
            } else {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("invalid ai_split_document type", K(ret), K(option_value));
            }
          } else if (0 == option_value.case_compare("sentence")) {
            options.by_sentence_ = true;
          } else if (0 == option_value.case_compare("word")) {
            options.by_sentence_ = false;
          } else {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid ai_split_document split unit", K(ret), K(option_value));
          }
        }
      } else if (0 == key.case_compare("max") || 0 == key.case_compare("overlap")) {
        int64_t number = 0;
        if (ObJsonNodeType::J_INT == value->json_type()) {
          number = value->get_int();
        } else if (ObJsonNodeType::J_UINT == value->json_type()
                   && value->get_uint() <= static_cast<uint64_t>(INT64_MAX)) {
          number = static_cast<int64_t>(value->get_uint());
        } else {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("ai_split_document numeric option has invalid type", K(ret), K(key));
        }
        if (OB_SUCC(ret) && 0 == key.case_compare("max")) {
          if (number <= 0) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("ai_split_document max must be positive", K(ret), K(number));
          } else {
            options.max_units_ = number;
          }
        } else if (OB_SUCC(ret)) {
          if (number < 0) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("ai_split_document overlap cannot be negative", K(ret), K(number));
          } else {
            options.overlap_ = number;
          }
        }
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("unknown ai_split_document option", K(ret), K(key));
      }
    }
  }
  if (OB_SUCC(ret) && options.overlap_ >= options.max_units_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ai_split_document overlap must be smaller than max", K(ret),
             K(options.overlap_), K(options.max_units_));
  }
  return ret;
}

int ObFunctionTableOp::build_document_units(const ObString &content,
                                            int64_t range_start,
                                            int64_t range_end,
                                            bool by_sentence,
                                            ObIArray<DocumentUnit> &units)
{
  int ret = OB_SUCCESS;
  range_start = std::max<int64_t>(0, range_start);
  range_end = std::min<int64_t>(content.length(), range_end);
  if (by_sentence) {
    int64_t unit_start = range_start;
    while (unit_start < range_end && is_document_space(content[unit_start])) {
      ++unit_start;
    }
    int64_t pos = unit_start;
    while (OB_SUCC(ret) && pos < range_end) {
      int64_t delimiter_len = sentence_delimiter_length(content, pos);
      const bool newline_delimiter = '\n' == content[pos] || '\r' == content[pos];
      if (delimiter_len > 0 || newline_delimiter) {
        int64_t unit_end = newline_delimiter ? pos : pos + delimiter_len;
        if (!newline_delimiter) {
          int64_t closing_len = 0;
          while (unit_end < range_end
                 && (closing_len = closing_punctuation_length(content, unit_end)) > 0) {
            unit_end += closing_len;
          }
        }
        while (unit_end > unit_start && is_document_space(content[unit_end - 1])) {
          --unit_end;
        }
        if (unit_end > unit_start && OB_FAIL(units.push_back(DocumentUnit(unit_start, unit_end)))) {
          LOG_WARN("failed to append sentence unit", K(ret));
        }
        pos = newline_delimiter ? pos + 1 : unit_end;
        while (pos < range_end && is_document_space(content[pos])) {
          ++pos;
        }
        unit_start = pos;
      } else {
        ++pos;
      }
    }
    if (OB_SUCC(ret)) {
      int64_t unit_end = range_end;
      while (unit_end > unit_start && is_document_space(content[unit_end - 1])) {
        --unit_end;
      }
      if (unit_end > unit_start && OB_FAIL(units.push_back(DocumentUnit(unit_start, unit_end)))) {
        LOG_WARN("failed to append final sentence unit", K(ret));
      }
    }
  } else {
    int64_t pos = range_start;
    while (OB_SUCC(ret) && pos < range_end) {
      while (pos < range_end && is_document_space(content[pos])) {
        ++pos;
      }
      const int64_t unit_start = pos;
      while (pos < range_end && !is_document_space(content[pos])) {
        ++pos;
      }
      if (pos > unit_start && OB_FAIL(units.push_back(DocumentUnit(unit_start, pos)))) {
        LOG_WARN("failed to append word unit", K(ret));
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::append_split_chunk(const ObString &content,
                                          const ObString &heading,
                                          const DocumentUnit &first_unit,
                                          const DocumentUnit &last_unit)
{
  int ret = OB_SUCCESS;
  SplitDocumentChunk chunk;
  const int64_t body_len = last_unit.end_ - first_unit.start_;
  const int64_t prefix_len = heading.empty() ? 0 : heading.length() + 1;
  const int64_t total_len = prefix_len + body_len;
  char *buffer = NULL;
  if (body_len < 0 || total_len < body_len) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("invalid split document chunk length", K(ret), K(body_len), K(prefix_len));
  } else if (total_len > 0
             && OB_ISNULL(buffer = static_cast<char *>(ctx_.get_allocator().alloc(total_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate split document chunk", K(ret), K(total_len));
  } else {
    int64_t pos = 0;
    if (!heading.empty()) {
      MEMCPY(buffer + pos, heading.ptr(), heading.length());
      pos += heading.length();
      buffer[pos++] = '\n';
    }
    if (body_len > 0) {
      MEMCPY(buffer + pos, content.ptr() + first_unit.start_, body_len);
      pos += body_len;
    }
    chunk.id_ = split_chunks_.count();
    chunk.offset_ = first_unit.start_;
    chunk.length_ = total_len;
    chunk.text_.assign_ptr(buffer, static_cast<int32_t>(total_len));
    if (OB_FAIL(split_chunks_.push_back(chunk))) {
      LOG_WARN("failed to save split document chunk", K(ret));
    }
  }
  return ret;
}

int ObFunctionTableOp::split_document_range(const ObString &content,
                                            int64_t range_start,
                                            int64_t range_end,
                                            const ObString &heading,
                                            const SplitDocumentOptions &options)
{
  int ret = OB_SUCCESS;
  ObSEArray<DocumentUnit, 64> units;
  if (OB_FAIL(build_document_units(content,
                                   range_start,
                                   range_end,
                                   options.by_sentence_,
                                   units))) {
    LOG_WARN("failed to build document units", K(ret));
  } else {
    const int64_t step = options.max_units_ - options.overlap_;
    for (int64_t start = 0; OB_SUCC(ret) && start < units.count(); start += step) {
      const int64_t end = std::min<int64_t>(start + options.max_units_, units.count());
      if (OB_FAIL(append_split_chunk(content, heading, units.at(start), units.at(end - 1)))) {
        LOG_WARN("failed to append document chunk", K(ret), K(start), K(end));
      } else if (end == units.count()) {
        break;
      }
    }
  }
  return ret;
}

static bool get_markdown_heading(const ObString &content,
                                 const int64_t line_start,
                                 const int64_t line_end,
                                 ObString &heading)
{
  bool is_heading = false;
  int64_t pos = line_start;
  int64_t leading_spaces = 0;
  while (pos < line_end && leading_spaces < 3 && ' ' == content[pos]) {
    ++pos;
    ++leading_spaces;
  }
  const int64_t heading_start = pos;
  int64_t level = 0;
  while (pos < line_end && level < 6 && '#' == content[pos]) {
    ++pos;
    ++level;
  }
  if (level > 0 && (pos == line_end || is_document_space(content[pos]))) {
    int64_t heading_end = line_end;
    while (heading_end > heading_start && ('\r' == content[heading_end - 1]
                                            || ' ' == content[heading_end - 1]
                                            || '\t' == content[heading_end - 1])) {
      --heading_end;
    }
    heading.assign_ptr(content.ptr() + heading_start,
                       static_cast<int32_t>(heading_end - heading_start));
    is_heading = true;
  }
  return is_heading;
}

int ObFunctionTableOp::split_document(const ObString &content,
                                      const SplitDocumentOptions &options)
{
  int ret = OB_SUCCESS;
  if (!options.is_markdown_) {
    ret = split_document_range(content, 0, content.length(), ObString(), options);
  } else {
    int64_t body_start = 0;
    int64_t line_start = 0;
    ObString current_heading;
    while (OB_SUCC(ret) && line_start < content.length()) {
      int64_t line_end = line_start;
      while (line_end < content.length() && '\n' != content[line_end]) {
        ++line_end;
      }
      ObString heading;
      if (get_markdown_heading(content, line_start, line_end, heading)) {
        if (OB_FAIL(split_document_range(content,
                                         body_start,
                                         line_start,
                                         current_heading,
                                         options))) {
          LOG_WARN("failed to split markdown section", K(ret), K(body_start), K(line_start));
        } else {
          current_heading = heading;
          body_start = line_end < content.length() ? line_end + 1 : line_end;
        }
      }
      line_start = line_end < content.length() ? line_end + 1 : line_end;
    }
    if (OB_SUCC(ret)
        && OB_FAIL(split_document_range(content,
                                        body_start,
                                        content.length(),
                                        current_heading,
                                        options))) {
      LOG_WARN("failed to split final markdown section", K(ret), K(body_start));
    }
  }
  return ret;
}

int ObFunctionTableOp::init_split_document()
{
  int ret = OB_SUCCESS;
  ObDatum *content_datum = NULL;
  ObDatum *parameters_datum = NULL;
  ObString content;
  ObString parameters;
  SplitDocumentOptions options;
  ObExpr *value_expr = MY_SPEC.value_expr_;
  if (OB_ISNULL(value_expr) || OB_UNLIKELY(value_expr->arg_cnt_ < 1 || value_expr->arg_cnt_ > 2)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid ai_split_document expression", K(ret), KP(value_expr));
  } else if (OB_FAIL(value_expr->args_[0]->eval(eval_ctx_, content_datum))) {
    LOG_WARN("failed to evaluate ai_split_document content", K(ret));
  } else if (content_datum->is_null()) {
    // NULL content produces an empty table.
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                       ctx_.get_allocator(),
                       *content_datum,
                       value_expr->args_[0]->datum_meta_,
                       value_expr->args_[0]->obj_meta_.has_lob_header(),
                       content))) {
    LOG_WARN("failed to read ai_split_document content", K(ret));
  } else if (2 == value_expr->arg_cnt_) {
    if (OB_FAIL(value_expr->args_[1]->eval(eval_ctx_, parameters_datum))) {
      LOG_WARN("failed to evaluate ai_split_document parameters", K(ret));
    } else if (!parameters_datum->is_null()
               && OB_FAIL(ObTextStringHelper::read_real_string_data(
                            ctx_.get_allocator(),
                            *parameters_datum,
                            value_expr->args_[1]->datum_meta_,
                            value_expr->args_[1]->obj_meta_.has_lob_header(),
                            parameters))) {
      LOG_WARN("failed to read ai_split_document parameters", K(ret));
    }
  }
  if (OB_SUCC(ret) && !parameters.empty() && OB_FAIL(parse_split_options(parameters, options))) {
    LOG_WARN("failed to parse ai_split_document options", K(ret));
  } else if (OB_SUCC(ret) && !content_datum->is_null()
             && OB_FAIL(split_document(content, options))) {
    LOG_WARN("failed to split document", K(ret));
  }
  if (OB_SUCC(ret)) {
    split_initialized_ = true;
    split_chunk_idx_ = 0;
  }
  return ret;
}

int ObFunctionTableOp::inner_get_next_row_ai_split_document()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("failed to check execution status", K(ret));
  } else if (!split_initialized_ && OB_FAIL(init_split_document())) {
    LOG_WARN("failed to initialize ai_split_document", K(ret));
  } else if (split_chunk_idx_ >= split_chunks_.count()) {
    ret = OB_ITER_END;
  } else if (OB_UNLIKELY(4 != MY_SPEC.column_exprs_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected ai_split_document output column count", K(ret),
             K(MY_SPEC.column_exprs_.count()));
  } else {
    const SplitDocumentChunk &chunk = split_chunks_.at(split_chunk_idx_++);
    MY_SPEC.column_exprs_.at(0)->locate_datum_for_write(eval_ctx_).set_int(chunk.id_);
    MY_SPEC.column_exprs_.at(1)->locate_datum_for_write(eval_ctx_).set_int(chunk.offset_);
    MY_SPEC.column_exprs_.at(2)->locate_datum_for_write(eval_ctx_).set_int(chunk.length_);
    MY_SPEC.column_exprs_.at(3)->locate_datum_for_write(eval_ctx_).set_string(chunk.text_);
    for (int64_t i = 0; i < MY_SPEC.column_exprs_.count(); ++i) {
      MY_SPEC.column_exprs_.at(i)->set_evaluated_projected(eval_ctx_);
    }
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
