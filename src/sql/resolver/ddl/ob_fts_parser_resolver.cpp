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

#include "storage/fts/ob_fts_literal.h"
#include "storage/fts/ob_fts_parser_property.h"
#define USING_LOG_PREFIX STORAGE_FTS

#include "sql/resolver/ddl/ob_fts_parser_resolver.h"

namespace oceanbase
{
namespace sql
{

static int qualify_fts_dictionary_table(common::ObIAllocator &allocator,
                                        const ObString &database_name,
                                        const ObString &input,
                                        ObString &output)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(input.find('.'))) {
    output = input;
  } else if (database_name.empty()) {
    ret = OB_ERR_NO_DB_SELECTED;
  } else {
    const int64_t length = database_name.length() + 1 + input.length();
    char *buffer = static_cast<char *>(allocator.alloc(length));
    if (OB_ISNULL(buffer)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(buffer, database_name.ptr(), database_name.length());
      buffer[database_name.length()] = '.';
      MEMCPY(buffer + database_name.length() + 1, input.ptr(), input.length());
      output.assign_ptr(buffer, static_cast<int32_t>(length));
    }
  }
  return ret;
}

int ObFTParserResolverHelper::resolve_parser_properties(
    const ParseNode &parse_tree,
    common::ObIAllocator &allocator,
    const common::ObString &database_name,
    common::ObString &parser_property)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(parse_tree.num_child_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument, parser properties is empty", K(ret), K(parse_tree.num_child_));
  } else {
    storage::ObFTParserJsonProps property;
    if (OB_FAIL(property.init())) {
      LOG_WARN("fail to init parser properties", K(ret));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
      if (OB_ISNULL(parse_tree.children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("option_node child is nullptr", K(ret));
      } else if (OB_FAIL(resolve_fts_index_parser_properties(parse_tree.children_[i], allocator,
                                                             database_name, property))) {
        LOG_WARN("fail to resolve fts index parser properties", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(property.to_format_json(allocator, parser_property))) {
      LOG_WARN("fail to serialize parser properties", K(ret), K(property));
    }
  }
  return ret;
}

int ObFTParserResolverHelper::resolve_fts_index_parser_properties(
    const ParseNode *node,
    common::ObIAllocator &allocator,
    const common::ObString &database_name,
    storage::ObFTParserJsonProps &property)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node) || node->num_child_ != 1 || OB_ISNULL(node->children_[0])){
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parse node", K(ret), KP(node));
  } else {
    switch (node->type_) {
      case T_PARSER_MIN_TOKEN_SIZE: {
        if (OB_ISNULL(node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("option_node child is null", K(node->children_[0]), K(ret));
        } else if (OB_UNLIKELY(!property.is_valid_min_token_size(node->children_[0]->value_))) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid min token size.",
                   K(ObString(ObFTSLiteral::MIN_TOKEN_SIZE_SCOPE_STR)),
                   K(ret),
                   K(node->children_[0]->value_));
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MIN_TOKEN_SIZE_SCOPE_STR);
        } else if (OB_FAIL(property.config_set_min_token_size(node->children_[0]->value_))) {
          LOG_WARN("fail to set min token size", K(ret));
        }
        break;
      }
      case T_PARSER_MAX_TOKEN_SIZE: {
        if (OB_ISNULL(node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("option_node child is null", K(node->children_[0]), K(ret));
        } else if (OB_UNLIKELY(!property.is_valid_max_token_size(node->children_[0]->value_))) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid  max_token_size",
                   K(ObString(ObFTSLiteral::MAX_TOKEN_SIZE_SCOPE_STR)),
                   K(ret),
                   K(node->children_[0]->value_));
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MAX_TOKEN_SIZE_SCOPE_STR);
        } else if (OB_FAIL(property.config_set_max_token_size(node->children_[0]->value_))) {
          LOG_WARN("fail to set max token size", K(ret));
        }
        break;
      }
      case T_PARSER_NGRAM_TOKEN_SIZE: {
        if (OB_ISNULL(node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("option_node child is null", K(node->children_[0]), K(ret));
        } else if (OB_UNLIKELY(!property.is_valid_ngram_token_size(node->children_[0]->value_))) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid ngram token size",
                   K(ObString(ObFTSLiteral::NGRAM_TOKEN_SIZE_SCOPE_STR)),
                   K(ret),
                   K(node->children_[0]->value_));
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::NGRAM_TOKEN_SIZE_SCOPE_STR);
        } else if OB_FAIL (property.config_set_ngram_token_size(node->children_[0]->value_)) {
          LOG_WARN("fail to set ngram token size", K(ret));
        }
        break;
      }
      case T_PARSER_STOPWORD_TABLE: {
        if (OB_ISNULL(node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("option_node child is nullptr", K(ret));
        } else if (OB_UNLIKELY(node->children_[0]->str_len_ <= 0)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret), K(node->children_[0]->str_len_));
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "the stopword table is empty");
        } else {
          int32_t str_len = static_cast<int32_t>(node->children_[0]->str_len_);
          ObString table_name(str_len, node->children_[0]->str_value_);
          ObString qualified_name;
          if (OB_FAIL(qualify_fts_dictionary_table(allocator, database_name, table_name,
                                                   qualified_name))) {
            LOG_WARN("fail to qualify stopword table", K(ret), K(table_name));
          } else if (OB_FAIL(property.config_set_stopword_table(qualified_name))) {
            LOG_WARN("fail to set stopword table", K(ret));
          }
        }
        break;
      }
      case T_PARSER_DICT_TABLE: {
        if (OB_ISNULL(node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("option_node child is nullptr", K(ret));
        } else if (OB_UNLIKELY(node->children_[0]->str_len_ <= 0)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret), K(node->children_[0]->str_len_));
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "the dict table is empty");
        } else {
          int32_t str_len = static_cast<int32_t>(node->children_[0]->str_len_);
          ObString table_name(str_len, node->children_[0]->str_value_);
          ObString qualified_name;
          if (OB_FAIL(qualify_fts_dictionary_table(allocator, database_name, table_name,
                                                   qualified_name))) {
            LOG_WARN("fail to qualify dict table", K(ret), K(table_name));
          } else if (OB_FAIL(property.config_set_dict_table(qualified_name))) {
            LOG_WARN("fail to set dict table", K(ret));
          }
        }
        break;
      }
      case T_PARSER_QUANTIFIER_TABLE: {
        if (OB_ISNULL(node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("option_node child is nullptr", K(ret));
        } else if (OB_UNLIKELY(node->children_[0]->str_len_ <= 0)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret), K(node->children_[0]->str_len_));
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "the quanitfier table is empty");
        } else {
          int32_t str_len = static_cast<int32_t>(node->children_[0]->str_len_);
          ObString table_name(str_len, node->children_[0]->str_value_);
          ObString qualified_name;
          if (OB_FAIL(qualify_fts_dictionary_table(allocator, database_name, table_name,
                                                   qualified_name))) {
            LOG_WARN("fail to qualify quantifier table", K(ret), K(table_name));
          } else if (OB_FAIL(property.config_set_quantifier_table(qualified_name))) {
            LOG_WARN("fail to set quantifier table", K(ret));
          }
        }
        break;
      }
      case T_IK_MODE: {
        if (OB_ISNULL(node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("option_node child is nullptr", K(ret));
        } else if (OB_UNLIKELY(node->children_[0]->str_len_ <= 0)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret), K(node->children_[0]->str_len_));
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "the mode str is empty");
        } else {
          ObString ik_mode_str(static_cast<int32_t>(node->children_[0]->str_len_),
                               (char *)(node->children_[0]->str_value_));
          if (0 == ik_mode_str.case_compare(ObFTSLiteral::FT_IK_MODE_MAX_WORD)) {
            if (OB_FAIL(property.config_set_ik_mode(ObFTSLiteral::FT_IK_MODE_MAX_WORD))) {
              LOG_WARN("fail to set use ik smart", K(ret));
            }
          } else if (0 == ik_mode_str.case_compare(ObFTSLiteral::FT_IK_MODE_SMART)) {
            if (OB_FAIL(property.config_set_ik_mode(ObFTSLiteral::FT_IK_MODE_SMART))) {
              LOG_WARN("fail to set use ik smart", K(ret));
            }
          } else {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid fts index parser properties option", K(ret), K(ik_mode_str));
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::IK_MODE_SCOPE_STR);
          }
        }
        break;
      }
      case T_PARSER_MIN_NGRAM_SIZE: {
        if (OB_ISNULL(node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("option_node child is nullptr", K(ret));
        } else if (OB_UNLIKELY(!property.is_valid_min_ngram_token_size(node->children_[0]->value_))) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MIN_NGRAM_SIZE_SCOPE_STR);
          LOG_WARN("invalid min ngram token size",
                   K(ObString(ObFTSLiteral::MIN_NGRAM_SIZE_SCOPE_STR)),
                   K(ret),
                   K(node->children_[0]->value_));
        } else if (OB_FAIL(property.config_set_min_ngram_token_size(node->children_[0]->value_))) {
          LOG_WARN("fail to set min ngram token size", K(ret));
        }
        break;
      }
      case T_PARSER_MAX_NGRAM_SIZE: {
        if (OB_ISNULL(node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("option_node child is nullptr", K(ret));
        } else if (OB_UNLIKELY(
                       !property.is_valid_max_ngram_token_size(node->children_[0]->value_))) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MAX_NGRAM_SIZE_SCOPE_STR);
          LOG_WARN("invalid max ngram token size",
                   K(ObString(ObFTSLiteral::MAX_NGRAM_SIZE_SCOPE_STR)),
                   K(ret),
                   K(node->children_[0]->value_));
        } else if (OB_FAIL(property.config_set_max_ngram_token_size(node->children_[0]->value_))) {
          LOG_WARN("fail to set max ngram token size", K(ret));
        }
        break;
      }

      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid fts index parser properties option", K(ret), K(node->type_));
      }
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
