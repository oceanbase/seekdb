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

#define USING_LOG_PREFIX SERVER
#include "common/json_type/ob_json_parse.h"
#include "ob_query_parse.h"

namespace oceanbase
{
namespace share
{


const ObString ObESQueryParser::SCORE_NAME("_score");
const ObString ObESQueryParser::FTS_SCORE_NAME("_keyword_score");
const ObString ObESQueryParser::VS_SCORE_NAME("_semantic_score");
const ObString ObESQueryParser::SIMILARITY_SCORE_NAME("_similarity_score");
const ObString ObESQueryParser::FTS_RANK_NAME("_keyword_rank");
const ObString ObESQueryParser::VS_RANK_NAME("_semantic_rank");
const ObString ObESQueryParser::ROWKEY_NAME("__pk_increment");
const ObString ObESQueryParser::RANK_CONST_DEFAULT("60");
const ObString ObESQueryParser::SIZE_DEFAULT("10");
const ObString ObESQueryParser::FTS_ALIAS("_fts");
const ObString ObESQueryParser::VS_ALIAS("_vs");
const ObString ObESQueryParser::MSM_KEY("minimum_should_match");
const ObString ObESQueryParser::FTS_SUB_SCORE_PREFIX("_fts_sub_score_");
const ObString ObESQueryParser::PART_COL_ALIAS_PREFIX("_part_col_");
const ObString ObESQueryParser::HIDDEN_COLUMN_VISIBLE_HINT("opt_param('hidden_column_visible', 'true')");

int ObESQueryParser::parse(const common::ObString &req_str, ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;
  ObJsonNode *j_node = NULL;
  const char *syntaxerr = NULL;
  ObString fusion_key = "rank";
  ObIJsonBase *fusion_node = NULL;
  uint64_t err_offset = 0;
  uint32_t parse_flag = ObJsonParser::JSN_RELAXED_FLAG | ObJsonParser::JSN_UNIQUE_FLAG;
  if (OB_FAIL(ObJsonParser::parse_json_text(&alloc_, req_str.ptr(), req_str.length(), syntaxerr, &err_offset, j_node, parse_flag))) {
  } else if (OB_FAIL(init_default_params(*j_node))) {
  } else {
    uint64_t count = j_node->element_count();
    ObQueryReqFromJson *query = NULL;
    ObQueryReqFromJson *knn = NULL;
    ObReqConstExpr *from_expr = NULL;
    ObIJsonBase *es_mode_node = NULL;
    ObString es_mode_key("es_mode");
    if (OB_FAIL(j_node->get_object_value(es_mode_key, es_mode_node))) {
      if (ret == OB_SEARCH_NOT_FOUND) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to get type field", K(ret));
      }
    } else if (es_mode_node->json_type() == ObJsonNodeType::J_BOOLEAN) {
      enable_es_mode_ = es_mode_node->get_boolean();
    } else if (es_mode_node->json_type() == ObJsonNodeType::J_STRING) {
      ObString es_mode_str = ObString(es_mode_node->get_data_length(), es_mode_node->get_data()).trim();
      if (es_mode_str.case_compare("true") == 0) {
        enable_es_mode_ = true;
      } else if (es_mode_str.case_compare("false") == 0) {
        enable_es_mode_ = false;
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("es_mode field must be boolean type or string 'true' or 'false'", K(ret));
      }
    } else {
      ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
      LOG_WARN("es_mode field must be boolean type or string 'true' or 'false'", K(ret), K(es_mode_node->json_type()));
    }
    for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      ObString key;
      ObIJsonBase *req_node = NULL;
      if (OB_FAIL(j_node->get_key(i, key))) {
      } else if (OB_FAIL(j_node->get_object_value(i, req_node))) {
      } else if (key.case_compare("query") == 0) {
        if (OB_FAIL(parse_query(*req_node, query))) {
        }
      } else if (key.case_compare("knn") == 0) {
        if (OB_FAIL(parse_multi_knn(*req_node, knn))) {
        }
      } else if (key.case_compare("_source") == 0) {
        if (OB_FAIL(parse_source(*req_node))) {
        }
      } else if (key.case_compare("from") == 0) {
        if (OB_FAIL(parse_const(*req_node, from_expr, true))) {
        }
      } else if (key.case_compare("size") == 0) {
        // do nothing, parsed in init_default_params
      } else if (key.case_compare(es_mode_key) == 0) {
        // do nothing and continue
      } else if (key.case_compare(fusion_key) == 0) {
        // do nothing
      } else {
        ret = OB_ERR_PARSER_SYNTAX;
        LOG_WARN("invalid query param", K(ret), K(key));
      }
    }
    if (OB_SUCC(ret)) {
      bool is_hybrid = query != NULL && knn != NULL;
      out_cols_ = source_cols_.empty() ? &user_cols_ : &source_cols_;
      if (is_hybrid) {
        if (OB_FAIL(set_fts_limit_expr(query, default_size_, from_expr))) {
        } else if (OB_FAIL(construct_hybrid_query(query, knn, query_req))) {
        }
      } else {
        query_req = (query == NULL ? knn : query);
        if (OB_ISNULL(query_req) && OB_FAIL(construct_all_query(query_req))) {
          LOG_WARN("fail to construct all query", K(ret));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (default_size_ == NULL && from_expr != NULL) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not supported sytnax in query, 'size' must be set when 'from' is specified", K(ret));
      } else {
        query_req->set_offset(from_expr);
        if (query_req->get_limit() == NULL) {
          query_req->set_limit(default_size_);
        } else if (default_size_ != NULL && OB_FAIL(choose_limit(query_req, default_size_))) {
          LOG_WARN("fail to choose limit expr", K(ret));
        }
      }

      // when the score is equal, use __pk_increment to order by
      if (OB_SUCC(ret)) {
        ObEsQueryItem query_item = is_hybrid ? QUERY_ITEM_HYBRID : (query != NULL ? QUERY_ITEM_QUERY : QUERY_ITEM_KNN);
        if (OB_FAIL(add_pk_to_sort(query_req, query_item))) {
        }
      }

      if (OB_SUCC(ret) && !out_cols_->empty()) {
        if (OB_FAIL(set_output_columns(*query_req, is_hybrid))) {
        } else if (need_json_wrap_ && OB_FAIL(wrap_json_result(query_req))) {
          LOG_WARN("fail to wrap json result", K(ret));
        }
      }
    }
  }
  return ret;
}


int ObESQueryParser::add_pk_to_sort(ObQueryReqFromJson *query_req, const ObEsQueryItem query_item)
{
  int ret = OB_SUCCESS;
  const ObString rowkey = ROWKEY_NAME;
  if (OB_ISNULL(query_req)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null pointer", K(ret));
  } else if (QUERY_ITEM_QUERY != query_item &&
             QUERY_ITEM_KNN != query_item &&
             QUERY_ITEM_HYBRID != query_item) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid query item", K(ret), K(query_item));
  } else if (QUERY_ITEM_QUERY == query_item) {
    // when is full text search, add hit
    ObQueryReqFromJson *base_table_req = NULL;
    ObReqColumnExpr *rowkey_expr = NULL;
    const ObString rowkey_hint = HIDDEN_COLUMN_VISIBLE_HINT;
    if (OB_FAIL(get_base_table_query(query_req, base_table_req))) {
    } else if (OB_FAIL(base_table_req->add_req_hint(rowkey_hint))) {
    } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(rowkey_expr, alloc_, rowkey))) {
    } else if (query_req != base_table_req) {
      // need to add __pk_increment to select items, 
      // ignore occurence of 'Unknown column '__pk_increment'' error
      if (OB_FAIL(base_table_req->select_items_.push_back(rowkey_expr))) {
      }
    }
  }

  // add __pk_increment to order by
  if (OB_FAIL(ret)) {
  } else if (QUERY_ITEM_QUERY == query_item) {
    if (OB_FAIL(set_order_by_column(query_req, rowkey, "", true))) {
    }
  } else if (QUERY_ITEM_HYBRID == query_item) {
    ObReqColumnExpr *vs_pk = NULL;
    ObReqColumnExpr *fts_pk = NULL;
    ObReqExpr *if_null = NULL;
    OrderInfo *order_info = NULL;
    if (OB_FAIL(ObReqColumnExpr::construct_column_expr(vs_pk, alloc_, rowkey, VS_ALIAS))) {
    } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(fts_pk, alloc_, rowkey, FTS_ALIAS))) {
    } else if (OB_FAIL(ObReqExpr::construct_expr(if_null, alloc_, N_IFNULL, vs_pk, fts_pk))) {
    } else if (OB_FAIL(construct_order_by_item(if_null, true, order_info))) {
    } else if (OB_FAIL(query_req->order_items_.push_back(order_info))) {
    }
  }
  return ret;
}

int ObESQueryParser::choose_limit(ObQueryReqFromJson *query_req, ObReqConstExpr *size_expr)
{
  int ret = OB_SUCCESS;
  int64_t limit_val = 0;
  int64_t size_val = 0;
  if (!query_req->get_limit()->expr_name.is_numeric() ||
      !size_expr->expr_name.is_numeric()) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd value type", K(ret), K(query_req->get_limit()->expr_name), K(size_expr->expr_name));
  } else if (OB_FAIL(convert_const_numeric(query_req->get_limit()->expr_name, limit_val))) {
  } else if (OB_FAIL(convert_const_numeric(size_expr->expr_name, size_val))) {
  } else if (size_val < limit_val) {
    query_req->set_limit(size_expr);
  }
  return ret;
}

int ObESQueryParser::parse_multi_knn(ObIJsonBase &req_node, ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;
  uint64_t knn_count = req_node.json_type() == ObJsonNodeType::J_OBJECT ? 1 : req_node.element_count();
  ObQueryReqFromJson *knn_req = NULL;
  common::ObSEArray<ObQueryReqFromJson*, 4, common::ModulePageAllocator, true> knn_queries;
  for (uint64_t i = 0; OB_SUCC(ret) && i < knn_count; i++) {
    ObIJsonBase *val_node = NULL;
    if (req_node.json_type() == ObJsonNodeType::J_OBJECT) {
      val_node = &req_node;
    } else if (OB_FAIL(req_node.get_array_element(i, val_node))) {
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(parse_knn(*val_node, knn_req))) {
    } else if (OB_FAIL(knn_queries.push_back(knn_req))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (knn_queries.count() > 1) {
      if (OB_FAIL(knn_fusion(knn_queries, query_req))) {
      }
    } else {
      query_req = knn_req;
    }
  }
  return ret;
}

int ObESQueryParser::init_default_params(ObIJsonBase &req_node)
{
  int ret = OB_SUCCESS;
  ObString fusion_key = "rank";
  ObString size_key = "size";
  ObIJsonBase *fusion_node = NULL;
  ObIJsonBase *size_node = NULL;
  if (OB_FAIL(req_node.get_object_value(fusion_key, fusion_node))) {
    if (OB_SEARCH_NOT_FOUND == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get rank node", K(ret));
    }
  } else if (fusion_node != NULL && OB_FAIL(parse_rank(*fusion_node))) {
    LOG_WARN("fail to parse rank node", K(ret));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(req_node.get_object_value(size_key, size_node))) {
    if (OB_SEARCH_NOT_FOUND == ret) {
      // set default size
      ret = OB_SUCCESS;
      if (OB_FAIL(ObReqConstExpr::construct_const_expr(default_size_, alloc_, SIZE_DEFAULT, ObIntType))) {
      }
    } else {
      LOG_WARN("fail to get rank node", K(ret));
    }
  } else if (size_node != NULL && OB_FAIL(parse_const(*size_node, default_size_, true))) {
    LOG_WARN("fail to parse rank node", K(ret));
  }

  if (OB_SUCC(ret)) {
    if (fusion_config_.method == ObFusionMethod::RRF) {
      int64_t window_size = 0;
      if (fusion_config_.size == NULL) {
        // use size as default value
        fusion_config_.size = default_size_;
      }
      if (OB_FAIL(convert_const_numeric(fusion_config_.size->expr_name, window_size))) {
      } else if (size_node != NULL) {
        // size isn't default value
        int64_t size_val = 0;
        if (OB_FAIL(convert_const_numeric(default_size_->expr_name, size_val))) {
        } else if (OB_FAIL(convert_const_numeric(fusion_config_.size->expr_name, window_size))) {
        } else if (size_val > window_size) {
          ret = OB_WARN_OPTION_BELOW_LIMIT;
          LOG_USER_WARN(OB_WARN_OPTION_BELOW_LIMIT, "rank_window_size", "size");
        } else if (window_size < 1) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid window size value", K(ret), K(window_size));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (window_size < 1) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid window size value", K(ret), K(window_size));
      } else if (fusion_config_.rank_const == NULL && // use default rank const
                 OB_FAIL(ObReqConstExpr::construct_const_expr(fusion_config_.rank_const, alloc_, RANK_CONST_DEFAULT, ObIntType))) {
        LOG_WARN("fail to create const expr", K(ret));
      } else {
        // verify validilty
        int64_t rank_const = 0;
        if (OB_FAIL(convert_const_numeric(fusion_config_.rank_const->expr_name, rank_const))) {
        } else if (rank_const < 1) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid rank const value", K(ret), K(rank_const));
        }
      }
    }
  }
  return ret;
}

int ObESQueryParser::parse_rank(ObIJsonBase &req_node)
{
  int ret = OB_SUCCESS;
  ObString key;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (req_node.element_count() != 1) {
    ret = OB_ERR_PARSER_SYNTAX;
    LOG_WARN("unexpected param count", K(ret));
  } else if (OB_FAIL(req_node.get_key(0, key))) {
  } else if (key.case_compare("rrf") == 0) {
    ObIJsonBase *sub_node = NULL;
    if (OB_FAIL(req_node.get_object_value(0, sub_node))) {
    } else if (OB_FAIL(parse_rrf(*sub_node))) {
    } else {
      fusion_config_.method = ObFusionMethod::RRF;
    }
  } else {
    ret = OB_ERR_PARSER_SYNTAX;
    LOG_WARN("invalid query param", K(ret), K(key));
  }

  return ret;
}

int ObESQueryParser::parse_rrf(ObIJsonBase &req_node)
{
  int ret = OB_SUCCESS;
  uint64_t count = req_node.element_count();
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    ObString key;
    ObIJsonBase *sub_node = NULL;
    if (OB_FAIL(req_node.get_key(i, key))) {
    } else if (OB_FAIL(req_node.get_object_value(i, sub_node))) {
    } else if (key.case_compare("rank_constant") == 0) {
      if (OB_FAIL(parse_const(*sub_node, fusion_config_.rank_const, true))) {
      }
    } else if (key.case_compare("rank_window_size") == 0) {
      if (OB_FAIL(parse_const(*sub_node, fusion_config_.size, true))) {
      }
    } else {
      ret = OB_ERR_PARSER_SYNTAX;
      LOG_WARN("not supported sytnax in query", K(ret), K(key));
    }
  }
  return ret;
}

int ObESQueryParser::knn_fusion(const ObIArray<ObQueryReqFromJson*> &knn_queries, ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;
  ObMultiSetTable *multi_set_table = NULL;
  ObQueryReqFromJson *res = NULL;
  ObReqColumnExpr *rowkey_expr = NULL;
  const ObString rowkey = ROWKEY_NAME;
  const ObString rowkey_hint = HIDDEN_COLUMN_VISIBLE_HINT;
  if (OB_ISNULL(multi_set_table = OB_NEWx(ObMultiSetTable, &alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else if (OB_ISNULL(res = OB_NEWx(ObQueryReqFromJson, &alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(rowkey_expr, alloc_, rowkey))) {
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < knn_queries.count(); i++) {
    ObString empty_str;
    ObReqTable *sub_query = NULL;
    ObQueryReqFromJson *base_table_req = NULL;
    if (OB_FAIL(get_base_table_query(knn_queries.at(i), base_table_req))) {
    } else if (OB_FAIL(base_table_req->add_req_hint(rowkey_hint))) {
    } else if (OB_FAIL(base_table_req->select_items_.push_back(rowkey_expr))) {
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < part_cols_.count(); j++) {
      if (OB_FAIL(base_table_req->select_items_.push_back(part_cols_.at(j)))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(construct_sub_query_table(empty_str, knn_queries.at(i), sub_query))) {
    } else if (OB_FAIL(multi_set_table->sub_queries_.push_back(sub_query))) {
    }
  }

  ObReqExpr *sum_expr = NULL;
  ObReqColumnExpr *score_col = NULL;
  OrderInfo *order_info = NULL;
  ObString sum_name = "sum";
  ObString score_name = SCORE_NAME;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(score_col, alloc_, score_name))) {
  } else if (OB_FAIL(ObReqExpr::construct_expr(sum_expr, alloc_, sum_name, score_col, score_name))) {
  } else if (OB_FAIL(res->from_items_.push_back(multi_set_table))) {
  } else if (OB_FAIL(res->score_items_.push_back(sum_expr))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < part_aliases_.count(); i++) {
    if (OB_FAIL(res->group_items_.push_back(part_aliases_.at(i)))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(res->group_items_.push_back(rowkey_expr))) {
  } else if (OB_FAIL(construct_order_by_item(sum_expr, false, order_info))) {
  } else if (OB_FAIL(res->order_items_.push_back(order_info))) {
  } else {
    multi_set_table->joined_type_ = ObReqJoinType::UNION_ALL;
    multi_set_table->table_type_ = MULTI_SET;
    query_req = res;
  }

  return ret;
}

int ObESQueryParser::convert_const_numeric(const ObString &cont_val, int64_t &val)
{
  int ret = OB_SUCCESS;
  int err = 0;
  val = ObCharset::strntoll(cont_val.ptr(), cont_val.length(), 10, &err);
  if (err == 0) {
    if (val > UINT_MAX32) {
      ret = OB_ERR_INVALID_PARAM_ENCOUNTERED;
      LOG_WARN("input value out of range", K(ret), K(val));
    }
  } else {
    ret = OB_ERR_INVALID_PARAM_ENCOUNTERED;
    LOG_WARN("input value out of range", K(ret));
  }
  return ret;
}

int ObESQueryParser::convert_signed_const_numeric(const ObString &cont_val, int64_t &val)
{
  int ret = OB_SUCCESS;
  int err = 0;
  val = ObCharset::strntoll(cont_val.ptr(), cont_val.length(), 10, &err);
  if (err == 0) {
    if (val > INT_MAX32 || val < INT_MIN32) {
      ret = OB_ERR_INVALID_PARAM_ENCOUNTERED;
      LOG_WARN("input value out of 32-bit range", K(ret), K(val));
    }
  } else {
    ret = OB_ERR_INVALID_PARAM_ENCOUNTERED;
    LOG_WARN("input value must be a integer", K(ret));
  }
  return ret;
}

int ObESQueryParser::wrap_json_result(ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;

  ObReqExpr *j_obj_expr = NULL;
  ObReqExpr *j_arrayagg_expr = NULL;
  if (OB_FAIL(ObReqExpr::construct_expr(j_obj_expr, alloc_, "json_object"))) {
  } else if (OB_FAIL(ObReqExpr::construct_expr(j_arrayagg_expr, alloc_, "json_arrayagg", "hits"))) {
  } else {
    for (uint64_t i = 0; OB_SUCC(ret) && i < out_cols_->count(); i++) {
      ObReqConstExpr *col_name = NULL;
      ObReqColumnExpr *col_expr = NULL;
      if (OB_FAIL(ObReqConstExpr::construct_const_expr(col_name, alloc_, out_cols_->at(i), ObVarcharType))) {
      } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(col_expr, alloc_, out_cols_->at(i)))) {
      } else if (OB_FAIL(j_obj_expr->params.push_back(col_name))) {
      } else {
        bool found = false;
        for (uint64_t j = 0; OB_SUCC(ret) && !found && j < query_req->select_items_.count(); j++) {
          ObReqExpr *sel_expr = query_req->select_items_.at(j);
          if ((!sel_expr->alias_name.empty() && out_cols_->at(i).case_compare(sel_expr->alias_name) == 0) ||
              (!sel_expr->expr_name.empty() && out_cols_->at(i).case_compare(sel_expr->expr_name) == 0)) {
            found = true;
          }
          if (found && OB_FAIL(j_obj_expr->params.push_back(col_expr))) {
            LOG_WARN("fail to append select item", K(ret));
          }
        }
        if (OB_SUCC(ret) && !found) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("fail to find output expr", K(ret), K(col_name->expr_name));
        }
      }
    }
    for (uint64_t i = 0; OB_SUCC(ret) && i < query_req->score_items_.count(); i++) {
      ObReqExpr *score = query_req->score_items_.at(i);
      ObReqConstExpr *col_name = NULL;
      ObReqColumnExpr *col = NULL;
      if (score->alias_name.empty()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get score item alias", K(ret));
      } else if (OB_FAIL(ObReqConstExpr::construct_const_expr(col_name, alloc_, score->alias_name, ObVarcharType))) {
      } else if (OB_FAIL(j_obj_expr->params.push_back(col_name))) {
      } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(col, alloc_, score->alias_name))) {
      } else if (OB_FAIL(j_obj_expr->params.push_back(col))) {
      }
    }
    ObString sub_query_name;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(j_arrayagg_expr->params.push_back(j_obj_expr))) {
    } else if (OB_FAIL(wrap_sub_query(sub_query_name, query_req))) {
    } else if (OB_FAIL(query_req->select_items_.push_back(j_arrayagg_expr))) {
    } else {
      query_req->output_all_columns_ = false;
    }
  }
  return ret;
}

