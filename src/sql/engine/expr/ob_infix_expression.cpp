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


#include "ob_infix_expression.h"
#include "common/json_type/ob_json_tree.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/expr/ob_expr_regexp.h"
#include "share/ob_unique_index_row_transformer.h"
#include "share/ob_json_access_utils.h"
#include "sql/engine/expr/ob_sql_expression.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace sql
{

OB_DEF_SERIALIZE_SIZE(ObInfixExprItem)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObInfixExprItem, ObPostExprItem));
  bool param_lazy = false;
  bool is_called_in_sql = IS_EXPR_OP(get_item_type()) ? get_expr_operator()->is_called_in_sql() : true;
  LST_DO_CODE(OB_UNIS_ADD_LEN, param_idx_, param_num_, param_lazy,
                               is_called_in_sql, is_boolean_);
  return len;
}

OB_DEF_SERIALIZE(ObInfixExprItem)
{
  int ret = OB_SUCCESS;
  BASE_SER((ObInfixExprItem, ObPostExprItem));
  bool param_lazy = false;
  bool is_called_in_sql = IS_EXPR_OP(get_item_type()) ? get_expr_operator()->is_called_in_sql() : true;
  LST_DO_CODE(OB_UNIS_ENCODE, param_idx_, param_num_, param_lazy,
                              is_called_in_sql, is_boolean_);
  return ret;
}

OB_DEF_DESERIALIZE(ObInfixExprItem)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObPostExprItem::deserialize(CURRENT_CONTEXT->get_arena_allocator(), buf, data_len, pos))) {
  } else {
    bool param_lazy = false;
    bool is_called_in_sql = true;
    LST_DO_CODE(OB_UNIS_DECODE, param_idx_, param_num_, param_lazy,
                                is_called_in_sql, is_boolean_);
    if (IS_EXPR_OP(get_item_type())) {
      param_lazy_eval_ = get_expr_operator()->is_param_lazy_eval();
      get_expr_operator()->set_is_called_in_sql(is_called_in_sql);
    }
  }
  return ret;
}

// same with ob_write_expr_item in ob_postfix_expression.cpp
int ObInfixExprItem::deep_copy(common::ObIAllocator &alloc, const bool only_obj /* = false */)
{
  int ret = OB_SUCCESS;
  if (T_REF_COLUMN == item_type_) {
    // do nothing
  } else if (IS_DATATYPE_OR_QUESTIONMARK_OP(item_type_)) {
    DatumValue owned;
    if (OB_FAIL(clone_typed_value(get_obj(), owned, alloc))) {
    } else {
      new(&v2_.value_bytes_) DatumValue(owned);
    }
  } else if (IS_EXPR_OP(item_type_)) {
    if (!only_obj) {
      ObExprOperatorFactory factory(alloc);
      ObExprOperator *op = NULL;
      if (OB_FAIL(factory.alloc(item_type_, op))) {
      } else if (OB_ISNULL(op)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(op->assign(*get_expr_operator()))) {
      } else {
        v2_.op_ = op;
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

ObInfixExpression::ObInfixExpression(ObIAllocator &alloc, const int64_t expr_cnt)
  : alloc_(alloc), exprs_()
{
  UNUSED(expr_cnt);
}

ObInfixExpression::~ObInfixExpression()
{
}

void ObInfixExpression::reset()
{
  exprs_.reset(alloc_);
}

OB_DEF_SERIALIZE_SIZE(ObInfixExpression)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN_ARRAY(exprs_.get_ptr(), exprs_.count());
  return len;
}

OB_DEF_SERIALIZE(ObInfixExpression)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE_ARRAY(exprs_.get_ptr(), exprs_.count());
  return ret;
}

OB_DEF_DESERIALIZE(ObInfixExpression)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  OB_UNIS_DECODE(count);
  if (OB_SUCC(ret) && count > 0) {
    if (OB_FAIL(exprs_.init(count, alloc_))) {
    } else {
      ObInfixExprItem tmp_expr_item;
      for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
        OB_UNIS_DECODE(tmp_expr_item);
        if (OB_FAIL(ret)) {
          // do mothing
        } else if (OB_FAIL(exprs_.push_back(tmp_expr_item))) {
        }
      }
    }
  }
  return ret;
}

