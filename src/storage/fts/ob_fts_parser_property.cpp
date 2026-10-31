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

#include "data_plane/fts/ob_fts_parser_property.h"

#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_parse.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/allocator/page_arena.h"
#include "lib/list/ob_dlist.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"
#include "lib/utility/alloc_assist.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "storage/fts/ob_fts_literal.h"
#include "data_plane/fts/ob_fts_parser_helper.h"

#define USING_LOG_PREFIX STORAGE_FTS

namespace oceanbase
{
namespace storage
{

namespace
{
/**
 * A small cache of the parsed parser properties.
 *
 * The properties of a parser are parsed from a json string, and the very same
 * json string is used by every row of a fulltext index and by every
 * evaluation of the tokenize expression. Parsing the json again for every
 * call costs more than the tokenization itself, so the parsed result is
 * cached and reused.
 */
class ObFTParserPropertyCache
{
public:
  static constexpr int64_t CACHE_ENTRY_NUM = 4;

  static ObFTParserPropertyCache &instance()
  {
    static thread_local ObFTParserPropertyCache cache;
    return cache;
  }

  int get(int64_t parser_type, const ObString &json_str, ObFTParserProperty &property)
  {
    int ret = OB_ENTRY_NOT_EXIST;
    for (int64_t i = 0; i < CACHE_ENTRY_NUM; ++i) {
      if (entries_[i].match(parser_type, json_str)) {
        property = entries_[i].property_;
        ret = OB_SUCCESS;
        break;
      }
    }
    return ret;
  }

  int put(int64_t parser_type, const ObString &json_str, const ObFTParserProperty &property)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(entries_[next_idx_].set(parser_type, json_str, property))) {
    } else {
      next_idx_ = (next_idx_ + 1) % CACHE_ENTRY_NUM;
    }
    return ret;
  }

private:
  struct Entry
  {
    Entry() : parser_type_(-1), alloc_(), json_str_(), property_() {}

    bool match(int64_t parser_type, const ObString &json_str) const
    {
      return parser_type_ == parser_type && json_str_.length() == json_str.length()
             && (json_str.length() == 0
                 || 0 == MEMCMP(json_str_.ptr(), json_str.ptr(), json_str.length()));
    }

    int set(int64_t parser_type, const ObString &json_str, const ObFTParserProperty &property)
    {
      int ret = OB_SUCCESS;
      alloc_.reset();
      json_str_ = ObString();
      if (json_str.length() > 0) {
        char *buf = static_cast<char *>(alloc_.alloc(json_str.length()));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMCPY(buf, json_str.ptr(), json_str.length());
          json_str_ = ObString(json_str.length(), buf);
        }
      }
      if (OB_SUCC(ret)) {
        parser_type_ = parser_type;
        property_ = property;
      } else {
        alloc_.reset();
        parser_type_ = -1;
        json_str_ = ObString();
        property_ = ObFTParserProperty();
      }
      return ret;
    }

    int64_t parser_type_;
    ObArenaAllocator alloc_;
    ObString json_str_;
    ObFTParserProperty property_;
  };

  ObFTParserPropertyCache() : next_idx_(0) {}

  int64_t next_idx_;
  Entry entries_[CACHE_ENTRY_NUM];
};

int64_t get_ft_parser_type(const ObFTParser &parser)
{
  int64_t parser_type = 0;
  if (parser.is_ik()) {
    parser_type = 1;
  } else if (parser.is_space()) {
    parser_type = 2;
  } else if (parser.is_ngram()) {
    parser_type = 3;
  } else if (parser.is_beng()) {
    parser_type = 4;
  } else if (parser.is_ngram2()) {
    parser_type = 5;
  }
  return parser_type;
}

