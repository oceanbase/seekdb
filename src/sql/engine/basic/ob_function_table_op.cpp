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
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"


namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER((ObFunctionTableSpec, ObOpSpec), value_expr_, column_exprs_, has_correlated_expr_);

namespace
{
static const int64_t AI_SPLIT_DOCUMENT_COLUMN_COUNT = 4;
static const int64_t AI_SPLIT_DOCUMENT_MAX_INPUT_LENGTH = common::OB_MAX_LONGTEXT_LENGTH;

bool ai_split_document_equal_string(const ObString &left, const char *right)
{
  return 0 == left.case_compare(right);
}

int ai_split_document_invalid_argument(const char *message)
{
  int ret = OB_INVALID_ARGUMENT;
  LOG_WARN("invalid AI_SPLIT_DOCUMENT argument", K(ret), K(message));
  LOG_USER_ERROR(OB_INVALID_ARGUMENT, message);
  return ret;
}

int get_ai_split_document_json_string(ObJsonObject &json_obj,
                                      const char *field_name,
                                      ObString &value)
{
  int ret = OB_SUCCESS;
  ObJsonNode *node = json_obj.get_value(ObString::make_string(field_name));
  if (OB_ISNULL(node)) {
    // use default
  } else if (ObJsonNodeType::J_STRING != node->json_type()) {
    ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT string parameter type");
    LOG_WARN("AI_SPLIT_DOCUMENT json string field has unexpected type",
             K(ret), K(field_name), K(node->json_type()));
  } else {
    value = static_cast<ObJsonString *>(node)->get_str();
  }
  return ret;
}

int get_ai_split_document_json_int(ObJsonObject &json_obj,
                                   const char *field_name,
                                   int64_t &value)
{
  int ret = OB_SUCCESS;
  ObJsonNode *node = json_obj.get_value(ObString::make_string(field_name));
  if (OB_ISNULL(node)) {
    // use default
  } else if (ObJsonNodeType::J_INT == node->json_type()) {
    value = static_cast<ObJsonInt *>(node)->value();
  } else if (ObJsonNodeType::J_UINT == node->json_type()) {
    const uint64_t uint_value = static_cast<ObJsonUint *>(node)->value();
    if (OB_UNLIKELY(uint_value > static_cast<uint64_t>(INT64_MAX))) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("AI_SPLIT_DOCUMENT integer parameter is too large",
               K(ret), K(field_name), K(uint_value));
    } else {
      value = static_cast<int64_t>(uint_value);
    }
  } else {
    ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT integer parameter type");
    LOG_WARN("AI_SPLIT_DOCUMENT json integer field has unexpected type",
             K(ret), K(field_name), K(node->json_type()));
  }
  return ret;
}
}

bool ObFunctionTableOp::is_ai_split_document() const
{
  return OB_NOT_NULL(MY_SPEC.value_expr_)
      && T_FUN_SYS_AI_SPLIT_DOCUMENT == MY_SPEC.value_expr_->type_;
}

