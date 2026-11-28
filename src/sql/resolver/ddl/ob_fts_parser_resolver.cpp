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

#include "data_plane/fts/ob_fts_literal.h"
#include "data_plane/fts/ob_fts_parser_property.h"
#define USING_LOG_PREFIX STORAGE_FTS

#include "sql/resolver/ddl/ob_fts_parser_resolver.h"

namespace oceanbase
{
namespace sql
{

int ObFTParserResolverHelper::resolve_parser_properties(
    const peg::Node &options,
    common::ObIAllocator &allocator,
    common::ObString &parser_property)
{
  int ret = OB_SUCCESS;
  storage::ObFTParserJsonProps property;
  const auto properties = peg::table_option_properties(options);
  OZ(property.init());
  for (size_t i = 0; OB_SUCC(ret) && i < properties.size(); ++i) {
    OZ(resolve_fts_index_parser_properties(properties[i], property));
  }
  OZ(property.to_format_json(allocator, parser_property));
  return ret;
}

int ObFTParserResolverHelper::resolve_fts_index_parser_properties(
    const peg::Node &node,
    storage::ObFTParserJsonProps &property)
{
  int ret = OB_SUCCESS;
  switch (node.kind()) {
    case peg::NodeKind::ParserMinTokenSize: {
      const int64_t value = peg::table_option_number(node);
      if (!property.is_valid_min_token_size(value)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, storage::ObFTSLiteral::MIN_TOKEN_SIZE_SCOPE_STR);
      } else {
        OZ(property.config_set_min_token_size(value));
      }
      break;
    }
    case peg::NodeKind::ParserMaxTokenSize: {
      const int64_t value = peg::table_option_number(node);
      if (!property.is_valid_max_token_size(value)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, storage::ObFTSLiteral::MAX_TOKEN_SIZE_SCOPE_STR);
      } else {
        OZ(property.config_set_max_token_size(value));
      }
      break;
    }
    case peg::NodeKind::ParserNgramTokenSize: {
      const int64_t value = peg::table_option_number(node);
      if (!property.is_valid_ngram_token_size(value)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, storage::ObFTSLiteral::NGRAM_TOKEN_SIZE_SCOPE_STR);
      } else {
        OZ(property.config_set_ngram_token_size(value));
      }
      break;
    }
    case peg::NodeKind::ParserMinNgramSize: {
      const int64_t value = peg::table_option_number(node);
      if (!property.is_valid_min_ngram_token_size(value)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, storage::ObFTSLiteral::MIN_NGRAM_SIZE_SCOPE_STR);
      } else {
        OZ(property.config_set_min_ngram_token_size(value));
      }
      break;
    }
    case peg::NodeKind::ParserMaxNgramSize: {
      const int64_t value = peg::table_option_number(node);
      if (!property.is_valid_max_ngram_token_size(value)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, storage::ObFTSLiteral::MAX_NGRAM_SIZE_SCOPE_STR);
      } else {
        OZ(property.config_set_max_ngram_token_size(value));
      }
      break;
    }
    case peg::NodeKind::ParserStopwordTable: {
      const auto text = peg::table_option_text(node);
      if (text.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "the stopword table is empty");
      } else {
        OZ(property.config_set_stopword_table(syntax_text(text)));
      }
      break;
    }
    case peg::NodeKind::ParserDictTable: {
      const auto text = peg::table_option_text(node);
      if (text.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "the dict table is empty");
      } else {
        OZ(property.config_set_dict_table(syntax_text(text)));
      }
      break;
    }
    case peg::NodeKind::ParserQuantifierTable: {
      const auto text = peg::table_option_text(node);
      if (text.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "the quanitfier table is empty");
      } else {
        OZ(property.config_set_quantifier_table(syntax_text(text)));
      }
      break;
    }
    case peg::NodeKind::ParserIkMode: {
      const auto text = peg::table_option_text(node);
      const ObString mode(syntax_text(text));
      if (mode.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "the mode str is empty");
      } else if (0 == mode.case_compare(storage::ObFTSLiteral::FT_IK_MODE_MAX_WORD)) {
        OZ(property.config_set_ik_mode(storage::ObFTSLiteral::FT_IK_MODE_MAX_WORD));
      } else if (0 == mode.case_compare(storage::ObFTSLiteral::FT_IK_MODE_SMART)) {
        OZ(property.config_set_ik_mode(storage::ObFTSLiteral::FT_IK_MODE_SMART));
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, storage::ObFTSLiteral::IK_MODE_SCOPE_STR);
      }
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      break;
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