int ObESQueryParser::set_output_columns(ObQueryReqFromJson &query_res, bool is_hybrid, bool include_inner_column/* true */)
{
  int ret = OB_SUCCESS;
  query_res.output_all_columns_ = false;
  if (!query_res.is_score_item_exist()) {
    ObReqColumnExpr *score_col = NULL;
    if (OB_FAIL(ObReqColumnExpr::construct_column_expr(score_col, alloc_, SCORE_NAME))) {
    } else if (OB_FAIL(query_res.add_score_item(alloc_, score_col))) {
    }
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < out_cols_->count(); i++) {
    if (is_hybrid && (!is_inner_column(out_cols_->at(i)) || out_cols_->at(i) == ROWKEY_NAME)) {
      ObReqColumnExpr *fts_col = NULL;
      ObReqColumnExpr *vs_col = NULL;
      ObReqExpr *if_null = NULL;
      if (OB_FAIL(ObReqColumnExpr::construct_column_expr(fts_col, alloc_, out_cols_->at(i), FTS_ALIAS))) {
      } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(vs_col, alloc_, out_cols_->at(i), VS_ALIAS))) {
      } else if (OB_FAIL(ObReqExpr::construct_expr(if_null, alloc_, N_IFNULL, out_cols_->at(i)))) {
      } else if (OB_FAIL(if_null->params.push_back(fts_col))) {
      } else if (OB_FAIL(if_null->params.push_back(vs_col))) {
      } else if (OB_FAIL(query_res.select_items_.push_back(if_null))) {
      }
    } else if (!is_inner_column(out_cols_->at(i)) || include_inner_column) {
      ObReqColumnExpr *col = NULL;
      if (OB_FAIL(ObReqColumnExpr::construct_column_expr(col, alloc_, out_cols_->at(i)))) {
      } else if (OB_FAIL(query_res.select_items_.push_back(col))) {
      }
    }
  }
  return ret;
}

int ObESQueryParser::parse_source(ObIJsonBase &req_node)
{
  int ret = OB_SUCCESS;
  if (req_node.json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < req_node.element_count(); i++) {
    ObIJsonBase *val_node = NULL;
    if (OB_FAIL(req_node.get_array_element(i, val_node))) {
    } else if (val_node->json_type() != ObJsonNodeType::J_STRING) {
      ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
      LOG_WARN("unexpectd json type", K(ret), K(val_node->json_type()));
    } else {
      ObString field_name(val_node->get_data_length(), val_node->get_data());
      if (OB_FAIL(source_cols_.push_back(field_name))) {
      }
    }
  }
  return ret;
}

int ObESQueryParser::construct_join_condition(const ObString &l_table, const ObString &r_table,
                                              const ObString &l_expr_name, const ObString &r_expr_name,
                                              ObItemType condition, ObReqOpExpr *&join_condition)
{
  int ret = OB_SUCCESS;
  ObReqColumnExpr *l_expr = nullptr;
  ObReqColumnExpr *r_expr = nullptr;
  if (OB_FAIL(ObReqColumnExpr::construct_column_expr(l_expr, alloc_, l_expr_name, l_table))) {
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(r_expr, alloc_, r_expr_name, r_table))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(join_condition, alloc_, condition, l_expr, r_expr))) {
  }
  return ret;
}

int ObESQueryParser::construct_join_multi_condition(const ObString &l_table, const ObString &r_table,
                                                    const ObString &rowkey, ObItemType condition, ObReqOpExpr *&join_condition)
{
  int ret = OB_SUCCESS;
  ObReqOpExpr *rowkey_condition = nullptr;
  ObSEArray<ObReqExpr *, 8, ModulePageAllocator, true> conditions;
  for (int64_t i = 0; OB_SUCC(ret) && i < part_aliases_.count(); i++) {
    const ObString &key_name = part_aliases_.at(i)->expr_name;
    ObReqOpExpr *key_condition = nullptr;
    if (OB_FAIL(construct_join_condition(l_table, r_table, key_name, key_name, condition, key_condition))) {
    } else if (OB_FAIL(conditions.push_back(key_condition))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(construct_join_condition(l_table, r_table, rowkey, rowkey, condition, rowkey_condition))) {
  } else if (OB_FAIL(conditions.push_back(rowkey_condition))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(join_condition, alloc_, T_OP_AND, conditions))) {
  }
  return ret;
}

int ObESQueryParser::add_partition_keys_to_select(ObQueryReqFromJson *fts_base, ObQueryReqFromJson *knn_base)
{
  int ret = OB_SUCCESS;
  if (part_cols_.empty()) {
    // no partition keys, do nothing
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < part_cols_.count(); i++) {
      if (OB_NOT_NULL(fts_base) && OB_FAIL(fts_base->select_items_.push_back(part_cols_.at(i)))) {
        LOG_WARN("failed to add partition expr to fts select items", K(ret));
      } else if (OB_NOT_NULL(knn_base) && OB_FAIL(knn_base->select_items_.push_back(part_cols_.at(i)))) {
        LOG_WARN("failed to add partition expr to knn select items", K(ret));
      }
    }
  }
  return ret;
}

int ObESQueryParser::set_default_score(ObQueryReqFromJson *query_req, double default_score)
{
  int ret = OB_SUCCESS;
  ObReqConstExpr *score_expr = nullptr;
  // negative score is invalid
  if (default_score < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid default score", K(ret), K(default_score));
  } else if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(score_expr, alloc_, default_score, ObIntType))) {
  } else if (OB_FAIL(query_req->add_score_item(alloc_, score_expr))) {
  }
  return ret;
}

int ObESQueryParser::set_order_by_column(ObQueryReqFromJson *query_req, const ObString &column_name, const ObString &table_name, bool ascent/* true */)
{
  int ret = OB_SUCCESS;
  ObReqColumnExpr *column_expr = nullptr;
  OrderInfo *order_info = nullptr;
  if (OB_FAIL(ObReqColumnExpr::construct_column_expr(column_expr, alloc_, column_name))) {
  } else if (OB_FAIL(construct_order_by_item(column_expr, ascent, order_info))) {
  } else if (OB_FAIL(query_req->order_items_.push_back(order_info))) {
  } else if (!table_name.empty()) {
    column_expr->table_name = table_name;
  }
  return ret;
}

int ObESQueryParser::construct_hybrid_query(ObQueryReqFromJson *fts, ObQueryReqFromJson *knn, ObQueryReqFromJson *&hybrid)
{
  int ret = OB_SUCCESS;
  ObReqColumnExpr *fts_rowkey = NULL;
  ObReqColumnExpr *knn_rowkey = NULL;
  ObReqExpr *fts_score = NULL;
  ObReqExpr *knn_score = NULL;
  ObReqColumnExpr *fts_col = NULL;
  ObReqColumnExpr *knn_col = NULL;
  ObReqJoinedTable *join_table = NULL;
  ObReqTable *fts_table = NULL;
  ObReqTable *knn_table = NULL;
  const ObString fts_alias = FTS_ALIAS;
  const ObString knn_alias = VS_ALIAS;
  const ObString score_alias = SCORE_NAME;
  const ObString rowkey = ROWKEY_NAME;
  const ObString rowkey_hint = HIDDEN_COLUMN_VISIBLE_HINT;
  ObReqOpExpr *join_condition = NULL;
  ObReqOpExpr *score_res = NULL;
  OrderInfo *order_info = NULL;
  ObQueryReqFromJson *base_table_fts_req = NULL;
  ObQueryReqFromJson *base_table_knn_req = NULL;
  ReqTableType knn_table_type = UNKNOWN_TABLE;
  if (OB_FAIL(get_base_table_query(fts, base_table_fts_req))) {
  } else if (OB_FAIL(get_base_table_query(knn, base_table_knn_req, &knn_table_type))) {
  } else if (OB_ISNULL(hybrid = OB_NEWx(ObQueryReqFromJson, &alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(fts_rowkey, alloc_, rowkey))) {
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(knn_rowkey, alloc_, rowkey))) {
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(fts_col, alloc_, FTS_SCORE_NAME))) {
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(knn_col, alloc_, VS_SCORE_NAME))) {
  } else if (FALSE_IT(fts_score = fts_col)) {
  } else if (FALSE_IT(knn_score = knn_col)) {
  } else if (OB_ISNULL(join_table = OB_NEWx(ObReqJoinedTable, &alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else if (OB_FAIL(base_table_fts_req->add_req_hint(rowkey_hint))) {
  } else if (OB_FAIL(knn_table_type != MULTI_SET && base_table_knn_req->add_req_hint(rowkey_hint))) {
  } else if (OB_FAIL(base_table_fts_req->select_items_.push_back(fts_rowkey))) {
  } else if (OB_FAIL(knn_table_type != MULTI_SET && base_table_knn_req->select_items_.push_back(knn_rowkey))) {
  }
  // add partition keys to base table SELECT items
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(add_partition_keys_to_select(base_table_fts_req, knn_table_type != MULTI_SET ? base_table_knn_req : nullptr))) {
  } else {
    fts->output_all_columns_ = false;
    fts->score_alias_ = FTS_SCORE_NAME;
    knn->score_alias_ = VS_SCORE_NAME;
    if (!fts->order_items_.empty()) {
      if (query_not_need_order(fts)) {
        fts->order_items_.reset();
      } else {
        fts->order_items_.at(0)->order_item->set_alias(fts->score_alias_);
      }
    }
    if (!knn->order_items_.empty() && query_not_need_order(knn)) {
      knn->order_items_.reset();
    }
    if (!out_cols_->empty()) {
      if (OB_FAIL(set_output_columns(*knn, false, false))) {
      } else if (OB_FAIL(set_output_columns(*fts, false, false))) {
      }
    } else {
      // only for unitest
      if (!knn->is_score_item_exist() && OB_FAIL(add_score_col("", *knn))) {
        LOG_WARN("fail to add score col", K(ret));
      }
    }
    // add partition key column references to outer queries if there are any sub queries
    for (int64_t i = 0; OB_SUCC(ret) && i < part_aliases_.count(); i++) {
      if (base_table_fts_req != fts && OB_FAIL(fts->select_items_.push_back(part_aliases_.at(i)))) {
        LOG_WARN("fail to add partition key to fts outer query select items", K(ret), K(i));
      } else if (base_table_knn_req != knn && !knn->output_all_columns_ &&
                 OB_FAIL(knn->select_items_.push_back(part_aliases_.at(i)))) {
        LOG_WARN("fail to add partition key to knn outer query select items", K(ret), K(i));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (base_table_fts_req != fts && OB_FAIL(fts->select_items_.push_back(fts_rowkey))) {
      LOG_WARN("fail to create query request", K(ret));
    } else if (base_table_knn_req != knn && !knn->output_all_columns_ && OB_FAIL(knn->select_items_.push_back(knn_rowkey))) {
      LOG_WARN("fail to create query request", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (fusion_config_.method == ObFusionMethod::RRF) {
    ObString empty_str = "";
    ObString fts_rank_alias = FTS_RANK_NAME;
    ObString vs_rank_alias = VS_RANK_NAME;
    if (OB_FAIL(construct_rank_query(empty_str, fts_score, fts_rank_alias, fts))) {
    } else if (OB_FAIL(construct_rank_query(empty_str, knn_score, vs_rank_alias, knn))) {
    } else if (OB_FAIL(construct_rank_score(fts_alias, fts_rank_alias, fts_score))) {
    } else if (OB_FAIL(construct_rank_score(knn_alias, vs_rank_alias, knn_score))) {
    }
  } else if (FALSE_IT(static_cast<ObReqColumnExpr *>(fts_score)->table_name = fts_alias)) {
  } else if (FALSE_IT(static_cast<ObReqColumnExpr *>(knn_score)->table_name = knn_alias)) {
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(construct_sub_query_table(fts_alias, fts, fts_table))) {
  } else if (OB_FAIL(construct_sub_query_table(knn_alias, knn, knn_table))) {
  } else if (OB_FAIL(construct_join_multi_condition(fts_alias, knn_alias, rowkey, T_OP_EQ, join_condition))) {
  } else if (FALSE_IT(join_table->init(fts_table, knn_table, join_condition, ObReqJoinType::FULL_OUTER_JOIN))) {
  } else if (OB_FAIL(hybrid->from_items_.push_back(join_table))) {
  } else if (OB_FAIL(construct_score_sum_expr(fts_score, knn_score, score_alias, score_res))) {
  } else if (OB_FAIL(hybrid->score_items_.push_back(score_res))) {
  } else if (OB_FAIL(construct_order_by_item(score_res, false, order_info))) {
  } else if (OB_FAIL(hybrid->order_items_.push_back(order_info))) {
  }
  return ret;
}

int ObESQueryParser::construct_rank_query(ObString &sub_query_name, ObReqExpr *order_expr, ObString &rank_alias, ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;
  ObReqWindowFunExpr *rank_expr = NULL;
  OrderInfo *order_info = NULL;
  ObString expr_name = "RANK";
  if (OB_FAIL(wrap_sub_query(sub_query_name, query_req))) {
  } else if (OB_FAIL(construct_order_by_item(order_expr, false, order_info))) {
  } else if (OB_FAIL(ObReqWindowFunExpr::construct_window_fun_expr(alloc_, order_info, expr_name, rank_alias, rank_expr))) {
  } else if (OB_FAIL(query_req->select_items_.push_back(rank_expr))) {
  }
  return ret;
}

int ObESQueryParser::construct_rank_score(const ObString &table_name, const ObString &rank_alias, ObReqExpr *&rank_score)
{
  int ret = OB_SUCCESS;
  ObReqOpExpr *div_expr = NULL;
  ObReqOpExpr *add_expr = NULL;
  ObReqConstExpr *const_expr = NULL;
  ObReqColumnExpr *ref_expr = NULL;

  if (OB_FAIL(ObReqColumnExpr::construct_column_expr(ref_expr, alloc_, rank_alias, table_name))) {
  } else if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(const_expr, alloc_, 1.0, ObIntType))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(add_expr, alloc_, T_OP_ADD, ref_expr, fusion_config_.rank_const))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(div_expr, alloc_, T_OP_DIV, const_expr, add_expr))) {
  } else {
    rank_score = div_expr;
  }

  return ret;
}

int ObESQueryParser::parse_basic_table(const ObString &table_name, ObQueryReqFromJson *query_req)
{
  int ret = OB_SUCCESS;
  ObReqTable *table = NULL;
  if (OB_ISNULL(table = OB_NEWx(ObReqTable, &alloc_, BASE_TABLE, table_name, database_name_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else if (OB_FAIL(query_req->from_items_.push_back(table))) {
  }
  return ret;
}

int ObESQueryParser::parse_query(ObIJsonBase &req_node, ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;
  ObEsQueryInfo *query_info = nullptr;
  ObReqExpr *score_expr = nullptr;
  ObReqExpr *condition_expr = nullptr;
  if (OB_ISNULL(query_req = OB_NEWx(ObQueryReqFromJson, &alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else if (OB_FAIL(ObEsQueryInfo::init_query_info(query_info, alloc_, query_req, nullptr, QUERY_ITEM_QUERY, true))) {
  } else if (OB_FAIL(get_query_depth(req_node, query_info->total_depth_))) {
  } else if (OB_FAIL(parse_single_term(req_node, *query_info))) {
  } else {
    score_expr = query_info->score_expr_;
    condition_expr = query_info->condition_expr_;
  }

  if (OB_FAIL(ret)) {
  } else if (OB_NOT_NULL(condition_expr) && OB_FAIL(query_req->condition_items_.push_back(condition_expr))) {
    LOG_WARN("failed add term to query request", K(ret));
  } else if (!query_info->need_construct_sub_query_with_minimum_should_match() &&
             OB_NOT_NULL(score_expr) && OB_FAIL(query_req->add_score_item(alloc_, score_expr))) {
    LOG_WARN("failed add term to score items", K(ret));
  } else if (OB_FAIL(parse_basic_table(table_name_, query_req))) {
  } else if (OB_FAIL(construct_sub_query_with_minimum_should_match(query_req, *query_info, "_fts_sub"))) {
  } else if (query_req->score_items_.empty()) {
    if (OB_FAIL(set_default_score(query_req, 0.0))) {
    } else {
      query_info->score_is_const_ = true;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (query_info->score_is_const_) {
    // do nothing
  } else {
    OrderInfo *order_info = nullptr;
    if (OB_FAIL(construct_order_by_item(query_req->score_items_.at(0), false, order_info))) {
    } else if (OB_FAIL(query_req->order_items_.push_back(order_info))) {
    }
  }
  return ret;
}

int ObESQueryParser::construct_expr_with_boost(ObReqExpr *base_expr, ObReqConstExpr *boost_expr, ObReqExpr *&result)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(boost_expr) && boost_expr->get_numeric_value() < 0.0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("boost value must not be negative", K(ret));
  } else if (OB_ISNULL(boost_expr) || boost_expr->get_numeric_value() == 1.0) {
    result = base_expr;
  } else {
    ObReqOpExpr *boost_mul_expr = nullptr;
    if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(boost_mul_expr, alloc_, T_OP_MUL, base_expr, boost_expr))) {
    } else {
      result = boost_mul_expr;
    }
  }
  return ret;
}

int ObESQueryParser::construct_es_expr_field(ObReqColumnExpr *raw_field, ObReqExpr *&field)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(raw_field)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("raw_field is null", K(ret));
  } else {
    ObReqColumnExpr *col_field = nullptr;
    double weight = (raw_field->weight_ == -1.0) ? 1.0 : raw_field->weight_;
    if (OB_FAIL(ObReqColumnExpr::construct_column_expr(col_field, alloc_, raw_field->expr_name, weight, true))) {
    } else {
      field = col_field;
    }
  }
  return ret;
}

int ObESQueryParser::construct_es_expr_options(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  const int64_t MATCH_PARAMS_BUF_SIZE = 128;
  char *buf = static_cast<char *>(alloc_.alloc(MATCH_PARAMS_BUF_SIZE));
  const char *score_type_str = (query_info.score_type_ == SCORE_TYPE_BEST_FIELDS) ? "best_fields" : "most_fields";
  int64_t pos = 0;
  uint64_t msm_val = query_info.msm_info_.get_msm_val();
  ObString options_str;
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate memory for match params buffer", K(ret));
  } else if (OB_FAIL(databuff_printf(buf, MATCH_PARAMS_BUF_SIZE, pos, "operator=or"))) {
  } else if (OB_FAIL(databuff_printf(buf, MATCH_PARAMS_BUF_SIZE, pos, ";boost=%.15g",
                     OB_NOT_NULL(query_info.boost_expr_) ? query_info.boost_expr_->get_numeric_value() : 1.0))) {
  } else if (msm_val > 0 &&
             OB_FAIL(databuff_printf(buf, MATCH_PARAMS_BUF_SIZE, pos, ";minimum_should_match=%ld", msm_val))) {
    LOG_WARN("fail to write minimum_should_match", K(ret));
  } else if (OB_FAIL(databuff_printf(buf, MATCH_PARAMS_BUF_SIZE, pos, ";type=%s", score_type_str))) {
  } else if (OB_FALSE_IT(options_str.assign_ptr(buf, pos))) {
  } else if (OB_FAIL(ObReqConstExpr::construct_const_expr(query_info.esql_options_expr_, alloc_, options_str, ObVarcharType))) {
  }
  return ret;
}

int ObESQueryParser::construct_es_expr(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  if (!query_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid query info for esql", K(ret));
  } else {
    // construct fields
    common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> params;
    for (int i = 0; OB_SUCC(ret) && i < query_info.field_exprs_.count(); i++) {
      ObReqExpr *field = nullptr;
      if (OB_FAIL(construct_es_expr_field(query_info.field_exprs_.at(i), field))) {
      } else if (OB_FAIL(params.push_back(field))) {
      }
    }

    // construct keywords
    if (OB_SUCC(ret)) {
      ObReqConstExpr *keywords = nullptr;
      char *buf = static_cast<char *>(alloc_.alloc(OB_MAX_SQL_LENGTH));
      int64_t pos = 0;
      ObString keywords_str;
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate memory for keyword param buffer", K(ret));
      } else if (OB_FAIL(databuff_printf(buf, OB_MAX_SQL_LENGTH, pos, "%.*s", query_info.query_text_.length(), query_info.query_text_.ptr()))) {
      } else if (OB_FALSE_IT(keywords_str = ObString(pos, buf))) {
      } else if (OB_FAIL(ObReqConstExpr::construct_const_expr(keywords, alloc_, keywords_str, ObVarcharType))) {
      } else if (OB_FAIL(params.push_back(keywords))) {
      }
    }

    // construct options
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(construct_es_expr_options(query_info))) {
    } else if (OB_FAIL(params.push_back(query_info.esql_options_expr_))) {
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObReqExpr::construct_expr(query_info.esql_condition_expr_, alloc_, "MATCH", params))) {
    }
  }
  return ret;
}

