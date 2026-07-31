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
#include "sql/engine/expr/ob_expr_xml_func_helper.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

int ObXMLExprHelper::set_string_result(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res, ObString &res_str)
{
  int ret = OB_SUCCESS;
  ObTextStringDatumResult text_result(expr.datum_meta_.type_, &expr, &ctx, &res);
  int64_t res_len = res_str.length();
  if (OB_FAIL(text_result.init(res_len))) {
    LOG_WARN("fail to init string result length", K(ret), K(text_result), K(res_len));
  } else if (OB_FAIL(text_result.append(res_str))) {
    LOG_WARN("fail to append xml format string", K(ret), K(res_str), K(text_result));
  } else {
    text_result.set_result();
  }
  return ret;
}

int ObXMLExprHelper::get_str_from_expr(const ObExpr *expr, 
                                       ObEvalCtx &ctx, 
                                       ObString &res,
                                       ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObDatum *datum = NULL;
  ObObjType val_type = expr->datum_meta_.type_;
  MultimodeAlloctor &alloc = static_cast<MultimodeAlloctor&>(allocator);
  if (OB_FAIL(alloc.eval_arg(expr, ctx, datum))) {
    LOG_WARN("eval xml arg failed", K(ret));
  } else if (!ob_is_string_type(val_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("input type error", K(val_type));
  } else if (FALSE_IT(res = datum->get_string())) {
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, allocator, *datum,
                expr->datum_meta_, expr->obj_meta_.has_lob_header(), res))) {
    LOG_WARN("fail to get real data.", K(ret), K(res));
  }
  return ret;
}

bool ObXMLExprHelper::is_xml_leaf_node(ObMulModeNodeType node_type)
{
  return node_type == ObMulModeNodeType::M_ATTRIBUTE ||
         node_type == ObMulModeNodeType::M_NAMESPACE ||
         node_type == ObMulModeNodeType::M_CDATA ||
         node_type == ObMulModeNodeType::M_TEXT;
}

bool ObXMLExprHelper::is_xml_text_node(ObMulModeNodeType node_type)
{
  return node_type == ObMulModeNodeType::M_CDATA ||
         node_type == ObMulModeNodeType::M_TEXT;
}

bool ObXMLExprHelper::is_xml_attribute_node(ObMulModeNodeType node_type)
{
  return node_type == ObMulModeNodeType::M_ATTRIBUTE ||
         node_type == ObMulModeNodeType::M_NAMESPACE;
}

bool ObXMLExprHelper::is_xml_element_node(ObMulModeNodeType node_type)
{
  return node_type == ObMulModeNodeType::M_ELEMENT ||
         node_type == ObMulModeNodeType::M_DOCUMENT ||
         node_type == ObMulModeNodeType::M_CONTENT;
}

bool ObXMLExprHelper::is_xml_root_node(ObMulModeNodeType node_type)
{
  return node_type == ObMulModeNodeType::M_DOCUMENT ||
         node_type == ObMulModeNodeType::M_CONTENT;
}

void ObXMLExprHelper::replace_xpath_ret_code(int &ret)
{
  if (ret == OB_OP_NOT_ALLOW) {
    ret = OB_XPATH_EXPRESSION_UNSUPPORTED;
  } else if (ret == OB_ERR_PARSER_SYNTAX) {
    ret = OB_ERR_XML_PARSE;
  } else if (ret == OB_ALLOCATE_MEMORY_FAILED) {
    // do nothing
  } else if (ret == OB_ERR_WRONG_VALUE) {
    ret = OB_ERR_INVALID_INPUT;
  } else {
    ret = OB_ERR_INVALID_XPATH_EXPRESSION;
  }
}





