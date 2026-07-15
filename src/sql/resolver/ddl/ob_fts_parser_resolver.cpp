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
#include "sql/resolver/ob_schema_checker.h"

namespace oceanbase
{
namespace sql
{

int ObFTParserResolverHelper::resolve_parser_properties(
    const ParseNode &parse_tree,
    common::ObIAllocator &allocator,
    ObSchemaChecker &schema_checker,
    const common::ObString &current_database_name,
    common::ObString &parser_property)
{
  // 统一处理三个词典属性，防止 CREATE/ALTER 全文索引走出不同的校验语义。
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
      } else if (OB_FAIL(resolve_fts_index_parser_properties(parse_tree.children_[i], property,
                                                              schema_checker, current_database_name))) {
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
    storage::ObFTParserJsonProps &property,
    ObSchemaChecker &schema_checker,
    const common::ObString &current_database_name)
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
          const ObString table_name(str_len, node->children_[0]->str_value_);
          uint64_t dict_table_id = OB_INVALID_ID;
          // 停用词表必须先通过词典表 schema 校验，才能写入索引属性 JSON。
          if (OB_FAIL(resolve_and_validate_dict_table_(schema_checker, current_database_name, table_name, dict_table_id))) {
            LOG_WARN("invalid stopword dictionary table", K(ret), K(table_name));
          } else if (OB_FAIL(property.config_set_stopword_table(
                  common::ObString(str_len, node->children_[0]->str_value_)))) {
            LOG_WARN("fail to set stopword table", K(ret));
          } else if (OB_FAIL(property.config_set_stopword_table_id(dict_table_id))) {
            // 停用词也必须持久化稳定 schema ID，refresh 才能只失效其对应缓存。
            LOG_WARN("failed to set internal stopword dictionary table id", K(ret), K(dict_table_id));
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
          const ObString table_name(str_len, node->children_[0]->str_value_);
          uint64_t dict_table_id = OB_INVALID_ID;
          // 主词典表必须先通过词典表 schema 校验，才能写入索引属性 JSON。
          if (OB_FAIL(resolve_and_validate_dict_table_(schema_checker, current_database_name, table_name, dict_table_id))) {
            LOG_WARN("invalid main dictionary table", K(ret), K(table_name));
          } else if (OB_FAIL(property.config_set_dict_table(
                  common::ObString(str_len, node->children_[0]->str_value_)))) {
            LOG_WARN("fail to set dict table", K(ret));
          } else if (OB_FAIL(property.config_set_dict_table_id(dict_table_id))) {
            // 将 schema ID 随属性持久化，运行时才能按稳定身份获取 refresh generation。
            LOG_WARN("failed to set internal main dictionary table id", K(ret), K(dict_table_id));
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
          const ObString table_name(str_len, node->children_[0]->str_value_);
          uint64_t dict_table_id = OB_INVALID_ID;
          // 量词词典表必须先通过词典表 schema 校验，才能写入索引属性 JSON。
          if (OB_FAIL(resolve_and_validate_dict_table_(schema_checker, current_database_name, table_name, dict_table_id))) {
            LOG_WARN("invalid quantifier dictionary table", K(ret), K(table_name));
          } else if (OB_FAIL(property.config_set_quantifier_table(
                  common::ObString(str_len, node->children_[0]->str_value_)))) {
            LOG_WARN("fail to set quantifier table", K(ret));
          } else if (OB_FAIL(property.config_set_quantifier_table_id(dict_table_id))) {
            // 量词词典使用独立 schema ID，避免与主词典/停用词共享刷新代次。
            LOG_WARN("failed to set internal quantifier dictionary table id", K(ret), K(dict_table_id));
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

int ObFTParserResolverHelper::resolve_and_validate_dict_table_(
    ObSchemaChecker &schema_checker,
    const ObString &current_database_name,
    const ObString &raw_table_name,
    uint64_t &dict_table_id)
{
  // 引用校验在 DDL 阶段完成，避免首次写入全文索引时才因错误词典表失败。
  int ret = OB_SUCCESS;
  dict_table_id = OB_INVALID_ID;
  ObString database_name = current_database_name;
  ObString table_name = raw_table_name;
  const share::schema::ObTableSchema *table_schema = nullptr;
  const char *dot = raw_table_name.find('.');
  if (OB_UNLIKELY(raw_table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "dictionary table name is empty");
  } else if (nullptr != dot) {
    // 带库名时拆分 db.table；未带库名则沿用当前 database。
    const int32_t database_length = static_cast<int32_t>(dot - raw_table_name.ptr());
    database_name.assign_ptr(raw_table_name.ptr(), database_length);
    table_name.assign_ptr(const_cast<char *>(dot + 1), raw_table_name.length() - database_length - 1);
  }
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(database_name.empty() || table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "invalid dictionary table name");
  } else if (OB_FAIL(schema_checker.get_table_schema(database_name, table_name, false, table_schema))) {
    LOG_WARN("dictionary table does not exist", K(ret), K(database_name), K(table_name));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dictionary table schema is null", K(ret));
  } else if (!table_schema->is_fulltext_dict_table()) {
    // 只有显式标记的表才能作为分词词典，普通表不能延迟到运行时才报错。
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "referenced table is not a FULLTEXT_DICT table");
  } else {
    dict_table_id = table_schema->get_table_id();
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
