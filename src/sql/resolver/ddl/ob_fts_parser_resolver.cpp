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
#include "lib/string/ob_sql_string.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/resolver/ob_schema_checker.h"
#define USING_LOG_PREFIX STORAGE_FTS

#include "sql/resolver/ddl/ob_fts_parser_resolver.h"

namespace oceanbase
{
using namespace storage;
namespace sql
{

int ObFTParserResolverHelper::resolve_dict_table_name_and_id(
    const common::ObString &index_database_name,
    const common::ObString &table_name,
    share::schema::ObSchemaGetterGuard &schema_guard,
    common::ObIAllocator &allocator,
    const bool check_database_name,
    uint64_t &table_id,
    common::ObString &full_table_name)
{
  int ret = OB_SUCCESS;
  table_id = OB_INVALID_ID;
  full_table_name.reset();

  if (table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table name is empty", K(ret));
  } else {
    ObString database_name;
    ObString parsed_table_name;
    const char *dot_pos = table_name.find('.');
    if (OB_NOT_NULL(dot_pos)) {
      const int64_t db_name_len = dot_pos - table_name.ptr();
      const int64_t table_name_len = table_name.length() - db_name_len - 1;
      if (db_name_len <= 0 || table_name_len <= 0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid qualified dictionary table name", K(ret), K(table_name));
      } else {
        database_name.assign_ptr(table_name.ptr(), static_cast<ObString::obstr_size_t>(db_name_len));
        parsed_table_name.assign_ptr(dot_pos + 1, static_cast<ObString::obstr_size_t>(table_name_len));
      }
    } else if (index_database_name.empty()) {
      ret = OB_ERR_NO_DB_SELECTED;
      LOG_WARN("index database name is empty", K(ret));
      LOG_USER_ERROR(OB_ERR_NO_DB_SELECTED);
    } else {
      database_name = index_database_name;
      parsed_table_name = table_name;
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_guard.get_table_id(database_name,
                                            parsed_table_name,
                                            false /* is_index */,
                                            share::schema::ObSchemaGetterGuard::ALL_NON_HIDDEN_TYPES,
                                            table_id))) {
        LOG_WARN("failed to get dictionary table id", K(ret), K(database_name), K(parsed_table_name));
      } else if (OB_INVALID_ID == table_id) {
        ret = OB_ERR_UNKNOWN_TABLE;
        LOG_WARN("dictionary table not found", K(ret), K(database_name), K(parsed_table_name));
        LOG_USER_ERROR(OB_ERR_UNKNOWN_TABLE,
                       parsed_table_name.length(), parsed_table_name.ptr(),
                       database_name.length(), database_name.ptr());
      } else if (OB_NOT_NULL(dot_pos) && check_database_name && !index_database_name.empty()
                 && 0 != database_name.case_compare(index_database_name) && !is_inner_table(table_id)) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("custom dictionary table must be in the same database as index table",
                 K(ret), K(database_name), K(index_database_name), K(parsed_table_name));
        LOG_USER_ERROR(OB_NOT_SUPPORTED,
                       "custom dictionary table is not in the same database as index table");
      }
    }

    if (OB_SUCC(ret) && !is_inner_table(table_id)) {
      const share::schema::ObTableSchema *table_schema = nullptr;
      if (OB_FAIL(schema_guard.get_table_schema(table_id, table_schema))) {
        LOG_WARN("failed to get dictionary table schema", K(ret), K(table_id));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("dictionary table schema is null", K(ret), K(table_id));
      } else if (!table_schema->is_fulltext_dict()) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("referenced table is not a fulltext dictionary table",
                 K(ret), K(table_id), K(database_name), K(parsed_table_name));
        LOG_USER_ERROR(OB_NOT_SUPPORTED,
                       "using a non-fulltext-dictionary table as dictionary reference is");
      }
    }

    if (OB_SUCC(ret)) {
      const int64_t buf_size = database_name.length() + 1 + parsed_table_name.length() + 1;
      char *buf = static_cast<char *>(allocator.alloc(buf_size));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate dictionary table name buffer", K(ret), K(buf_size));
      } else {
        int64_t pos = 0;
        if (OB_FAIL(databuff_printf(buf, buf_size, pos, "%.*s.%.*s",
                                   database_name.length(), database_name.ptr(),
                                   parsed_table_name.length(), parsed_table_name.ptr()))) {
          LOG_WARN("fail to build fully qualified dictionary table name", K(ret));
        } else {
          full_table_name.assign_ptr(buf, static_cast<int32_t>(pos));
        }
      }
    }
  }
  return ret;
}

