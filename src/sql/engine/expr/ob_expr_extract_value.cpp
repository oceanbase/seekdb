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

#include "ob_expr_extract_value.h"
#include "sql/engine/expr/ob_expr_xml_func_helper.h"
#include "sql/engine/ob_exec_context.h"

#define USING_LOG_PREFIX SQL_ENG


using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObExprExtractValue::ObExprExtractValue(common::ObIAllocator &alloc) 
  : ObFuncExprOperator(alloc, T_FUN_SYS_XML_EXTRACTVALUE, N_EXTRACTVALUE, MORE_THAN_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprExtractValue::~ObExprExtractValue() {}

int ObExprExtractValue::calc_result_typeN(ObExprResType &type,
                                          ObExprResType *types,
                                          int64_t param_num,
                                          common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num != 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("invalid param number", K(ret), K(param_num));
  } else if (ObNullType == types[1].get_type()) {
    ret = OB_ERR_INVALID_XPATH_EXPRESSION;
    LOG_WARN("xpath syntax get null", K(ret));
  } else if (!ob_is_string_tc(types[1].get_type())) {
    ret = OB_ERR_INVALID_XPATH_EXPRESSION;
    LOG_WARN("xpath syntax get invalid type", K(ret), K(types[1].get_type()));
  } else {
    for (int8_t i = 0; OB_SUCC(ret) && i < param_num; i++) {
      ObObjType param_type = types[i].get_type();
      if (param_type == ObNullType) {
      } else if (ob_is_string_type(param_type)) {
        if (ob_is_string_tc(param_type)) {
          types[i].set_calc_type(ObVarcharType);
        }
        if (types[i].get_charset_type() != CHARSET_UTF8MB4) {
          types[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        }
      } else {
        ret = OB_ERR_INVALID_XPATH_EXPRESSION;
        LOG_WARN("xpath syntax get invalid type", K(ret), K(param_type));
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else {
    type.set_type(ObLongTextType);
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_IMPLICIT);
    type.set_length(OB_MAX_MYSQL_VARCHAR_LENGTH);
    type.set_length_semantics(LS_BYTE);
  }
  return ret;
}

int ObExprExtractValue::eval_mysql_extract_value(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  uint64_t tenant_id = ObMultiModeExprHelper::get_tenant_id(ctx.exec_ctx_.get_my_session());
  MultimodeAlloctor allocator(tmp_alloc_g.get_allocator(), expr.type_, tenant_id, ret);
  ObTextStringDatumResult output_result(expr.datum_meta_.type_, &expr, &ctx, &res);
  ObString xml_frag;
  ObString xpath_expr;
  ObIMulModeBase *xml_base = nullptr;
  ObPathExprIter xpath_iter(&allocator);
  ObStringBuffer xml_res(&allocator);

  ObMulModeMemCtx* xml_mem_ctx = nullptr;
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(tenant_id, "XMLModule"));
  if (OB_FAIL(ObXmlUtil::create_mulmode_tree_context(&allocator, xml_mem_ctx))) {
    LOG_WARN("fail to create tree memory context", K(ret));
  } else if (expr.arg_cnt_ != 2) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("invalid arg_cnt_", K(ret), K(expr.arg_cnt_));
  } else if (!expr.args_[1]->is_const_expr()) {
    ret = OB_XPATH_EXPRESSION_UNSUPPORTED;
    LOG_WARN("args_[1] get const expr invalid", K(ret), K(expr.args_[1]));
  } else if (OB_FAIL(ObXMLExprHelper::get_str_from_expr(expr.args_[0], ctx, xml_frag, allocator))) {
    LOG_WARN("get xml frag string failed", K(ret));
  } else if (xml_frag.empty()) {
    // do nothing
  } else if (OB_FALSE_IT(allocator.set_baseline_size(xml_frag.length()))) {
  } else if (OB_FAIL(ObXMLExprHelper::get_str_from_expr(expr.args_[1], ctx, xpath_expr, allocator))) {
    LOG_WARN("get xpath expr failed.", K(ret));
  } else if (OB_FAIL(ObMulModeFactory::get_xml_base(xml_mem_ctx, xml_frag, ObNodeMemType::TREE_TYPE, ObNodeMemType::BINARY_TYPE, xml_base, M_DOCUMENT))) {
    ret = OB_SUCCESS;
    if (OB_FAIL(ObMulModeFactory::get_xml_base(xml_mem_ctx, xml_frag, ObNodeMemType::TREE_TYPE, ObNodeMemType::BINARY_TYPE, xml_base, M_CONTENT))) {
      LOG_USER_WARN(OB_ERR_XML_PARSE);
      ret = OB_SUCCESS;
      LOG_WARN("parse xml_frag failed.", K(xml_frag));
    }
  }

  if (OB_FAIL(ret) || OB_ISNULL(xml_base)) {
  } else if (OB_FAIL(extract_mysql_xpath_result(xml_mem_ctx, xpath_expr, xml_base, xml_res))) {
    LOG_WARN("failed to extract xpath result.", K(ret), K(xpath_expr));
  }

  ObString value = xml_res.string();
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(xml_base)) {
    if (xml_frag.empty()) {
      if (OB_FAIL(ObXMLExprHelper::set_string_result(expr, ctx, res, value))) {
        LOG_WARN("failed to set res when xml frag empty.", K(ret), K(xml_res));
      }
    } else {
      res.set_null();
    }
  } else if (OB_FAIL(ObXMLExprHelper::set_string_result(expr, ctx, res, value))) {
    LOG_WARN("failed to set res", K(ret), K(xml_res));
  }

  return ret;
}

