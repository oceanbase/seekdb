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

#ifndef OCEANBASE_SQL_OB_EXPR_XML_FUNC_HELPER_H_
#define OCEANBASE_SQL_OB_EXPR_XML_FUNC_HELPER_H_

#include "sql/engine/expr/ob_expr_util.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "common/xml/ob_xml_parser.h"
#include "common/xml/ob_xpath.h"
#include "common/xml/ob_xml_tree.h"
#include "common/xml/ob_xml_util.h"
#include "sql/engine/expr/ob_expr_multi_mode_func_helper.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
class ObExpr;
class ObEvalCtx;

class ObXMLExprHelper final
{
public:
  static int set_string_result(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res, ObString &res_str);
  // get xmltype str from input expr
  static int get_xmltype_from_expr(const ObExpr *expr,
                                   ObEvalCtx &ctx,
                                   ObDatum *&xml_datum);
  static int get_str_from_expr(const ObExpr *expr,
                               ObEvalCtx &ctx,
                               ObString &res,
                               ObIAllocator &allocator);
  static int get_xml_base_from_expr(const ObExpr *expr,
                                    ObMulModeMemCtx *mem_ctx,
                                    ObEvalCtx &ctx,
                                    ObIMulModeBase *&node);
  static int binary_agg_xpath_result(ObPathExprIter &xpath_iter,
                                     ObMulModeNodeType &node_type,
                                     ObMulModeMemCtx* mem_ctx,
                                     ObStringBuffer &res,
                                     int64_t &append_node_num,
                                     bool add_ns);

  static bool is_xml_leaf_node(ObMulModeNodeType node_type);
  static bool is_xml_text_node(ObMulModeNodeType node_type);
  static bool is_xml_element_node(ObMulModeNodeType node_type);
  static bool is_xml_root_node(ObMulModeNodeType node_type);
  static bool is_xml_attribute_node(ObMulModeNodeType node_type);

  static void replace_xpath_ret_code(int &ret);
  static int update_new_nodes_ns(ObIAllocator &allocator, ObXmlNode *parent, ObXmlNode *update_node);
  static int get_valid_default_ns_from_parent(ObXmlNode *cur_node, ObXmlAttribute *&default_ns);
  static int set_ns_recrusively(ObXmlNode *update_node, ObXmlAttribute *ns);
};
} // sql
} // oceanbase

#endif // OCEANBASE_SQL_OB_EXPR_XML_FUNC_HELPER_H_