int parse_ft_parser_properties(const ObFTParser &parser,
                               const ObString &json_str,
                               ObFTParserProperty &property)
{
  int ret = OB_SUCCESS;
  ObFTParserJsonProps props;
  if (OB_FAIL(props.init())) {
  } else if (OB_FAIL(props.parse_from_valid_str(json_str))) {
  } else {
    if (parser.is_ik()) {
      // set dict tables and copy dict name
      property.dict_table_ = ObString(ObFTSLiteral::CONFIG_NAME_DICT_TABLE);
      property.stopword_table_ = ObString(ObFTSLiteral::CONFIG_NAME_STOPWORD_TABLE);
      property.quantifier_table_ = ObString(ObFTSLiteral::CONFIG_NAME_QUANTIFIER_TABLE);
      ObString ik_smart;
      if (OB_FAIL(props.config_get_ik_mode(ik_smart))) {
      } else {
        if (0 == ik_smart.case_compare(ObString(ObFTSLiteral::FT_IK_MODE_SMART))) {
          property.ik_mode_smart_ = true;
        } else if (0 == ik_smart.case_compare(ObString(ObFTSLiteral::FT_IK_MODE_MAX_WORD))) {
          property.ik_mode_smart_ = false;
        } else {
          ret = OB_INVALID_ARGUMENT;
        }
      }
    } else if (parser.is_space()) {
      if (json_str.empty()) {
        property.min_token_size_ = ObFTSLiteral::FT_DEFAULT_MIN_TOKEN_SIZE;
        property.max_token_size_ = ObFTSLiteral::FT_DEFAULT_MAX_TOKEN_SIZE;
      } else if (OB_FAIL(props.config_get_min_token_size(property.min_token_size_))) {
      } else if (OB_FAIL(props.config_get_max_token_size(property.max_token_size_))) {
      }
    } else if (parser.is_ngram()) {
      if (json_str.empty()) {
        property.ngram_token_size_ = ObFTSLiteral::FT_DEFAULT_NGRAM_TOKEN_SIZE;
      } else if (OB_FAIL(props.config_get_ngram_token_size(property.ngram_token_size_))) {
      }
    } else if (parser.is_beng()) {
      if (json_str.empty()) {
        property.min_token_size_ = ObFTSLiteral::FT_DEFAULT_MIN_TOKEN_SIZE;
        property.max_token_size_ = ObFTSLiteral::FT_DEFAULT_MAX_TOKEN_SIZE;
      } else if (OB_FAIL(props.config_get_min_token_size(property.min_token_size_))) {
      } else if (OB_FAIL(props.config_get_max_token_size(property.max_token_size_))) {
      }
    } else if (parser.is_ngram2()) {
      if (json_str.empty()) {
        property.min_ngram_token_size_ = ObFTSLiteral::FT_DEFAULT_MIN_NGRAM_SIZE;
        property.max_ngram_token_size_ = ObFTSLiteral::FT_DEFAULT_MAX_NGRAM_SIZE;
      } else if (OB_FAIL(props.config_get_min_ngram_token_size(property.min_ngram_token_size_))) {
      } else if (OB_FAIL(props.config_get_max_ngram_token_size(property.max_ngram_token_size_))) {
      }
    }
  }
  return ret;
}
} // namespace


int ObFTParserJsonProps::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(root_ = OB_NEWx(ObJsonObject, &allocator_, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    is_inited_ = true;
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObIJsonBase, &allocator_, root_);
    root_ = nullptr;
  }

  return ret;
}

int ObFTParserJsonProps::config_set_min_token_size(const int64_t size)
{
  int ret = OB_SUCCESS;
  ObJsonInt *min_token_size = nullptr;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(min_token_size = OB_NEWx(ObJsonInt, &allocator_, size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (!is_valid_min_token_size(size)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(root_->object_add(ObString(ObFTSLiteral::CONFIG_NAME_MIN_TOKEN_SIZE),
                                       min_token_size))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObJsonInt, &allocator_, min_token_size);
  }

  return ret;
}

int ObFTParserJsonProps::config_set_max_token_size(const int64_t size)
{
  int ret = OB_SUCCESS;
  ObJsonInt *max_token_size = nullptr;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(max_token_size = OB_NEWx(ObJsonInt, &allocator_, size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (!is_valid_max_token_size(size)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(root_->object_add(ObString(ObFTSLiteral::CONFIG_NAME_MAX_TOKEN_SIZE),
                                       max_token_size))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObJsonInt, &allocator_, max_token_size);
  }

  return ret;
}

int ObFTParserJsonProps::config_set_ngram_token_size(const int64_t size)
{
  int ret = OB_SUCCESS;
  ObJsonInt *ngram_token_size = nullptr;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(ngram_token_size = OB_NEWx(ObJsonInt, &allocator_, size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (!is_valid_ngram_token_size(size)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(root_->object_add(ObString(ObFTSLiteral::CONFIG_NAME_NGRAM_TOKEN_SIZE),
                                       ngram_token_size))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObJsonInt, &allocator_, ngram_token_size);
  }

  return ret;
}

// should string deep copy?
int ObFTParserJsonProps::config_set_dict_table(const ObString &str)
{
  int ret = OB_SUCCESS;
  ObJsonString *dict_table = nullptr;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(dict_table = OB_NEWx(ObJsonString, &allocator_, str))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(
                 root_->object_add(ObString(ObFTSLiteral::CONFIG_NAME_DICT_TABLE), dict_table))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObJsonString, &allocator_, dict_table);
  }

  return ret;
}

int ObFTParserJsonProps::config_set_stopword_table(const ObString &str)
{
  int ret = OB_SUCCESS;
  ObJsonString *stopword_table = nullptr;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(stopword_table = OB_NEWx(ObJsonString, &allocator_, str))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(root_->object_add(ObString(ObFTSLiteral::CONFIG_NAME_STOPWORD_TABLE),
                                       stopword_table))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObJsonString, &allocator_, stopword_table);
  }

  return ret;
}