int ObExprExtractValue::get_new_xpath(ObString xpath_str, ObString &new_xpath, bool &cal_count)
{
  INIT_SUCC(ret);
  int64_t xpath_start = 0;
  int64_t xpath_end = 0;
  ObString count_str = ObString(6, "count(");
  if (xpath_str.prefix_match_ci(count_str)) {
    for (int64_t start = 0; start < xpath_str.length(); start++) {
      if (xpath_str.ptr()[start] == '(') {
        xpath_start = start + 1;
        for (int64_t end = xpath_str.length() - 1; end > start + 1; end--) {
          if (xpath_str.ptr()[end] == ')') {
            xpath_end = end;
            break;
          }
        }
        break;
      }
    }
    if (xpath_end != 0) {
      cal_count = true;
      new_xpath = ObString(xpath_end - xpath_start, xpath_str.ptr() + xpath_start);
    }
  }

  return ret;
}

int ObExprExtractValue::extract_mysql_xpath_result(ObMulModeMemCtx *xml_mem_ctx, ObString& xpath_str,
                                                   ObIMulModeBase* xml_doc, ObStringBuffer &xml_res)
{
  INIT_SUCC(ret);
  ObPathExprIter xpath_iter(xml_mem_ctx->allocator_);
  ObIMulModeBase *node = NULL;
  ObSEArray<ObIMulModeBase *, 8> result_nodes;
  ObString default_ns;
  ObPathVarObject prefix_ns(*(xml_mem_ctx->allocator_));
  ObString new_xpath = xpath_str;
  bool cal_count = false;
  if (OB_FAIL(get_new_xpath(xpath_str, new_xpath, cal_count))) {
    LOG_WARN("get new xpath failed.", K(ret));
  } else if (OB_FAIL(xpath_iter.init(xml_mem_ctx, new_xpath, default_ns, xml_doc, &prefix_ns))) {
    LOG_WARN("fail to init xpath iterator", K(new_xpath), K(default_ns), K(ret));
    ObXMLExprHelper::replace_xpath_ret_code(ret);
  } else if (OB_FAIL(xpath_iter.open())) {
    LOG_WARN("fail to open xpath iterator", K(ret));
    ObXMLExprHelper::replace_xpath_ret_code(ret);
  }

  while (OB_SUCC(ret)) {
    if (OB_FAIL(xpath_iter.get_next_node(node))) {
      if (ret != OB_ITER_END) {
        LOG_WARN("fail to get next xpath result node", K(ret));
      }
    } else if (OB_ISNULL(node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("xpath result node is null", K(ret));
    } else if (ObXMLExprHelper::is_xml_text_node(node->type())) {
      if (OB_FAIL(result_nodes.push_back(node))) {
        LOG_WARN("fail to push back result node", K(ret));
      }
    } else if (ObXMLExprHelper::is_xml_element_node(node->type()) ||
               ObXMLExprHelper::is_xml_attribute_node(node->type())) {
      if (OB_FAIL(result_nodes.push_back(node))) {
        LOG_WARN("fail to push back result node", K(ret));
      }
    }
  }

  if (ret == OB_ITER_END) {
    ret = OB_SUCCESS;
  }

  if (OB_FAIL(ret)) {
    if (ret == OB_NOT_IMPLEMENT) {
      ret = OB_SUCCESS;
      xml_res.append(xpath_str);
    }
  } else if (cal_count) {
    common::ObSqlString sql_string;
    sql_string.assign_fmt("%ld", result_nodes.count());
    if (OB_FAIL(xml_res.append(sql_string.ptr(), sql_string.length()))) {
      LOG_WARN("failed to append sql str", K(ret), K(sql_string));
    }
  } else if (OB_FAIL(append_text_into_buffer(xml_mem_ctx->allocator_, result_nodes, xml_res))) {
    LOG_WARN("fail to merge text nodes", K(ret), K(result_nodes.count()));
  }

  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = xpath_iter.close())) {
    LOG_WARN("fail to close xpath iter", K(tmp_ret));
    ret = COVER_SUCC(tmp_ret);
  }

  return ret;
}


