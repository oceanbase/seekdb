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
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "common/json_type/ob_json_parse.h"


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
      reset_ai_split_document();
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
  reset_ai_split_document();
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
  reset_ai_split_document();
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

bool ObFunctionTableOp::is_split_space(const char ch)
{
  return ' ' == ch || '\t' == ch || '\n' == ch || '\r' == ch
      || '\f' == ch || '\v' == ch;
}

bool ObFunctionTableOp::is_markdown_heading(const ObString &content,
                                            const int64_t begin,
                                            const int64_t end,
                                            int64_t &heading_begin)
{
  int64_t pos = begin;
  int64_t spaces = 0;
  while (pos < end && spaces < 4 && ' ' == content.ptr()[pos]) {
    ++pos;
    ++spaces;
  }
  heading_begin = pos;
  int64_t hashes = 0;
  while (pos < end && hashes < 7 && '#' == content.ptr()[pos]) {
    ++pos;
    ++hashes;
  }
  return spaces <= 3 && hashes >= 1 && hashes <= 6
      && (pos == end || is_split_space(content.ptr()[pos]));
}

void ObFunctionTableOp::reset_ai_split_document()
{
  split_chunks_.reset();
  split_content_.reset();
  split_allocator_.reset();
}

int ObFunctionTableOp::parse_split_param(const ObString &param_str, SplitParam &param)
{
  int ret = OB_SUCCESS;
  ObJsonNode *root = nullptr;
  if (param_str.empty()) {
    // Keep defaults.
  } else if (OB_FAIL(ObJsonParser::get_tree(
                 &split_allocator_,
                 param_str,
                 root,
                 ObJsonParser::JSN_STRICT_FLAG,
                 ObJsonExprHelper::get_json_max_depth_config()))) {
    LOG_WARN("failed to parse ai_split_document parameters", K(ret), K(param_str));
  } else if (OB_ISNULL(root) || ObJsonNodeType::J_OBJECT != root->json_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ai_split_document parameters must be a JSON object", K(ret));
  } else {
    for (uint64_t i = 0; OB_SUCC(ret) && i < root->element_count(); ++i) {
      ObString key;
      ObIJsonBase *value = nullptr;
      if (OB_FAIL(root->get_object_value(i, key, value))) {
        LOG_WARN("failed to get ai_split_document parameter", K(ret), K(i));
      } else if (OB_ISNULL(value)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("JSON parameter value is null", K(ret), K(key));
      } else if (0 == key.case_compare("type")) {
        if (ObJsonNodeType::J_STRING != value->json_type()) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          const ObString val(value->get_data_length(), value->get_data());
          if (0 == val.case_compare("text")) {
            param.type_ = SplitType::TEXT;
          } else if (0 == val.case_compare("markdown")) {
            param.type_ = SplitType::MARKDOWN;
          } else {
            ret = OB_INVALID_ARGUMENT;
          }
        }
      } else if (0 == key.case_compare("by")) {
        if (ObJsonNodeType::J_STRING != value->json_type()) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          const ObString val(value->get_data_length(), value->get_data());
          if (0 == val.case_compare("word")) {
            param.by_ = SplitBy::WORD;
          } else if (0 == val.case_compare("sentence")) {
            param.by_ = SplitBy::SENTENCE;
          } else {
            ret = OB_INVALID_ARGUMENT;
          }
        }
      } else if (0 == key.case_compare("max") || 0 == key.case_compare("overlap")) {
        int64_t val = 0;
        if (ObJsonNodeType::J_INT == value->json_type()) {
          val = value->get_int();
        } else if (ObJsonNodeType::J_UINT == value->json_type()
                   && value->get_uint() <= static_cast<uint64_t>(INT64_MAX)) {
          val = static_cast<int64_t>(value->get_uint());
        } else {
          ret = OB_INVALID_ARGUMENT;
        }
        if (OB_SUCC(ret) && 0 == key.case_compare("max")) {
          param.max_ = val;
        } else if (OB_SUCC(ret)) {
          param.overlap_ = val;
        }
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("unsupported ai_split_document parameter", K(ret), K(key));
      }
      if (OB_FAIL(ret)) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "invalid AI_SPLIT_DOCUMENT parameters");
      }
    }
  }
  if (OB_SUCC(ret)
      && (param.max_ <= 0 || param.overlap_ < 0 || param.overlap_ >= param.max_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ai_split_document window", K(ret), K(param.max_), K(param.overlap_));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "max must be positive and overlap must be less than max");
  }
  return ret;
}