int ObFTParserResolverHelper::resolve_parser_properties(
    const common::ObString &index_database_name,
    const ParseNode &parse_tree,
    common::ObIAllocator &allocator,
    sql::ObSchemaChecker *schema_checker,
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
    sql::ObSchemaChecker *schema_checker)
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
          LOG_WARN("schema checker is null", K(ret));
        } else if (OB_FAIL(resolve_table_config(index_database_name, node,
                                                ObFTSLiteral::CONFIG_NAME_STOPWORD_TABLE_ID,
                                                property, allocator, *schema_checker))) {
          LOG_WARN("fail to resolve stopword table config", K(ret));
        }
        break;
      }
      case T_PARSER_DICT_TABLE: {
        if (OB_ISNULL(schema_checker)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("schema checker is null", K(ret));
        } else if (OB_FAIL(resolve_table_config(index_database_name, node,
                                                ObFTSLiteral::CONFIG_NAME_DICT_TABLE_ID,
                                                property, allocator, *schema_checker))) {
          LOG_WARN("fail to resolve dict table config", K(ret));
        }
        break;
      }
      case T_PARSER_QUANTIFIER_TABLE: {
        if (OB_ISNULL(schema_checker)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("schema checker is null", K(ret));
        } else if (OB_FAIL(resolve_table_config(index_database_name, node,
                                                ObFTSLiteral::CONFIG_NAME_QUANTIFIER_TABLE_ID,
                                                property, allocator, *schema_checker))) {
          LOG_WARN("fail to resolve quantifier table config", K(ret));
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
    const char *table_id_config_name,
    storage::ObFTParserJsonProps &property,
    common::ObIAllocator &allocator,
    sql::ObSchemaChecker &schema_checker)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node) || OB_ISNULL(node->children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dictionary table option node is null", K(ret));
  } else if (OB_UNLIKELY(node->children_[0]->str_len_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dictionary table name is empty", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "the dictionary table is empty");
  } else if (OB_ISNULL(table_id_config_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table id config name is null", K(ret));
  } else if (OB_ISNULL(schema_checker.get_schema_guard())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema guard is null", K(ret));
  } else {
    const int32_t str_len = static_cast<int32_t>(node->children_[0]->str_len_);
    const common::ObString table_name(str_len, node->children_[0]->str_value_);
    const char *table_name_config_name =
        ObFTSLiteral::get_table_name_config_name(ObString(table_id_config_name));
    uint64_t table_id = OB_INVALID_ID;
    common::ObString full_table_name;
    if (OB_ISNULL(table_name_config_name)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("table name config name is null", K(ret), K(table_id_config_name));
    } else if (OB_FAIL(resolve_dict_table_name_and_id(index_database_name,
                                                      table_name,
                                                      *schema_checker.get_schema_guard(),
                                                      allocator,
                                                      true /* check_database_name */,
                                                      table_id,
                                                      full_table_name))) {
      LOG_WARN("fail to resolve dictionary table name and id", K(ret), K(table_name));
    } else if (OB_FAIL(property.config_set_table_id_impl(table_id_config_name, table_id))) {
      LOG_WARN("fail to set dictionary table id", K(ret), K(table_id));
    } else if (OB_FAIL(property.config_set_table_name_impl(table_name_config_name, full_table_name))) {
      LOG_WARN("fail to set dictionary table name", K(ret), K(full_table_name));
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