int ObFTParserJsonProps::config_set_quantifier_table(const ObString &str)
{
  int ret = OB_SUCCESS;
  ObJsonString *quantifier_table = nullptr;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(quantifier_table = OB_NEWx(ObJsonString, &allocator_, str))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(root_->object_add(ObString(ObFTSLiteral::CONFIG_NAME_QUANTIFIER_TABLE),
                                       quantifier_table))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObJsonString, &allocator_, quantifier_table);
  }

  return ret;
}

int ObFTParserJsonProps::config_set_ik_mode(const ObString &ik_mode)
{
  int ret = OB_SUCCESS;
  ObJsonString *ik_mode_node = nullptr;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(ik_mode_node = OB_NEWx(ObJsonString, &allocator_, ik_mode))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(root_->object_add(ObString(ObFTSLiteral::CONFIG_NAME_IK_MODE),
                                       ik_mode_node))) {
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObJsonString, &allocator_, ik_mode_node);
  }

  return ret;
}

int ObFTParserJsonProps::config_set_min_ngram_token_size(const int64_t size)
{
  int ret = OB_SUCCESS;
  ObJsonInt *ngram_token_size = nullptr;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(ngram_token_size = OB_NEWx(ObJsonInt, &allocator_, size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (!is_valid_min_ngram_token_size(size)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(root_->object_add(ObString(ObFTSLiteral::CONFIG_NAME_MIN_NGRAM_SIZE),
                                       ngram_token_size))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObJsonInt, &allocator_, ngram_token_size);
  }

  return ret;
}

int ObFTParserJsonProps::config_set_max_ngram_token_size(const int64_t size)
{
  int ret = OB_SUCCESS;
  ObJsonInt *ngram_token_size = nullptr;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(ngram_token_size = OB_NEWx(ObJsonInt, &allocator_, size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (!is_valid_max_ngram_token_size(size)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(root_->object_add(ObString(ObFTSLiteral::CONFIG_NAME_MAX_NGRAM_SIZE),
                                       ngram_token_size))) {
  } else {
  }

  if (OB_FAIL(ret)) {
    OB_DELETEx(ObJsonInt, &allocator_, ngram_token_size);
  }

  return ret;
}

int ObFTParserJsonProps::parse_from_valid_str(const ObString &str)
{
  int ret = OB_SUCCESS;
  ObJsonNode *root = nullptr;

  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (str.empty()) {
    // do nothing and use default {}
  } else if (OB_FAIL(ObJsonParser::get_tree(&allocator_, str, root))) {
  } else {
    root_ = root;
  }
  return ret;
}

bool ObFTParserJsonProps::is_empty() const
{
  return (root_ == nullptr) || (root_->element_count() == 0);
}

int ObFTParserJsonProps::to_format_json(ObIAllocator &alloc, ObString &str)
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    if (is_empty()) {
      str = ObString(); // just make it empty;
    } else {
      ObJsonBuffer j_buf(&alloc);
      if (OB_FAIL(root_->print(j_buf, false))) {
      } else {
        str = j_buf.string();
      }
    }
  }
  return ret;
}

int ObFTParserJsonProps::config_get_min_token_size(int64_t &size) const
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(
            root_->get_object_value(ObString(ObFTSLiteral::CONFIG_NAME_MIN_TOKEN_SIZE), value))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
      } else {
      }
    } else if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (value->json_type() != ObJsonNodeType::J_INT) {
    } else {
      size = value->get_int();
    }
  }
  return ret;
}