int ObFunctionTableOp::inner_open()
{
  int ret = OB_SUCCESS;
  node_idx_ = 0;
  already_calc_ = false;
  if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value expr is not init", K(ret));
  } else if (is_ai_split_document()) {
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
    if (is_ai_split_document()) {
      reset_ai_split_document_state();
    } else if (MY_SPEC.has_correlated_expr_) {
      row_count_ = 0;
      col_count_ = 0;
      value_table_ = NULL;
      already_calc_ = false;
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
  reset_ai_split_document_state();
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
  reset_ai_split_document_state();
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

void ObFunctionTableOp::reset_ai_split_document_state()
{
  ai_split_chunks_.reset();
  ai_split_alloc_.reuse();
  ai_split_content_.reset();
  ai_split_next_idx_ = 0;
  ai_split_inited_ = false;
}

int ObFunctionTableOp::parse_ai_split_document_params(ObIAllocator &allocator,
                                                      const ObExpr &expr,
                                                      AISplitParams &params)
{
  int ret = OB_SUCCESS;
  ObDatum *params_datum = NULL;
  ObString params_str;
  ObIJsonBase *json_base = NULL;
  ObJsonObject *json_obj = NULL;
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("AI_SPLIT_DOCUMENT expects one or two arguments", K(ret), K(expr.arg_cnt_));
  } else if (1 == expr.arg_cnt_) {
    // use default parameters
  } else if (OB_ISNULL(expr.args_) || OB_ISNULL(expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI_SPLIT_DOCUMENT parameter expr is null", K(ret), KP(expr.args_));
  } else if (OB_FAIL(expr.args_[1]->eval(eval_ctx_, params_datum))) {
    LOG_WARN("failed to eval AI_SPLIT_DOCUMENT parameters", K(ret));
  } else if (OB_ISNULL(params_datum)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI_SPLIT_DOCUMENT parameter datum is null", K(ret));
  } else if (params_datum->is_null()) {
    // use default parameters
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(allocator,
                                                               *params_datum,
                                                               expr.args_[1]->datum_meta_,
                                                               expr.args_[1]->obj_meta_.has_lob_header(),
                                                               params_str,
                                                               &get_exec_ctx()))) {
    LOG_WARN("failed to read AI_SPLIT_DOCUMENT parameter string", K(ret));
  } else if (params_str.empty()) {
    ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT parameters must be a JSON object");
  } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator,
                                                      params_str,
                                                      ObJsonInType::JSON_TREE,
                                                      ObJsonInType::JSON_TREE,
                                                      json_base))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("failed to parse AI_SPLIT_DOCUMENT parameters json", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "AI_SPLIT_DOCUMENT parameters must be a JSON object");
  } else if (OB_ISNULL(json_base) || ObJsonNodeType::J_OBJECT != json_base->json_type()) {
    ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT parameters must be a JSON object");
    LOG_WARN("AI_SPLIT_DOCUMENT parameters json is not object", K(ret), KP(json_base));
  } else if (FALSE_IT(json_obj = static_cast<ObJsonObject *>(json_base))) {
  } else if (OB_FAIL(get_ai_split_document_json_string(*json_obj, "type", params.type_))) {
    LOG_WARN("failed to get AI_SPLIT_DOCUMENT type parameter", K(ret));
  } else if (OB_FAIL(get_ai_split_document_json_string(*json_obj, "by", params.by_))) {
    LOG_WARN("failed to get AI_SPLIT_DOCUMENT by parameter", K(ret));
  } else if (OB_FAIL(get_ai_split_document_json_int(*json_obj, "max", params.max_))) {
    LOG_WARN("failed to get AI_SPLIT_DOCUMENT max parameter", K(ret));
  } else if (OB_FAIL(get_ai_split_document_json_int(*json_obj, "overlap", params.overlap_))) {
    LOG_WARN("failed to get AI_SPLIT_DOCUMENT overlap parameter", K(ret));
  }

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(params.max_ <= 0)) {
      ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT max must be greater than 0");
    } else if (OB_UNLIKELY(params.max_ > AI_SPLIT_DOCUMENT_MAX_INPUT_LENGTH)) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("AI_SPLIT_DOCUMENT max is too large",
               K(ret), K(params.max_), K(AI_SPLIT_DOCUMENT_MAX_INPUT_LENGTH));
    } else if (OB_UNLIKELY(params.overlap_ < 0)) {
      ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT overlap must be greater than or equal to 0");
    } else if (OB_UNLIKELY(params.overlap_ >= params.max_)) {
      ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT overlap must be less than max");
    } else if (!ai_split_document_equal_string(params.type_, "text")
               && !ai_split_document_equal_string(params.type_, "markdown")) {
      ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT type must be text or markdown");
    } else if (!ai_split_document_equal_string(params.by_, "word")
               && !ai_split_document_equal_string(params.by_, "sentence")) {
      ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT by must be word or sentence");
    }
  }
  return ret;
}

