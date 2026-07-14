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
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"

#include <limits.h>


namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER((ObFunctionTableSpec, ObOpSpec), value_expr_, column_exprs_, column_ids_, has_correlated_expr_);

namespace
{

struct SplitUnit
{
  SplitUnit() : start_(0), end_(0) {}
  SplitUnit(int64_t start, int64_t end) : start_(start), end_(end) {}
  int64_t start_;
  int64_t end_;
};

bool is_split_space(char value)
{
  return value == ' ' || value == '\t' || value == '\r' || value == '\n'
      || value == '\f' || value == '\v';
}

int64_t sentence_mark_length(const ObString &content, int64_t pos, int64_t end)
{
  int64_t length = 0;
  const unsigned char current = static_cast<unsigned char>(content[pos]);
  if (content[pos] == '.' || content[pos] == '!' || content[pos] == '?') {
    length = 1;
  } else if (pos + 2 < end
             && current == 0xE3
             && static_cast<unsigned char>(content[pos + 1]) == 0x80
             && static_cast<unsigned char>(content[pos + 2]) == 0x82) {
    length = 3;
  } else if (pos + 2 < end
             && current == 0xEF
             && static_cast<unsigned char>(content[pos + 1]) == 0xBC
             && (static_cast<unsigned char>(content[pos + 2]) == 0x81
                 || static_cast<unsigned char>(content[pos + 2]) == 0x9F)) {
    length = 3;
  }
  return length;
}

int collect_word_units(const ObString &content,
                       int64_t range_start,
                       int64_t range_end,
                       ObIArray<SplitUnit> &units)
{
  int ret = OB_SUCCESS;
  int64_t pos = range_start;
  while (OB_SUCC(ret) && pos < range_end) {
    while (pos < range_end && is_split_space(content[pos])) {
      ++pos;
    }
    const int64_t start = pos;
    while (pos < range_end && !is_split_space(content[pos])) {
      ++pos;
    }
    if (start < pos && OB_FAIL(units.push_back(SplitUnit(start, pos)))) {
      LOG_WARN("store word unit failed", K(ret));
    }
  }
  return ret;
}

int collect_sentence_units(const ObString &content,
                           int64_t range_start,
                           int64_t range_end,
                           ObIArray<SplitUnit> &units)
{
  int ret = OB_SUCCESS;
  int64_t pos = range_start;
  while (OB_SUCC(ret) && pos < range_end) {
    while (pos < range_end && is_split_space(content[pos])) {
      ++pos;
    }
    const int64_t start = pos;
    int64_t sentence_end = range_end;
    while (pos < range_end) {
      const int64_t mark_length = sentence_mark_length(content, pos, range_end);
      if (mark_length > 0) {
        sentence_end = pos + mark_length;
        pos = sentence_end;
        break;
      }
      ++pos;
    }
    while (sentence_end > start && is_split_space(content[sentence_end - 1])) {
      --sentence_end;
    }
    if (start < sentence_end && OB_FAIL(units.push_back(SplitUnit(start, sentence_end)))) {
      LOG_WARN("store sentence unit failed", K(ret));
    }
  }
  return ret;
}

int deep_copy_chunk_text(ObIAllocator &allocator,
                         const ObString &heading,
                         const ObString &body,
                         ObString &chunk_text)
{
  int ret = OB_SUCCESS;
  const int64_t result_length = heading.empty() ? body.length() : heading.length() + 1 + body.length();
  char *buffer = nullptr;
  if (result_length == 0) {
    chunk_text.reset();
  } else if (OB_ISNULL(buffer = static_cast<char *>(allocator.alloc(result_length)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate chunk text failed", K(ret), K(result_length));
  } else {
    int64_t pos = 0;
    if (!heading.empty()) {
      MEMCPY(buffer + pos, heading.ptr(), heading.length());
      pos += heading.length();
      buffer[pos++] = '\n';
    }
    MEMCPY(buffer + pos, body.ptr(), body.length());
    chunk_text.assign_ptr(buffer, static_cast<int32_t>(result_length));
  }
  return ret;
}

int append_range_chunks(ObIAllocator &allocator,
                        const ObString &content,
                        const ObString &heading,
                        int64_t range_start,
                        int64_t range_end,
                        bool split_by_sentence,
                        int64_t max_units,
                        int64_t overlap,
                        ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  ObSEArray<SplitUnit, 64> units;
  if (split_by_sentence) {
    ret = collect_sentence_units(content, range_start, range_end, units);
  } else {
    ret = collect_word_units(content, range_start, range_end, units);
  }
  const int64_t step = max_units - overlap;
  for (int64_t start_idx = 0; OB_SUCC(ret) && start_idx < units.count(); start_idx += step) {
    const int64_t end_idx = MIN(start_idx + max_units, units.count());
    const int64_t chunk_start = units.at(start_idx).start_;
    const int64_t chunk_end = units.at(end_idx - 1).end_;
    ObAISplitDocumentChunk chunk;
    chunk.offset_ = chunk_start;
    chunk.length_ = chunk_end - chunk_start;
    const ObString body(static_cast<int32_t>(chunk.length_), content.ptr() + chunk_start);
    if (OB_FAIL(deep_copy_chunk_text(allocator, heading, body, chunk.text_))) {
      LOG_WARN("copy chunk text failed", K(ret));
    } else if (OB_FAIL(chunks.push_back(chunk))) {
      LOG_WARN("store document chunk failed", K(ret));
    }
  }
  return ret;
}

bool is_markdown_heading(const ObString &content,
                         int64_t line_start,
                         int64_t line_end,
                         int64_t &heading_end)
{
  int64_t pos = line_start;
  while (pos < line_end && pos - line_start < 6 && content[pos] == '#') {
    ++pos;
  }
  const int64_t level = pos - line_start;
  const bool is_heading = level > 0
      && (pos == line_end || content[pos] == ' ' || content[pos] == '\t');
  heading_end = line_end;
  while (heading_end > line_start
         && (content[heading_end - 1] == '\r'
             || content[heading_end - 1] == ' '
             || content[heading_end - 1] == '\t')) {
    --heading_end;
  }
  return is_heading;
}

int split_markdown_document(ObIAllocator &allocator,
                            const ObString &content,
                            bool split_by_sentence,
                            int64_t max_units,
                            int64_t overlap,
                            ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  ObString heading;
  int64_t section_start = 0;
  int64_t line_start = 0;
  while (OB_SUCC(ret) && line_start < content.length()) {
    int64_t line_end = line_start;
    while (line_end < content.length() && content[line_end] != '\n') {
      ++line_end;
    }
    int64_t heading_end = line_end;
    if (is_markdown_heading(content, line_start, line_end, heading_end)) {
      if (OB_FAIL(append_range_chunks(allocator, content, heading,
                                      section_start, line_start,
                                      split_by_sentence, max_units, overlap, chunks))) {
        LOG_WARN("split markdown section failed", K(ret));
      } else {
        heading.assign_ptr(content.ptr() + line_start,
                           static_cast<int32_t>(heading_end - line_start));
        section_start = line_end < content.length() ? line_end + 1 : line_end;
      }
    }
    line_start = line_end < content.length() ? line_end + 1 : line_end;
  }
  if (OB_SUCC(ret)) {
    ret = append_range_chunks(allocator, content, heading,
                              section_start, content.length(),
                              split_by_sentence, max_units, overlap, chunks);
  }
  return ret;
}

int get_json_integer(ObJsonObject *object, const char *key, int64_t default_value, int64_t &value)
{
  int ret = OB_SUCCESS;
  value = default_value;
  ObJsonNode *node = object == nullptr ? nullptr : object->get_value(key);
  if (node == nullptr) {
  } else if (node->json_type() == ObJsonNodeType::J_INT) {
    value = static_cast<ObJsonInt *>(node)->value();
  } else if (node->json_type() == ObJsonNodeType::J_UINT
             && static_cast<ObJsonUint *>(node)->value() <= INT64_MAX) {
    value = static_cast<int64_t>(static_cast<ObJsonUint *>(node)->value());
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "AI_SPLIT_DOCUMENT numeric parameters must be integers");
  }
  return ret;
}

int get_json_string(ObJsonObject *object, const char *key, const ObString &default_value, ObString &value)
{
  int ret = OB_SUCCESS;
  value = default_value;
  ObJsonNode *node = object == nullptr ? nullptr : object->get_value(key);
  if (node == nullptr) {
  } else if (node->json_type() == ObJsonNodeType::J_STRING) {
    value = static_cast<ObJsonString *>(node)->value();
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "AI_SPLIT_DOCUMENT type and by parameters must be strings");
  }
  return ret;
}

} // namespace