int ObFTParserJsonProps::config_get_max_token_size(int64_t &size) const
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(
            root_->get_object_value(ObString(ObFTSLiteral::CONFIG_NAME_MAX_TOKEN_SIZE), value))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
      } else {
      }
    } else if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (value->json_type() != ObJsonNodeType::J_INT) {
    } else {
      size = value->get_int();
    }
  }
  return ret;
}

int ObFTParserJsonProps::config_get_ngram_token_size(int64_t &size) const
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(
            root_->get_object_value(ObString(ObFTSLiteral::CONFIG_NAME_NGRAM_TOKEN_SIZE), value))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
      } else {
      }
    } else if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (value->json_type() != ObJsonNodeType::J_INT) {
    } else {
      size = value->get_int();
    }
  }
  return ret;
}

int ObFTParserJsonProps::config_get_dict_table(ObString &str) const
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(root_->get_object_value(ObString(ObFTSLiteral::CONFIG_NAME_DICT_TABLE), value))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
      } else {
      }
    } else if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (value->json_type() != ObJsonNodeType::J_STRING) {
    } else {
      ObJsonString *json_str = static_cast<ObJsonString *>(value);
      str = json_str->get_str();
    }
  }
  return ret;
}

int ObFTParserJsonProps::config_get_stopword_table(ObString &str) const
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(
            root_->get_object_value(ObString(ObFTSLiteral::CONFIG_NAME_STOPWORD_TABLE), value))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
      } else {
      }
    } else if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (value->json_type() != ObJsonNodeType::J_STRING) {
    } else {
      ObJsonString *json_str = static_cast<ObJsonString *>(value);
      str = json_str->get_str();
    }
  }
  return ret;
}

int ObFTParserJsonProps::config_get_quantifier_table(ObString &str) const
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(
            root_->get_object_value(ObString(ObFTSLiteral::CONFIG_NAME_QUANTIFIER_TABLE), value))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
      } else {
      }
    } else if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (value->json_type() != ObJsonNodeType::J_STRING) {
    } else {
      ObJsonString *json_str = static_cast<ObJsonString *>(value);
      str = json_str->get_str();
    }
  }
  return ret;
}

int ObFTParserJsonProps::config_get_ik_mode(ObString &ik_mode) const
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(root_->get_object_value(ObString(ObFTSLiteral::CONFIG_NAME_IK_MODE), value))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
      } else {
      }
    } else if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (value->json_type() != ObJsonNodeType::J_STRING) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ObJsonString *json_str = static_cast<ObJsonString *>(value);
      ik_mode = json_str->get_str();
    }
  }
  return ret;
}

int ObFTParserJsonProps::config_get_min_ngram_token_size(int64_t &size) const
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(
            root_->get_object_value(ObString(ObFTSLiteral::CONFIG_NAME_MIN_NGRAM_SIZE), value))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
      } else {
      }
    } else if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (value->json_type() != ObJsonNodeType::J_INT) {
    } else {
      size = value->get_int();
    }
  }
  return ret;
}

int ObFTParserJsonProps::config_get_max_ngram_token_size(int64_t &size) const
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(
            root_->get_object_value(ObString(ObFTSLiteral::CONFIG_NAME_MAX_NGRAM_SIZE), value))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
      } else {
      }
    } else if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (value->json_type() != ObJsonNodeType::J_INT) {
    } else {
      size = value->get_int();
    }
  }
  return ret;
}

int ObFTParserJsonProps::check_unsupported_config(const char **config_array,
                                                  int32_t config_count,
                                                  bool &has_unsupported) const
{
  int ret = OB_SUCCESS;
  has_unsupported = false;
  for (int32_t i = 0; OB_SUCC(ret) && i < root_->element_count() && !has_unsupported; ++i) {
    ObString key;
    if (OB_FAIL(root_->get_key(i, key))) {
    } else {
      bool is_supported = false;
      for (int32_t j = 0; OB_SUCC(ret) && j < config_count && !is_supported; ++j) {
        if (0 == (ObString(config_array[j]).case_compare(key))) {
          is_supported = true;
        }
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (!is_supported) {
        has_unsupported = true;
        LOG_WARN("unsupported config", K(key));
      }
    }
  }

  return ret;
}

int ObFTParserJsonProps::rebuild_props_for_ddl(const ObString &parser_name,
                                               const common::ObCollationType &type,
                                               const bool log_to_user)
{
  int ret = OB_SUCCESS;

  ObFTParser parser;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (parser_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parser.parse_from_str(parser_name.ptr(), parser_name.length()))) {
  } else {
    if (parser.is_ik()) {
      if (OB_FAIL(ik_rebuild_props_for_ddl(log_to_user))) {
      }
    } else if (parser.is_space()) {
      if (OB_FAIL(space_rebuild_props_for_ddl(log_to_user))) {
      }
    } else if (parser.is_ngram()) {
      if (OB_FAIL(ngram_rebuild_props_for_ddl(log_to_user))) {
      }
    } else if (parser.is_beng()) {
      if (OB_FAIL(beng_rebuild_props_for_ddl(log_to_user))) {
      }
    } else if (parser.is_ngram2()) {
      if (OB_FAIL(ngram2_rebuild_props_for_ddl(log_to_user))) {
      }
    } else {
      ret = OB_FUNCTION_NOT_DEFINED;
    }
  }

  return ret;
}

