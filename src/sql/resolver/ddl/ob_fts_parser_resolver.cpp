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
#include "sql/resolver/ob_schema_checker.h"
#include "share/schema/ob_table_schema.h"
#define USING_LOG_PREFIX STORAGE_FTS

#include "sql/resolver/ddl/ob_fts_parser_resolver.h"

namespace oceanbase
{
namespace sql
{

int ObFTParserResolverHelper::resolve_parser_properties(
    const common::ObString &index_database_name,
    const ParseNode &parse_tree,
    common::ObIAllocator &allocator,
    ObSchemaChecker *schema_checker,
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
      } else if (OB_FAIL(resolve_fts_index_parser_properties(index_database_name,
                                                             parse_tree.children_[i],
                                                             property,
                                                             allocator,
                                                             schema_checker))) {
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
    const common::ObString &index_database_name,
    const ParseNode *node,
    storage::ObFTParserJsonProps &property,
    common::ObIAllocator &allocator,
    ObSchemaChecker *schema_checker)
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
        if (OB_ISNULL(schema_checker)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(resolve_table_config(index_database_name, node, property,
                                                allocator, *schema_checker))) {
          LOG_WARN("fail to resolve stopword table", K(ret));
        }
        break;
      }
      case T_PARSER_DICT_TABLE: {
        if (OB_ISNULL(schema_checker)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(resolve_table_config(index_database_name, node, property,
                                                allocator, *schema_checker))) {
          LOG_WARN("fail to resolve dict table", K(ret));
        }
        break;
      }
      case T_PARSER_QUANTIFIER_TABLE: {
        if (OB_ISNULL(schema_checker)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(resolve_table_config(index_database_name, node, property,
                                                allocator, *schema_checker))) {
          LOG_WARN("fail to resolve quantifier table", K(ret));
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

int ObFTParserResolverHelper::resolve_table_config(
    const common::ObString &index_database_name,
    const ParseNode *node,
    storage::ObFTParserJsonProps &property,
    common::ObIAllocator &allocator,
    ObSchemaChecker &schema_checker)
{
  int ret = OB_SUCCESS;
  ObString database_name = index_database_name;
  ObString table_name;
  ObString raw_name;
  const share::schema::ObTableSchema *table_schema = nullptr;
  if (OB_ISNULL(node) || OB_ISNULL(node->children_[0])
      || node->children_[0]->str_len_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "dictionary table name is empty");
  } else {
    raw_name.assign_ptr(node->children_[0]->str_value_,
                        static_cast<int32_t>(node->children_[0]->str_len_));
    const char *dot = raw_name.find('.');
    if (OB_NOT_NULL(dot)) {
      database_name.assign_ptr(raw_name.ptr(), static_cast<int32_t>(dot - raw_name.ptr()));
      table_name.assign_ptr(dot + 1,
          static_cast<int32_t>(raw_name.length() - (dot - raw_name.ptr()) - 1));
    } else {
      table_name = raw_name;
    }
    if (database_name.empty()) {
      ret = OB_ERR_NO_DB_SELECTED;
      LOG_USER_ERROR(OB_ERR_NO_DB_SELECTED);
    } else if (table_name.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "dictionary table name is empty");
    } else if (OB_FAIL(schema_checker.get_table_schema(database_name, table_name,
                                                       false, table_schema))) {
      LOG_WARN("failed to get dictionary table schema", K(ret), K(database_name), K(table_name));
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNKNOWN_TABLE;
      LOG_USER_ERROR(OB_ERR_UNKNOWN_TABLE, table_name.length(), table_name.ptr(),
                     database_name.length(), database_name.ptr());
    } else if (!table_schema->is_fulltext_dict()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "using a non-fulltext-dictionary table as dictionary");
    } else {
      const int64_t length = database_name.length() + table_name.length() + 1;
      char *buf = static_cast<char *>(allocator.alloc(length + 1));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        int64_t pos = 0;
        if (OB_FAIL(databuff_printf(buf, length + 1, pos, "%.*s.%.*s",
                                   database_name.length(), database_name.ptr(),
                                   table_name.length(), table_name.ptr()))) {
          LOG_WARN("failed to format dictionary table name", K(ret));
        } else {
          ObString full_name(static_cast<int32_t>(pos), buf);
          switch (node->type_) {
            case T_PARSER_DICT_TABLE:
              ret = property.config_set_dict_table(full_name);
              break;
            case T_PARSER_STOPWORD_TABLE:
              ret = property.config_set_stopword_table(full_name);
              break;
            case T_PARSER_QUANTIFIER_TABLE:
              ret = property.config_set_quantifier_table(full_name);
              break;
            default:
              ret = OB_INVALID_ARGUMENT;
              break;
          }
        }
      }
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
