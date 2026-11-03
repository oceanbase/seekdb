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

#define USING_LOG_PREFIX SQL_DAS
#include "query/das/ob_fts_eval_node_access.h"
#include "sql/engine/ob_exec_context.h"
#include "ob_das_text_retrieval_eval_node.h"
#include "rust/sql_parser.h"

namespace oceanbase
{
namespace sql
{
int ObFtsEvalNode::print_node(ObFtsEvalNode *node, const ObArray<ObString> &tokens, int level)
{
  int ret = OB_SUCCESS;  
  if (!node) {
  } else if (node->leaf_node_) {
    LOG_WARN("fts compute leaf node:", K(level), K(node->postion_), K(tokens.at(node->postion_)));
  } else {
    for (int i = 0; i < node->child_nodes_.count(); i++) {
      LOG_WARN("fts compute children node:", K(level), K(node->child_flags_[i]));
      print_node(node->child_nodes_[i], tokens, level + 1);
    }
  }
  return ret;
}

int ObFtsEvalNode::fts_boolean_eval(ObFtsEvalNode *node, const common::ObIArray<double> &relevences, double &result)
{
  int ret = OB_SUCCESS;
  if (node->leaf_node_) {
    if (OB_UNLIKELY(relevences.count() <= node->postion_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      result = relevences.at(node->postion_);
    }
  } else {
    result = 0;
    int32_t size = node->child_nodes_.count();
    bool maybe = false;
    bool must = true;
    bool must_not = false;
    for (size_t i = 0; OB_SUCC(ret) && i < size; i++) {
      double relevence = 0;
      if (OB_FAIL(fts_boolean_eval(node->child_nodes_[i], relevences, relevence))) {
      } else {
        switch (node->child_flags_[i])
        {
        case AND:
          must = relevence > 0 ? must : false;
          result = relevence <= 0 ? result : result + relevence;
          break;
        case NOT:
          must_not = relevence > 0 ? true : must_not;
          break;
        case OR:
          maybe = relevence <= 0 ? maybe : true;
          result = relevence <= 0 ? result : result + relevence;
          break;
        case NO_OPERATOR:
          result = relevence <= 0 ? result : result + relevence;
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected fts compute flag", K(node->child_flags_[i]));
          break;
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (maybe) {
        // do nothing
      } else if (must_not) {
        result = -1;
      } else if (must) {
        // do nothing
      } else {
        result = -1;
      }
    }
  }
  return ret;
}

int ObFtsEvalNode::fts_boolean_node_create(
    ObFtsEvalNode *&parent_node,
    const peg::SearchNode &search_node,
    const ObCollationType &cs_type,
    ObIAllocator &allocator,
    ObArray<ObString> &tokens,
    hash::ObHashMap<ObString, int32_t> &tokens_map,
    bool &has_duplicate_tokens)
{
  int ret = OB_SUCCESS;
  ObFtsEvalNode *node = nullptr;
  void *buf = nullptr;
  if (OB_ISNULL(buf = allocator.alloc(sizeof(ObFtsEvalNode)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    node = new(buf) ObFtsEvalNode();
    node->parant_node_ = nullptr;
    if (search_node.is_term()) {
      ObString token_string;
      const auto text = search_node.text();
      const ObString term(text.size(), text.data());
      if (OB_FAIL(common::ObCharset::charset_convert(
          allocator, term, ObCollationType::CS_TYPE_UTF8MB4_GENERAL_CI, cs_type, token_string,
          common::ObCharset::CONVERT_FLAG::COPY_STRING_ON_SAME_CHARSET))) {
      } else {
        int32_t token_idx = 0;
        const int32_t map_size = tokens_map.size();
        if (OB_FAIL(tokens_map.get_refactored(token_string, token_idx))) {
          if (OB_HASH_NOT_EXIST != ret) {
          } else if (OB_FAIL(tokens_map.set_refactored(token_string, map_size, 1/*overwrite*/))) {
          } else if (OB_FAIL(tokens.push_back(token_string))) {
          } else {
            token_idx = map_size;
            ret = OB_SUCCESS;
          }
        } else {
          has_duplicate_tokens = true;
        }
        node->postion_ = token_idx;
        node->leaf_node_ = true;
      }
    } else {
      for (const auto &child : search_node.children()) {
        if (OB_FAIL(ret)) {
          break;
        }
        FtsComputeFlag flag = NO_OPERATOR;
        switch (child.occurrence()) {
          case peg::SearchOccurrence::Required: flag = AND; break;
          case peg::SearchOccurrence::Excluded: flag = NOT; break;
          case peg::SearchOccurrence::Alternative: flag = OR; break;
          case peg::SearchOccurrence::Optional: break;
          default:
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected search occurrence");
            break;
        }
        ObFtsEvalNode *child_node = nullptr;
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(fts_boolean_node_create(
            child_node, child, cs_type, allocator, tokens, tokens_map, has_duplicate_tokens))) {
        } else if (OB_FAIL(node->child_flags_.push_back(flag))) {
        } else if (OB_FAIL(node->child_nodes_.push_back(child_node))) {
        }
        if (OB_FAIL(ret) && OB_NOT_NULL(child_node)) {
          child_node->release();
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    parent_node = node;
  } else if (OB_NOT_NULL(node)) {
    node->release();
  }
  return ret;
}

} // namespace sql

namespace query
{

void release_fts_eval_node(sql::ObFtsEvalNode *node)
{
  if (OB_NOT_NULL(node)) {
    node->release();
  }
}

int evaluate_fts_boolean(
    sql::ObFtsEvalNode *node,
    const common::ObIArray<double> &relevances,
    double &result)
{
  return OB_ISNULL(node)
      ? common::OB_INVALID_ARGUMENT
      : sql::ObFtsEvalNode::fts_boolean_eval(node, relevances, result);
}

} // namespace query
} // namespace oceanbase
