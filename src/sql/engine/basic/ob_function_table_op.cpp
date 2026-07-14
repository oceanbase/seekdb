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
#include "sql/parser/ob_item_type.h"

#include <cctype>
#include <cstdlib>
#include <string>


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
  if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value expr is not init", K(ret));
  } else if (T_FUN_SYS_AI_SPLIT_DOCUMENT == MY_SPEC.value_expr_->type_) {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_split_document;
    split_prepared_ = false;
    split_row_idx_ = 0;
    split_chunks_.reuse();
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
    }
    split_prepared_ = false;
    split_row_idx_ = 0;
    split_chunks_.reuse();
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
  split_prepared_ = false;
  split_row_idx_ = 0;
  split_chunks_.reuse();
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

bool json_find_string(const ObString &json, const char *key, ObString &value)
{
  const std::string input(json.ptr(), json.length());
  const std::string needle = std::string("\"") + key + "\"";
  size_t pos = input.find(needle);
  bool found = pos != std::string::npos;
  if (found) {
    pos = input.find(':', pos + needle.length());
    pos = pos == std::string::npos ? pos : input.find('"', pos + 1);
    const size_t end = pos == std::string::npos ? pos : input.find('"', pos + 1);
    found = pos != std::string::npos && end != std::string::npos;
    if (found) {
      value.assign_ptr(json.ptr() + pos + 1, static_cast<int32_t>(end - pos - 1));
    }
  }
  return found;
}

bool json_find_int(const ObString &json, const char *key, int64_t &value)
{
  const std::string input(json.ptr(), json.length());
  const std::string needle = std::string("\"") + key + "\"";
  size_t pos = input.find(needle);
  bool found = pos != std::string::npos;
  if (found) {
    pos = input.find(':', pos + needle.length());
    if (pos != std::string::npos) {
      ++pos;
      while (pos < input.length() && std::isspace(static_cast<unsigned char>(input[pos]))) { ++pos; }
      char *end = nullptr;
      value = std::strtoll(input.c_str() + pos, &end, 10);
      found = end != input.c_str() + pos;
    } else {
      found = false;
    }
  }
  return found;
}

int parse_split_options(const ObString &json, SplitOptions &options)
{
  int ret = OB_SUCCESS;
  if (!json.empty()) {
    ObString value;
    int64_t number = 0;
    if (json_find_string(json, "type", value)) {
      if (0 == value.case_compare("text")) {
        options.markdown_ = false;
      } else if (0 == value.case_compare("markdown")) {
        options.markdown_ = true;
      } else {
        ret = OB_INVALID_ARGUMENT;
      }
    }
    if (OB_SUCC(ret) && json_find_string(json, "by", value)) {
      if (0 == value.case_compare("sentence")) {
        options.by_sentence_ = true;
      } else if (0 == value.case_compare("word")) {
        options.by_sentence_ = false;
      } else {
        ret = OB_INVALID_ARGUMENT;
      }
    }
    if (OB_SUCC(ret) && json_find_int(json, "max", number)) {
      options.max_ = number;
    }
    if (OB_SUCC(ret) && json_find_int(json, "overlap", number)) {
      options.overlap_ = number;
    }
  }
  if (OB_SUCC(ret) && (options.max_ <= 0 || options.overlap_ < 0 || options.overlap_ >= options.max_)) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}
} // namespace

int ObFunctionTableOp::prepare_split_document()
{
  int ret = OB_SUCCESS;
  ObDatum *content_datum = nullptr;
  ObDatum *params_datum = nullptr;
  SplitOptions options;
  if (OB_ISNULL(MY_SPEC.value_expr_) || MY_SPEC.value_expr_->arg_cnt_ < 1
      || MY_SPEC.value_expr_->arg_cnt_ > 2) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ai_split_document expression", K(ret));
  } else if (OB_FAIL(MY_SPEC.value_expr_->args_[0]->eval(eval_ctx_, content_datum))) {
    LOG_WARN("failed to evaluate document content", K(ret));
  } else if (MY_SPEC.value_expr_->arg_cnt_ == 2
             && OB_FAIL(MY_SPEC.value_expr_->args_[1]->eval(eval_ctx_, params_datum))) {
    LOG_WARN("failed to evaluate split parameters", K(ret));
  } else if (content_datum->is_null()) {
    // A NULL document produces an empty table.
  } else if (MY_SPEC.value_expr_->arg_cnt_ == 2 && !params_datum->is_null()
             && OB_FAIL(parse_split_options(params_datum->get_string(), options))) {
    LOG_WARN("invalid ai_split_document parameters", K(ret), K(params_datum->get_string()));
  } else {
    const ObString source = content_datum->get_string();
    char *content_buf = static_cast<char *>(ctx_.get_allocator().alloc(source.length()));
    if (source.length() > 0 && OB_ISNULL(content_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      if (source.length() > 0) {
        MEMCPY(content_buf, source.ptr(), source.length());
      }
      const ObString content(source.length(), content_buf);
      if (options.markdown_) {
        ret = split_markdown(content, options.by_sentence_, options.max_, options.overlap_);
      } else {
        ret = split_plain_text(content, options.by_sentence_, options.max_, options.overlap_);
      }
    }
  }
  split_prepared_ = OB_SUCC(ret);
  return ret;
}

