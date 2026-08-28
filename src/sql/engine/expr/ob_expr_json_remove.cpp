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
#include "ob_expr_json_remove.h"
#include "ob_expr_json_func_helper.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase
{
namespace sql
{
ObExprJsonRemove::ObExprJsonRemove(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_REMOVE, N_JSON_REMOVE, MORE_THAN_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonRemove::~ObExprJsonRemove()
{
}

int ObExprJsonRemove::calc_result_typeN(ObExprResType& type,
                                        ObExprResType* types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;

  // json doc
  if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, 0, N_JSON_REMOVE))) {
  }
  // json path
  for (int64_t i = 1; OB_SUCC(ret) && i < param_num; i++) {
    if (OB_FAIL(ObJsonExprHelper::is_valid_for_path(types_stack, i))) {
    }
  }
  type.set_json();
  type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
  return ret;
}

static int remove_from_json(ObJsonPath *path_node, ObIJsonBase *child)
{
  INIT_SUCC(ret);
  // remove item in hits
  ObJsonPathBasicNode* last_node = path_node->last_path_node();
  // get node to be removed
  ObJsonNodeType type;
  ObIJsonBase* parent = nullptr;
  if (OB_FAIL(child->get_parent(parent)) || OB_ISNULL(parent)) {
    // may be null parent
    ret = OB_SUCCESS;
  } else if (FALSE_IT(type = parent->json_type())) {
  } else if (type == ObJsonNodeType::J_OBJECT && last_node->get_node_type() == JPN_MEMBER) {
    ObPathMember member = last_node->get_object();
    ObString key(member.len_, member.object_name_);
    if (OB_FAIL(parent->object_remove(key))) {
    }
  } else if (type == ObJsonNodeType::J_ARRAY && last_node->get_node_type() == JPN_ARRAY_CELL) {
    ObJsonArrayIndex array_index;
    if (OB_FAIL(last_node->get_first_array_index(parent->element_count(), array_index))) {
    } else if (array_index.is_within_bounds() && OB_FAIL(parent->array_remove(array_index.get_array_index()))) {
      LOG_WARN("fail to remove json_array node", K(ret));
    }
  }
  return ret;
}

int ObExprJsonRemove::eval_json_remove(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *json_doc = NULL;

  bool is_null_result = false;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  if (expr.datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
    LOG_WARN("invalid out put charset", K(ret), K(expr.datum_meta_.cs_type_));
  } else if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, 0,
                                                    json_doc, is_null_result))) {
  }

  ObJsonPathCache ctx_cache(&temp_allocator);
  ObJsonPathCache* path_cache = NULL;
  if (OB_SUCC(ret) && !is_null_result) {
    path_cache = ObJsonExprHelper::get_path_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);
    path_cache = ((path_cache != NULL) ? path_cache : &ctx_cache);
  }
  
  ObJsonSeekResult hits;
  for (int64_t i = 1; OB_SUCC(ret) && !is_null_result && i < expr.arg_cnt_; i++) {
    hits.clear();
    ObDatum *path_data = NULL;
    if (expr.args_[i]->datum_meta_.type_ == ObNullType) {
      is_null_result = true;
    } else if (OB_FAIL(temp_allocator.eval_arg(expr.args_[i], ctx, path_data))) {
      ret = OB_ERR_INVALID_JSON_PATH;
      LOG_USER_ERROR(OB_ERR_INVALID_JSON_PATH);
    } else {
      ObString path_val = path_data->get_string();
      ObJsonPath *json_path;
      if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(expr.args_[i], ctx, temp_allocator, path_val, is_null_result))) {
      } else if (OB_FAIL(ObJsonExprHelper::find_and_add_cache(path_cache, json_path, path_val, i, false))) {
      } else if (json_path->path_node_cnt() == 0) {
        ret = OB_ERR_JSON_VACUOUS_PATH;
        LOG_USER_ERROR(OB_ERR_JSON_VACUOUS_PATH); 
      } else if (OB_FAIL(json_doc->seek(*json_path, json_path->path_node_cnt(), true, false, hits))) {
      } else if (hits.size() == 0){
        continue;
      } else if (hits.size() > 1){
        ret = OB_INVALID_ERROR;
        LOG_WARN("More than one results after seek with only_need_one mode.", K(ret));
      } else {
        if (OB_FAIL(remove_from_json(json_path, hits[0]))) {
        } else if (OB_FAIL(ObJsonExprHelper::refresh_root_when_bin_rebuild_all(json_doc))) {
        }
      }
    }
  }

  // set result
  if (OB_FAIL(ret)) {
  } else if (is_null_result) {
    res.set_null();
  } else if (OB_FAIL(ObJsonExprHelper::pack_json_res(expr, ctx, temp_allocator, json_doc, res))) {
  }
  if (OB_NOT_NULL(json_doc)) {
    json_doc->reset();
  }
  return ret;
}

int ObExprJsonRemove::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  INIT_SUCC(ret);
  if (OB_FAIL(ObJsonExprHelper::init_json_expr_extra_info(expr_cg_ctx.allocator_, raw_expr, type_, rt_expr))) {
  } else {
    rt_expr.eval_func_ = eval_json_remove;
  }
  return ret;
}

}
}