int ObESQueryParser::parse_bool(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  uint64_t count = 0;
  ObQueryReqFromJson *query_req = query_info.query_req_;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> score_items;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> condition_items;
  query_info.query_item_ = QUERY_ITEM_BOOL;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (OB_FALSE_IT(count = req_node.element_count())) {
  // Affects the default value of minimum_should_match.
  // IF exists must or filter, the default value of minimum_should_match will be 0.
  } else if (OB_FAIL(construct_minimum_should_match_info(req_node, query_info))) {
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    ObString key;
    ObIJsonBase *sub_node = nullptr;
    ObReqExpr *condition_item = nullptr;
    if (OB_FAIL(req_node.get_key(i, key))) {
    } else if (OB_FAIL(req_node.get_object_value(i, sub_node))) {
    } else if (key.case_compare("must") == 0) {
      if (OB_FAIL(parse_must_clauses(*sub_node, query_info, condition_item, score_items))) {
      }
    } else if (key.case_compare("should") == 0) {
      if (OB_FAIL(parse_should_clauses(*sub_node, query_info, condition_item, score_items))) {
      }
    } else if (key.case_compare("filter") == 0) {
      if (OB_FAIL(parse_filter_clauses(*sub_node, query_info, condition_item))) {
      }
    } else if (key.case_compare("must_not") == 0) {
      if (OB_FAIL(parse_must_not_clauses(*sub_node, query_info, condition_item))) {
      }
    } else if (key.case_compare("boost") == 0) {
      // has been parsed in construct_minimum_should_match_info()
      continue;
    } else if (key.case_compare("minimum_should_match") == 0) {
      // if no should clause, minimum_should_match must be specified
      if (query_info.should_cnt_ < 1 && query_info.msm_info_.get_msm_val() > 0) {
        ObReqConstExpr *zero_expr = nullptr;
        if (query_info.msm_info_.term_cnt_ != 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("term count must be 0 when no should clause", K(ret));
        } else if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(zero_expr, alloc_, 0.0, ObIntType))) {
        } else {
          condition_item = zero_expr;
        }
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not supported sytnax in query", K(ret), K(key));
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(condition_item) && OB_FAIL(condition_items.push_back(condition_item))) {
      LOG_WARN("failed add term to bool expr array", K(ret), K(i));
    }
  }

  if (OB_SUCC(ret) && query_info.need_cal_score_ && score_items.empty() && query_info.must_not_cnt_ < 1 && query_info.filter_cnt_ < 1) {
    ObReqConstExpr *score_expr = nullptr;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(score_expr, alloc_, 1.0, ObIntType))) {
    } else if (OB_FAIL(score_items.push_back(score_expr))) {
    } else if (query_info.outer_query_item_ == QUERY_ITEM_QUERY) {
      query_info.score_is_const_ = true;
    }
  }
  if (OB_SUCC(ret) && !score_items.empty()) {
    ObReqOpExpr *tmp_score_expr = nullptr;
    if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_score_expr, alloc_, T_OP_ADD, score_items))) {
    } else if (OB_FAIL(construct_expr_with_boost(tmp_score_expr, query_info.boost_expr_, query_info.score_expr_))) {
    } else if (!query_info.score_alias_items_.empty()) {
      if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_score_expr, alloc_, T_OP_ADD, query_info.score_alias_items_))) {
      } else if (OB_FAIL(construct_expr_with_boost(tmp_score_expr, query_info.boost_expr_, query_info.score_alias_expr_))) {
      } else if (OB_FAIL(query_info.query_req_->outer_score_items_.push_back(query_info.score_alias_expr_))) {
      }
    }
  }
  if (OB_SUCC(ret) && condition_items.empty() &&
      (query_info.outer_query_item_ == QUERY_ITEM_MUST_NOT ||
       query_info.outer_query_item_ == QUERY_ITEM_SHOULD)) {
    ObReqConstExpr *one_expr = nullptr;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(one_expr, alloc_, 1.0, ObIntType))) {
    } else if (OB_FAIL(condition_items.push_back(one_expr))) {
    }
  }
  if (OB_SUCC(ret) && !condition_items.empty()) {
    ObReqOpExpr *tmp_condition_expr = nullptr;
    if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_condition_expr, alloc_, T_OP_AND, condition_items))) {
    } else {
      query_info.condition_expr_ = tmp_condition_expr;
    }
  }
  return ret;
}

int ObESQueryParser::parse_must_clauses(ObIJsonBase &req_node, ObEsQueryInfo &query_info, ObReqExpr *&condition_expr, ObIArray<ObReqExpr *> &score_items)
{
  int ret = OB_SUCCESS;
  uint64_t count = 0;
  ObIJsonBase *clause_val = NULL;
  common::ObSEArray<ObReqExpr*, 4, common::ModulePageAllocator, true> condition_items;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT &&
      req_node.json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (OB_FALSE_IT(count = req_node.element_count())) {
  } else if (count == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("must clause must not be empty", K(ret));
  } else if (req_node.json_type() == ObJsonNodeType::J_OBJECT && count > 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("must clause must only has one key", K(ret));
  } else {
    query_info.must_cnt_ = count;
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    if (req_node.json_type() == ObJsonNodeType::J_OBJECT) {
      clause_val = &req_node;
    } else if (OB_FAIL(req_node.get_array_element(i, clause_val))) {
    }
    ObEsQueryInfo *sub_query_info = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObEsQueryInfo::init_query_info(sub_query_info, alloc_, query_info.query_req_, &query_info, QUERY_ITEM_MUST, query_info.need_cal_score_))) {
    } else if (OB_FAIL(query_info.sub_query_infos_.push_back(sub_query_info))) {
    } else if (OB_FAIL(parse_single_term(*clause_val, *sub_query_info))) {
    } else if (OB_NOT_NULL(sub_query_info->score_expr_) &&
               OB_FAIL(score_items.push_back(sub_query_info->score_expr_))) {
      LOG_WARN("failed add term to score items", K(ret), K(i));
    } else if (OB_NOT_NULL(sub_query_info->condition_expr_) &&
               OB_FAIL(condition_items.push_back(sub_query_info->condition_expr_))) {
      LOG_WARN("failed add term to condition items", K(ret), K(i));
    } else if (OB_FAIL(handle_msm_for_sub_score(query_info, *sub_query_info, sub_query_info->score_expr_))) {
    }
  }
  if (OB_SUCC(ret)) {
    ObReqOpExpr *tmp_condition_expr = nullptr;
    if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_condition_expr, alloc_, T_OP_AND, condition_items))) {
    } else {
      condition_expr = tmp_condition_expr;
    }
  }
  return ret;
}

int ObESQueryParser::parse_must_not_clauses(ObIJsonBase &req_node, ObEsQueryInfo &query_info, ObReqExpr *&condition_expr)
{
  int ret = OB_SUCCESS;
  uint64_t count = 0;
  ObIJsonBase *clause_val = nullptr;
  common::ObSEArray<ObReqExpr*, 4, common::ModulePageAllocator, true> condition_items;
  ObReqConstExpr *one_expr = nullptr;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT &&
      req_node.json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (OB_FALSE_IT(count = req_node.element_count())) {
  } else if (count == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("must not clause must not be empty", K(ret));
  } else if (req_node.json_type() == ObJsonNodeType::J_OBJECT && count > 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("must not clause must only has one key", K(ret));
  } else {
    query_info.must_not_cnt_ = count;
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    if (req_node.json_type() == ObJsonNodeType::J_OBJECT) {
      clause_val = &req_node;
    } else if (OB_FAIL(req_node.get_array_element(i, clause_val))) {
    }
    ObEsQueryInfo *sub_query_info = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObEsQueryInfo::init_query_info(sub_query_info, alloc_, query_info.query_req_, &query_info, QUERY_ITEM_MUST_NOT))) {
    } else if (OB_FAIL(query_info.sub_query_infos_.push_back(sub_query_info))) {
    } else if (OB_FAIL(parse_single_term(*clause_val, *sub_query_info))) {
    } else if (OB_NOT_NULL(sub_query_info->condition_expr_)) {
      if (sub_query_info->msm_info_.apply_type_ != MSM_APPLY_WITH_SUB && OB_FAIL(condition_items.push_back(sub_query_info->condition_expr_))) {
        LOG_WARN("failed add term to condition items", K(ret), K(i));
      }
    } else if (OB_ISNULL(one_expr)) {
      if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(one_expr, alloc_, 1.0, ObIntType))) {
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("sub query filter expr is null", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    ObReqOpExpr *or_expr = nullptr;
    // if one_expr is not null, clear condition_items and add one_expr to condition_items as the only one
    if (OB_NOT_NULL(one_expr)) {
      condition_items.reset();
      if (OB_FAIL(condition_items.push_back(one_expr))) {
      }
    }
    if (OB_FAIL(ret) || condition_items.empty()) {
    } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(or_expr, alloc_, T_OP_OR, condition_items))) {
    } else {
      ObReqOpExpr *not_expr = nullptr;
      if (OB_FAIL(ObReqOpExpr::construct_unary_op_expr(not_expr, alloc_, T_OP_NOT, or_expr))) {
      } else {
        condition_expr = not_expr;
      }
    }
  }
  return ret;
}

int ObESQueryParser::parse_should_clauses(ObIJsonBase &req_node, ObEsQueryInfo &query_info, ObReqExpr *&condition_expr, ObIArray<ObReqExpr *> &score_items)
{
  int ret = OB_SUCCESS;
  uint64_t count = 0;
  ObIJsonBase *clause_val = NULL;
  common::ObSEArray<ObReqExpr*, 4, common::ModulePageAllocator, true> condition_items;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT &&
      req_node.json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (OB_FALSE_IT(count = req_node.element_count())) {
  } else if (count == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("should clause must not be empty", K(ret));
  } else if (req_node.json_type() == ObJsonNodeType::J_OBJECT && count > 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("should clause must only has one key", K(ret));
  } else {
    query_info.should_cnt_ = count;
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    if (req_node.json_type() == ObJsonNodeType::J_OBJECT) {
      clause_val = &req_node;
    } else if (OB_FAIL(req_node.get_array_element(i, clause_val))) {
    }
    ObEsQueryInfo *sub_query_info = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObEsQueryInfo::init_query_info(sub_query_info, alloc_, query_info.query_req_, &query_info, QUERY_ITEM_SHOULD, query_info.need_cal_score_))) {
    } else if (OB_FAIL(query_info.sub_query_infos_.push_back(sub_query_info))) {
    } else if (OB_FAIL(parse_single_term(*clause_val, *sub_query_info))) {
    } else if (OB_NOT_NULL(sub_query_info->score_expr_) && OB_FAIL(score_items.push_back(sub_query_info->score_expr_))) {
      LOG_WARN("fail to add score expr to score items", K(ret), K(i));
    } else if (OB_NOT_NULL(sub_query_info->condition_expr_) && OB_FAIL(condition_items.push_back(sub_query_info->condition_expr_))) {
      LOG_WARN("fail to add condition expr to should exprs", K(ret), K(i));
    } else if (OB_FAIL(handle_msm_for_sub_score(query_info, *sub_query_info, sub_query_info->score_expr_))) {
    }
  }
  if (OB_SUCC(ret) && query_info.msm_info_.msm_expr_ && !condition_items.empty()) {
    ObReqExpr *should_condition = nullptr;
    if (query_info.msm_info_.apply_type_ == MSM_APPLY_WITH_SUB) {
      ObReqOpExpr *or_expr = nullptr;
      if (OB_FAIL(handle_msm_for_sub_condition(query_info))) {
      } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(or_expr, alloc_, T_OP_OR, condition_items))) {
      } else {
        should_condition = or_expr;
      }
    } else if (OB_FAIL(build_should_condition_combine(0, query_info.msm_info_.get_msm_val(), condition_items, nullptr, should_condition))) {
    }
    if (OB_SUCC(ret)) {
      condition_expr = should_condition;
    }
  }
  return ret;
}

int ObESQueryParser::parse_filter_clauses(ObIJsonBase &req_node, ObEsQueryInfo &query_info, ObReqExpr *&condition_expr)
{
  int ret = OB_SUCCESS;
  uint64_t count = 0;
  ObIJsonBase *clause_val = NULL;
  common::ObSEArray<ObReqExpr*, 4, common::ModulePageAllocator, true> condition_items;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT &&
      req_node.json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (OB_FALSE_IT(count = req_node.element_count())) {
  } else if (count == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("filter clause must not be empty", K(ret));
  } else if (req_node.json_type() == ObJsonNodeType::J_OBJECT && count > 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("filter clause must only has one key", K(ret));
  } else {
    query_info.filter_cnt_ = count;
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    if (req_node.json_type() == ObJsonNodeType::J_OBJECT) {
      clause_val = &req_node;
    } else if (OB_FAIL(req_node.get_array_element(i, clause_val))) {
    }
    ObEsQueryInfo *sub_query_info = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObEsQueryInfo::init_query_info(sub_query_info, alloc_, query_info.query_req_, &query_info, QUERY_ITEM_FILTER))) {
    } else if (OB_FAIL(query_info.sub_query_infos_.push_back(sub_query_info))) {
    } else if (OB_FAIL(parse_single_term(*clause_val, *sub_query_info))) {
    } else if (OB_NOT_NULL(sub_query_info->condition_expr_) && OB_FAIL(condition_items.push_back(sub_query_info->condition_expr_))) {
      LOG_WARN("failed add term to condition items", K(ret), K(i));
    }
  }
  if (OB_SUCC(ret)) {
    ObReqOpExpr *tmp_condition_expr = nullptr;
    if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_condition_expr, alloc_, T_OP_AND, condition_items))) {
    } else {
      condition_expr = tmp_condition_expr;
    }
  }
  return ret;
}

int ObESQueryParser::parse_single_term(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (req_node.element_count() != 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("single term must only contain one term", K(ret));
  }
  ObString key;
  ObIJsonBase *sub_node = nullptr;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(req_node.get_key(0, key))) {
  } else if (OB_FAIL(req_node.get_object_value(0, sub_node))) {
  } else if (key.case_compare("bool") == 0) {
    if (OB_FAIL(parse_bool(*sub_node, query_info))) {
    }
  } else if (key.case_compare("range") == 0) {
    if (OB_FAIL(parse_range(*sub_node, query_info))) {
    }
  } else if (key.case_compare("match") == 0) {
    if (OB_FAIL(parse_match(*sub_node, query_info))) {
    }
  } else if (key.case_compare("term") == 0) {
    if (OB_FAIL(parse_term(*sub_node, query_info))) {
    }
  } else if (key.case_compare("query_string") == 0) {
    if (OB_FAIL(parse_query_string(*sub_node, query_info))) {
    }
  } else if (key.case_compare("multi_match") == 0) {
    if (OB_FAIL(parse_multi_match(*sub_node, query_info))) {
    }
  } else if (key.case_compare("rank_feature") == 0) {
    if (OB_FAIL(parse_rank_feature(*sub_node, query_info))) {
    }
  } else if (key.case_compare("terms") == 0) {
    if (OB_FAIL(parse_terms(*sub_node, query_info))) {
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported sytnax in query", K(ret), K(key));
  }

  if (OB_SUCC(ret) && enable_es_mode_ && query_info.support_es_mode()) {
    if (OB_FAIL(construct_es_expr(query_info))) {
    } else {
      ObReqExpr *esql_score_expr = nullptr;
      if (OB_FAIL(ObReqExpr::construct_expr(esql_score_expr, alloc_, "score()"))) {
      } else {
        query_info.score_expr_ = esql_score_expr;
        query_info.condition_expr_ = query_info.esql_condition_expr_;
      }
    }
  }
  return ret;
}