int ObExprExtractValue::merge_text_nodes_with_same_parent(ObIAllocator *allocator, 
                                                          ObIArray<ObIMulModeBase *> &result_nodes, 
                                                          ObString &xml_res)
{
  int ret = OB_SUCCESS;
  // text-nodes or skip-nodes has same parent 
  ObIMulModeBase *child_node = NULL;
  ObStringBuffer *buffer = NULL;
  if (OB_ISNULL(buffer = OB_NEWx(ObStringBuffer, allocator, allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate buffer", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < result_nodes.count(); i++) {
    if (OB_ISNULL(child_node = result_nodes.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("node is NULL", K(ret));
    } else if (OB_FAIL(append_text_value(*buffer, child_node))) {
      LOG_WARN("fail to append text value", K(ret));
    }
  } // end for
  
  if (OB_SUCC(ret)) {
    xml_res.assign_ptr(buffer->ptr(), buffer->length());
  }
  return ret;
}


int ObExprExtractValue::append_text_value(ObStringBuffer &buffer, ObIMulModeBase *node)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node is NULL", K(ret));
  } else if (ObXMLExprHelper::is_xml_leaf_node(node->type())) {
    ObString tmp_res;
    if (OB_FAIL(node->get_value(tmp_res))) {
      LOG_WARN("fail to get node value", K(ret));
    } else if (OB_FAIL(buffer.append(tmp_res))) {
      LOG_WARN("fail to append buffer", K(ret), K(buffer));
    }     
  }
  return ret;
}

int ObExprExtractValue::append_text_into_buffer(ObIAllocator *allocator, 
                                                ObIArray<ObIMulModeBase *> &result_nodes, 
                                                ObStringBuffer &buffer)
{
  INIT_SUCC(ret);
  char space = ' ';
  for (int64_t i = 0; OB_SUCC(ret) && i < result_nodes.count(); i++) {
    ObIMulModeBase *child_node = result_nodes.at(i);
    ObString res_str;
    if (OB_ISNULL(child_node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get child node null", K(ret), K(i));
    } else if (ObXMLExprHelper::is_xml_leaf_node(child_node->type())) {
      if (OB_FAIL(child_node->get_value(res_str))) {
        LOG_WARN("failed to get node values.", K(ret), K(i));
      } else if (!buffer.empty() && OB_FAIL(buffer.append(&space, 1))) {
        LOG_WARN("failed to add space into buffer.", K(ret), K(buffer), K(i));
      } else if (OB_FAIL(buffer.append(res_str))) {
        LOG_WARN("append res into buffer failed", K(ret), K(i), K(res_str));
      }
    } else if (ObXMLExprHelper::is_xml_element_node(child_node->type())) {
      for (int64_t j = 0; OB_SUCC(ret) && j < child_node->size(); j++) {
        ObString tmp_str;
        ObIMulModeBase *grand_child = child_node->at(j);
        if (OB_ISNULL(grand_child)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get grand child null", K(ret), K(i), K(j));
        } else if (grand_child->type() == M_TEXT || grand_child->type() == M_CDATA) {
          if (OB_FAIL(grand_child->get_value(tmp_str))) {
            LOG_WARN("get grand child value failed", K(ret), K(i), K(j), K(grand_child->type()));
          } else if (!buffer.empty() && OB_FAIL(buffer.append(&space, 1))) {
            LOG_WARN("failed to add space into buffer.", K(ret), K(buffer), K(i));
          } else if (OB_FAIL(buffer.append(tmp_str))) {
            LOG_WARN("failed to append str", K(ret), K(tmp_str), K(i), K(j));
          }
        }
      }
    }
  }

  return ret;
}

int ObExprExtractValue::extract_node_value(ObIAllocator &allocator, ObIMulModeBase *node, ObString &xml_res)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("extract node is NULL", K(ret));
  } else if (ObXMLExprHelper::is_xml_leaf_node(node->type())) {
    if (OB_FAIL(node->get_value(xml_res))) {
      LOG_WARN("fail to get node value", K(ret));
    }
  } else if (ObXMLExprHelper::is_xml_element_node(node->type())) {
    int64_t child_size = node->size();
    if (child_size == 1 && ObXMLExprHelper::is_xml_element_node(node->at(0)->type())) {
      ret = OB_EXTRACTVALUE_NOT_LEAF_NODE;
      LOG_WARN("EXTRACTVALUE can only retrieve value of leaf node", K(ret));
    } else {
      int64_t ele_node_num = 0;
      ObIMulModeBase *child_node = NULL;
      ObStringBuffer *buffer = NULL;
      if (OB_ISNULL(buffer = OB_NEWx(ObStringBuffer, &allocator, (&allocator)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate buffer", K(ret));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < child_size; i++) {
        if (OB_ISNULL(child_node = node->at(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("node is NULL", K(ret));
        } else if (OB_FAIL(append_text_value(*buffer, child_node))) {
           LOG_WARN("fail to append text value", K(ret));
        } else if (ObXMLExprHelper::is_xml_element_node(child_node->type())) {
          ele_node_num++;
        } else {
          // skip other node type
        }
      } // end for
      if (ele_node_num > 0) {
        ret = OB_ERR_EXTRACTVALUE_MULTI_NODES;
        LOG_WARN("EXTRACTVALUE returns value of only one node", K(ele_node_num));
      } else {
        xml_res.assign_ptr(buffer->ptr(), buffer->length());
      }
    }
  } // type == M_ELEMENT
  return ret;
}

int ObExprExtractValue::has_same_parent_node(ObMulModeMemCtx *xml_mem_ctx, ObString& xpath_str, ObString& default_ns, 
                                             ObIMulModeBase* xml_doc, ObPathVarObject* prefix_ns, bool &is_same_parent)
{
  int ret = OB_SUCCESS;
  ObPathExprIter xpath_iter(xml_mem_ctx->allocator_);
  ObIMulModeBase *node = NULL;
  int64_t node_num = 0;
  ObStringBuffer buffer(xml_mem_ctx->allocator_);  
  ObString parent_xpath;
  if (OB_FAIL(buffer.append(xpath_str))) {
    LOG_WARN("fail to append buffer", K(ret));
  } else if (OB_FAIL(buffer.append("/parent::node()"))) {
    LOG_WARN("fail to append buffer", K(ret));
  } else {
    parent_xpath.assign_ptr(buffer.ptr(), buffer.length());
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(xpath_iter.init(xml_mem_ctx, parent_xpath, default_ns, xml_doc, prefix_ns))) {
    LOG_WARN("fail to init xpath iterator", K(parent_xpath), K(default_ns), K(ret));
    ObXMLExprHelper::replace_xpath_ret_code(ret);
  } else if (OB_FAIL(xpath_iter.open())) {
    LOG_WARN("fail to open xpath iterator", K(ret));
    ObXMLExprHelper::replace_xpath_ret_code(ret);
  }
  while (OB_SUCC(ret)) {
    if (OB_FAIL(xpath_iter.get_next_node(node))) {
      if (ret != OB_ITER_END) {
        LOG_WARN("fail to get next xpath result node", K(ret));
      }
    } else {
      node_num++;
    }
  }

  if (ret == OB_ITER_END) {
    ret = OB_SUCCESS;
  }

  if (node_num > 1) { // has different parent
    is_same_parent = false;
  } else {
    is_same_parent = true;
  }

  return ret;
}

int ObExprExtractValue::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  
  rt_expr.eval_func_ = eval_mysql_extract_value;
  
  return OB_SUCCESS;
}

} // sql
} // oceanbase