int ObFunctionTableOp::init_ai_split_document()
{
  int ret = OB_SUCCESS;
  ObDatum *content_datum = NULL;
  ObString content;
  AISplitParams params;
  ObEvalCtx::TempAllocGuard tmp_alloc_guard(eval_ctx_);
  ObIAllocator &tmp_allocator = tmp_alloc_guard.get_allocator();

  reset_ai_split_document_state();
  if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI_SPLIT_DOCUMENT value expr is null", K(ret));
  } else if (OB_UNLIKELY(MY_SPEC.value_expr_->arg_cnt_ < 1 || MY_SPEC.value_expr_->arg_cnt_ > 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("AI_SPLIT_DOCUMENT expects one or two arguments",
             K(ret), K(MY_SPEC.value_expr_->arg_cnt_));
  } else if (OB_ISNULL(MY_SPEC.value_expr_->args_) || OB_ISNULL(MY_SPEC.value_expr_->args_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI_SPLIT_DOCUMENT content expr is null", K(ret), KP(MY_SPEC.value_expr_->args_));
  } else if (OB_FAIL(MY_SPEC.value_expr_->args_[0]->eval(eval_ctx_, content_datum))) {
    LOG_WARN("failed to eval AI_SPLIT_DOCUMENT content", K(ret));
  } else if (OB_ISNULL(content_datum)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI_SPLIT_DOCUMENT content datum is null", K(ret));
  } else if (content_datum->is_null()) {
    ai_split_inited_ = true;
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(tmp_allocator,
                                                               *content_datum,
                                                               MY_SPEC.value_expr_->args_[0]->datum_meta_,
                                                               MY_SPEC.value_expr_->args_[0]->obj_meta_.has_lob_header(),
                                                               content,
                                                               &get_exec_ctx()))) {
    LOG_WARN("failed to read AI_SPLIT_DOCUMENT content string", K(ret));
  } else if (OB_UNLIKELY(content.length() > AI_SPLIT_DOCUMENT_MAX_INPUT_LENGTH)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("AI_SPLIT_DOCUMENT content is too large",
             K(ret), K(content.length()), K(AI_SPLIT_DOCUMENT_MAX_INPUT_LENGTH));
  } else if (0 == content.length()) {
    ai_split_inited_ = true;
  } else if (OB_FAIL(parse_ai_split_document_params(tmp_allocator, *MY_SPEC.value_expr_, params))) {
    LOG_WARN("failed to parse AI_SPLIT_DOCUMENT parameters", K(ret));
  } else {
    char *content_buf = static_cast<char *>(ai_split_alloc_.alloc(content.length()));
    if (OB_ISNULL(content_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate AI_SPLIT_DOCUMENT content buffer", K(ret), K(content.length()));
    } else {
      MEMCPY(content_buf, content.ptr(), content.length());
      ai_split_content_.assign_ptr(content_buf, content.length());
      if (OB_FAIL(build_ai_split_document_chunks(params))) {
        LOG_WARN("failed to build AI_SPLIT_DOCUMENT chunks", K(ret));
      } else {
        ai_split_inited_ = true;
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::build_ai_split_document_chunks(const AISplitParams &params)
{
  int ret = OB_SUCCESS;
  if (ai_split_document_equal_string(params.type_, "markdown")) {
    if (OB_FAIL(build_ai_split_document_markdown_chunks(params))) {
      LOG_WARN("failed to build markdown chunks for AI_SPLIT_DOCUMENT", K(ret));
    }
  } else if (ai_split_document_equal_string(params.by_, "word")) {
    if (OB_FAIL(build_ai_split_document_word_chunks(params))) {
      LOG_WARN("failed to build word chunks for AI_SPLIT_DOCUMENT", K(ret));
    }
  } else if (ai_split_document_equal_string(params.by_, "sentence")) {
    if (OB_FAIL(build_ai_split_document_sentence_chunks(params))) {
      LOG_WARN("failed to build sentence chunks for AI_SPLIT_DOCUMENT", K(ret));
    }
  } else {
    ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT by must be word or sentence");
  }
  return ret;
}

int ObFunctionTableOp::build_ai_split_document_word_chunks(const AISplitParams &params)
{
  int ret = OB_SUCCESS;
  ObSEArray<AISplitRange, 128> words;
  const char *data = ai_split_content_.ptr();
  const int64_t len = ai_split_content_.length();
  int64_t pos = 0;
  while (OB_SUCC(ret) && pos < len) {
    while (pos < len && is_ai_split_ascii_space(data[pos])) {
      ++pos;
    }
    if (pos < len) {
      const int64_t word_start = pos;
      while (pos < len && !is_ai_split_ascii_space(data[pos])) {
        ++pos;
      }
      const int64_t word_end = pos;
      if (OB_FAIL(words.push_back(AISplitRange(word_start, word_end)))) {
        LOG_WARN("failed to append AI_SPLIT_DOCUMENT word range", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t step = params.max_ - params.overlap_;
    for (int64_t word_idx = 0; OB_SUCC(ret) && word_idx < words.count(); word_idx += step) {
      const int64_t last_word_idx = MIN(word_idx + params.max_, words.count()) - 1;
      if (OB_FAIL(add_ai_split_document_chunk(words.at(word_idx).start_,
                                              words.at(last_word_idx).end_))) {
        LOG_WARN("failed to add AI_SPLIT_DOCUMENT word chunk", K(ret), K(word_idx), K(last_word_idx));
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::build_ai_split_document_sentence_chunks(const AISplitParams &params)
{
  int ret = OB_SUCCESS;
  ObSEArray<AISplitRange, 64> sentences;
  const char *data = ai_split_content_.ptr();
  const int64_t len = ai_split_content_.length();
  int64_t pos = 0;
  while (OB_SUCC(ret) && pos < len) {
    while (pos < len && is_ai_split_ascii_space(data[pos])) {
      ++pos;
    }
    if (pos < len) {
      const int64_t sentence_start = pos;
      int64_t sentence_end = len;
      bool found_terminator = false;
      while (pos < len && !found_terminator) {
        const int64_t char_len = get_ai_split_utf8_char_len(data + pos, len - pos);
        if (is_ai_split_sentence_terminator(data + pos, char_len)) {
          pos += char_len;
          sentence_end = pos;
          found_terminator = true;
        } else {
          pos += char_len;
        }
      }
      if (!found_terminator) {
        sentence_end = pos;
        while (sentence_end > sentence_start && is_ai_split_ascii_space(data[sentence_end - 1])) {
          --sentence_end;
        }
      }
      if (sentence_end > sentence_start
          && OB_FAIL(sentences.push_back(AISplitRange(sentence_start, sentence_end)))) {
        LOG_WARN("failed to append AI_SPLIT_DOCUMENT sentence range", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t step = params.max_ - params.overlap_;
    for (int64_t sentence_idx = 0; OB_SUCC(ret) && sentence_idx < sentences.count(); sentence_idx += step) {
      const int64_t last_sentence_idx = MIN(sentence_idx + params.max_, sentences.count()) - 1;
      if (OB_FAIL(add_ai_split_document_chunk(sentences.at(sentence_idx).start_,
                                              sentences.at(last_sentence_idx).end_))) {
        LOG_WARN("failed to add AI_SPLIT_DOCUMENT sentence chunk",
                 K(ret), K(sentence_idx), K(last_sentence_idx));
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::build_ai_split_document_markdown_chunks(const AISplitParams &params)
{
  int ret = OB_SUCCESS;
  const char *data = ai_split_content_.ptr();
  const int64_t len = ai_split_content_.length();
  ObString current_heading;
  int64_t section_body_start = 0;
  int64_t pos = 0;
  while (OB_SUCC(ret) && pos < len) {
    const int64_t line_start = pos;
    while (pos < len && '\n' != data[pos] && '\r' != data[pos]) {
      ++pos;
    }
    const int64_t line_end = pos;
    int64_t next_line = pos;
    if (next_line < len && '\r' == data[next_line]) {
      ++next_line;
      if (next_line < len && '\n' == data[next_line]) {
        ++next_line;
      }
    } else if (next_line < len && '\n' == data[next_line]) {
      ++next_line;
    }

    ObString heading;
    if (is_ai_split_markdown_heading(data + line_start, line_end - line_start, heading)) {
      if (ai_split_document_equal_string(params.by_, "word")) {
        if (OB_FAIL(build_ai_split_document_markdown_word_chunks(params,
                                                                 current_heading,
                                                                 section_body_start,
                                                                 line_start))) {
          LOG_WARN("failed to build AI_SPLIT_DOCUMENT markdown word section",
                   K(ret), K(section_body_start), K(line_start));
        }
      } else if (ai_split_document_equal_string(params.by_, "sentence")) {
        if (OB_FAIL(build_ai_split_document_markdown_sentence_chunks(params,
                                                                     current_heading,
                                                                     section_body_start,
                                                                     line_start))) {
          LOG_WARN("failed to build AI_SPLIT_DOCUMENT markdown sentence section",
                   K(ret), K(section_body_start), K(line_start));
        }
      } else {
        ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT by must be word or sentence");
      }
      current_heading = heading;
      section_body_start = next_line;
    }
    pos = next_line;
  }
  if (OB_SUCC(ret)) {
    if (ai_split_document_equal_string(params.by_, "word")) {
      if (OB_FAIL(build_ai_split_document_markdown_word_chunks(params,
                                                               current_heading,
                                                               section_body_start,
                                                               len))) {
        LOG_WARN("failed to build final AI_SPLIT_DOCUMENT markdown word section",
                 K(ret), K(section_body_start), K(len));
      }
    } else if (ai_split_document_equal_string(params.by_, "sentence")) {
      if (OB_FAIL(build_ai_split_document_markdown_sentence_chunks(params,
                                                                   current_heading,
                                                                   section_body_start,
                                                                   len))) {
        LOG_WARN("failed to build final AI_SPLIT_DOCUMENT markdown sentence section",
                 K(ret), K(section_body_start), K(len));
      }
    } else {
      ret = ai_split_document_invalid_argument("AI_SPLIT_DOCUMENT by must be word or sentence");
    }
  }
  return ret;
}

int ObFunctionTableOp::build_ai_split_document_markdown_word_chunks(const AISplitParams &params,
                                                                    const ObString &heading,
                                                                    const int64_t body_start,
                                                                    const int64_t body_end)
{
  int ret = OB_SUCCESS;
  ObSEArray<AISplitRange, 128> words;
  const char *data = ai_split_content_.ptr();
  int64_t pos = body_start;
  if (body_start < 0 || body_end < body_start || body_end > ai_split_content_.length()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid AI_SPLIT_DOCUMENT markdown body range",
             K(ret), K(body_start), K(body_end), K(ai_split_content_.length()));
  }
  while (OB_SUCC(ret) && pos < body_end) {
    while (pos < body_end && is_ai_split_ascii_space(data[pos])) {
      ++pos;
    }
    if (pos < body_end) {
      const int64_t word_start = pos;
      while (pos < body_end && !is_ai_split_ascii_space(data[pos])) {
        ++pos;
      }
      const int64_t word_end = pos;
      if (OB_FAIL(words.push_back(AISplitRange(word_start, word_end)))) {
        LOG_WARN("failed to append AI_SPLIT_DOCUMENT markdown word range", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t step = params.max_ - params.overlap_;
    for (int64_t word_idx = 0; OB_SUCC(ret) && word_idx < words.count(); word_idx += step) {
      const int64_t last_word_idx = MIN(word_idx + params.max_, words.count()) - 1;
      if (OB_FAIL(add_ai_split_document_markdown_chunk(heading,
                                                       words.at(word_idx).start_,
                                                       words.at(last_word_idx).end_))) {
        LOG_WARN("failed to add AI_SPLIT_DOCUMENT markdown word chunk",
                 K(ret), K(word_idx), K(last_word_idx));
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::build_ai_split_document_markdown_sentence_chunks(const AISplitParams &params,
                                                                        const ObString &heading,
                                                                        const int64_t body_start,
                                                                        const int64_t body_end)
{
  int ret = OB_SUCCESS;
  ObSEArray<AISplitRange, 64> sentences;
  const char *data = ai_split_content_.ptr();
  int64_t pos = body_start;
  if (body_start < 0 || body_end < body_start || body_end > ai_split_content_.length()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid AI_SPLIT_DOCUMENT markdown body range",
             K(ret), K(body_start), K(body_end), K(ai_split_content_.length()));
  }
  while (OB_SUCC(ret) && pos < body_end) {
    while (pos < body_end && is_ai_split_ascii_space(data[pos])) {
      ++pos;
    }
    if (pos < body_end) {
      const int64_t sentence_start = pos;
      int64_t sentence_end = body_end;
      bool found_terminator = false;
      while (pos < body_end && !found_terminator) {
        const int64_t char_len = get_ai_split_utf8_char_len(data + pos, body_end - pos);
        if (is_ai_split_sentence_terminator(data + pos, char_len)) {
          pos += char_len;
          sentence_end = pos;
          found_terminator = true;
        } else {
          pos += char_len;
        }
      }
      if (!found_terminator) {
        sentence_end = pos;
        while (sentence_end > sentence_start && is_ai_split_ascii_space(data[sentence_end - 1])) {
          --sentence_end;
        }
      }
      if (sentence_end > sentence_start
          && OB_FAIL(sentences.push_back(AISplitRange(sentence_start, sentence_end)))) {
        LOG_WARN("failed to append AI_SPLIT_DOCUMENT markdown sentence range", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t step = params.max_ - params.overlap_;
    for (int64_t sentence_idx = 0; OB_SUCC(ret) && sentence_idx < sentences.count(); sentence_idx += step) {
      const int64_t last_sentence_idx = MIN(sentence_idx + params.max_, sentences.count()) - 1;
      if (OB_FAIL(add_ai_split_document_markdown_chunk(heading,
                                                       sentences.at(sentence_idx).start_,
                                                       sentences.at(last_sentence_idx).end_))) {
        LOG_WARN("failed to add AI_SPLIT_DOCUMENT markdown sentence chunk",
                 K(ret), K(sentence_idx), K(last_sentence_idx));
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::add_ai_split_document_chunk(const int64_t start, const int64_t end)
{
  int ret = OB_SUCCESS;
  ObString chunk_text;
  if (OB_UNLIKELY(start < 0 || end < start || end > ai_split_content_.length())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid AI_SPLIT_DOCUMENT chunk range",
             K(ret), K(start), K(end), K(ai_split_content_.length()));
  } else {
    chunk_text.assign_ptr(ai_split_content_.ptr() + start, end - start);
    if (OB_FAIL(add_ai_split_document_chunk(start, end, chunk_text))) {
      LOG_WARN("failed to add AI_SPLIT_DOCUMENT chunk", K(ret), K(start), K(end));
    }
  }
  return ret;
}

int ObFunctionTableOp::add_ai_split_document_chunk(const int64_t start,
                                                   const int64_t end,
                                                   const ObString &chunk_text)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(start < 0 || end < start || end > ai_split_content_.length())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid AI_SPLIT_DOCUMENT chunk range",
             K(ret), K(start), K(end), K(ai_split_content_.length()));
  } else {
    AISplitChunk chunk;
    chunk.chunk_id_ = ai_split_chunks_.count();
    chunk.chunk_offset_ = start;
    chunk.chunk_length_ = end - start;
    chunk.chunk_text_ = chunk_text;
    if (OB_FAIL(ai_split_chunks_.push_back(chunk))) {
      LOG_WARN("failed to append AI_SPLIT_DOCUMENT chunk", K(ret));
    }
  }
  return ret;
}

int ObFunctionTableOp::add_ai_split_document_markdown_chunk(const ObString &heading,
                                                            const int64_t start,
                                                            const int64_t end)
{
  int ret = OB_SUCCESS;
  ObString chunk_text;
  const int64_t body_len = end - start;
  if (OB_UNLIKELY(start < 0 || end < start || end > ai_split_content_.length())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid AI_SPLIT_DOCUMENT markdown chunk range",
             K(ret), K(start), K(end), K(ai_split_content_.length()));
  } else if (0 == body_len) {
    // no row for an empty body chunk
  } else if (heading.empty()) {
    chunk_text.assign_ptr(ai_split_content_.ptr() + start, body_len);
    if (OB_FAIL(add_ai_split_document_chunk(start, end, chunk_text))) {
      LOG_WARN("failed to add AI_SPLIT_DOCUMENT markdown chunk without heading", K(ret), K(start), K(end));
    }
  } else if (OB_UNLIKELY(heading.length() > AI_SPLIT_DOCUMENT_MAX_INPUT_LENGTH - body_len - 1)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("AI_SPLIT_DOCUMENT markdown chunk is too large",
             K(ret), K(heading.length()), K(body_len));
  } else {
    const int64_t chunk_text_len = heading.length() + 1 + body_len;
    char *buf = static_cast<char *>(ai_split_alloc_.alloc(chunk_text_len));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate AI_SPLIT_DOCUMENT markdown chunk buffer",
               K(ret), K(chunk_text_len));
    } else {
      MEMCPY(buf, heading.ptr(), heading.length());
      buf[heading.length()] = '\n';
      MEMCPY(buf + heading.length() + 1, ai_split_content_.ptr() + start, body_len);
      chunk_text.assign_ptr(buf, chunk_text_len);
      if (OB_FAIL(add_ai_split_document_chunk(start, end, chunk_text))) {
        LOG_WARN("failed to add AI_SPLIT_DOCUMENT markdown chunk with heading", K(ret), K(start), K(end));
      }
    }
  }
  return ret;
}

bool ObFunctionTableOp::is_ai_split_ascii_space(const char c)
{
  return ' ' == c || '\t' == c || '\n' == c || '\r' == c || '\f' == c || '\v' == c;
}

bool ObFunctionTableOp::is_ai_split_markdown_heading(const char *line,
                                                     const int64_t len,
                                                     ObString &heading)
{
  bool is_heading = false;
  int64_t sharp_count = 0;
  heading.reset();
  if (OB_ISNULL(line) || len <= 0) {
    is_heading = false;
  } else {
    while (sharp_count < len && sharp_count < 6 && '#' == line[sharp_count]) {
      ++sharp_count;
    }
    if (sharp_count > 0 && sharp_count < len && ' ' == line[sharp_count]) {
      heading.assign_ptr(line, len);
      is_heading = true;
    }
  }
  return is_heading;
}

bool ObFunctionTableOp::is_ai_split_sentence_terminator(const char *ptr, const int64_t len)
{
  bool is_terminator = false;
  if (OB_ISNULL(ptr) || len <= 0) {
    is_terminator = false;
  } else if (1 == len) {
    is_terminator = '.' == ptr[0] || '!' == ptr[0] || '?' == ptr[0];
  } else if (3 == len) {
    const unsigned char *u8 = reinterpret_cast<const unsigned char *>(ptr);
    is_terminator =
        (0xE3 == u8[0] && 0x80 == u8[1] && 0x82 == u8[2])
        || (0xEF == u8[0] && 0xBC == u8[1] && 0x81 == u8[2])
        || (0xEF == u8[0] && 0xBC == u8[1] && 0x9F == u8[2]);
  }
  return is_terminator;
}

int64_t ObFunctionTableOp::get_ai_split_utf8_char_len(const char *ptr, const int64_t remain)
{
  int64_t char_len = 1;
  if (OB_ISNULL(ptr) || remain <= 0) {
    char_len = 0;
  } else {
    const unsigned char first = static_cast<unsigned char>(ptr[0]);
    if (first < 0x80) {
      char_len = 1;
    } else if ((first & 0xE0) == 0xC0 && remain >= 2) {
      char_len = 2;
    } else if ((first & 0xF0) == 0xE0 && remain >= 3) {
      char_len = 3;
    } else if ((first & 0xF8) == 0xF0 && remain >= 4) {
      char_len = 4;
    } else {
      char_len = 1;
    }
  }
  return char_len;
}

int ObFunctionTableOp::inner_get_next_row_ai_split_document()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  clear_evaluated_flag();
  if (OB_ISNULL(plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get plan ctx", K(ret), K(plan_ctx));
  } else if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("failed to check status", K(ret));
  } else if (OB_UNLIKELY(MY_SPEC.column_exprs_.count() < AI_SPLIT_DOCUMENT_COLUMN_COUNT)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI_SPLIT_DOCUMENT output column exprs are invalid",
             K(ret), K(MY_SPEC.column_exprs_.count()));
  } else if (!ai_split_inited_ && OB_FAIL(init_ai_split_document())) {
    LOG_WARN("failed to init AI_SPLIT_DOCUMENT", K(ret));
  } else if (ai_split_next_idx_ >= ai_split_chunks_.count()) {
    ret = OB_ITER_END;
  } else {
    const AISplitChunk &chunk = ai_split_chunks_.at(ai_split_next_idx_);
    MY_SPEC.column_exprs_.at(0)->locate_datum_for_write(eval_ctx_).set_int(chunk.chunk_id_);
    MY_SPEC.column_exprs_.at(1)->locate_datum_for_write(eval_ctx_).set_int(chunk.chunk_offset_);
    MY_SPEC.column_exprs_.at(2)->locate_datum_for_write(eval_ctx_).set_int(chunk.chunk_length_);
    MY_SPEC.column_exprs_.at(3)->locate_datum_for_write(eval_ctx_).set_string(chunk.chunk_text_);
    for (int64_t i = 0; i < AI_SPLIT_DOCUMENT_COLUMN_COUNT; ++i) {
      MY_SPEC.column_exprs_.at(i)->set_evaluated_projected(eval_ctx_);
    }
    ++ai_split_next_idx_;
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