int ObESQueryParser::construct_weighted_expr(ObReqExpr *base_expr, double weight, ObReqExpr *&weighted_expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(base_expr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("base_expr is null", K(ret));
  } else if (weight == 1.0 || weight == -1.0) {
    weighted_expr = base_expr;
  } else {
    ObReqConstExpr *weight_const = nullptr;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(weight_const, alloc_, weight, ObDoubleType))) {
    } else {
      ObReqOpExpr *tmp_expr = nullptr;
      if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(tmp_expr, alloc_, T_OP_MUL, base_expr, weight_const))) {
      } else {
        weighted_expr = tmp_expr;
      }
    }
  }
  return ret;
}

int ObESQueryParser::parse_range(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  ObString col_name;
  ObIJsonBase *sub_node = nullptr;
  ObReqExpr *key_expr = nullptr;
  uint64_t count = 0;
  int condition_num = 0;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> condition_exprs;
  query_info.query_item_ = QUERY_ITEM_RANGE;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (req_node.element_count() != 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("range must only contain one term", K(ret));
  } else if (OB_FAIL(req_node.get_key(0, col_name))) {
  } else if (OB_FAIL(req_node.get_object_value(0, sub_node))) {
  } else if (FALSE_IT(count = sub_node->element_count())) {
  } else if (count == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unexpectd range condition", K(ret));
  } else if (OB_FAIL(create_column_or_base_expr(col_name, key_expr))) {
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    ObString key;
    ObIJsonBase *var_node = NULL;
    ObReqConstExpr *var = NULL;
    ObReqOpExpr *cmp_expr = NULL;
    ObItemType type = T_INVALID;
    if (OB_FAIL(sub_node->get_key(i, key))) {
    } else if (OB_FAIL(sub_node->get_object_value(i, var_node))) {
    } else if (OB_FAIL(parse_const(*var_node, var, true))) {
    } else if (key.case_compare("gt") == 0) {
      type = T_OP_GT;
      condition_num++;
    } else if (key.case_compare("gte") == 0) {
      type = T_OP_GE;
      condition_num++;
    } else if (key.case_compare("lt") == 0) {
      type = T_OP_LT;
      condition_num++;
    } else if (key.case_compare("lte") == 0) {
      type = T_OP_LE;
      condition_num++;
    } else if (key.case_compare("boost") == 0) {
      if (var->get_numeric_value() < 0.0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("boost value must not be negative", K(ret));
        break;
      } else {
        query_info.boost_expr_ = var;
        continue;
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not supported sytnax in query", K(ret), K(key));
    }

    if (OB_FAIL(ret)) {
    } else if (type != T_INVALID && OB_FAIL(ObReqOpExpr::construct_binary_op_expr(cmp_expr, alloc_, type, key_expr, var))) {
      LOG_WARN("fail to construct cmp expr", K(ret));
    } else if (OB_FAIL(condition_exprs.push_back(cmp_expr))) {
    }
  }

  if (OB_SUCC(ret) && condition_exprs.count() > 0) {
    ObReqOpExpr *and_expr = nullptr;
    if (OB_FAIL(ObReqOpExpr::construct_op_expr(and_expr, alloc_, T_OP_AND, condition_exprs))) {
    } else if (OB_FALSE_IT(query_info.condition_expr_ = and_expr)) {
    } else if (!query_info.need_cal_score_) {
    } else if (OB_FAIL(construct_expr_with_boost(and_expr, query_info.boost_expr_, query_info.score_expr_))) {
    }
  }

  return ret;
}

int ObESQueryParser::parse_rank_feature(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  query_info.query_item_ = QUERY_ITEM_RANK_FEATURE;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()), K(req_node.element_count()));
  }
  ObRankFeatDef rank_feat_def;
  bool has_field = false;
  uint64_t algorithm_count = 0;
  for (uint64_t i = 0; OB_SUCC(ret) && i < req_node.element_count(); i++) {
    ObString key;
    ObIJsonBase *sub_node = NULL;
    if (OB_FAIL(req_node.get_key(i, key))) {
    } else if (OB_FAIL(req_node.get_object_value(i, sub_node))) {
    } else if (key.case_compare("field") == 0) {
      if (OB_FAIL(check_rank_feat_param(sub_node, algorithm_count, has_field, key))) {
      } else if (OB_FAIL(parse_field(*sub_node, rank_feat_def.number_field))) {
      } else {
        ObReqConstExpr *const_expr = NULL;
        ObReqOpExpr *is_not_expr = NULL;
        if (OB_FAIL(ObReqConstExpr::construct_const_expr(const_expr, alloc_, "NULL", ObNullType))) {
        } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(is_not_expr, alloc_, T_OP_IS_NOT, rank_feat_def.number_field, const_expr))) {
        } else {
          query_info.condition_expr_ = is_not_expr;
        }
      }
    } else if (key.case_compare("saturation") == 0) {
      ObString empty_str;
      if (OB_FAIL(check_rank_feat_param(sub_node, algorithm_count, has_field, key))) {
      } else if (OB_FAIL(parse_rank_feat_param(*sub_node, "pivot", empty_str, rank_feat_def.pivot,
                                        rank_feat_def.exponent, rank_feat_def.positive_impact))) {
      } else {
        rank_feat_def.type = ObRankFeatureType::SATURATION;
      }
    } else if (key.case_compare("sigmoid") == 0) {
      ObString ex_str = "exponent";
      if (OB_FAIL(check_rank_feat_param(sub_node, algorithm_count, has_field, key))) {
      } else if (OB_FAIL(parse_rank_feat_param(*sub_node, "pivot", ex_str, rank_feat_def.pivot,
                                        rank_feat_def.exponent, rank_feat_def.positive_impact))) {
      } else {
        rank_feat_def.type = ObRankFeatureType::SIGMOID;
      }
    } else if (key.case_compare("linear") == 0) {
      ObString empty_str;
      if (OB_FAIL(check_rank_feat_param(sub_node, algorithm_count, has_field, key))) {
      } else if (OB_FAIL(parse_rank_feat_param(*sub_node, empty_str, empty_str, rank_feat_def.pivot,
                                        rank_feat_def.exponent, rank_feat_def.positive_impact))) {
      } else {
        rank_feat_def.type = ObRankFeatureType::LINEAR;
      }
    } else if (key.case_compare("log") == 0) {
      ObString empty_str;
      if (OB_FAIL(check_rank_feat_param(sub_node, algorithm_count, has_field, key))) {
      } else if (OB_FAIL(parse_rank_feat_param(*sub_node, "scaling_factor", empty_str, rank_feat_def.scaling_factor,
                                        rank_feat_def.exponent, rank_feat_def.positive_impact))) {
      } else {
        rank_feat_def.type = ObRankFeatureType::LOG;
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unexpectd rank feature param", K(ret), K(key));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!has_field || algorithm_count == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("rank feature must has field and one algorithm", K(ret));
  } else if (!query_info.need_cal_score_) {
  } else if (OB_FAIL(construct_rank_feat_expr(rank_feat_def, query_info.score_expr_))) {
  }

  return ret;
}

int ObESQueryParser::check_rank_feat_param(ObIJsonBase *sub_node, uint64_t &algorithm_count, bool &has_field, const ObString &key)
{
  int ret = OB_SUCCESS;
  uint64_t count = 0;
  ObIJsonBase *pos_val = NULL;
  if (key.case_compare("field") == 0) {
    has_field = true;
  } else {
    if (FALSE_IT(count = sub_node->element_count())) {
    } else if (OB_FAIL(sub_node->get_object_value("positive_score_impact", pos_val))) {
      if (ret == OB_SEARCH_NOT_FOUND) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to get positive_score_impact value", K(ret));
      }
    } else {
      --count;
    }
    if (OB_FAIL(ret)) {
    } else if (FALSE_IT(algorithm_count++)) {
    } else if (algorithm_count > 1) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unexpectd rank feature param, only one algorithm is supported", K(ret), K(key));
    } else if (key.case_compare("saturation") == 0 &&
               count != 1) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unexpectd rank feature param, saturation must has one param", K(ret), K(key));
    } else if (key.case_compare("sigmoid") == 0 &&
               count != 2) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unexpectd rank feature param, sigmoid must has two params", K(ret), K(key));
    } else if (key.case_compare("linear") == 0 &&
               count != 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unexpectd rank feature param, linear must has no param", K(ret), K(key));
    } else if (key.case_compare("log") == 0 &&
               count != 1) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unexpectd rank feature param, log must has one param", K(ret), K(key));
    }
  }
  return ret;
}

int ObESQueryParser::parse_rank_feat_param(ObIJsonBase &req_node, const ObString &para1, const ObString &para2,
                                          ObReqConstExpr *&const_para1, ObReqConstExpr *&const_para2, bool &positive)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *val1 = NULL;
  ObIJsonBase *val2 = NULL;
  ObString positive_str = "positive_score_impact";
  ObIJsonBase *pos_val = NULL;
  positive = true;
  if (!para1.empty() && OB_FAIL(req_node.get_object_value(para1, val1))) {
    LOG_WARN("fail to get pivot value", K(ret));
  } else if (!para2.empty() && OB_FAIL(req_node.get_object_value(para2, val2))) {
    LOG_WARN("fail to get pivot value", K(ret));
  } else if (OB_FAIL(req_node.get_object_value(positive_str, pos_val))) {
    if (ret == OB_SEARCH_NOT_FOUND) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get positive_score_impact value", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (val1 != NULL && OB_FAIL(parse_const(*val1, const_para1, true))) {
    LOG_WARN("fail to parse const value", K(ret));
  } else if (val2 != NULL && OB_FAIL(parse_const(*val2, const_para2, true))) {
    LOG_WARN("fail to parse const value", K(ret));
  } else if (pos_val != NULL) {
    positive = pos_val->get_boolean();
  }
  return ret;
}

int ObESQueryParser::construct_rank_feat_expr(const ObRankFeatDef &rank_feat_def, ObReqExpr *&rank_feat_expr)
{
  int ret = OB_SUCCESS;
  switch (rank_feat_def.type) {
    case ObRankFeatureType::SATURATION : {
      ObReqOpExpr *div_expr = NULL;
      ObReqOpExpr *add_expr = NULL;
      if (OB_ISNULL(rank_feat_def.number_field) || OB_ISNULL(rank_feat_def.pivot)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpectd null ptr", K(ret));
      } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(add_expr, alloc_, T_OP_ADD, rank_feat_def.number_field, rank_feat_def.pivot))) {
      } else if (rank_feat_def.positive_impact && OB_FAIL(ObReqOpExpr::construct_binary_op_expr(div_expr, alloc_, T_OP_DIV, rank_feat_def.number_field, add_expr))) {
        LOG_WARN("fail to create div expr", K(ret));
      } else if (!rank_feat_def.positive_impact && OB_FAIL(ObReqOpExpr::construct_binary_op_expr(div_expr, alloc_, T_OP_DIV, rank_feat_def.pivot, add_expr))) {
        LOG_WARN("fail to create div expr", K(ret));
      } else {
        rank_feat_expr = div_expr;
      }
      break;
    }
    case ObRankFeatureType::LINEAR : {
      if (rank_feat_def.positive_impact) {
        rank_feat_expr = rank_feat_def.number_field;
      } else {
        ObReqOpExpr *div_expr = NULL;
        ObReqConstExpr *one_expr = NULL;
        if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(one_expr, alloc_, 1.0, ObIntType))) {
        } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(div_expr, alloc_, T_OP_DIV, one_expr, rank_feat_def.number_field))) {
        } else {
          rank_feat_expr = div_expr;
        }
      }
      break;
    }
    case ObRankFeatureType::LOG : {
      // only positive impact
      ObReqExpr *ln_expr = NULL;
      ObReqOpExpr *add_expr = NULL;
      if (OB_ISNULL(rank_feat_def.number_field) || OB_ISNULL(rank_feat_def.scaling_factor)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpectd null ptr", K(ret));
      } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(add_expr, alloc_, T_OP_ADD, rank_feat_def.number_field, rank_feat_def.scaling_factor))) {
      } else if (OB_FAIL(ObReqExpr::construct_expr(ln_expr, alloc_, N_LN, add_expr))) {
      } else {
        rank_feat_expr = ln_expr;
      }
      break;
    }
    case ObRankFeatureType::SIGMOID : {
      ObReqExpr *field_pow_expr = NULL;
      ObReqExpr *piv_pow_expr = NULL;
      ObReqOpExpr *add_expr = NULL;
      ObReqOpExpr *div_expr = NULL;
      if (OB_ISNULL(rank_feat_def.number_field) || OB_ISNULL(rank_feat_def.pivot) || OB_ISNULL(rank_feat_def.exponent)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpectd null ptr", K(ret));
      } else if (OB_FAIL(ObReqExpr::construct_expr(field_pow_expr, alloc_, N_POW, rank_feat_def.number_field, rank_feat_def.exponent))) {
      } else if (OB_FAIL(ObReqExpr::construct_expr(piv_pow_expr, alloc_, N_POW, rank_feat_def.pivot, rank_feat_def.exponent))) {
      } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(add_expr, alloc_, T_OP_ADD, field_pow_expr, piv_pow_expr))) {
      } else if (rank_feat_def.positive_impact && OB_FAIL(ObReqOpExpr::construct_binary_op_expr(div_expr, alloc_, T_OP_DIV, field_pow_expr, add_expr))) {
        LOG_WARN("fail to create div expr", K(ret));
      } else if (!rank_feat_def.positive_impact && OB_FAIL(ObReqOpExpr::construct_binary_op_expr(div_expr, alloc_, T_OP_DIV, piv_pow_expr, add_expr))) {
        LOG_WARN("fail to create div expr", K(ret));
      } else {
        rank_feat_expr = div_expr;
      }
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unexpect rank feature expr type", K(ret), K(rank_feat_def.type));
    }
  }
  return ret;
}

int ObESQueryParser::parse_match(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  ObString col_name;
  ObString query_text;
  ObString idx_name;
  ObIJsonBase *col_para = nullptr;
  ObReqColumnExpr *col_expr = nullptr;
  ObReqConstExpr *query_expr = nullptr;
  ObReqMatchExpr *match_expr = nullptr;
  ObReqExpr *score_expr = nullptr;
  query_info.query_item_ = QUERY_ITEM_MATCH;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("match expr should be object", K(ret));
  } else if (req_node.element_count() != 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("match expr should have exactly one element", K(ret));
  } else if (OB_FAIL(req_node.get_object_value(0, col_name, col_para))) {
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(col_expr, alloc_, col_name))) {
  } else if (col_para->json_type() == ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("match field should have exactly one element", K(ret));
  } else if (col_para->json_type() != ObJsonNodeType::J_OBJECT) {
    query_text = ObString(col_para->get_data_length(), col_para->get_data()).trim();
    if (OB_FAIL(ObReqConstExpr::construct_const_expr(query_expr, alloc_, query_text, ObVarcharType))) {
    } else if (OB_FAIL(ObReqMatchExpr::construct_match_expr(match_expr, alloc_, col_expr, query_expr))) {
    } else if (query_info.need_cal_score_) {
      score_expr = match_expr;
    }
  } else /*if (col_para->json_type() == ObJsonNodeType::J_OBJECT)*/ {
    bool found_query = false;
    for (uint64_t i = 0; OB_SUCC(ret) && i < col_para->element_count(); i++) {
      ObIJsonBase *value_node = NULL;
      ObString key;
      if (OB_FAIL(col_para->get_object_value(i, key, value_node))) {
      } else if (key.case_compare("query") == 0) {
        query_text = ObString(value_node->get_data_length(), value_node->get_data()).trim();
        if (OB_FAIL(ObReqConstExpr::construct_const_expr(query_expr, alloc_, query_text, ObVarcharType))) {
        } else if (OB_FAIL(ObReqMatchExpr::construct_match_expr(match_expr, alloc_, col_expr, query_expr))) {
        } else {
          found_query = true;
        }
      } else if (key.case_compare("boost") == 0) {
        if (OB_FAIL(parse_boost(*value_node, query_info.boost_expr_))) {
        }
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("It's not supported to use this key in match expr", K(ret), K(key));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (!found_query) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("match expr should have query", K(ret));
    } else if (!query_info.need_cal_score_) {
    } else if (OB_FAIL(construct_expr_with_boost(match_expr, query_info.boost_expr_, score_expr))) {
    }
  }

  // index hint
  ObQueryReqFromJson *query_req = query_info.query_req_;
  if (OB_SUCC(ret) && OB_FAIL(get_match_idx_name(col_name, idx_name))) {
    LOG_WARN("fail to get match index name", K(ret));
  } else if (!idx_name.empty()) {
    if (query_req->match_idxs_.count() == 0) {
      // add table name first, for generate union merge hint
      if (OB_FAIL(query_req->match_idxs_.push_back(table_name_))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(query_req->match_idxs_.push_back(idx_name))) {
    }
  }

  if (OB_SUCC(ret)) {
    query_info.score_expr_ = score_expr;
    query_info.condition_expr_ = match_expr;
    if (OB_FAIL(query_info.field_exprs_.push_back(col_expr))) {
    } else if (OB_FAIL(query_info.keyword_exprs_.push_back(query_expr))) {
    } else {
      query_info.query_text_ = query_text;
    }
  }

  return ret;
}

int ObESQueryParser::parse_term(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  ObString col_name;
  ObIJsonBase *col_para = NULL;
  ObReqOpExpr *eq_expr = NULL;
  ObReqExpr *key_expr = NULL;
  ObReqConstExpr *value_expr = NULL;
  query_info.query_item_ = QUERY_ITEM_TERM;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("term expr should be object", K(ret));
  } else if (req_node.element_count() != 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("term expr should have exactly one element", K(ret));
  } else if (OB_FAIL(req_node.get_object_value(0, col_name, col_para))) {
  } else if (OB_FAIL(create_column_or_base_expr(col_name, key_expr))) {
  } else if (col_para->json_type() == ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("term field should have exactly one element", K(ret));
  } else if (col_para->json_type() != ObJsonNodeType::J_OBJECT) {
    if (OB_FAIL(parse_const(*col_para, value_expr))) {
    } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(eq_expr, alloc_, T_OP_EQ, key_expr, value_expr))) {
    } else {
      query_info.score_expr_ = eq_expr;
      query_info.condition_expr_ = eq_expr;
    }
  } else /*if (col_para->json_type() == ObJsonNodeType::J_OBJECT)*/ {
    for (uint64_t i = 0; OB_SUCC(ret) && i < col_para->element_count(); i++) {
      ObIJsonBase *value_node = NULL;
      ObString key;
      if (OB_FAIL(col_para->get_object_value(i, key, value_node))) {
      } else if (key.case_compare("value") == 0) {
        if (OB_FAIL(parse_const(*value_node, value_expr))) {
        } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(eq_expr, alloc_, T_OP_EQ, key_expr, value_expr))) {
        }
      } else if (key.case_compare("boost") == 0) {
        if (OB_FAIL(parse_boost(*value_node, query_info.boost_expr_))) {
        }
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("unsupported key in term expr", K(ret), K(key));
      }
    }
    if (OB_SUCC(ret) && OB_ISNULL(eq_expr)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("term expr should have value", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(query_info.condition_expr_ = eq_expr)) {
  } else if (!query_info.need_cal_score_) {
  } else if (OB_FAIL(construct_expr_with_boost(eq_expr, query_info.boost_expr_, query_info.score_expr_))) {
  }
  return ret;
}