int ObFTParserJsonProps::ik_rebuild_props_for_ddl(const bool log_to_user)
{
  int ret = OB_SUCCESS;

  static const char *supported[] = {
      ObFTSLiteral::CONFIG_NAME_DICT_TABLE,
      ObFTSLiteral::CONFIG_NAME_QUANTIFIER_TABLE,
      ObFTSLiteral::CONFIG_NAME_STOPWORD_TABLE,
      ObFTSLiteral::CONFIG_NAME_IK_MODE,
  };

  bool has_unsupported = false;
  if (OB_FAIL(check_unsupported_config(supported, ARRAYSIZEOF(supported), has_unsupported))) {
  } else if (has_unsupported) {
    ret = OB_NOT_SUPPORTED;
    if (log_to_user) {
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "ik config");
    }
  };

  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    ObString table_name = "";
    if (OB_FAIL(config_get_dict_table(table_name))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        if (OB_FAIL(config_set_dict_table(ObFTSLiteral::FT_DEFAULT_IK_DICT_UTF8_TABLE))) {
        }
      }
    } else {
      // check dict table valid
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(config_get_quantifier_table(table_name))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        if (OB_FAIL(
                config_set_quantifier_table(ObFTSLiteral::FT_DEFAULT_IK_QUANTIFIER_UTF8_TABLE))) {
        }
      }
    } else {
      // check quantifier table valid
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(config_get_quantifier_table(table_name))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        if (OB_FAIL(
                config_set_quantifier_table(ObFTSLiteral::FT_DEFAULT_IK_QUANTIFIER_UTF8_TABLE))) {
        }
      }
    } else {
      // check quantifier table valid
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else {
      ObString ik_mode;
      if (OB_FAIL(config_get_ik_mode(ik_mode))) {
        if (OB_SEARCH_NOT_FOUND == ret) {
          if (OB_FAIL(config_set_ik_mode(ObFTSLiteral::FT_IK_MODE_SMART))) {
          }
        } else {
          if (log_to_user) {
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::IK_MODE_SCOPE_STR);
          }
        }
      } else {
        if (0 == ObString(ObFTSLiteral::FT_IK_MODE_SMART).case_compare(ik_mode)) {
          // okay
        } else if (0 == ObString(ObFTSLiteral::FT_IK_MODE_MAX_WORD).case_compare(ik_mode)) {
          // okay
        } else {
          ret = OB_INVALID_ARGUMENT;
          if (log_to_user) {
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::IK_MODE_SCOPE_STR);
          }
        }
      }
    }
  }
  return ret;
}

int ObFTParserJsonProps::ngram_rebuild_props_for_ddl(const bool log_to_user)
{
  int ret = OB_SUCCESS;

  static const char *supported[] = {
      ObFTSLiteral::CONFIG_NAME_NGRAM_TOKEN_SIZE,
  };

  bool has_unsupported = false;
  if (OB_FAIL(check_unsupported_config(supported, ARRAYSIZEOF(supported), has_unsupported))) {
  } else if (has_unsupported) {
    ret = OB_NOT_SUPPORTED;
    if (log_to_user) {
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "ngram config");
    }
  };

  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    int64_t ngram_token_size = 0;

    if (OB_FAIL(config_get_ngram_token_size(ngram_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        if (OB_FAIL(config_set_ngram_token_size(ObFTSLiteral::FT_DEFAULT_NGRAM_TOKEN_SIZE))) {
        }
      }
    } else if (!is_valid_ngram_token_size(ngram_token_size)) {
      ret = OB_INVALID_ARGUMENT;
      if (log_to_user) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::NGRAM_TOKEN_SIZE_SCOPE_STR);
      }
    }
  }

  return ret;
}