int ObXMLExprHelper::update_new_nodes_ns(ObIAllocator &allocator, ObXmlNode *parent, ObXmlNode *update_node)
{
  int ret = OB_SUCCESS;
  ObXmlAttribute *empty_ns = NULL;
  ObXmlAttribute *default_ns = NULL;
  ObXmlAttribute *update_node_default_ns = NULL;
  if (OB_ISNULL(parent) || OB_ISNULL(update_node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node is NULL", K(ret), K(parent), K(update_node));
  } else if (OB_FAIL(get_valid_default_ns_from_parent(parent, default_ns))) {
    LOG_WARN("unexpected error in find default ns from parent", K(ret));
  } else if (OB_NOT_NULL(default_ns) && !default_ns->get_value().empty()) {
    // need to update the new node default ns with empty default ns
    if (OB_FAIL(get_valid_default_ns_from_parent(update_node, update_node_default_ns))) {
      LOG_WARN("unexpected error in find default ns from parent", K(ret));
    } else if (OB_ISNULL(update_node_default_ns) || update_node_default_ns->get_value().empty()) {
      if (OB_ISNULL(empty_ns = OB_NEWx(ObXmlAttribute, (&allocator), ObMulModeNodeType::M_NAMESPACE, parent->get_mem_ctx()))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc failed", K(ret)); 
      } else {
        empty_ns->set_xml_key(ObXmlConstants::XMLNS_STRING);
        empty_ns->set_value(ObString::make_empty_string());
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(set_ns_recrusively(update_node, empty_ns))) {
        LOG_WARN("fail to set empty default ns recrusively", K(ret));
      }
    }
  }
  return ret;
}

// found valid default ns from down to top
int ObXMLExprHelper::get_valid_default_ns_from_parent(ObXmlNode *cur_node, ObXmlAttribute* &default_ns)
{
  int ret = OB_SUCCESS;
  ObXmlNode* t_node = NULL;
  bool is_found = false;
  if (OB_ISNULL(cur_node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("update node is NULL", K(ret));
  } else if (!ObXMLExprHelper::is_xml_element_node(cur_node->type())) {
    t_node = cur_node->get_parent();
  } else {
    t_node = cur_node;
  }

  while(!is_found && OB_SUCC(ret) && OB_NOT_NULL(t_node)) {
    ObXmlElement *t_element = static_cast<ObXmlElement*>(t_node);
    ObArray<ObIMulModeBase *> attr_list;
    if (OB_FAIL(t_element->get_namespace_list(attr_list))) {
      LOG_WARN("fail to get namespace list", K(ret));
    }
    for (int i = 0; !is_found && OB_SUCC(ret) && i < attr_list.size(); i ++) {
      ObXmlAttribute *attr = static_cast<ObXmlAttribute *>(attr_list.at(i));
      if (attr->get_key().compare(ObXmlConstants::XMLNS_STRING) == 0) {
        is_found = true;
        default_ns = attr;
      }
    }
    t_node = t_node->get_parent();
  }
  return ret;
}

int ObXMLExprHelper::set_ns_recrusively(ObXmlNode *update_node, ObXmlAttribute *ns)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(update_node) || OB_ISNULL(ns)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("update node is NULL", K(ret), K(update_node), K(ns));
  } else if (!ObXMLExprHelper::is_xml_element_node(update_node->type())) {
    // no need to set default ns
  } else {
    bool is_stop = false;
    ObXmlElement *ele_node = static_cast<ObXmlElement *>(update_node);
    ObString key = ns->get_key();
    if (ele_node->type() != M_ELEMENT) {
      // skip
    } else if (key.compare(ObXmlConstants::XMLNS_STRING) == 0) {
      // update default ns 
      if (ele_node->get_prefix().empty()) {
        // this condition mean: has no ns || has non-empty default ns
        is_stop = true;
        ObXmlAttribute *default_ns = NULL;
        if (OB_FAIL(get_valid_default_ns_from_parent(update_node, default_ns))) {
          LOG_WARN("get default ns failed.", K(ret));
        } else if (OB_ISNULL(default_ns) || default_ns->get_value().empty()) {
          ele_node->add_attribute(ns, false, 0);
          ele_node->set_ns(ns);
        } else { /* has non-empty default ns, skip and stop find */ }
      }
    } else { // has prefix
      ObXmlAttribute *tmp_ns = NULL;
      if (ele_node->get_ns() == ns ||
          ele_node->has_attribute_with_ns(ns) ||
          OB_NOT_NULL(tmp_ns = ele_node->get_ns_by_name(key))) {
        // match condition below will stop recrusive
        // element use this prefix ns || attributes of element use this prefix ns || this prefix in attributes
        is_stop = true;
        if (OB_NOT_NULL(tmp_ns)) { // if the prefix not in attributes
        } else if (OB_FAIL(ele_node->add_attribute(ns, false, 0))) {
          LOG_WARN("fail to add namespace node", K(ret), K(key));
        }
      }
    }

    if (!is_stop) {
      // find its child node recrusivle when no need to set default ns
      for (int64_t i = 0; OB_SUCC(ret) && i < ele_node->size(); i++) {
        if (OB_FAIL(SMART_CALL(set_ns_recrusively(ele_node->at(i), ns)))) {
          LOG_WARN("fail set default ns in origin tree recursively", K(ret));
        }
      } // end for
    } // end is_stop
  }
  return ret;
}




} // sql
} // oceanbase