int ObESQueryParser::parse_terms(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  uint64_t count = 0;
  ObReqExpr *key_expr = NULL;
  ObReqConstExpr *value_expr = NULL;
  ObReqOpExpr *in_expr = NULL;
  common::ObSEArray<ObReqConstExpr*, 4, common::ModulePageAllocator, true> value_exprs;
  bool has_field = false;
  query_info.query_item_ = QUERY_ITEM_TERMS;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("terms expr should be object", K(ret));
  } else if (FALSE_IT(count = req_node.element_count())) {
  } else if (count == 0 || count > 2) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("terms expr only supports field and boost", K(ret));
  }

  for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    ObIJsonBase *value_node = NULL;
    ObString key;
    if (OB_FAIL(req_node.get_object_value(i, key, value_node))) {
    } else if (key.case_compare("boost") == 0) {
      if (OB_FAIL(parse_boost(*value_node, query_info.boost_expr_))) {
      }
    } else {
      if (has_field) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("terms expr only supports one field", K(ret));
      } else if (value_node->json_type() == ObJsonNodeType::J_OBJECT) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("It's not supported to use object as field value", K(ret), K(key));
      } else if (value_node->json_type() != ObJsonNodeType::J_ARRAY) {
        ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
        LOG_WARN("unexpectd value type, should be array", K(ret), K(value_node->json_type()));
      } else if (key.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("field should not be empty string", K(ret));
      } else if (FALSE_IT(has_field = true)) {
      } else if (OB_FAIL(create_column_or_base_expr(key, key_expr))) {
      } else if (OB_FAIL(parse_keyword_array(*value_node, value_exprs))) {
      } else if (OB_FAIL(ObReqOpExpr::construct_in_expr(alloc_, key_expr, value_exprs, in_expr))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (!has_field) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("terms expr should have field", K(ret));
  } else if (FALSE_IT(query_info.condition_expr_ = in_expr)) {
  } else if (!query_info.need_cal_score_) {
  } else if (OB_FAIL(construct_expr_with_boost(in_expr, query_info.boost_expr_, query_info.score_expr_))) {
  }
  return ret;
}

int ObESQueryParser::parse_query_string_type(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *type_node = nullptr;
  ObString type_key("type");
  if (OB_FAIL(req_node.get_object_value(type_key, type_node))) {
    if (ret == OB_SEARCH_NOT_FOUND) {
    } else {
      LOG_WARN("fail to get type field", K(ret));
    }
  } else if (OB_ISNULL(type_node) || type_node->json_type() == ObJsonNodeType::J_NULL) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("type field is null", K(ret));
  } else if (type_node->json_type() == ObJsonNodeType::J_STRING) {
    ObString type_str(type_node->get_data_length(), type_node->get_data());
    if (type_str.case_compare("best_fields") == 0) {
      query_info.score_type_ = SCORE_TYPE_BEST_FIELDS;
    } else if (type_str.case_compare("cross_fields") == 0) {
      query_info.score_type_ = SCORE_TYPE_CROSS_FIELDS;
    } else if (type_str.case_compare("most_fields") == 0) {
      query_info.score_type_ = SCORE_TYPE_MOST_FIELDS;
    } else if (type_str.case_compare("phrase") == 0) {
      query_info.score_type_ = SCORE_TYPE_PHRASE;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unsupported query_string type", K(type_str));
    }
  } else {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("type field should be string", K(ret), K(type_node->json_type()));
  }
  return ret;
}

int ObESQueryParser::parse_query_string_fields(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *fields_node = nullptr;
  ObString fields_key("fields");
  if (OB_FAIL(req_node.get_object_value(fields_key, fields_node))) {
  } else if (OB_ISNULL(fields_node) || fields_node->json_type() == ObJsonNodeType::J_NULL) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("fields field is null", K(ret));
  } else if (fields_node->json_type() == ObJsonNodeType::J_ARRAY) {
    if (fields_node->element_count() == 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("fields should not be empty", K(ret));
    }
    for (uint64_t i = 0; OB_SUCC(ret) && i < fields_node->element_count(); i++) {
      ObIJsonBase *field_node = nullptr;
      ObReqColumnExpr *field = nullptr;
      if (OB_FAIL(fields_node->get_array_element(i, field_node))) {
      } else if (OB_FAIL(parse_field(*field_node, field))) {
      } else if (OB_FAIL(query_info.field_exprs_.push_back(field))) {
      }
    }
  } else if (fields_node->json_type() == ObJsonNodeType::J_STRING) {
    ObReqColumnExpr *field = nullptr;
    if (OB_FAIL(parse_field(*fields_node, field))) {
    } else if (OB_FAIL(query_info.field_exprs_.push_back(field))) {
    }
  } else {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("fields should be string or array", K(ret), K(fields_node->json_type()));
  }
  return ret;
}

int ObESQueryParser::parse_query_string_operator(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *operator_node = nullptr;
  ObString operator_key(query_info.query_item_ == QUERY_ITEM_MULTI_MATCH ? "operator" : "default_operator");
  if (OB_FAIL(req_node.get_object_value(operator_key, operator_node))) {
    if (ret == OB_SEARCH_NOT_FOUND) {
    } else {
      LOG_WARN("fail to get default_operator field", K(ret));
    }
  } else if (OB_ISNULL(operator_node) || operator_node->json_type() == ObJsonNodeType::J_NULL) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("default_operator field is null", K(ret));
  } else if (operator_node->json_type() == ObJsonNodeType::J_STRING) {
    ObString operator_str(operator_node->get_data_length(), operator_node->get_data());
    if (operator_str.case_compare("AND") == 0) {
      query_info.opr_ = T_OP_AND;
    } else if (operator_str.case_compare("OR") == 0) {
      query_info.opr_ = T_OP_OR;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unsupported default_operator value", K(operator_str));
    }
  } else {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("default_operator field should be string", K(ret), K(operator_node->json_type()));
  }
  return ret;
}

int ObESQueryParser::parse_query_string_query(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *query_node = nullptr;
  ObString query_key("query");
  ObString query_text;
  if (OB_FAIL(req_node.get_object_value(query_key, query_node))) {
  } else if (OB_ISNULL(query_node) || query_node->json_type() == ObJsonNodeType::J_NULL) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("query field is null", K(ret));
  } else if (query_node->json_type() != ObJsonNodeType::J_STRING) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("query should be string", K(ret), K(query_node->json_type()));
  } else if (OB_FALSE_IT(query_text.assign_ptr(query_node->get_data(), query_node->get_data_length()))) {
  } else if (query_text.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("query should not be empty", K(ret));
  } else if (OB_FAIL(parse_keyword(query_text, query_info))) {
  }
  return ret;
}

int ObESQueryParser::parse_query_string_boost(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *boost_node = nullptr;
  ObString boost_key("boost");
  if (OB_FAIL(req_node.get_object_value(boost_key, boost_node))) {
    if (ret == OB_SEARCH_NOT_FOUND) {
    } else {
      LOG_WARN("fail to get boost field", K(ret));
    }
  } else if (OB_ISNULL(boost_node) || boost_node->json_type() == ObJsonNodeType::J_NULL) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("boost field is null", K(ret));
  } else if (!boost_node->is_json_number(boost_node->json_type()) &&
             boost_node->json_type() != ObJsonNodeType::J_STRING) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("boost field should be number or string", K(ret), K(boost_node->json_type()));
  } else if (OB_FAIL(parse_boost(*boost_node, query_info.boost_expr_))) {
  }
  return ret;
}

int ObESQueryParser::parse_multi_match(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  query_info.query_item_ = QUERY_ITEM_MULTI_MATCH;
  return parse_query_string(req_node, query_info);
}

int ObESQueryParser::parse_query_string(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  uint32_t count = 0;
  if (query_info.query_item_ == QUERY_ITEM_UNKNOWN) {
    query_info.query_item_ = QUERY_ITEM_QUERY_STRING;
  }
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (OB_FALSE_IT(count = req_node.element_count())) {
  } else if (count == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("query_string should have at least one element", K(ret));
  } else {
    uint32_t parsed_keys = 0;
    if (OB_SUCC(parse_query_string_type(req_node, query_info))) {
      parsed_keys++;
    } else if (ret == OB_SEARCH_NOT_FOUND) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to parse query_string type", K(ret));
    }

    if (OB_SUCC(ret)) {
      if (OB_SUCC(parse_query_string_operator(req_node, query_info))) {
        parsed_keys++;
      } else if (ret == OB_SEARCH_NOT_FOUND) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to parse query_string operator", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_SUCC(parse_query_string_fields(req_node, query_info))) {
        parsed_keys++;
      } else {
        LOG_WARN("fail to parse query_string fields", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_SUCC(parse_query_string_query(req_node, query_info))) {
        parsed_keys++;
      } else {
        LOG_WARN("fail to parse query_string query", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_SUCC(parse_minimum_should_match(req_node, query_info))) {
        parsed_keys++;
      } else if (ret == OB_SEARCH_NOT_FOUND) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to parse minimum_should_match", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_SUCC(parse_query_string_boost(req_node, query_info))) {
        parsed_keys++;
      } else if (ret == OB_SEARCH_NOT_FOUND) {
        ret = OB_SUCCESS;
      } else {
      LOG_WARN("fail to parse query_string boost", K(ret));
      }
    }

    if (OB_SUCC(ret) && OB_FAIL(parse_query_string_by_type(query_info))) {
      LOG_WARN("fail to parse query_string by type", K(ret));
    }

    if (OB_SUCC(ret) && parsed_keys != count) {
      for (uint32_t i = 0; OB_SUCC(ret) && i < count; i++) {
        ObString key;
        if (OB_FAIL(req_node.get_key(i, key))) {
        } else if (key.case_compare("type") != 0 &&
                   key.case_compare("fields") != 0 &&
                   key.case_compare("query") != 0 &&
                   key.case_compare(query_info.query_item_ == QUERY_ITEM_MULTI_MATCH ? "operator" : "default_operator") != 0 &&
                   key.case_compare("minimum_should_match") != 0 &&
                   key.case_compare("boost") != 0) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("unsupported key in query_string", K(ret), K(key));
        }
      }
    }
  }
  return ret;
}

int ObESQueryParser::parse_field(ObIJsonBase &val_node, ObReqColumnExpr *&field)
{
  int ret = OB_SUCCESS;
  ObString field_str;
  if (val_node.json_type() != ObJsonNodeType::J_STRING) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(val_node.json_type()));
  } else if (OB_FALSE_IT(field_str = ObString(val_node.get_data_length(), val_node.get_data()))) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("field name is null", K(ret));
  } else {
    char *pure_field_str = static_cast<char *>(alloc_.alloc(field_str.length() + 1));
    int64_t str_len = 0;
    if (OB_ISNULL(pure_field_str)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to create field(s) expr", K(ret));
    } else {
      for (int64_t i = 0; i < field_str.length(); i++) {
        if (field_str.ptr()[i] != ' ') {
          pure_field_str[str_len++] = field_str.ptr()[i];
        }
      }
      pure_field_str[str_len] = '\0';
    }
    if (OB_SUCC(ret)) {
      ObString expr_name;
      double weight = -1.0;
      const char *caret_ptr = strchr(pure_field_str, '^');
      if (caret_ptr != nullptr && caret_ptr > pure_field_str) {
        int64_t field_len = caret_ptr - pure_field_str;
        expr_name = ObString(field_len, pure_field_str);
        const char *weight_start = caret_ptr + 1;
        if (*weight_start != '\0') {
          char *end_ptr = nullptr;
          weight = strtod(weight_start, &end_ptr);
          if (end_ptr <= weight_start || weight < 0) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid field weight", K(weight));
          }
        }
      } else {
        expr_name = ObString(str_len, pure_field_str);
      }
      if (OB_SUCC(ret) && OB_FAIL(ObReqColumnExpr::construct_column_expr(field, alloc_, expr_name, weight))) {
        LOG_WARN("fail to create field(s) expr", K(ret));
      }
    }
  }
  return ret;
}

int ObESQueryParser::parse_keyword(const ObString &query_text, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  const char *end = nullptr;
  const char *current = nullptr;
  char *query_str = static_cast<char *>(alloc_.alloc(query_text.length() + 1));
  if (OB_ISNULL(query_str)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate memory for query copy", K(ret));
  } else {
    MEMCPY(query_str, query_text.ptr(), query_text.length());
    query_str[query_text.length()] = '\0';
    end = query_str + query_text.length();
    current = query_str;
  }

  common::ObSEArray<ObReqConstExpr *, 4, common::ModulePageAllocator, true> raw_keywords;
  if (OB_FAIL(ret)) {
  } else if (query_info.query_item_ == QUERY_ITEM_QUERY_STRING) {
    if (OB_FAIL(parse_keyword_query_string(query_info, current, end, raw_keywords))) {
    }
  } else if (query_info.query_item_ == QUERY_ITEM_MULTI_MATCH) {
    if (OB_FAIL(parse_keyword_multi_match(query_info, current, end, raw_keywords))) {
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unsupported item type", K(ret), K(query_info.query_item_));
  }

  if (OB_FAIL(ret)) {
  } else if (raw_keywords.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("query text is empty", K(ret));
  } else {
    query_info.msm_info_.term_cnt_ = query_info.keyword_exprs_.count();
    query_info.query_text_ = query_text;
    query_info.tkn_cnt_ = raw_keywords.count();
  }
  return ret;
}

int ObESQueryParser::parse_keyword_multi_match(ObEsQueryInfo &query_info,
                                               const char *&current, const char *end,
                                               common::ObIArray<ObReqConstExpr *> &raw_keywords)
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && current < end) {
    while (current < end && (isspace(*current) || ispunct(*current))) {
      current++;
    }
    if (current >= end) {
      break;
    }
    const char *keyword_start = current;
    while (current < end && !isspace(*current) && !ispunct(*current)) {
      current++;
    }
    if (current > keyword_start) {
      int64_t keyword_len = current - keyword_start;
      ObString keyword_str(keyword_len, keyword_start);
      ObReqConstExpr *keyword = nullptr;
      if (OB_FAIL(ObReqConstExpr::construct_const_expr(keyword, alloc_, keyword_str, ObVarcharType))) {
      } else if (OB_FAIL(raw_keywords.push_back(keyword))) {
      }
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < raw_keywords.count(); i++) {
    if (OB_FAIL(query_info.keyword_exprs_.push_back(raw_keywords.at(i)))) {
    }
  }
  return ret;
}

int ObESQueryParser::parse_keyword_query_string(ObEsQueryInfo &query_info,
                                                const char *&current, const char *end,
                                                common::ObIArray<ObReqConstExpr *> &raw_keywords)
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && current < end) {
    while (current < end && *current == ' ') {
      current++;
    }
    if (current >= end) {
      break;
    }
    if (*current == '^') {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid keyword in query", K(ret));
      break;
    }
    const char *keyword_start = current;
    while (current < end && *current != ' ' && *current != '^') {
      current++;
    }
    if (current > keyword_start) {
      int64_t keyword_len = current - keyword_start;
      ObString keyword_str(keyword_len, keyword_start);
      ObReqConstExpr *keyword = nullptr;
      if (OB_FAIL(ObReqConstExpr::construct_const_expr(keyword, alloc_, keyword_str, ObVarcharType))) {
      } else {
        while (current < end && *current == ' ') {
          current++;
        }
        if (current < end && *current == '^') {
          current++;
          if (current >= end || !isdigit(*current)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("weight must follow ^ immediately", K((current < end) ? *current : 'E'));
          } else {
            const char *weight_start = current;
            char *end_ptr = nullptr;
            double weight = strtod(current, &end_ptr);
            if (end_ptr > current && weight >= 0) {
              keyword->weight_ = weight;
              current = end_ptr;
            } else {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("invalid keyword weight", K(weight));
            }
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(raw_keywords.push_back(keyword))) {
          LOG_WARN("fail to add raw keyword", K(ret));
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (query_info.score_type_ == SCORE_TYPE_PHRASE ||
             (query_info.score_type_ != SCORE_TYPE_CROSS_FIELDS && query_info.opr_ != T_OP_AND)) {
    common::ObSEArray<ObReqConstExpr *, 4, common::ModulePageAllocator, true> current_phrase_keywords;
    for (int64_t i = 0; OB_SUCC(ret) && i < raw_keywords.count(); i++) {
      ObReqConstExpr *current_keyword = raw_keywords.at(i);
      if (current_keyword->weight_ != -1.0) {
        if (OB_FAIL(process_phrase_keywords(current_phrase_keywords, query_info))) {
        } else if (OB_FAIL(query_info.keyword_exprs_.push_back(current_keyword))) {
        }
      } else if (OB_FAIL(current_phrase_keywords.push_back(current_keyword))) {
      } else if (i == raw_keywords.count() - 1 && OB_FAIL(process_phrase_keywords(current_phrase_keywords, query_info))) {
        LOG_WARN("fail to process phrase keywords", K(ret));
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < raw_keywords.count(); i++) {
      if (OB_FAIL(query_info.keyword_exprs_.push_back(raw_keywords.at(i)))) {
      }
    }
  }
  return ret;
}

int ObESQueryParser::parse_keyword_array(ObIJsonBase &val_node, common::ObIArray<ObReqConstExpr *> &value_items)
{
  int ret = OB_SUCCESS;
  uint64_t count = 0;
  if (val_node.json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(val_node.json_type()));
  } else if (FALSE_IT(count = val_node.element_count())) {
  } else if (count == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("keyword array should have at least one element", K(ret));
  } else {
    for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      ObIJsonBase *value_node = NULL;
      ObReqConstExpr *value_expr = NULL;
      if (OB_FAIL(val_node.get_array_element(i, value_node))) {
      } else if (OB_FAIL(parse_const(*value_node, value_expr))) {
      } else if (OB_FAIL(value_items.push_back(value_expr))) {
      }
    }
  }
  return ret;
}

int ObESQueryParser::parse_boost(ObIJsonBase &req_node, ObReqConstExpr *&boost_expr)
{
  int ret = OB_SUCCESS;
  ObReqConstExpr *tmp_boost_expr = nullptr;
  if (OB_FAIL(parse_const(req_node, tmp_boost_expr, true))) {
  } else if (tmp_boost_expr->get_numeric_value() < 0.0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("boost value must be greater than 0", K(ret));
  } else {
    boost_expr = tmp_boost_expr;
  }
  return ret;
}

//TODO: remove cover_value_to_str
int ObESQueryParser::parse_const(ObIJsonBase &val_node, ObReqConstExpr *&var, const bool accept_numeric_string/*= false*/, const bool cover_value_to_str/*= false*/)
{
  int ret = OB_SUCCESS;
  ObJsonBuffer j_buffer(&alloc_);
  if (!cover_value_to_str &&
    (val_node.json_type() == ObJsonNodeType::J_ARRAY || val_node.json_type() == ObJsonNodeType::J_OBJECT)) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(val_node.json_type()));
  } else if (OB_FAIL(val_node.print(j_buffer, false))) {
  } else {
    ObString expr_name;
    j_buffer.get_result_string(expr_name);
    bool is_numeric_value = val_node.is_json_number(val_node.json_type());
    if (accept_numeric_string || is_numeric_value) {
      if (accept_numeric_string) {
        expr_name = expr_name.trim();
      }
      int64_t str_len = expr_name.length();
      char *temp_str = static_cast<char *>(alloc_.alloc(str_len + 1));
      if (OB_ISNULL(temp_str)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate memory for temp string", K(ret));
      } else {
        MEMCPY(temp_str, expr_name.ptr(), str_len);
        temp_str[str_len] = '\0';
        char *end_ptr = nullptr;
        double num_value = strtod(temp_str, &end_ptr);
        if (end_ptr == temp_str || end_ptr != temp_str + str_len) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid numeric string", K(expr_name));
        } else if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(var, alloc_, num_value, ObNumberType))) {
        }
      }
    } else if (OB_FAIL(ObReqConstExpr::construct_const_expr(var, alloc_, expr_name, ObVarcharType))) {
    }
  }
  return ret;
}