int ObFTParserJsonProps::space_rebuild_props_for_ddl(const bool log_to_user)
{
  int ret = OB_SUCCESS;

  static const char *supported[] = {
      ObFTSLiteral::CONFIG_NAME_MIN_TOKEN_SIZE,
      ObFTSLiteral::CONFIG_NAME_MAX_TOKEN_SIZE,
  };

  bool has_unsupported = false;
  if (OB_FAIL(check_unsupported_config(supported, ARRAYSIZEOF(supported), has_unsupported))) {
  } else if (has_unsupported) {
    ret = OB_NOT_SUPPORTED;
    if (log_to_user) {
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "space config");
    }
  };

  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    int64_t min_token_size = 0;
    int64_t max_token_size = 0;

    if (OB_FAIL(config_get_min_token_size(min_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        if (OB_FAIL(config_set_min_token_size(ObFTSLiteral::FT_DEFAULT_MIN_TOKEN_SIZE))) {
        }
      }
    } else if (!is_valid_min_token_size(min_token_size)) {
      ret = OB_INVALID_ARGUMENT;
      if (log_to_user) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MIN_TOKEN_SIZE_SCOPE_STR);
      }
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(config_get_max_token_size(max_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        if (OB_FAIL(config_set_max_token_size(ObFTSLiteral::FT_DEFAULT_MAX_TOKEN_SIZE))) {
        }
      }
    } else if (!is_valid_max_token_size(max_token_size)) {
      ret = OB_INVALID_ARGUMENT;
      if (log_to_user) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MAX_TOKEN_SIZE_SCOPE_STR);
      }
    } else if (min_token_size > max_token_size) {
      ret = OB_INVALID_ARGUMENT;
      if (log_to_user) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MIN_MAX_TOKEN_SIZE_SCOPE_STR);
      }
    }
  }
  return ret;
}

int ObFTParserJsonProps::beng_rebuild_props_for_ddl(const bool log_to_user)
{
  int ret = OB_SUCCESS;

  static const char *supported[] = {
      ObFTSLiteral::CONFIG_NAME_MIN_TOKEN_SIZE,
      ObFTSLiteral::CONFIG_NAME_MAX_TOKEN_SIZE,
  };

  bool has_unsupported = false;
  if (OB_FAIL(check_unsupported_config(supported, ARRAYSIZEOF(supported), has_unsupported))) {
  } else if (has_unsupported) {
    ret = OB_NOT_SUPPORTED;
    if (log_to_user) {
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "beng config");
    }
  };

  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    int64_t min_token_size = 0;
    int64_t max_token_size = 0;

    if (OB_FAIL(config_get_min_token_size(min_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        min_token_size = ObFTSLiteral::FT_DEFAULT_MIN_TOKEN_SIZE;
        if (OB_FAIL(config_set_min_token_size(min_token_size))) {
        }
      }
    } else if (!is_valid_min_token_size(min_token_size)) {
      ret = OB_INVALID_ARGUMENT;
      if (log_to_user) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MIN_TOKEN_SIZE_SCOPE_STR);
      }
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(config_get_max_token_size(max_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        max_token_size = ObFTSLiteral::FT_DEFAULT_MAX_TOKEN_SIZE;
        if (OB_FAIL(config_set_max_token_size(max_token_size))) {
        }
      }
    } else if (!is_valid_max_token_size(max_token_size)) {
      ret = OB_INVALID_ARGUMENT;
      if (log_to_user) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MAX_TOKEN_SIZE_SCOPE_STR);
      }
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (min_token_size > max_token_size) {
      ret = OB_INVALID_ARGUMENT;
      if (log_to_user) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MIN_MAX_TOKEN_SIZE_SCOPE_STR);
      }
    }
  }
  return ret;
}