int ObInfixExpression::set_item_count(const int64_t count)
{
  int ret = OB_SUCCESS;
  if (count < 0 || count >= std::numeric_limits<uint16_t>::max()) {
    ret = OB_INVALID_ARGUMENT;
  }
  return exprs_.init(count, alloc_);
}

int ObInfixExpression::assign(const ObInfixExpression &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    if (other.exprs_.count() > 0) {
      if (OB_FAIL(exprs_.reserve(other.exprs_.count(), alloc_))) {
      } else {
        FOREACH_CNT_X(item, other.exprs_, OB_SUCC(ret)) {
          if (OB_FAIL(exprs_.push_back(*item))) {
          } else {
            if (OB_FAIL(exprs_.at(exprs_.count() - 1).deep_copy(alloc_))) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObInfixExpression::add_expr_item(const ObInfixExprItem &item)
{
  int ret = OB_SUCCESS;
  const bool copy_obj_only = true;
  // check T_OP_AGG_PARAM_LIST must be the root node of expression
  if (T_OP_AGG_PARAM_LIST == item.get_item_type() && 0 != exprs_.count()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(exprs_.push_back(item))) {
  } else {
    ObInfixExprItem &new_item = exprs_.at(exprs_.count() - 1);
    if (OB_FAIL(new_item.deep_copy(alloc_, copy_obj_only))) {
    } else if (IS_EXPR_OP(new_item.get_item_type())) {
      if (OB_ISNULL(new_item.get_expr_operator())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        if (new_item.get_expr_operator()->is_param_lazy_eval()) {
          new_item.set_param_lazy_eval();
        }
      }
    }
  }
  return ret;
}



// copy && paste from ob_postfix_expression.cpp
int ObInfixExpression::generate_idx_for_regexp_ops(int16_t &cur_regexp_op_count)
{
  /*please note that, cur_regexp_op_count is a reference of a variable in ObCodeGeneratorImpl
   * it is used as a total counter here
   * This function maybe be called many times within different exprs
   * so, you should NOT set cur_regexp_op_count to be 0 here.
   */
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < exprs_.count(); ++i) {
    if (T_OP_REGEXP == exprs_.at(i).get_item_type()) {
      ObExprRegexp *regexp_op = static_cast<ObExprRegexp *>(exprs_.at(i).get_expr_operator());
      if (OB_ISNULL(regexp_op)) {
        ret = OB_BAD_NULL_ERROR;
      } else {
        regexp_op->set_regexp_idx(cur_regexp_op_count++);
      }
    }
  }
  return ret;
}

// copy && paste from ob_postfix_expression.cpp
bool ObInfixExpression::is_equijoin_cond(int64_t &c1, int64_t &c2,
    common::SqlValueType &cmp_type, common::ObCollationType &cmp_cs_type,
    bool &is_null_safe) const
{
  bool ret = false;
  if (exprs_.count() == 3) {
    if ((exprs_[0].get_item_type() == T_OP_EQ || exprs_[0].get_item_type() == T_OP_NSEQ)
        && exprs_[1].get_item_type() == T_REF_COLUMN
        && exprs_[2].get_item_type() == T_REF_COLUMN) {
      c1 = exprs_[1].get_column();
      c2 = exprs_[2].get_column();
      cmp_type = exprs_[0].get_expr_operator()->get_result_type().get_calc_type();
      cmp_cs_type = exprs_[0].get_expr_operator()->get_result_type().get_calc_collation_type();
      if (exprs_[0].get_item_type() == T_OP_NSEQ) {
        is_null_safe = true;
      }
      ret = true;
    }
  }
  return ret;
}

// copy && paste from ob_postfix_expression.cpp


} // end namespace sql
} // end namespace oceanbase