int ObESQueryParser::construct_order_by_item(ObReqExpr *order_expr, bool ascent, OrderInfo *&order_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(order_info = OB_NEWx(OrderInfo, &alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create order info", K(ret));
  } else {
    order_info->order_item = order_expr;
    order_info->ascent = ascent;
  }
  return ret;
}

int ObESQueryParser::construct_required_params(const char *params_name[], uint32_t name_len, RequiredParamsSet &required_params)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(required_params.create(32))) {
  } else {
    for (int64_t idx = 0; OB_SUCC(ret) && idx < name_len; ++idx) {
      ObString para_name(strlen(params_name[idx]), params_name[idx]);
      if (OB_FAIL(required_params.set_refactored(para_name))) {
      }
    }
  }
  return ret;
}

int ObESQueryParser::parse_knn(ObIJsonBase &req_node, ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;
  RequiredParamsSet required_params;
  const char *params_name[] = {"field", "k", "query_vector"};
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else if (OB_ISNULL(query_req = OB_NEWx(ObQueryReqFromJson, &alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else if (OB_FAIL(construct_required_params(params_name, 3, required_params))) {
  }
  ObReqColumnExpr *vec_field = NULL;
  ObReqConstExpr *query_vec = NULL;
  ObReqConstExpr *K = NULL;
  ObReqConstExpr *boost = NULL;
  ObReqConstExpr *similar = NULL;
  ObReqExpr *dist_vec = NULL;
  OrderInfo *order_info = NULL;
  ObReqExpr *filter_expr = NULL;
  ObEsQueryInfo *query_info = nullptr;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> score_array;
  for (uint64_t i = 0; OB_SUCC(ret) && i < req_node.element_count(); i++) {
    ObString key;
    ObIJsonBase *sub_node = NULL;
    if (OB_FAIL(req_node.get_key(i, key))) {
    } else if (OB_FAIL(req_node.get_object_value(i, sub_node))) {
    } else if (key.case_compare("field") == 0) {
      if (OB_FAIL(parse_field(*sub_node, vec_field))) {
      } else if (OB_FAIL(required_params.erase_refactored("field"))) {
      }
    } else if (key.case_compare("k") == 0) {
      if (OB_FAIL(parse_const(*sub_node, K, true))) {
      } else if (OB_FAIL(required_params.erase_refactored("k"))) {
      }
    } else if (key.case_compare("query_vector") == 0) {
      if (OB_FAIL(parse_const(*sub_node, query_vec, false, true))) {
      } else if (OB_FAIL(required_params.erase_refactored("query_vector"))) {
      }
    } else if (key.case_compare("boost") == 0) {
      if (OB_FAIL(parse_boost(*sub_node, boost))) {
      }
    } else if (key.case_compare("similarity") == 0 ) {
      if (OB_FAIL(parse_const(*sub_node, similar, true))) {
      }
    } else if (key.case_compare("num_candidates") == 0) {
      // do nothing, ignore
    } else if (key.case_compare("filter") == 0) {
      if (OB_FAIL(ObEsQueryInfo::init_query_info(query_info, alloc_, query_req, nullptr, QUERY_ITEM_UNKNOWN))) {
      } else if (OB_FAIL(get_query_depth(*sub_node, query_info->total_depth_))) {
      } else if (OB_FALSE_IT(query_info->query_item_ = QUERY_ITEM_KNN)) {
      } else if (OB_FAIL(parse_filter_clauses(*sub_node, *query_info, filter_expr))) {
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not supported sytnax in query", K(ret), K(key));
    }
  }
  // construct normalize expr
  ObReqExpr *normalize_expr = NULL;
  ObReqOpExpr *div_expr = NULL;
  ObReqCaseWhenExpr *case_when_expr = NULL;
  ObReqOpExpr *add_expr = NULL;
  ObReqConstExpr *norm_const = NULL;
  ObReqConstExpr *round_const = NULL;
  ObReqOpExpr *boost_expr = NULL;
  ObReqExpr *order_by_4ip = NULL;
  ObVectorIndexDistAlgorithm alg_type = ObVectorIndexDistAlgorithm::VIDA_L2;
  if (OB_FAIL(ret)) {
  } else if (!required_params.empty()) {
    ret = OB_ERR_PARSER_SYNTAX;
    ObString param_name = required_params.begin()->first;
    LOG_WARN("query required params is missed", K(ret), K(param_name));
  } else if (filter_expr != NULL && OB_FAIL(query_req->condition_items_.push_back(filter_expr))) {
    LOG_WARN("fail to push query item", K(ret));
  } else if (OB_FAIL(parse_basic_table(table_name_, query_req))) {
  } else if (OB_FAIL(get_distance_algor_type(*vec_field, alg_type))) {
  } else if (alg_type == ObVectorIndexDistAlgorithm::VIDA_IP) {
    if (OB_FAIL(construct_ip_expr(vec_field, query_vec, case_when_expr, add_expr, order_by_4ip))) {
    } else if (OB_FAIL(construct_order_by_item(order_by_4ip, true, order_info))) {
    } else if (OB_FAIL(query_req->select_items_.push_back(add_expr))) {
    }
  } else {
    if (OB_FAIL(set_distance_score_expr(alg_type, norm_const, dist_vec, add_expr, div_expr))) {
    } else if (OB_FAIL(dist_vec->params.push_back(vec_field))) {
    } else if (OB_FAIL(dist_vec->params.push_back(query_vec))) {
    } else if (OB_FAIL(construct_order_by_item(dist_vec, true, order_info))) {
    } else if (OB_FAIL(query_req->select_items_.push_back(dist_vec))) {
    }
  }

  if (OB_SUCC(ret)) {
    query_req->set_vec_approx();
    query_req->set_limit(K);
    ObReqExpr *score = (alg_type == ObVectorIndexDistAlgorithm::VIDA_IP) ?
      static_cast<ObReqExpr *>(case_when_expr) : static_cast<ObReqExpr *>(div_expr);
    ObReqExpr *score_expr = nullptr;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(round_const, alloc_, 8.0, ObIntType))) {
    } else if (OB_FAIL(ObReqExpr::construct_expr(normalize_expr, alloc_, "round", score, round_const))) {
    } else if (OB_FAIL(query_req->order_items_.push_back(order_info))) {
    } else if (OB_FAIL(construct_expr_with_boost(normalize_expr, boost, score_expr))) {
    } else if (OB_FAIL(query_req->add_score_item(alloc_, score_expr))) {
    } else if (similar != NULL) {
      ObReqExpr *dist = (alg_type == ObVectorIndexDistAlgorithm::VIDA_IP) ? add_expr : dist_vec;
      if (OB_FAIL(construct_query_with_similarity(alg_type, dist, similar, query_req))) {
      }
    }
  }

  if (OB_SUCC(ret) && OB_NOT_NULL(query_info) &&
      OB_FAIL(construct_sub_query_with_minimum_should_match(query_req, *query_info, "_vs_sub"))) {
    LOG_WARN("fail to construct sub query with minimum should match", K(ret));
  }

  return ret;
}

// distance : 0 - negative_inner_product(vec_field, query_vec)
// score : case distance < 0 then 1 / (1 - 1 * distance)  else 1 + distance end
// equals
// score : case when negative_inner_product(vec_field, query_vec) > 0 then 1 / (1 + negative_inner_product) else 1 - negative_inner_product end
int ObESQueryParser::construct_ip_expr(ObReqColumnExpr *vec_field, ObReqConstExpr *query_vec, ObReqCaseWhenExpr *&case_when/* score */,
                                       ObReqOpExpr *&minus_expr/* distance */, ObReqExpr *&order_by_vec)
{
  int ret = OB_SUCCESS;
  ObReqOpExpr *negative_expr = NULL;
  ObReqOpExpr *negative_score_expr = NULL;
  ObReqConstExpr *one_const = NULL;
  ObReqConstExpr *zero_const = NULL;
  ObReqOpExpr *cmp_expr = NULL;
  ObReqOpExpr *add_expr = NULL;
  if (OB_FAIL(ObReqExpr::construct_expr(order_by_vec, alloc_, N_VECTOR_NEGATIVE_INNER_PRODUCT, vec_field, query_vec))) {
  } else if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(one_const, alloc_, 1.0, ObIntType))) {
  } else if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(zero_const, alloc_, 0.0, ObIntType))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(add_expr, alloc_, T_OP_ADD, one_const, order_by_vec))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(minus_expr, alloc_, T_OP_MINUS, zero_const, order_by_vec, "_distance"))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(cmp_expr, alloc_, T_OP_GT, order_by_vec, zero_const))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(negative_score_expr, alloc_, T_OP_DIV, one_const, add_expr))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(negative_expr, alloc_, T_OP_MINUS, one_const, order_by_vec))) {
  } else if (OB_FAIL(ObReqCaseWhenExpr::construct_case_when_expr(case_when, alloc_, cmp_expr, negative_score_expr, negative_expr))) {
  }
  return ret;
}

int ObESQueryParser::set_fts_limit_expr(ObQueryReqFromJson *query, const ObReqConstExpr *size_expr, const ObReqConstExpr *from_expr)
{
  // add limit for fts query
  int ret = OB_SUCCESS;
  const int64_t FTS_LIMIT_FACTOR = 20;
  int64_t size_val = 0;
  int64_t from_val = 0;
  char *buf = NULL;
  if (fusion_config_.size != NULL) {
    if (OB_FAIL(convert_const_numeric(fusion_config_.size->expr_name, size_val))) {
    }
  } else if (OB_ISNULL(size_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null ptr", K(ret));
  } else if (OB_FAIL(convert_const_numeric(size_expr->expr_name, size_val))) {
  }
  if (OB_FAIL(ret)) {
  } else if (from_expr != NULL && OB_FAIL(convert_const_numeric(from_expr->expr_name, from_val))) {
    LOG_WARN("fail to convert from expr", K(ret));
  } else if (OB_ISNULL(buf = reinterpret_cast<char*>(alloc_.alloc(ObFastFormatInt::MAX_DIGITS10_STR_SIZE)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate memory", K(ret));
  } else {
    int64_t limit_val = (size_val + from_val) * FTS_LIMIT_FACTOR;
    ObReqConstExpr *fts_limit_expr = nullptr;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(fts_limit_expr, alloc_, limit_val, ObIntType))) {
    } else {
      query->set_limit(fts_limit_expr);
    }
  }
  return ret;
}

int ObESQueryParser::get_distance_algor_type(const ObReqColumnExpr &vec_field, ObVectorIndexDistAlgorithm &alg_type)
{
  int ret = OB_SUCCESS;
  ObColumnIndexInfo *index_info = nullptr;
  if (!index_name_map_.created()) {
    // do nothing
  } else if (OB_FAIL(index_name_map_.get_refactored(vec_field.expr_name, index_info))) {
    LOG_WARN("fail to get vector index info", K(ret), K(vec_field.expr_name));
    if (ret == OB_HASH_NOT_EXIST) {
      ret = OB_SUCCESS;
    }
  } else if (OB_ISNULL(index_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpectd null ptr", K(ret), K(vec_field.expr_name));
  } else {
    alg_type = index_info->dist_algorithm_;
  }
  return ret;
}

int ObESQueryParser::get_match_idx_name(const ObString &match_field, ObString &idx_name)
{
  int ret = OB_SUCCESS;
  ObColumnIndexInfo *index_info = nullptr;
  if (!index_name_map_.created()) {
    // do nothing
  } else if (OB_FAIL(index_name_map_.get_refactored(match_field, index_info))) {
    LOG_WARN("fail to get vector index info", K(ret), K(match_field));
    if (ret == OB_HASH_NOT_EXIST) {
      ret = OB_SUCCESS;
    }
  } else if (OB_ISNULL(index_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpectd null ptr", K(ret), K(match_field));
  } else {
    idx_name = index_info->index_name_;
  }
  return ret;
}

int ObESQueryParser::set_distance_score_expr(const ObVectorIndexDistAlgorithm alg_type, ObReqConstExpr *&norm_const, ObReqExpr *&dist_vec,
                                             ObReqOpExpr *&add_expr, ObReqOpExpr *&score_expr)
{
  int ret = OB_SUCCESS;
  switch (alg_type) {
    case ObVectorIndexDistAlgorithm::VIDA_L2 : {
      // l2_distance : score = 1 / (1 + l2_distance)
      if (OB_FAIL(ObReqExpr::construct_expr(dist_vec, alloc_, N_VECTOR_L2_DISTANCE, "_distance"))) {
      } else if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(norm_const, alloc_, 1.0, ObIntType))) {
      } else {
        if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(add_expr, alloc_, T_OP_ADD, norm_const, dist_vec))) {
        } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(score_expr, alloc_, T_OP_DIV, norm_const, add_expr))) {
        }
      }
      break;
    }
    case ObVectorIndexDistAlgorithm::VIDA_COS : {
      if (OB_FAIL(ObReqExpr::construct_expr(dist_vec, alloc_, N_VECTOR_COS_DISTANCE, "_distance"))) {
      } else if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(norm_const, alloc_, 2.0, ObIntType))) {
      } else {
        ObReqOpExpr *minus_expr = NULL;
        ObReqConstExpr *const_minus = NULL;
        if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(const_minus, alloc_, 1.0, ObIntType))) {
        } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(score_expr, alloc_, T_OP_DIV, dist_vec, norm_const))) {
        } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(minus_expr, alloc_, T_OP_MINUS, const_minus, score_expr))) {
        } else {
          // cos_distance : score = 1 - (cos_distance / 2)
          score_expr = minus_expr;
        }
      }
      break;
    }
    default : {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpectd dist algorithm type", K(ret), K(alg_type));
    }
  }
  return ret;
}

int ObESQueryParser::construct_score_sum_expr(ObReqExpr *fts_score, ObReqExpr *vs_score, const ObString &score_alias, ObReqOpExpr *&score)
{
  int ret = OB_SUCCESS;
  ObReqExpr *if_null_fts = NULL;
  ObReqExpr *if_null_vs = NULL;
  ObReqConstExpr *zero_const = NULL;
  if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(zero_const, alloc_, 0.0, ObIntType))) {
  } else if (OB_FAIL(ObReqExpr::construct_expr(if_null_fts, alloc_, N_IFNULL, fts_score, zero_const))) {
  } else if (OB_FAIL(ObReqExpr::construct_expr(if_null_vs, alloc_, N_IFNULL, vs_score, zero_const))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(score, alloc_, T_OP_ADD, if_null_fts, if_null_vs, score_alias))) {
  }
  return ret;
}

int ObESQueryParser::construct_sub_query_table(const ObString &sub_query_name, ObQueryReqFromJson *query_req, ObReqTable *&sub_query)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sub_query = OB_NEWx(ObReqTable, &alloc_, SUB_QUERY, sub_query_name, database_name_, query_req))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else {
    sub_query->alias_name_ = sub_query_name;
  }
  return ret;
}

int ObESQueryParser::wrap_sub_query(const ObString &sub_query_name, ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;
  ObQueryReqFromJson *wrap_query = NULL;
  ObReqTable *sub_query = NULL;
  if (OB_ISNULL(wrap_query = OB_NEWx(ObQueryReqFromJson, &alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else if (OB_FAIL(construct_sub_query_table(sub_query_name, query_req, sub_query))) {
  } else if (OB_FAIL(wrap_query->from_items_.push_back(sub_query))) {
  } else {
    query_req = wrap_query;
  }

  return ret;
}

int ObESQueryParser::construct_query_with_similarity(ObVectorIndexDistAlgorithm algor, ObReqExpr *dist, ObReqConstExpr *similar, ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;
  ObReqOpExpr *cmp_expr = NULL;
  ObReqColumnExpr *col = NULL;
  ObString sub_query_name("_vs0");
  ObItemType op_type = (algor == ObVectorIndexDistAlgorithm::VIDA_IP) ? T_OP_GE : T_OP_LE;
  ObString col_name = dist->alias_name.empty() ? dist->expr_name : dist->alias_name;
  if (OB_FAIL(wrap_sub_query(sub_query_name, query_req))) {
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(col, alloc_, col_name, sub_query_name))) {
  } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(cmp_expr, alloc_, op_type, col, similar))) {
  } else if (OB_FAIL(query_req->condition_items_.push_back(cmp_expr))) {
  } else {
    col->table_name = sub_query_name;
    col->expr_name = dist->alias_name.empty() ? dist->expr_name : dist->alias_name;
  }
  return ret;
}