int ObFunctionTableOp::split_plain_text(const ObString &content,
                                        bool by_sentence,
                                        int64_t max_units,
                                        int64_t overlap,
                                        const ObString &heading,
                                        int64_t base_offset)
{
  int ret = OB_SUCCESS;
  struct Unit {
    int64_t start_;
    int64_t end_;
    TO_STRING_KV(K_(start), K_(end));
  };
  ObSEArray<Unit, 64> units;
  int64_t pos = 0;
  if (by_sentence) {
    while (OB_SUCC(ret) && pos < content.length()) {
      while (pos < content.length() && std::isspace(static_cast<unsigned char>(content.ptr()[pos]))) { ++pos; }
      if (pos >= content.length()) { break; }
      Unit unit{pos, pos};
      while (pos < content.length()) {
        const char ch = content.ptr()[pos++];
        if (ch == '.' || ch == '!' || ch == '?') { break; }
      }
      unit.end_ = pos;
      while (unit.end_ > unit.start_ && std::isspace(static_cast<unsigned char>(content.ptr()[unit.end_ - 1]))) { --unit.end_; }
      OZ (units.push_back(unit));
    }
  } else {
    while (OB_SUCC(ret) && pos < content.length()) {
      while (pos < content.length() && std::isspace(static_cast<unsigned char>(content.ptr()[pos]))) { ++pos; }
      if (pos >= content.length()) { break; }
      Unit unit{pos, pos};
      while (pos < content.length() && !std::isspace(static_cast<unsigned char>(content.ptr()[pos]))) { ++pos; }
      unit.end_ = pos;
      OZ (units.push_back(unit));
    }
  }

  const int64_t step = max_units - overlap;
  for (int64_t first = 0; OB_SUCC(ret) && first < units.count(); first += step) {
    const int64_t last = MIN(first + max_units, units.count()) - 1;
    const int64_t start = units.at(first).start_;
    const int64_t end = units.at(last).end_;
    ObString chunk_text(static_cast<int32_t>(end - start), content.ptr() + start);
    if (!heading.empty()) {
      const int64_t output_len = heading.length() + 1 + chunk_text.length();
      char *buf = static_cast<char *>(ctx_.get_allocator().alloc(output_len));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(buf, heading.ptr(), heading.length());
        buf[heading.length()] = '\n';
        MEMCPY(buf + heading.length() + 1, chunk_text.ptr(), chunk_text.length());
        chunk_text.assign_ptr(buf, static_cast<int32_t>(output_len));
      }
    }
    OZ (split_chunks_.push_back(DocumentChunk(base_offset + start,
                                              chunk_text.length(),
                                              chunk_text)));
  }
  return ret;
}

int ObFunctionTableOp::split_markdown(const ObString &content,
                                      bool by_sentence,
                                      int64_t max_units,
                                      int64_t overlap)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  ObString heading;
  int64_t body_start = 0;
  while (OB_SUCC(ret) && pos <= content.length()) {
    const int64_t line_start = pos;
    while (pos < content.length() && content.ptr()[pos] != '\n') { ++pos; }
    const int64_t line_end = pos;
    const bool is_heading = line_start < line_end && content.ptr()[line_start] == '#';
    if (is_heading) {
      if (body_start < line_start) {
        int64_t body_end = line_start;
        while (body_end > body_start && std::isspace(static_cast<unsigned char>(content.ptr()[body_end - 1]))) { --body_end; }
        if (body_end > body_start) {
          OZ (split_plain_text(ObString(static_cast<int32_t>(body_end - body_start), content.ptr() + body_start),
                               by_sentence, max_units, overlap, heading, body_start));
        }
      }
      heading.assign_ptr(content.ptr() + line_start, static_cast<int32_t>(line_end - line_start));
      body_start = pos < content.length() ? pos + 1 : pos;
    }
    if (pos >= content.length()) { break; }
    ++pos;
  }
  if (OB_SUCC(ret) && body_start < content.length()) {
    int64_t body_end = content.length();
    while (body_end > body_start && std::isspace(static_cast<unsigned char>(content.ptr()[body_end - 1]))) { --body_end; }
    if (body_end > body_start) {
      OZ (split_plain_text(ObString(static_cast<int32_t>(body_end - body_start), content.ptr() + body_start),
                           by_sentence, max_units, overlap, heading, body_start));
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_get_next_row_split_document()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (!split_prepared_ && OB_FAIL(prepare_split_document())) {
    LOG_WARN("failed to prepare split document", K(ret));
  } else if (split_row_idx_ >= split_chunks_.count()) {
    ret = OB_ITER_END;
  } else if (OB_UNLIKELY(MY_SPEC.column_exprs_.count() != 4)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected ai_split_document output column count", K(ret), K(MY_SPEC.column_exprs_.count()));
  } else {
    const DocumentChunk &chunk = split_chunks_.at(split_row_idx_);
    MY_SPEC.column_exprs_.at(0)->locate_datum_for_write(eval_ctx_).set_int(split_row_idx_);
    MY_SPEC.column_exprs_.at(1)->locate_datum_for_write(eval_ctx_).set_int(chunk.offset_);
    MY_SPEC.column_exprs_.at(2)->locate_datum_for_write(eval_ctx_).set_int(chunk.length_);
    MY_SPEC.column_exprs_.at(3)->locate_datum_for_write(eval_ctx_).set_string(chunk.text_);
    for (int64_t i = 0; i < 4; ++i) {
      MY_SPEC.column_exprs_.at(i)->set_evaluated_projected(eval_ctx_);
    }
    ++split_row_idx_;
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