int ObFunctionTableOp::add_split_chunks(const ObIArray<SplitUnit> &units,
                                        const ObString &heading,
                                        const SplitParam &param)
{
  int ret = OB_SUCCESS;
  const int64_t step = param.max_ - param.overlap_;
  for (int64_t first = 0; OB_SUCC(ret) && first < units.count(); first += step) {
    const int64_t last = MIN(first + param.max_, units.count()) - 1;
    const int64_t offset = units.at(first).offset_;
    const int64_t end = units.at(last).offset_ + units.at(last).length_;
    const ObString body(end - offset, split_content_.ptr() + offset);
    SplitChunk chunk;
    chunk.offset_ = offset;
    chunk.length_ = end - offset;
    if (heading.empty()) {
      chunk.text_ = body;
    } else {
      const int64_t result_length = heading.length() + 1 + body.length();
      char *buf = nullptr;
      if (OB_UNLIKELY(result_length > OB_MAX_MYSQL_VARCHAR_LENGTH)) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("ai_split_document chunk is too long", K(ret), K(result_length));
      } else if (OB_ISNULL(buf = static_cast<char *>(split_allocator_.alloc(result_length)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate markdown chunk", K(ret), K(result_length));
      } else {
        MEMCPY(buf, heading.ptr(), heading.length());
        buf[heading.length()] = '\n';
        MEMCPY(buf + heading.length() + 1, body.ptr(), body.length());
        chunk.text_.assign_ptr(buf, result_length);
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(split_chunks_.push_back(chunk))) {
      LOG_WARN("failed to add document chunk", K(ret));
    }
    if (last + 1 >= units.count()) {
      break;
    }
  }
  return ret;
}

int ObFunctionTableOp::split_text_range(const int64_t begin,
                                        const int64_t end,
                                        const ObString &heading,
                                        const SplitParam &param)
{
  int ret = OB_SUCCESS;
  ObSEArray<SplitUnit, 64> units;
  if (begin < 0 || end < begin || end > split_content_.length()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid split range", K(ret), K(begin), K(end), K(split_content_.length()));
  } else if (SplitBy::WORD == param.by_) {
    int64_t pos = begin;
    while (OB_SUCC(ret) && pos < end) {
      while (pos < end && is_split_space(split_content_.ptr()[pos])) {
        ++pos;
      }
      const int64_t word_begin = pos;
      while (pos < end && !is_split_space(split_content_.ptr()[pos])) {
        ++pos;
      }
      if (word_begin < pos && OB_FAIL(units.push_back(SplitUnit(word_begin, pos - word_begin)))) {
        LOG_WARN("failed to add word unit", K(ret));
      }
    }
  } else {
    int64_t unit_begin = begin;
    int64_t pos = begin;
    while (OB_SUCC(ret) && pos < end) {
      int64_t terminator_len = 0;
      const unsigned char ch = static_cast<unsigned char>(split_content_.ptr()[pos]);
      if ('.' == ch || '!' == ch || '?' == ch) {
        terminator_len = 1;
      } else if (pos + 2 < end && 0xE3 == ch
                 && 0x80 == static_cast<unsigned char>(split_content_.ptr()[pos + 1])
                 && 0x82 == static_cast<unsigned char>(split_content_.ptr()[pos + 2])) {
        terminator_len = 3; // U+3002 IDEOGRAPHIC FULL STOP
      } else if (pos + 2 < end && 0xEF == ch
                 && 0xBC == static_cast<unsigned char>(split_content_.ptr()[pos + 1])
                 && (0x81 == static_cast<unsigned char>(split_content_.ptr()[pos + 2])
                     || 0x9F == static_cast<unsigned char>(split_content_.ptr()[pos + 2]))) {
        terminator_len = 3; // U+FF01/U+FF1F
      }
      if (terminator_len > 0) {
        int64_t unit_end = pos + terminator_len;
        while (unit_begin < unit_end && is_split_space(split_content_.ptr()[unit_begin])) {
          ++unit_begin;
        }
        while (unit_end > unit_begin && is_split_space(split_content_.ptr()[unit_end - 1])) {
          --unit_end;
        }
        if (unit_begin < unit_end
            && OB_FAIL(units.push_back(SplitUnit(unit_begin, unit_end - unit_begin)))) {
          LOG_WARN("failed to add sentence unit", K(ret));
        }
        unit_begin = pos + terminator_len;
        pos = unit_begin;
      } else {
        ++pos;
      }
    }
    while (unit_begin < end && is_split_space(split_content_.ptr()[unit_begin])) {
      ++unit_begin;
    }
    int64_t unit_end = end;
    while (unit_end > unit_begin && is_split_space(split_content_.ptr()[unit_end - 1])) {
      --unit_end;
    }
    if (OB_SUCC(ret) && unit_begin < unit_end
        && OB_FAIL(units.push_back(SplitUnit(unit_begin, unit_end - unit_begin)))) {
      LOG_WARN("failed to add trailing sentence", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(add_split_chunks(units, heading, param))) {
    LOG_WARN("failed to build chunks from split units", K(ret));
  }
  return ret;
}

int ObFunctionTableOp::split_markdown(const SplitParam &param)
{
  int ret = OB_SUCCESS;
  ObString heading;
  int64_t body_begin = 0;
  int64_t line_begin = 0;
  while (OB_SUCC(ret) && line_begin < split_content_.length()) {
    int64_t line_end = line_begin;
    while (line_end < split_content_.length() && '\n' != split_content_.ptr()[line_end]) {
      ++line_end;
    }
    int64_t content_end = line_end;
    if (content_end > line_begin && '\r' == split_content_.ptr()[content_end - 1]) {
      --content_end;
    }
    int64_t heading_begin = line_begin;
    if (is_markdown_heading(split_content_, line_begin, content_end, heading_begin)) {
      if (OB_FAIL(split_text_range(body_begin, line_begin, heading, param))) {
        LOG_WARN("failed to split markdown section", K(ret));
      } else {
        heading.assign_ptr(split_content_.ptr() + heading_begin, content_end - heading_begin);
        body_begin = line_end < split_content_.length() ? line_end + 1 : line_end;
      }
    }
    line_begin = line_end < split_content_.length() ? line_end + 1 : line_end;
  }
  if (OB_SUCC(ret)
      && OB_FAIL(split_text_range(body_begin, split_content_.length(), heading, param))) {
    LOG_WARN("failed to split final markdown section", K(ret));
  }
  return ret;
}

int ObFunctionTableOp::init_ai_split_document()
{
  int ret = OB_SUCCESS;
  ObDatum *content_datum = nullptr;
  ObDatum *param_datum = nullptr;
  SplitParam param;
  reset_ai_split_document();
  if (OB_ISNULL(MY_SPEC.value_expr_) || MY_SPEC.value_expr_->arg_cnt_ < 1
      || MY_SPEC.value_expr_->arg_cnt_ > 2) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("invalid ai_split_document expression", K(ret));
  } else if (OB_FAIL(MY_SPEC.value_expr_->args_[0]->eval(eval_ctx_, content_datum))) {
    LOG_WARN("failed to evaluate document content", K(ret));
  } else if (content_datum->is_null()) {
    // A NULL document produces no rows.
  } else {
    ObString content;
    ObExpr *content_expr = MY_SPEC.value_expr_->args_[0];
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(
            split_allocator_,
            *content_datum,
            content_expr->datum_meta_,
            content_expr->obj_meta_.has_lob_header(),
            content))) {
      LOG_WARN("failed to read document content", K(ret));
    } else if (OB_FAIL(ob_write_string(split_allocator_, content, split_content_))) {
      LOG_WARN("failed to copy document content", K(ret), K(content.length()));
    } else if (2 == MY_SPEC.value_expr_->arg_cnt_
               && OB_FAIL(MY_SPEC.value_expr_->args_[1]->eval(eval_ctx_, param_datum))) {
      LOG_WARN("failed to evaluate split parameters", K(ret));
    } else if (2 == MY_SPEC.value_expr_->arg_cnt_ && !param_datum->is_null()
               && OB_FAIL(parse_split_param(param_datum->get_string(), param))) {
      LOG_WARN("failed to parse split parameters", K(ret));
    } else if (SplitType::MARKDOWN == param.type_) {
      OZ (split_markdown(param));
    } else {
      OZ (split_text_range(0, split_content_.length(), ObString(), param));
    }
  }
  already_calc_ = true;
  node_idx_ = 0;
  return ret;
}

int ObFunctionTableOp::inner_get_next_row_ai_split_document()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (!already_calc_ && OB_FAIL(init_ai_split_document())) {
    LOG_WARN("failed to initialize ai_split_document", K(ret));
  } else if (node_idx_ >= split_chunks_.count()) {
    ret = OB_ITER_END;
  } else if (OB_UNLIKELY(MY_SPEC.column_exprs_.count() < 4)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ai_split_document output columns are incomplete",
             K(ret), K(MY_SPEC.column_exprs_.count()));
  } else {
    const SplitChunk &chunk = split_chunks_.at(node_idx_);
    MY_SPEC.column_exprs_.at(0)->locate_datum_for_write(eval_ctx_).set_int(node_idx_);
    MY_SPEC.column_exprs_.at(1)->locate_datum_for_write(eval_ctx_).set_int(chunk.offset_);
    MY_SPEC.column_exprs_.at(2)->locate_datum_for_write(eval_ctx_).set_int(chunk.length_);
    MY_SPEC.column_exprs_.at(3)->locate_datum_for_write(eval_ctx_).set_string(chunk.text_);
    for (int64_t i = 0; i < 4; ++i) {
      MY_SPEC.column_exprs_.at(i)->set_evaluated_projected(eval_ctx_);
    }
    ++node_idx_;
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