int ObESQueryParser::construct_sub_query_with_minimum_should_match(ObQueryReqFromJson *&query_req, ObEsQueryInfo &query_info, const ObString &sub_query_name)
{
  int ret = OB_SUCCESS;
  if (!query_info.need_construct_sub_query_with_minimum_should_match()) {
  } else {
    ObReqOpExpr *score_expr = nullptr;
    ObReqOpExpr *condition_expr = nullptr;
    ObQueryReqFromJson *base_query_req = query_info.query_req_;
    for (uint64_t i = 0; OB_SUCC(ret) && i < base_query_req->inner_score_items_.count(); i++) {
      if (OB_FAIL(base_query_req->select_items_.push_back(base_query_req->inner_score_items_.at(i)))) {
      }
    }
    // if query_req is the same as base_query_req, then wrap the sub query,
    // otherwise, query_req is already a sub query
    if (OB_FAIL(ret)) {
    } else if (query_req == base_query_req && OB_FAIL(wrap_sub_query(sub_query_name, query_req))) {
      LOG_WARN("fail to wrap sub query", K(ret));
    } else if (OB_FALSE_IT(query_info.query_req_ = query_req)) {
    } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(condition_expr, alloc_, T_OP_AND, base_query_req->outer_condition_items_))) {
    } else if (OB_FAIL(query_req->condition_items_.push_back(condition_expr))) {
    } else if (!base_query_req->outer_score_items_.empty()) {
      if (OB_FAIL(ObReqOpExpr::construct_op_expr(score_expr, alloc_, T_OP_ADD, base_query_req->outer_score_items_))) {
      } else if (OB_FALSE_IT(score_expr->alias_name = SCORE_NAME)) {
      } else if (OB_FAIL(query_req->score_items_.push_back(score_expr))) {
      }
    } 
  }
  return ret;
}

int ObESQueryParser::construct_minimum_should_match_info(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("unexpectd json type", K(ret), K(req_node.json_type()));
  } else {
    for (uint64_t i = 0; OB_SUCC(ret) && i < req_node.element_count(); i++) {
      ObString key;
      ObIJsonBase *sub_node = NULL;
      if (OB_FAIL(req_node.get_key(i, key))) {
      } else if (OB_FAIL(req_node.get_object_value(i, sub_node))) {
      } else if (key.case_compare("must") == 0) {
        query_info.must_cnt_ = 0;
      } else if (key.case_compare("must_not") == 0) {
        query_info.must_not_cnt_ = 0;
      } else if (key.case_compare("filter") == 0) {
        query_info.filter_cnt_ = 0;
      } else if (key.case_compare("should") == 0) {
        query_info.should_cnt_ = 0;
        if (sub_node->json_type() == ObJsonNodeType::J_ARRAY) {
          query_info.msm_info_.term_cnt_ = sub_node->element_count();
        } else if (sub_node->json_type() == ObJsonNodeType::J_OBJECT) {
          query_info.msm_info_.term_cnt_ = 1;
        } else {
          ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
          LOG_WARN("should should be array or object", K(ret), K(sub_node->json_type()));
        }
      } else if (key.case_compare("boost") == 0) {
        if (OB_FAIL(parse_boost(*sub_node, query_info.boost_expr_))) {
        }
      } else if (key.case_compare("minimum_should_match") != 0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("unsupported key in bool query", K(ret), K(key));
      }
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(parse_minimum_should_match(req_node, query_info))) {
    LOG_WARN("fail to parse minimum should match", K(ret));
  }
  return ret;
}

int ObESQueryParser::parse_minimum_should_match_by_value(const common::ObString &val_str, const int64_t term_cnt, uint64_t &msm_val)
{
  int ret = OB_SUCCESS;
  int64_t val = 0;
  ObString num_part = val_str.trim();
  uint32_t num_part_len = num_part.length();
  bool is_percentage = false;

  if (num_part.length() > 0 && num_part.ptr()[num_part.length() - 1] == '%') {
    num_part.assign_ptr(num_part.ptr(), static_cast<int32_t>(num_part.length() - 1));
    is_percentage = true;
  }

  if (!num_part.is_numeric()) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("string value is empty or not numeric", K(ret), K(num_part));
  } else if (OB_FAIL(convert_signed_const_numeric(num_part, val))) {
  } else if (is_percentage) {
    val = (term_cnt * val) / 100;
  }

  if (OB_SUCC(ret)) {
    int64_t final_val = (val < 0) ? max(0, term_cnt + val) : val;
    msm_val = static_cast<uint64_t>(final_val);
  }

  return ret;
}

int ObESQueryParser::parse_minimum_should_match(ObIJsonBase &req_node, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  uint64_t raw_msm_val = -1;
  ObIJsonBase *msm_node = nullptr;
  ObReqConstExpr *raw_msm_expr = nullptr;
  MinimumShouldMatchInfo &msm_info = query_info.msm_info_;
  if (req_node.json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    LOG_WARN("minimum_should_match should be object", K(ret), K(req_node.json_type()));
  } else if (query_info.opr_ == T_OP_AND) {
  } else if (OB_FAIL(req_node.get_object_value(MSM_KEY, msm_node))) {
    if (ret == OB_SEARCH_NOT_FOUND) {
      raw_msm_val = 0;
      if (query_info.query_item_ == QUERY_ITEM_BOOL) {
        ret = OB_SUCCESS;
      }
    } else {
      LOG_WARN("fail to get minimum should match node", K(ret));
    }
  } else if (OB_FAIL(parse_const(*msm_node, raw_msm_expr))) {
  } else if (OB_FAIL(parse_minimum_should_match_by_value(raw_msm_expr->expr_name, msm_info.term_cnt_, raw_msm_val))) {
  }

  if ((OB_SUCC(ret) || ret == OB_SEARCH_NOT_FOUND) && raw_msm_val != -1) {
    raw_msm_val = (raw_msm_val != 0) ? raw_msm_val : 1;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(msm_info.msm_expr_, alloc_, raw_msm_val, ObIntType))) {
    } else {
      query_info.set_msm_apply_type();
    }
  }
  return ret;
}

int ObESQueryParser::add_score_col(const ObString &table_name, ObQueryReqFromJson &query_req)
{
  int ret = OB_SUCCESS;
  ObReqColumnExpr *score_col = NULL;
  if (OB_FAIL(ObReqColumnExpr::construct_column_expr(score_col, alloc_, SCORE_NAME, table_name))) {
  } else if (OB_FAIL(query_req.add_score_item(alloc_, score_col))) {
  }
  return ret;
}

int ObESQueryParser::construct_condition_best_fields(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  if (query_info.msm_info_.apply_type_ == MSM_APPLY_NOT_SUB) {
    if (OB_FAIL(construct_should_group_expr(query_info))) {
    }
  } else {
    int64_t field_cnt = query_info.field_exprs_.count();
    int64_t keyword_cnt = query_info.keyword_exprs_.count();
    common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> tmp_condition_items;
    if (query_info.combine_keywords()) {
      ObReqConstExpr *combined_keywords = nullptr;
      if (OB_FAIL(concat_const_exprs(query_info.keyword_exprs_, ObString(" "), combined_keywords))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < field_cnt; i++) {
          ObReqMatchExpr *match_expr = nullptr;
          if (OB_FAIL(ObReqMatchExpr::construct_match_expr(match_expr, alloc_, query_info.field_exprs_.at(i), combined_keywords, SCORE_TYPE_BEST_FIELDS))) {
          } else if (OB_FAIL(tmp_condition_items.push_back(match_expr))) {
          }
        }
      }
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < field_cnt; i++) {
        ObReqOpExpr *keyword_expr = nullptr;
        common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> keyword_conditions;
        for (int64_t j = 0; OB_SUCC(ret) && j < keyword_cnt; j++) {
          if (OB_FAIL(keyword_conditions.push_back(query_info.match_exprs_matrix_.at(j).at(i)))) {
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(keyword_expr, alloc_, query_info.opr_, keyword_conditions))) {
        } else if (OB_FAIL(tmp_condition_items.push_back(keyword_expr))) {
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < tmp_condition_items.count(); i++) {
      if (OB_FAIL(query_info.condition_items_.push_back(tmp_condition_items.at(i)))) {
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(handle_msm_for_sub_condition(query_info))) {
    LOG_WARN("fail to handle sub condition with msm", K(ret));
  }
  return ret;
}

int ObESQueryParser::construct_condition_cross_fields(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  if (query_info.opr_ == T_OP_OR && OB_FAIL(construct_condition_best_fields(query_info))) {
      LOG_WARN("fail to construct condition for cross_fields + OR", K(ret));
  } else if (query_info.opr_ == T_OP_AND) {
    for (int64_t i = 0; OB_SUCC(ret) && i < query_info.keyword_exprs_.count(); i++) {
      common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> field_conditions;
      for (int64_t j = 0; OB_SUCC(ret) && j < query_info.field_exprs_.count(); j++) {
        if (OB_FAIL(field_conditions.push_back(query_info.match_exprs_matrix_.at(i).at(j)))) {
        }
      }
      if (OB_SUCC(ret)) {
        ObReqOpExpr *field_expr = nullptr;
        if (OB_FAIL(ObReqOpExpr::construct_op_expr(field_expr, alloc_, T_OP_OR, field_conditions))) {
        } else if (OB_FAIL(query_info.condition_items_.push_back(field_expr))) {
        }
      }
    }
  }
  return ret;
}

int ObESQueryParser::construct_condition_most_fields(ObEsQueryInfo &query_info)
{
  return construct_condition_best_fields(query_info);
}

int ObESQueryParser::construct_condition_phrase(ObEsQueryInfo &query_info)
{
  return construct_condition_best_fields(query_info);
}

int ObESQueryParser::construct_match_exprs_matrix(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < query_info.keyword_exprs_.count(); i++) {
    common::ObSEArray<ObReqMatchExpr *, 4, common::ModulePageAllocator, true> field_exprs;
    ObReqConstExpr *keyword_expr = query_info.keyword_exprs_.at(i);
    for (int64_t j = 0; OB_SUCC(ret) && j < query_info.field_exprs_.count(); j++) {
      ObReqMatchExpr *match_expr = nullptr;
      ObReqColumnExpr *field_expr = query_info.field_exprs_.at(j);
      ObEsScoreType score_type = (query_info.score_type_ != SCORE_TYPE_PHRASE || keyword_expr->weight_ != -1.0) ? SCORE_TYPE_BEST_FIELDS : SCORE_TYPE_PHRASE;
      if (OB_FAIL(ObReqMatchExpr::construct_match_expr(match_expr, alloc_, field_expr, keyword_expr, score_type))) {
      } else if (OB_FAIL(field_exprs.push_back(match_expr))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(query_info.match_exprs_matrix_.push_back(field_exprs))) {
      LOG_WARN("fail to add field exprs to matrix", K(ret));
    }
  }
  return ret;
}

int ObESQueryParser::construct_query_string_score(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> keyword_exprs;
  for (int64_t i = 0; OB_SUCC(ret) && i < query_info.keyword_exprs_.count(); i++) {
    ObReqExpr *combined_expr = nullptr;
    ObReqExpr *keyword_weighted_expr = nullptr;
    common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> field_weighted_exprs;
    for (int64_t j = 0; OB_SUCC(ret) && j < query_info.field_exprs_.count(); j++) {
      ObReqExpr *field_weighted_expr = nullptr;
      if (OB_FAIL(construct_weighted_expr(query_info.match_exprs_matrix_.at(i).at(j),
                                         query_info.field_exprs_.at(j)->weight_,
                                         field_weighted_expr))) {
      } else if (OB_FAIL(field_weighted_exprs.push_back(field_weighted_expr))) {
      }
    }

    if (OB_FAIL(ret)) {
    } else if (query_info.score_type_ == SCORE_TYPE_MOST_FIELDS) {
      // most_fields
      ObReqOpExpr *tmp_combined_expr = nullptr;
      if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_combined_expr, alloc_, T_OP_ADD, field_weighted_exprs))) {
      } else {
        combined_expr = tmp_combined_expr;
      }
    } else {
      // not most_fields, then it must be best_fields, cross_fields or phrase
      if (field_weighted_exprs.count() == 1) {
        combined_expr = field_weighted_exprs.at(0);
      } else if (OB_FAIL(ObReqExpr::construct_expr(combined_expr, alloc_, "GREATEST", field_weighted_exprs))) {
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(construct_weighted_expr(combined_expr, query_info.keyword_exprs_.at(i)->weight_, keyword_weighted_expr))) {
    } else if (OB_FAIL(keyword_exprs.push_back(keyword_weighted_expr))) {
    } else if (OB_FAIL(handle_msm_for_sub_score(query_info, query_info, keyword_weighted_expr))) {
    }
  }

  ObReqOpExpr *tmp_add_expr = nullptr;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_add_expr, alloc_, T_OP_ADD, keyword_exprs))) {
  } else if (OB_FALSE_IT(query_info.score_expr_ = tmp_add_expr)) {
  } else if (OB_FAIL(construct_expr_with_boost(query_info.score_expr_, query_info.boost_expr_, query_info.score_expr_))) {
  } else if (query_info.score_alias_items_.empty()) {
  } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_add_expr, alloc_, T_OP_ADD, query_info.score_alias_items_))) {
  } else if (OB_FALSE_IT(query_info.score_alias_expr_ = tmp_add_expr)) {
  } else if (OB_FAIL(construct_expr_with_boost(query_info.score_alias_expr_, query_info.boost_expr_, query_info.score_alias_expr_))) {
  } else if (query_info.get_upward_depth() == 0) {
    if (OB_FAIL(query_info.query_req_->outer_score_items_.push_back(query_info.score_alias_expr_))) {
    }
  } else if (query_info.need_cal_score_ && OB_FAIL(query_info.parent_query_info_->score_alias_items_.push_back(query_info.score_alias_expr_))) {
    LOG_WARN("fail to add score alias expr to score alias items", K(ret));
  }
  return ret;
}

int ObESQueryParser::construct_query_string_condition(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  if (query_info.opr_ != T_OP_AND && query_info.opr_ != T_OP_OR) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("operator between conditions must be AND or OR", K(ret), K(query_info.opr_));
  } else {
    switch (query_info.score_type_) {
      case SCORE_TYPE_BEST_FIELDS: {
        if (OB_FAIL(construct_condition_best_fields(query_info))) {
        }
        break;
      }
      case SCORE_TYPE_MOST_FIELDS: {
        if (OB_FAIL(construct_condition_most_fields(query_info))) {
        }
        break;
      }
      case SCORE_TYPE_CROSS_FIELDS: {
        if (OB_FAIL(construct_condition_cross_fields(query_info))) {
        }
        break;
      }
      case SCORE_TYPE_PHRASE: {
        if (OB_FAIL(construct_condition_phrase(query_info))) {
        }
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("unsupported score type", K(ret), K(query_info.score_type_));
        break;
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_NOT_NULL(query_info.msm_info_.condition_expr_)) {
      query_info.condition_expr_ = query_info.msm_info_.condition_expr_;
    } else {
      ObReqOpExpr *final_condition = nullptr;
      ObItemType condition_operator = (query_info.score_type_ == SCORE_TYPE_CROSS_FIELDS && query_info.opr_ == T_OP_AND) ? T_OP_AND : T_OP_OR;
      if (OB_FAIL(ObReqOpExpr::construct_op_expr(final_condition, alloc_, condition_operator, query_info.condition_items_))) {
      } else {
        query_info.condition_expr_ = final_condition;
      }
    }
  }
  return ret;
}

int ObESQueryParser::construct_should_group_expr(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  MinimumShouldMatchInfo &msm_info = query_info.msm_info_;
  if (OB_ISNULL(msm_info.msm_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("msm expr is null", K(ret));
  } else if (msm_info.get_msm_val() > query_info.keyword_exprs_.count()) {
    // to improve performance, avoid creating unnecessary conditions in the WHERE clause.
    ObReqConstExpr *zero_expr = nullptr;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(zero_expr, alloc_, 0.0, ObIntType))) {
    } else {
      msm_info.condition_expr_ = zero_expr;
    }
  } else {
    common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> or_group_exprs;
    if (msm_info.get_msm_val() == 1 && query_info.combine_keywords()) {
      ObReqConstExpr *combined_keywords = nullptr;
      if (OB_FAIL(concat_const_exprs(query_info.keyword_exprs_, ObString(" "), combined_keywords))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < query_info.field_exprs_.count(); i++) {
          ObReqMatchExpr *match_expr = nullptr;
          if (OB_FAIL(ObReqMatchExpr::construct_match_expr(match_expr, alloc_, query_info.field_exprs_.at(i), combined_keywords, SCORE_TYPE_BEST_FIELDS))) {
          } else if (OB_FAIL(or_group_exprs.push_back(match_expr))) {
          }
        }
      }
    } else {
      for (uint64_t i = 0; OB_SUCC(ret) && i < query_info.keyword_exprs_.count(); i++) {
        common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> match_exprs;
        for (uint64_t j = 0; OB_SUCC(ret) && j < query_info.field_exprs_.count(); j++) {
          if (OB_FAIL(match_exprs.push_back(query_info.match_exprs_matrix_.at(i).at(j)))) {
          }
        }
        if (OB_SUCC(ret)) {
          ObReqOpExpr *expr = nullptr;
          if (OB_FAIL(ObReqOpExpr::construct_op_expr(expr, alloc_, T_OP_OR, match_exprs))) {
          } else if (OB_FAIL(or_group_exprs.push_back(expr))) {
          }
        }
      }
    }
    ObReqExpr *should_condition = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(build_should_condition_combine(0, msm_info.get_msm_val(), or_group_exprs, nullptr, should_condition))) {
    } else {
      msm_info.condition_expr_ = should_condition;
    }
  }
  return ret;
}

int ObESQueryParser::get_base_table_query(ObQueryReqFromJson *query_req, ObQueryReqFromJson *&base_table_req, ReqTableType *table_type/*=nullptr*/)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(query_req)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(query_req));
  } else if (OB_NOT_NULL(base_table_req)) {
  } else {
    // for sub_query and multi_set, suppose all the sub queries have the same base table
    for (int64_t i = 0; OB_SUCC(ret) && i < query_req->from_items_.count(); i++) {
      ObReqTable *table = query_req->from_items_.at(i);
      if (OB_NOT_NULL(table_type) && *table_type == UNKNOWN_TABLE) {
        if (table->table_type_ == UNKNOWN_TABLE) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret), K(table->table_type_));
        } else {
          *table_type = table->table_type_;
        }
      }
      if (table->table_type_ == BASE_TABLE) {
        base_table_req = query_req;
        break;
      } else if (table->table_type_ == SUB_QUERY) {
        ObQueryReqFromJson *query = NULL;
        if (OB_ISNULL(query = dynamic_cast<ObQueryReqFromJson *>(table->ref_query_))) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret), K(table->ref_query_));
        } else if (OB_FAIL(get_base_table_query(query, base_table_req, table_type))) {
        } else if (OB_NOT_NULL(base_table_req)) {
          break;
        }
      } else if (table->table_type_ == MULTI_SET) {
        ObMultiSetTable *multi_set = nullptr;
        ObQueryReqFromJson *query = nullptr;
        if (OB_ISNULL(multi_set = dynamic_cast<ObMultiSetTable *>(table)) || multi_set->sub_queries_.empty()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret), K(table));
        } else if (OB_ISNULL(query = dynamic_cast<ObQueryReqFromJson *>(multi_set->sub_queries_.at(0)->ref_query_))) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret), K(multi_set->sub_queries_.at(0)));
        } else if (OB_FAIL(get_base_table_query(query, base_table_req, table_type))) {
        } else if (OB_NOT_NULL(base_table_req)) {
          break;
        }
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(ret), K(table->table_type_));
      }
    }
    if (OB_SUCC(ret) && OB_ISNULL(base_table_req)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("base table not found", K(ret));
    }
  }
  return ret;
}