int ObFTParserJsonProps::ngram2_rebuild_props_for_ddl(const bool log_to_user)
{
  int ret = OB_SUCCESS;

  static const char *supported[] = {
      ObFTSLiteral::CONFIG_NAME_MIN_NGRAM_SIZE,
      ObFTSLiteral::CONFIG_NAME_MAX_NGRAM_SIZE,
  };

  bool has_unsupported = false;
  if (OB_FAIL(check_unsupported_config(supported, ARRAYSIZEOF(supported), has_unsupported))) {
  } else if (has_unsupported) {
    ret = OB_NOT_SUPPORTED;
    if (log_to_user) {
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "ngram2 config");
    }
  };

  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    int64_t min_ngram_size = 0;
    int64_t max_ngram_size = 0;

    if (OB_FAIL(config_get_min_ngram_token_size(min_ngram_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        min_ngram_size = ObFTSLiteral::FT_DEFAULT_MIN_NGRAM_SIZE;
        if (OB_FAIL(config_set_min_ngram_token_size(min_ngram_size))) {
        }
      }
    } else if (!is_valid_min_ngram_token_size(min_ngram_size)) {
      ret = OB_INVALID_ARGUMENT;
      if (log_to_user) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::MIN_NGRAM_SIZE_SCOPE_STR);
      }
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(config_get_max_ngram_token_size(max_ngram_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        max_ngram_size = ObFTSLiteral::FT_DEFAULT_MAX_NGRAM_SIZE;
        if (OB_FAIL(config_set_max_ngram_token_size(max_ngram_size))) {
        }
      }
    } else if (!is_valid_max_ngram_token_size(max_ngram_size)) {
      ret = OB_INVALID_ARGUMENT;
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (min_ngram_size > max_ngram_size) {
      ret = OB_INVALID_ARGUMENT;
      if (log_to_user) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, ObFTSLiteral::NGRAM_MIN_MAX_TOKEN_SIZE_SCOPE_STR);
      }
    }
  }
  return ret;
}

ObFTParserJsonProps::ObFTParserJsonProps()
    : allocator_("FTJsonProps"), root_(NULL), is_inited_(false)
{
}

ObFTParserJsonProps::~ObFTParserJsonProps()
{
  if (!OB_ISNULL(root_)) {
    root_->reset();
    root_->~ObIJsonBase();
    root_ = nullptr;
  }
  allocator_.reset();
}

#define __FT_PARSER_PROPERTY_SHOW_COMMA(need_comma)                                                \
  do {                                                                                             \
    if (need_comma) {                                                                              \
      if (OB_FAIL(databuff_printf(buf, buf_len, pos, ","))) {                                      \
        LOG_WARN("fail to printf comma", K(ret), K(buf_len), K(pos));                              \
      }                                                                                            \
    }                                                                                              \
    need_comma = true;                                                                             \
  } while (0)