int ObFunctionTableOp::inner_open()
{
  int ret = OB_SUCCESS;
  node_idx_ = 0;
  already_calc_ = false;
  split_document_initialized_ = false;
  split_document_chunks_.reset();
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
    if (MY_SPEC.has_correlated_expr_) {
      row_count_ = 0;
      col_count_ = 0;
      value_table_ = NULL;
      already_calc_ = false;
      split_document_initialized_ = false;
      split_document_chunks_.reset();
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
  split_document_initialized_ = false;
  split_document_chunks_.reset();
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

int ObFunctionTableOp::init_ai_split_document()
{
  int ret = OB_SUCCESS;
  ObExpr *value_expr = MY_SPEC.value_expr_;
  ObDatum *content_datum = nullptr;
  ObDatum *params_datum = nullptr;
  ObEvalCtx::TempAllocGuard alloc_guard(eval_ctx_);
  ObString content;
  ObString params;
  ObString document_type("markdown");
  ObString split_by("word");
  int64_t max_units = 256;
  int64_t overlap = 0;
  ObJsonObject *params_object = nullptr;
  if (OB_ISNULL(value_expr) || value_expr->arg_cnt_ < 1 || value_expr->arg_cnt_ > 2) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid AI_SPLIT_DOCUMENT expression", K(ret), KP(value_expr));
  } else if (OB_FAIL(value_expr->args_[0]->eval(eval_ctx_, content_datum))) {
    LOG_WARN("evaluate document content failed", K(ret));
  } else if (content_datum->is_null()) {
    split_document_initialized_ = true;
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                       alloc_guard.get_allocator(), *content_datum,
                       value_expr->args_[0]->datum_meta_,
                       value_expr->args_[0]->obj_meta_.has_lob_header(), content))) {
    LOG_WARN("read document content failed", K(ret));
  } else if (value_expr->arg_cnt_ == 2
             && OB_FAIL(value_expr->args_[1]->eval(eval_ctx_, params_datum))) {
    LOG_WARN("evaluate split parameters failed", K(ret));
  } else if (value_expr->arg_cnt_ == 2 && !params_datum->is_null()
             && OB_FAIL(ObTextStringHelper::read_real_string_data(
                          alloc_guard.get_allocator(), *params_datum,
                          value_expr->args_[1]->datum_meta_,
                          value_expr->args_[1]->obj_meta_.has_lob_header(), params))) {
    LOG_WARN("read split parameters failed", K(ret));
  } else if (!params.empty()
             && OB_FAIL(ObAIFuncJsonUtils::get_json_object_form_str(
                          alloc_guard.get_allocator(), params, params_object))) {
    LOG_WARN("parse AI_SPLIT_DOCUMENT parameters failed", K(ret));
  } else if (OB_FAIL(get_json_string(params_object, "type", ObString("markdown"), document_type))) {
    LOG_WARN("get document type failed", K(ret));
  } else if (OB_FAIL(get_json_string(params_object, "by", ObString("word"), split_by))) {
    LOG_WARN("get split unit failed", K(ret));
  } else if (OB_FAIL(get_json_integer(params_object, "max", 256, max_units))) {
    LOG_WARN("get max units failed", K(ret));
  } else if (OB_FAIL(get_json_integer(params_object, "overlap", 0, overlap))) {
    LOG_WARN("get overlap failed", K(ret));
  } else if (document_type.case_compare("text") != 0
             && document_type.case_compare("markdown") != 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "AI_SPLIT_DOCUMENT type must be text or markdown");
  } else if (split_by.case_compare("word") != 0
             && split_by.case_compare("sentence") != 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "AI_SPLIT_DOCUMENT by must be word or sentence");
  } else if (max_units <= 0 || max_units > INT32_MAX
             || overlap < 0 || overlap >= max_units) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                   "AI_SPLIT_DOCUMENT requires 0 < max <= INT32_MAX and 0 <= overlap < max");
  } else {
    const bool split_by_sentence = split_by.case_compare("sentence") == 0;
    if (document_type.case_compare("markdown") == 0) {
      ret = split_markdown_document(ctx_.get_allocator(), content,
                                    split_by_sentence, max_units, overlap,
                                    split_document_chunks_);
    } else {
      ret = append_range_chunks(ctx_.get_allocator(), content, ObString(),
                                0, content.length(), split_by_sentence,
                                max_units, overlap, split_document_chunks_);
    }
    if (OB_SUCC(ret)) {
      split_document_initialized_ = true;
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_get_next_row_ai_split_document()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("failed to check status", K(ret));
  } else if (!split_document_initialized_ && OB_FAIL(init_ai_split_document())) {
    LOG_WARN("initialize AI_SPLIT_DOCUMENT failed", K(ret));
  } else if (node_idx_ >= split_document_chunks_.count()) {
    ret = OB_ITER_END;
  } else if (MY_SPEC.column_exprs_.count() != MY_SPEC.column_ids_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("function table column metadata mismatch", K(ret),
             K(MY_SPEC.column_exprs_.count()), K(MY_SPEC.column_ids_.count()));
  } else {
    const ObAISplitDocumentChunk &chunk = split_document_chunks_.at(node_idx_);
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.column_exprs_.count(); ++i) {
      ObExpr *column_expr = MY_SPEC.column_exprs_.at(i);
      const uint64_t column_id = MY_SPEC.column_ids_.at(i);
      if (OB_ISNULL(column_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("function table column expression is null", K(ret), K(i));
      } else {
        ObDatum &datum = column_expr->locate_datum_for_write(eval_ctx_);
        switch (column_id - OB_APP_MIN_COLUMN_ID) {
          case 0:
            datum.set_int(node_idx_);
            break;
          case 1:
            datum.set_int(chunk.offset_);
            break;
          case 2:
            datum.set_int(chunk.length_);
            break;
          case 3:
            datum.set_string(chunk.text_);
            break;
          default:
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected AI_SPLIT_DOCUMENT column", K(ret), K(column_id));
            break;
        }
        if (OB_SUCC(ret)) {
          column_expr->set_evaluated_projected(eval_ctx_);
        }
      }
    }
    if (OB_SUCC(ret)) {
      ++node_idx_;
    }
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