int ObESQueryParser::parse_query_string_by_type(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  if (query_info.field_exprs_.empty() || query_info.keyword_exprs_.empty() || query_info.tkn_cnt_ == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(query_info.field_exprs_.count()), K(query_info.keyword_exprs_.count()), K(query_info.tkn_cnt_));
  } else if (OB_FAIL(construct_match_exprs_matrix(query_info))) {
  } else if (OB_FAIL(construct_query_string_score(query_info))) {
  } else if (OB_FAIL(construct_query_string_condition(query_info))) {
  }
  return ret;
}

// build a combination expression as a should condition
int ObESQueryParser::build_should_condition_combine(uint64_t start, uint64_t k, const common::ObIArray<ObReqExpr *> &items, common::ObIArray<ObReqExpr *> *work_array, ObReqExpr *&should_condition)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> temp_expr_array;
  common::ObIArray<ObReqExpr *> *expr_array = nullptr;
  expr_array = OB_NOT_NULL(work_array) ? work_array : &temp_expr_array;
  if (k == 0 || items.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (k > items.count()) {
    ObReqConstExpr *tmp_or_expr = nullptr;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(tmp_or_expr, alloc_, 0.0, ObIntType))) {
    } else {
      should_condition = tmp_or_expr;
    }
  } else if (k == items.count()) {
    ObReqOpExpr *tmp_or_expr = nullptr;
    if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_or_expr, alloc_, T_OP_AND, items))) {
    } else {
      should_condition = tmp_or_expr;
    }
  } else if (k == expr_array->count()) {
    ObReqOpExpr *and_expr = nullptr;
    if (OB_FAIL(ObReqOpExpr::construct_op_expr(and_expr, alloc_, T_OP_AND, *expr_array))) {
    } else if (OB_ISNULL(should_condition)) {
      common::ObSEArray<ObReqExpr*, 1, common::ModulePageAllocator, true> params;
      ObReqOpExpr *tmp_or_expr = nullptr;
      if (OB_FAIL(params.push_back(and_expr))) {
      } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(tmp_or_expr, alloc_, T_OP_OR, params))) {
      } else {
        should_condition = tmp_or_expr;
      }
    } else if (OB_FAIL(should_condition->params.push_back(and_expr))) {
    }
  } else {
    for (uint64_t i = start; OB_SUCC(ret) && i < items.count(); i++) {
      if (OB_FAIL(expr_array->push_back(items.at(i)))) {
      } else if (OB_FAIL(build_should_condition_combine(i + 1, k, items, expr_array, should_condition))) {
      } else {
        expr_array->pop_back();
      }
    }
  }
  return ret;
}

// build a comparison expression as a should condition
int ObESQueryParser::build_should_condition_compare(ObReqConstExpr *msm_expr, const common::ObIArray<ObReqExpr *> &items, ObReqExpr *&should_condition)
{
  int ret = OB_SUCCESS;
  uint64_t msm_val = msm_expr->get_numeric_value();
  if (msm_val == 0 || items.count() == 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(ret), K(msm_val), K(items.count()));
  } else if (msm_val > items.count()) {
    ObReqConstExpr *tmp_ge_expr = nullptr;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(tmp_ge_expr, alloc_, 0.0, ObIntType))) {
    } else {
      should_condition = tmp_ge_expr;
    }
  } else if (msm_val == items.count()) {
    ObReqOpExpr *and_expr = nullptr;
    if (OB_FAIL(ObReqOpExpr::construct_op_expr(and_expr, alloc_, T_OP_AND, items))) {
    } else {
      should_condition = and_expr;
    }
  } else {
    ObReqOpExpr *sum_expr = NULL;
    ObReqOpExpr *cmp_expr = NULL;
    ObReqConstExpr *zero_const = NULL;
    common::ObSEArray<ObReqExpr*, 4, common::ModulePageAllocator, true> params;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(zero_const, alloc_, 0.0, ObIntType))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < items.count(); i++) {
      ObReqExpr *group = items.at(i);
      ObReqOpExpr *gt_zero = NULL;
      if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(gt_zero, alloc_, T_OP_GT, group, zero_const))) {
      } else if (OB_FAIL(params.push_back(gt_zero))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(sum_expr, alloc_, T_OP_ADD, params))) {
    } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(cmp_expr, alloc_, T_OP_GE, sum_expr, msm_expr))) {
    } else {
      should_condition = cmp_expr;
    }
  }
  return ret;
}

int ObESQueryParser::handle_msm_for_sub_score(ObEsQueryInfo &query_info, ObEsQueryInfo &inner_query_info, ObReqExpr *score_expr)
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t pos = 0;
  ObString sub_score_alias;
  ObReqColumnExpr *sub_score_col = nullptr;
  ObQueryReqFromJson *query_req = query_info.query_req_;
  if (query_info.msm_info_.apply_type_ != MSM_APPLY_WITH_SUB &&
      (inner_query_info.total_depth_ > 2 ||
       (inner_query_info.outer_query_item_ != QUERY_ITEM_MUST &&
        inner_query_info.outer_query_item_ != QUERY_ITEM_SHOULD))) {
  } else if (query_info.total_depth_ == 2 && query_info.get_upward_depth() == 0 &&
             (inner_query_info.query_item_ == QUERY_ITEM_MULTI_MATCH || inner_query_info.query_item_ == QUERY_ITEM_QUERY_STRING)) {
  } else if (OB_ISNULL(score_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("score expr is null", K(ret));
  } else if (OB_ISNULL(buf = static_cast<char *>(alloc_.alloc(OB_MAX_COLUMN_NAME_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate memory for alias", K(ret));
  } else if (OB_FAIL(databuff_printf(buf, OB_MAX_COLUMN_NAME_LENGTH, pos, "%.*s%ld",
                     FTS_SUB_SCORE_PREFIX.length(), FTS_SUB_SCORE_PREFIX.ptr(), query_req->sub_score_item_seq_++))) {
  } else if (OB_FALSE_IT(sub_score_alias.assign_ptr(buf, pos))) {
  } else if (OB_FALSE_IT(score_expr->set_alias(sub_score_alias))) {
  } else if (OB_FAIL(query_req->inner_score_items_.push_back(score_expr))) {
  } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(sub_score_col, alloc_, sub_score_alias))) {
  } else if ((query_info.get_upward_depth() == 1 || inner_query_info.outer_query_item_ != QUERY_ITEM_MUST) &&
             OB_FAIL(query_info.msm_info_.msm_items_.push_back(sub_score_col))) {
    LOG_WARN("fail to push back sub score column expr to score alias items", K(ret));
  } else if (query_info.need_cal_score_ && OB_FAIL(query_info.score_alias_items_.push_back(sub_score_col))) {
    LOG_WARN("fail to push back sub score column expr", K(ret));
  }
  return ret;
}

int ObESQueryParser::handle_msm_for_sub_condition(ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  MinimumShouldMatchInfo &msm_info = query_info.msm_info_;
  if (msm_info.apply_type_ != MSM_APPLY_WITH_SUB &&
      !(msm_info.apply_type_ == MSM_APPLY_NOT_SUB &&
        query_info.get_upward_depth() == 1 &&
        query_info.outer_query_item_ == QUERY_ITEM_SHOULD &&
        query_info.parent_query_info_->msm_info_.apply_type_ == MSM_APPLY_WITH_SUB)) {
  } else if (msm_info.msm_items_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("msm items is empty", K(ret));
  } else {
    ObReqConstExpr *zero_expr = nullptr;
    ObReqOpExpr *add_expr = nullptr;
    ObReqOpExpr *cmp_expr = nullptr;
    common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> add_items;
    if (OB_FAIL(ObReqConstExpr::construct_const_numeric_expr(zero_expr, alloc_, 0.0, ObIntType))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < msm_info.msm_items_.count(); i++) {
      ObReqOpExpr *gt_expr = nullptr;
      if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(gt_expr, alloc_, T_OP_GT, msm_info.msm_items_.at(i), zero_expr))) {
      } else if (OB_FAIL(add_items.push_back(gt_expr))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObReqOpExpr::construct_op_expr(add_expr, alloc_, T_OP_ADD, add_items))) {
    } else if (OB_FAIL(ObReqOpExpr::construct_binary_op_expr(cmp_expr, alloc_, T_OP_GE, add_expr, msm_info.msm_expr_))) {
    } else {
      bool push_to_outer = (query_info.get_upward_depth() == 0) ||
                           (query_info.outer_query_item_ != QUERY_ITEM_SHOULD) ||
                           (query_info.parent_query_info_->msm_info_.apply_type_ != MSM_APPLY_WITH_SUB);
      if (push_to_outer) {
        if (query_info.outer_query_item_ == QUERY_ITEM_MUST_NOT) {
          cmp_expr->set_op_type(T_OP_LT);
        }
        if (OB_FAIL(query_info.query_req_->outer_condition_items_.push_back(cmp_expr))) {
        }
      } else if (OB_FAIL(query_info.parent_query_info_->msm_info_.msm_items_.push_back(cmp_expr))) {
      }
    }
  }
  return ret;
}

int ObESQueryParser::construct_all_query(ObQueryReqFromJson *&query_req)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(query_req = OB_NEWx(ObQueryReqFromJson, &alloc_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query request", K(ret));
  } else if (OB_FAIL(parse_basic_table(table_name_, query_req))) {
  } else if (OB_FAIL(set_default_score(query_req, 1.0))) {
  }
  return ret;
}

int ObESQueryParser::process_phrase_keywords(common::ObIArray<ObReqConstExpr *> &phrase_keywords, ObEsQueryInfo &query_info)
{
  int ret = OB_SUCCESS;
  if (!phrase_keywords.empty()) {
    if (phrase_keywords.count() == 1) {
      if (OB_FAIL(query_info.keyword_exprs_.push_back(phrase_keywords.at(0)))) {
      }
    } else {
      ObReqConstExpr *combined_keywords = nullptr;
      if (OB_FAIL(concat_const_exprs(phrase_keywords, ObString(" "), combined_keywords))) {
      } else if (OB_FAIL(query_info.keyword_exprs_.push_back(combined_keywords))) {
      }
    }
    phrase_keywords.reset();
  }
  return ret;
}

int ObESQueryParser::get_query_depth(ObIJsonBase &req_node, uint64_t &depth)
{
  int ret = OB_SUCCESS;
  int current_depth = depth;
  if (req_node.json_type() == ObJsonNodeType::J_ARRAY) {
    uint64_t count = req_node.element_count();
    for (uint64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      ObIJsonBase *elem = NULL;
      if (OB_FAIL(req_node.get_array_element(i, elem))) {
      } else if (elem != NULL && elem->json_type() == ObJsonNodeType::J_OBJECT) {
        ObString key;
        ObIJsonBase *sub_node = NULL;
        if (elem->element_count() == 0) {
          // skip empty object
        } else if (OB_FAIL(elem->get_key(0, key))) {
        } else if (OB_FAIL(elem->get_object_value(0, sub_node))) {
        } else if (check_is_bool_key(key)) {
          uint64_t sub_depth = (key.case_compare("bool") == 0) ? current_depth + 1 : current_depth;
          if (OB_FAIL(get_query_depth(*sub_node, sub_depth))) {
          } else {
            depth = max(depth, sub_depth);
          }
        }
      } else {
        // non-object elements do not affect basic query check
      }
    }
  } else if (req_node.json_type() == ObJsonNodeType::J_OBJECT) {
    for (uint64_t i = 0; OB_SUCC(ret) && i < req_node.element_count(); i++) {
      ObString key;
      ObIJsonBase *sub_node = nullptr;
      if (OB_FAIL(req_node.get_key(i, key))) {
      } else if (OB_FAIL(req_node.get_object_value(i, sub_node))) {
      } else if (check_is_bool_key(key)) {
        uint64_t sub_depth = (key.case_compare("bool") == 0) ? current_depth + 1 : current_depth;
        if (OB_FAIL(get_query_depth(*sub_node, sub_depth))) {
        } else {
          depth = max(depth, sub_depth);
        }
      }
    }
  }
  return ret;
}

int ObESQueryParser::concat_const_exprs(const common::ObIArray<ObReqConstExpr *> &array, const ObString &connect_str, ObReqConstExpr *&result)
{
  int ret = OB_SUCCESS;
  if (array.count() > 0) {
    int64_t total_len = 0;
    for (int64_t i = 0; i < array.count(); i++) {
      total_len += array.at(i)->expr_name.length();
    }
    total_len += (array.count() - 1) * connect_str.length();
    char *buf = static_cast<char *>(alloc_.alloc(total_len + 1));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate memory for concat result", K(ret));
    } else {
      int64_t pos = 0;
      for (int64_t i = 0; i < array.count(); i++) {
        if (i > 0) {
          MEMCPY(buf + pos, connect_str.ptr(), connect_str.length());
          pos += connect_str.length();
        }
        MEMCPY(buf + pos, array.at(i)->expr_name.ptr(), array.at(i)->expr_name.length());
        pos += array.at(i)->expr_name.length();
      }
      if (OB_FAIL(ObReqConstExpr::construct_const_expr(result, alloc_, ObString(total_len, buf), ObVarcharType))) {
      }
    }
  }
  return ret;
}

int ObESQueryParser::construct_partition_cols(const ObIArray<ObString> &column_names)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < column_names.count(); i++) {
    const ObString &col_name = column_names.at(i);
    ObReqColumnExpr *part_col = nullptr;
    ObReqColumnExpr *part_col_alias = nullptr;
    char *alias_buf = nullptr;
    const int64_t alias_buf_len = OB_MAX_COLUMN_NAME_LENGTH;
    int64_t alias_pos = 0;
    ObString alias_str;
    if (OB_ISNULL(alias_buf = static_cast<char *>(alloc_.alloc(alias_buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for partition expr alias", K(ret));
    } else if (OB_FAIL(databuff_printf(alias_buf, alias_buf_len, alias_pos, "%.*s%ld",
                                       PART_COL_ALIAS_PREFIX.length(), PART_COL_ALIAS_PREFIX.ptr(), i))) {
    } else if (OB_FALSE_IT(alias_str.assign_ptr(alias_buf, alias_pos))) {
    } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(part_col_alias, alloc_, alias_str))) {
    } else if (OB_FAIL(part_aliases_.push_back(part_col_alias))) {
    } else if (OB_FAIL(ObReqColumnExpr::construct_column_expr(part_col, alloc_, col_name))) {
    } else if (OB_FALSE_IT(part_col->set_alias(alias_str))) {
    } else if (OB_FAIL(part_cols_.push_back(part_col))) {
    }
  }
  return ret;
}

bool ObESQueryParser::check_is_column_name(const ObString &key)
{
  bool is_column_name = false;
  for (int64_t i = 0; !is_column_name && i < user_cols_.count(); i++) {
    if (user_cols_.at(i).case_compare(key) == 0) {
      is_column_name = true;
    }
  }
  LOG_INFO("hnwyllmm check_is_column_name", K(key), K(is_column_name));
  return is_column_name;
}

int ObESQueryParser::create_column_or_base_expr(const ObString &key, ObReqExpr *&expr)
{
  int ret = OB_SUCCESS;
  if (check_is_column_name(key)) {
    ObReqColumnExpr *col_expr = nullptr;
    if (OB_FAIL(ObReqColumnExpr::construct_column_expr(col_expr, alloc_, key))) {
    } else {
      expr = col_expr;
    }
  } else {
    if (OB_FAIL(ObReqExpr::construct_expr(expr, alloc_, key))) {
    }
  }
  return ret;
}

void ObEsQueryInfo::set_msm_apply_type()
{
  uint64_t msm_val = msm_info_.get_msm_val();
  bool apply_msm = true;
  if (opr_ != T_OP_OR || msm_val < 1) {
    apply_msm = false;
  } else if (msm_val > keyword_exprs_.count()) {
    if (tkn_cnt_ == 1) {
      apply_msm = false;
    } else if (score_type_ == SCORE_TYPE_CROSS_FIELDS || score_type_ == SCORE_TYPE_PHRASE) {
      apply_msm = keyword_exprs_.count() > 1;
    } else if (keyword_exprs_.count() == 1 && (tkn_cnt_ == 1 || field_exprs_.count() > 1)) {
      apply_msm = false;
    }
  }
  if (apply_msm) {
    if (total_depth_ > 2 || msm_val == msm_info_.term_cnt_ || msm_val == 1) {
      msm_info_.apply_type_ = MSM_APPLY_NOT_SUB;
    } else {
      msm_info_.apply_type_ = MSM_APPLY_WITH_SUB;
    }
  } else {
    msm_info_.apply_type_ = MSM_NOT_APPLY;
  }
}

bool ObEsQueryInfo::need_construct_sub_query_with_minimum_should_match() const
{
  ObEsQueryInfo *top_query_info = get_top_query_info();
  return !top_query_info->is_es_mode() && top_query_info->total_depth_ <= 2 && !top_query_info->query_req_->outer_condition_items_.empty();
}

bool ObEsQueryInfo::support_es_mode()
{
  bool support = false;
  if (outer_query_item_ == QUERY_ITEM_QUERY &&
      (query_item_ == QUERY_ITEM_MATCH || query_item_ == QUERY_ITEM_MULTI_MATCH|| query_item_ == QUERY_ITEM_QUERY_STRING)) {
    if (opr_ == T_OP_OR && (OB_ISNULL(boost_expr_) || boost_expr_->get_numeric_value() != 0.0) && (score_type_ == SCORE_TYPE_BEST_FIELDS || score_type_ == SCORE_TYPE_MOST_FIELDS)) {
      support = true;
    }
  } else if (query_item_ == QUERY_ITEM_BOOL) {
    //TODO: some cases need to be supported
  }
  set_es_mode_(support);
  return support;
}

int ObEsQueryInfo::init_query_info(ObEsQueryInfo *&query_info, ObIAllocator &alloc,
                                   ObQueryReqFromJson *query_req, ObEsQueryInfo *parent_query_info, ObEsQueryItem outer_query_item, bool need_cal_score/*=false*/)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(query_info = OB_NEWx(ObEsQueryInfo, &alloc, query_req, parent_query_info, outer_query_item, need_cal_score))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create query info", K(ret));
  }
  return ret;
}

uint64_t ObEsQueryInfo::get_upward_depth() const
{
  uint64_t depth = 0;
  const ObEsQueryInfo *current = parent_query_info_;
  while (current != nullptr) {
    depth++;
    current = current->parent_query_info_;
  }
  return depth;
}

uint64_t ObEsQueryInfo::get_total_depth() const
{
  return get_top_query_info()->total_depth_;
}


}  // namespace share
}  // namespace oceanbase