int ObFTParserJsonProps::show_parser_properties(const ObFTParserJsonProps &properties,
                                                char *buf,
                                                const int64_t buf_len,
                                                int64_t &pos)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!properties.is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (properties.is_empty()) {
    // not output properties
  } else {
    if (OB_FAIL(databuff_printf(buf, buf_len, pos, "PARSER_PROPERTIES=("))) {
    }

    bool need_comma = false;

    int64_t min_token_size = 0;
    if (FAILEDx(properties.config_get_min_token_size(min_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        ret = OB_SUCCESS;
      } else {
      }
    } else {
      __FT_PARSER_PROPERTY_SHOW_COMMA(need_comma);
      if (FAILEDx(databuff_printf(buf,
                                  buf_len,
                                  pos,
                                  "%s=%ld",
                                  ObFTSLiteral::CONFIG_NAME_MIN_TOKEN_SIZE,
                                  min_token_size))) {
      }
    }

    int64_t max_token_size = 0;
    if (FAILEDx(properties.config_get_max_token_size(max_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        ret = OB_SUCCESS;
      } else {
      }
    } else {
      __FT_PARSER_PROPERTY_SHOW_COMMA(need_comma);
      if (FAILEDx(databuff_printf(buf,
                                  buf_len,
                                  pos,
                                  "%s=%ld",
                                  ObFTSLiteral::CONFIG_NAME_MAX_TOKEN_SIZE,
                                  max_token_size))) {
      }
    }

    ObString ik_mode;
    if (FAILEDx(properties.config_get_ik_mode(ik_mode))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        ret = OB_SUCCESS;
      } else {
      }
    } else {
      __FT_PARSER_PROPERTY_SHOW_COMMA(need_comma);
      if (FAILEDx(databuff_printf(buf,
                                  buf_len,
                                  pos,
                                  "%s=\"%s\"",
                                  ObFTSLiteral::CONFIG_NAME_IK_MODE,
                                  ik_mode.ptr()))) {
      }
    }

    int64_t ngram_token_size = 0;
    if (FAILEDx(properties.config_get_ngram_token_size(ngram_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        ret = OB_SUCCESS;
      } else {
      }
    } else {
      __FT_PARSER_PROPERTY_SHOW_COMMA(need_comma);
      if (FAILEDx(databuff_printf(buf,
                                  buf_len,
                                  pos,
                                  "%s=%ld",
                                  ObFTSLiteral::CONFIG_NAME_NGRAM_TOKEN_SIZE,
                                  ngram_token_size))) {
      }
    }

    int64_t min_ngram_token_size = 0;
    if (FAILEDx(properties.config_get_min_ngram_token_size(min_ngram_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        ret = OB_SUCCESS;
      } else {
      }
    } else {
      __FT_PARSER_PROPERTY_SHOW_COMMA(need_comma);
      if (FAILEDx(databuff_printf(buf,
                                  buf_len,
                                  pos,
                                  "%s=%ld",
                                  ObFTSLiteral::CONFIG_NAME_MIN_NGRAM_SIZE,
                                  min_ngram_token_size))) {
      }
    }

    int64_t max_ngram_token_size = 0;
    if (FAILEDx(properties.config_get_max_ngram_token_size(max_ngram_token_size))) {
      if (OB_SEARCH_NOT_FOUND == ret) {
        ret = OB_SUCCESS;
      } else {
      }
    } else {
      __FT_PARSER_PROPERTY_SHOW_COMMA(need_comma);
      if (FAILEDx(databuff_printf(buf,
                                  buf_len,
                                  pos,
                                  "%s=%ld",
                                  ObFTSLiteral::CONFIG_NAME_MAX_NGRAM_SIZE,
                                  max_ngram_token_size))) {
      }
    }

    if (FAILEDx(databuff_printf(buf, buf_len, pos, ") "))) {
    }
  }

  return ret;
}

#undef __FT_PARSER_PROPERTY_SHOW_COMMA

int ObFTParserProperty::parse_for_parser_helper(const ObFTParser &parser, const ObString &json_str)
{
  int ret = OB_SUCCESS;
  const int64_t parser_type = get_ft_parser_type(parser);
  ObFTParserProperty property;
  if (OB_FAIL(ObFTParserPropertyCache::instance().get(parser_type, json_str, property))) {
    // The properties have not been parsed yet, parse them and remember the
    // result, the same properties are used again and again.
    if (OB_FAIL(parse_ft_parser_properties(parser, json_str, property))) {
    } else if (OB_FAIL(ObFTParserPropertyCache::instance().put(parser_type, json_str, property))) {
      // The cache is only an optimization, failing to cache is not an error.
      ret = OB_SUCCESS;
    }
  }
  if (OB_SUCC(ret)) {
    *this = property;
  }
  return ret;
}

ObFTParserProperty::ObFTParserProperty()
    : min_token_size_(ObFTSLiteral::FT_DEFAULT_MIN_TOKEN_SIZE),
      max_token_size_(ObFTSLiteral::FT_DEFAULT_MAX_TOKEN_SIZE),
      ngram_token_size_(ObFTSLiteral::FT_DEFAULT_NGRAM_TOKEN_SIZE),
      ik_mode_smart_(true),
      stopword_table_(),
      dict_table_(),
      quantifier_table_(),
      min_ngram_token_size_(ObFTSLiteral::FT_DEFAULT_MIN_NGRAM_SIZE),
      max_ngram_token_size_(ObFTSLiteral::FT_DEFAULT_MAX_NGRAM_SIZE)
{
}

int ObFTParserJsonProps::tokenize_array_to_props_json(ObIAllocator &allocator,
                                                      ObIJsonBase *array,
                                                      ObString &json_str)
{
  int ret = OB_SUCCESS;

  ObArenaAllocator alloc;
  ObJsonObject properties_root(&alloc);

  if (OB_ISNULL(array)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < array->element_count(); i++) {
      ObIJsonBase *array_value_item;
      ObString config_name;
      ObIJsonBase *config_value;
      if (OB_FAIL(array->get_array_element(i, array_value_item))) {
      } else if (array_value_item->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_ARGUMENT;
      } else if (1 != array_value_item->element_count()) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_FAIL(array_value_item->get_object_value(0, config_name, config_value))) {
      } else if (OB_FAIL(properties_root.object_add(config_name, config_value))) {
      }
    }
    ObJsonBuffer j_buf(&allocator);
    if (FAILEDx(properties_root.print(j_buf, false))) {
    } else {
      json_str = j_buf.string();
    }
  }

  return ret;
}
} // end namespace storage
} // end namespace oceanbase
