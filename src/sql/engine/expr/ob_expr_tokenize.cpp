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

#include "sql/engine/expr/ob_expr_tokenize.h"

#include "lib/alloc/alloc_struct.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "object/ob_object.h"
#include "plugin/sys/ob_plugin_helper.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "sql/session/ob_basic_session_info.h"
#include "share/ob_json_access_utils.h"
#include "storage/fts/dict/ob_gen_dic_loader.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_plugin_helper.h"

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_json_func_helper.h" // file not self-contained, there're logs inside.

namespace oceanbase
{
namespace sql
{
ObExprTokenize::ObExprTokenize(common::ObIAllocator &alloc)
    : ObStringExprOperator(alloc,
                           T_FUN_TOKENIZE,
                           N_TOKENIZE,
                           MORE_THAN_ZERO,
                           VALID_FOR_GENERATED_COL)
{
}

ObExprTokenize::~ObExprTokenize() {}

int ObExprTokenize::eval_tokenize(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;

  // FTS PERF OPT 17: repeated constant TOKENIZE calls are deterministic for
  // built-in parsers without user properties. Keep one bounded result per
  // worker thread; dynamic text, custom dictionaries and explicit properties
  // continue through the normal parser path.
  struct BuiltinTokenizeResultCache
  {
    BuiltinTokenizeResultCache()
        : allocator_(ObMemAttr("TokenizeCache")),
          fulltext_(),
          parser_name_(),
          raw_json_(),
          meta_type_(ObNullType),
          collation_type_(CS_TYPE_INVALID),
          is_valid_(false)
    {
    }

    void reuse()
    {
      allocator_.reuse();
      fulltext_.reset();
      parser_name_.reset();
      raw_json_.reset();
      meta_type_ = ObNullType;
      collation_type_ = CS_TYPE_INVALID;
      is_valid_ = false;
    }

    ObArenaAllocator allocator_;
    ObString fulltext_;
    ObString parser_name_;
    ObString raw_json_;
    ObObjType meta_type_;
    ObCollationType collation_type_;
    bool is_valid_;
  };
  static thread_local BuiltinTokenizeResultCache result_cache;
  static constexpr int64_t MAX_CACHEABLE_TEXT_LENGTH = 4 * 1024;
  static constexpr int64_t MAX_CACHEABLE_RESULT_LENGTH = 64 * 1024;

  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &temp_allocator = tmp_alloc_g.get_allocator();

  ObIJsonBase *json_result = nullptr;
  TokenizeParam param;
  ObString raw_parser_name;
  bool cache_candidate = false;
  bool cache_hit = false;

  // check param num, which is checked in ObExprOperator::calc_result_typeN.
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (FALSE_IT(cache_candidate = expr.arg_cnt_ < 3
                                         && expr.args_[0]->is_const_expr()
                                         && (expr.arg_cnt_ < 2
                                             || expr.args_[1]->is_const_expr()))) {
  } else if (cache_candidate) {
    ObDatum *parser_datum = nullptr;
    if (OB_FAIL(parse_fulltext(expr, ctx, param))) {
      LOG_WARN("Fail to parse constant fulltext.", K(ret));
    } else if (expr.arg_cnt_ < 2) {
      raw_parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);
    } else if (OB_FAIL(expr.args_[1]->eval(ctx, parser_datum))) {
      LOG_WARN("Fail to eval constant parser name.", K(ret));
    } else {
      raw_parser_name = parser_datum->is_null()
                            ? ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME)
                            : parser_datum->get_string().trim();
    }
    if (OB_SUCC(ret)) {
      cache_candidate = param.fulltext_.length() <= MAX_CACHEABLE_TEXT_LENGTH;
      cache_hit = cache_candidate
                  && result_cache.is_valid_
                  && result_cache.meta_type_ == param.meta_.get_type()
                  && result_cache.collation_type_ == param.meta_.get_collation_type()
                  && result_cache.parser_name_ == raw_parser_name
                  && result_cache.fulltext_ == param.fulltext_;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (cache_hit) {
    if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(
            expr, ctx, expr_datum, result_cache.raw_json_))) {
      LOG_WARN("fail to pack cached tokenize result", K(ret));
    }
  } else if (OB_FAIL(parse_param(expr, ctx, temp_allocator, param))) {
    LOG_WARN("Fail to parse param", K(ret));
  } else if (OB_FAIL(tokenize_fulltext(param, param.output_mode_, temp_allocator, json_result))) {
    LOG_WARN("Fail to tokenize fulltext", K(ret));
  } else {
    ObString raw_json;
    if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(
            json_result, raw_json, &temp_allocator))) {
      LOG_WARN("fail to serialize tokenize result", K(ret));
    } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(
                   expr, ctx, expr_datum, raw_json))) {
      LOG_WARN("fail to pack tokenize result", K(ret));
    } else if (cache_candidate
               && raw_json.length() <= MAX_CACHEABLE_RESULT_LENGTH) {
      storage::ObFTParser parser;
      const int parser_ret
          = parser.parse_from_str(param.parser_name_.ptr(), param.parser_name_.length());
      const bool is_builtin = OB_SUCCESS == parser_ret
                              && (parser.is_space() || parser.is_ngram()
                                  || parser.is_beng() || parser.is_ik()
                                  || parser.is_ngram2());
      if (is_builtin) {
        result_cache.reuse();
        int cache_ret = OB_SUCCESS;
        if (OB_SUCCESS != (cache_ret = ob_write_string(
                result_cache.allocator_, param.fulltext_, result_cache.fulltext_))) {
        } else if (OB_SUCCESS != (cache_ret = ob_write_string(
                       result_cache.allocator_, raw_parser_name, result_cache.parser_name_))) {
        } else if (OB_SUCCESS != (cache_ret = ob_write_string(
                       result_cache.allocator_, raw_json, result_cache.raw_json_))) {
        } else {
          result_cache.meta_type_ = param.meta_.get_type();
          result_cache.collation_type_ = param.meta_.get_collation_type();
          result_cache.is_valid_ = true;
        }
        if (OB_UNLIKELY(OB_SUCCESS != cache_ret)) {
          result_cache.reuse();
          LOG_DEBUG("fail to cache tokenize result", K(cache_ret));
        }
      }
    }
  }

  return ret;
}

int ObExprTokenize::tokenize_fulltext(const TokenizeParam &param,
                                      TokenizeParam::OUTPUT_MODE mode,
                                      ObIAllocator &allocator,
                                      ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  storage::ObFTParseHelper tokenize_helper;
  const int64_t ft_word_bkt_cnt = MIN(MAX(param.fulltext_.length() / 2, 2), 997);
  int64_t doc_len = 0;
  ObFTWordMap token_map;

  ObArenaAllocator tmp_parse_alloc(ObMemAttr("Tmp buffer"));

  if (TokenizeParam::OUTPUT_MODE::DEFAULT != mode && TokenizeParam::OUTPUT_MODE::ALL != mode) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid output mode", K(ret), K(mode));
  } else if (OB_FAIL(tokenize_helper.init(&allocator, param.parser_name_, param.properties_))) {
    LOG_WARN("Fail to init tokenize helper", K(ret));
  } else if (OB_FAIL(token_map.create(ft_word_bkt_cnt, common::ObMemAttr("FTWordMap")))) {
    LOG_WARN("Fail to create token map", K(ret));
  } else if (
      (0 != param.fulltext_.length())
      && OB_FAIL(tokenize_helper.segment(
                     param.meta_,
                     param.fulltext_.ptr(),
                     param.fulltext_.length(),
                     doc_len,
                     token_map))) {
    LOG_WARN("Fail to segment fulltext", K(ret));
  } else {
    switch (param.output_mode_) {
    case TokenizeParam::OUTPUT_MODE::DEFAULT: {
      if (OB_FAIL(tokenize_helper.make_token_array_json(token_map, result))) {
        LOG_WARN("Fail to construct json array", K(ret));
      } else {
        // pass
      }
      break;
    }
    case TokenizeParam::OUTPUT_MODE::ALL: {
      if (OB_FAIL(tokenize_helper.make_detail_json(token_map, doc_len, result))) {
        LOG_WARN("Fail to construct detaild json", K(ret));
      } else {
        // pass
      }
      break;
    }
    default:
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Invalid output mode", K(ret), K(param.output_mode_));
    }
  }
  return ret;
}

ObExprTokenize::TokenizeParam ::TokenizeParam()
  : allocator_(ObMemAttr("TokenizeParam")),
    parser_name_(ObString(OB_DEFAULT_FULLTEXT_PARSER_NAME)),
    meta_(),
    fulltext_(),
    output_mode_(OUTPUT_MODE::DEFAULT)
{
}

int ObExprTokenize::TokenizeParam::parse_json_param(const ObIJsonBase *obj)
{
  int ret = OB_SUCCESS;
  ObString str;
  ObIJsonBase *val;

  if (OB_UNLIKELY(nullptr == obj)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Json param is null.", K(ret));
  } else if (ObJsonNodeType::J_OBJECT != obj->json_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Json args should be an object", K(ret));
  } else if (obj->element_count() == 0) {
    // no data
  } else if (OB_FAIL(obj->get_object_value(0, str, val))) {
    LOG_WARN("Failed to take para key from json object.", K(ret));
  } else if (0 == str.case_compare(CASE_INDICATOR_STR)) {
    if (ObJsonNodeType::J_STRING != val->json_type()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Json argument invalid", K(ret));
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("UPPER")) {
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("LOWER")) {
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Case indentifier not valid", K(ret));
    }
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "case indentifier");
  } else if (0 == str.case_compare(OUTPUT_MODE_STR)) {
    if (ObJsonNodeType::J_STRING != val->json_type()) {
      LOG_WARN("Json argument invalid", K(ret));
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "output mode should be string default or all");
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("DEFAULT")) {
      output_mode_ = DEFAULT;
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("ALL")) {
      output_mode_ = ALL;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "output mode should be string default or all");
    }
  } else if (0 == str.case_compare(STOPWORDS_LIST_STR)) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "stopwords");
  } else if (0 == str.case_compare(ADDITIONAL_ARGS_STR)) {
    if (ObJsonNodeType::J_ARRAY != val->json_type()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Additional args should be an array", K(ret));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser arguments");
    } else {
      ObString json_str;
      if (OB_FAIL(ObFTParserJsonProps::tokenize_array_to_props_json(allocator_, val, json_str))) {
        LOG_WARN("Fail to tokenize array to props json", K(ret));
        ObSqlString message;
        message.append_fmt("format in %s form", ADDITIONAL_ARGS_STR);
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, message.ptr());
      } else {
        properties_ = json_str;
      }
    }
  } else {
    LOG_WARN("Unsupported parameter", K(ret), K(str));
    ret = OB_INVALID_ARGUMENT;
    ObSqlString message;
    message.append_fmt("config: %s", str.ptr());
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, message.ptr());
  }
  return ret;
}

int ObExprTokenize::parse_param(const ObExpr &expr,
                                ObEvalCtx &ctx,
                                common::ObArenaAllocator &allocator,
                                TokenizeParam &param)
{
  int ret = OB_SUCCESS;

  ObDatum *parser_params_datum;
  ObString raw_parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);

  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator(), expr.type_, ret);

  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(parse_fulltext(expr, ctx, param))) {
    LOG_WARN("Fail to parse fulltext.", K(ret));
  } else if (OB_FAIL(parse_parser_name(expr, ctx, param))) {
    LOG_WARN("Fail to parse parser params.", K(ret));
  } else if (OB_FAIL(parse_parser_properties(expr, ctx, temp_allocator, param))) {
    LOG_WARN("Fail to parse parser params.", K(ret));
  // FTS PERF OPT 16: TOKENIZE(text, parser) has no user properties. The
  // parser property object already supplies the same built-in defaults, so
  // avoid materializing default JSON and parsing it again in the helper.
  // Keep the complete normalization path for an explicit third argument.
  } else if (expr.arg_cnt_ >= 3
             && OB_FAIL(param.reform_parser_properties(param.properties_))) {
    LOG_WARN("Fail to reform parser params.", K(ret));
  } else if (expr.arg_cnt_ >= 3 && OB_ISNULL(ctx.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is null", K(ret));
  } else if (expr.arg_cnt_ >= 3
             && OB_FAIL(param.normalize_ik_dict_table_names(
                            ctx.exec_ctx_.get_my_session()->get_database_name()))) {
    LOG_WARN("Fail to normalize ik dict table names.", K(ret));
  } else if (OB_FAIL(param.try_load_dictionary_for_ik())) {
    LOG_WARN("fail to try load dictionary for ik", K(ret));
  }
  return ret;
}

int ObExprTokenize::construct_ft_parser_inner_name(const ObString &input_str, TokenizeParam &param)
{
  int ret = OB_SUCCESS;
  // make an extract parser name
  share::ObPluginName plugin_name;
  storage::ObFTParser parser;

  char *parser_name_buf = nullptr;
  if (OB_ISNULL(parser_name_buf
                = static_cast<char *>(param.allocator_.alloc(OB_PLUGIN_NAME_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Fail to alloc memory", K(ret));
  } else if (OB_FAIL(plugin_name.set_name(input_str))) {
    LOG_WARN("Fail to set plugin name", K(ret));
  } else if (OB_FAIL(plugin::ObPluginHelper::find_ftparser(input_str, parser))) {
    LOG_WARN("Fail to get ft parser", K(ret));
  } else if (OB_FAIL(parser.serialize_to_str(parser_name_buf, OB_PLUGIN_NAME_LENGTH))) {
    LOG_WARN("Fail to parse ft parser name", K(ret));
  } else {
    param.parser_name_ = ObString::make_string(parser_name_buf);
  }
  return ret;
}

int ObExprTokenize::calc_result_typeN(ObExprResType &type,
                                      ObExprResType *types,
                                      int64_t param_num,
                                      common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(param_num < 1 || param_num > 3)) {
    ret = OB_ERR_PARAM_SIZE;
    ObString expr_name(N_TOKENIZE);
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, expr_name.length(), expr_name.ptr());
  } else {
    // just okay
  }

  ObLength length = ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType].get_length();

  if (OB_SUCC(ret)) {
    // set res type
    type.set_json();
    type.set_length(length); // keep consistent with other json expr, maybe calc it later.

    // param type set, skip charset after first param
    for (int64_t i = 1; OB_SUCC(ret) && i < param_num; ++i) {
      if (ob_is_string_type(types[i].get_type())) {
        if (types[i].get_charset_type() != CHARSET_UTF8MB4) {
          types[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        }
      }
    }

    // handle param
    if (param_num >= 2) {
      types[1].set_varchar();
    }

    if (param_num >= 3) {
      if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types, 2, N_TOKENIZE))) {
        LOG_WARN("wrong type for json doc.", K(ret), K(types[2].get_type()));
      }
    }
  }

  return ret;
}

int ObExprTokenize::cg_expr(ObExprCGCtx &op_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  CK((rt_expr.arg_cnt_ >= 1 && rt_expr.arg_cnt_ <= 3));
  if (OB_SUCC(ret)) {
    // do register
    rt_expr.eval_func_ = eval_tokenize;
  }
  return ret;
}

int ObExprTokenize::parse_fulltext(const ObExpr &expr, ObEvalCtx &ctx, TokenizeParam &param)
{
  int ret = OB_SUCCESS;

  ObDatum *fulltext_datum;

  if (OB_FAIL(expr.args_[0]->eval(ctx, fulltext_datum))) {
    LOG_WARN("Fail to eval fulltext.", K(ret));
  } else {
    if (fulltext_datum->is_null()) {
      // do nothing, return empty result
      param.fulltext_ = ObString::make_empty_string();
    } else {
      param.fulltext_ = fulltext_datum->get_string();
    }
    param.meta_.set_varchar(); // as we hardcoded in fts_index
    param.meta_.set_collation_type(expr.args_[0]->obj_meta_.get_collation_type());
  }
  return ret;
}

int ObExprTokenize::parse_parser_name(const ObExpr &expr, ObEvalCtx &ctx, TokenizeParam &param)
{
  int ret = OB_SUCCESS;
  ObDatum *parser_datum = nullptr;
  ObString raw_parser_name;

  if (expr.arg_cnt_ < 2) {
    raw_parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, parser_datum))) {
    LOG_WARN("Fail to eval parser name.", K(ret));
  } else {
    if (parser_datum->is_null()) {
      raw_parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);
    } else {
      ObString name = parser_datum->get_string();
      raw_parser_name = name.trim();
    }
  }

  if (OB_FAIL(ret)) {
    // already logged
  } else if (OB_FAIL(construct_ft_parser_inner_name(raw_parser_name, param))) {
    LOG_WARN("Fail to construct ft parser inner name.", K(ret));
  }

  return ret;
}

int ObExprTokenize::parse_parser_properties(const ObExpr &expr,
                                            ObEvalCtx &ctx,
                                            MultimodeAlloctor &mm_alloc,
                                            TokenizeParam &param)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *base = nullptr;

  if (expr.arg_cnt_ < 3) {
    // do nothing
  } else {
    bool is_null = false;
    if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, mm_alloc, 2, base, is_null))) {
      LOG_WARN("Fail to get json doc", K(ret));
    } else {
      if (ObJsonNodeType::J_ARRAY != base->json_type()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Invalid json type", K(ret), K(base->json_type()));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser args should be in an array.");
      } else {
        for (uint64_t i = 0; OB_SUCC(ret) && i < base->element_count(); ++i) {
          ObIJsonBase *node = nullptr;
          if (OB_FAIL(base->get_array_element(i, node))) {
            LOG_WARN("Failed to get array element", K(ret));
          } else if (ObJsonNodeType::J_OBJECT != (node->json_type())) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("Argument of json array invalid", K(ret));
          } else if (OB_FAIL(param.parse_json_param(node))) {
            LOG_WARN("Failed to parse json object", K(ret));
          }
        } // for
      }
    }
  }

  return ret;
}

int ObExprTokenize::TokenizeParam::reform_parser_properties(const ObString &properties)
{
  int ret = OB_SUCCESS;
  storage::ObFTParserJsonProps parser_properties;

  if (OB_FAIL(parser_properties.init())) {
    LOG_WARN("fail to init parser properties", K(ret));
  } else if (OB_FAIL(parser_properties.parse_from_valid_str(properties))) {
    LOG_WARN("fail to parse properties", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser properties invalid.");
  } else if (OB_FAIL(parser_properties.rebuild_props_for_ddl(parser_name_,
                                                             ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                                             true))) {
    LOG_WARN("fail to serialize to string", K(ret), K(parser_properties));
  } else if (OB_FAIL(parser_properties.to_format_json(allocator_, properties_))) {
    LOG_WARN("fail to serialize to string", K(ret), K(parser_properties));
  }

  return ret;
}

int ObExprTokenize::TokenizeParam::normalize_ik_dict_table_names(const ObString &default_database_name)
{
  int ret = OB_SUCCESS;
  bool need_to_load_dic = false;
  storage::ObFTParserJsonProps parser_properties;
  ObString dict_table;
  ObString stopword_table;
  ObString quantifier_table;
  ObString normalized_table;
  bool is_custom_table = false;
  bool has_custom_table = false;

  if (OB_FAIL(share::ObFtsIndexBuilderUtil::check_need_to_load_dic(parser_name_, need_to_load_dic))) {
    LOG_WARN("fail to check need to load dic", K(ret), K(parser_name_));
  } else if (!need_to_load_dic) {
  } else if (OB_FAIL(parser_properties.init())) {
    LOG_WARN("fail to init parser properties", K(ret));
  } else if (OB_FAIL(parser_properties.parse_from_valid_str(properties_))) {
    LOG_WARN("fail to parse parser properties", K(ret), K(properties_));
  } else if (OB_FAIL(parser_properties.config_get_dict_table(dict_table))) {
    LOG_WARN("fail to get dict_table parser property", K(ret));
  } else if (OB_FAIL(share::ObFtsIndexBuilderUtil::normalize_fulltext_dict_table_name(default_database_name,
                                                                                      allocator_,
                                                                                      dict_table,
                                                                                      normalized_table,
                                                                                      is_custom_table))) {
    LOG_WARN("fail to normalize dict_table parser property", K(ret), K(dict_table));
  } else if (is_custom_table
             && OB_FAIL(parser_properties.config_set_dict_table(normalized_table))) {
    LOG_WARN("fail to set normalized dict_table parser property", K(ret), K(normalized_table));
  } else if (FALSE_IT(has_custom_table = has_custom_table || is_custom_table)) {
  } else if (OB_FAIL(parser_properties.config_get_stopword_table(stopword_table))) {
    LOG_WARN("fail to get stopword_table parser property", K(ret));
  } else if (OB_FAIL(share::ObFtsIndexBuilderUtil::normalize_fulltext_dict_table_name(default_database_name,
                                                                                      allocator_,
                                                                                      stopword_table,
                                                                                      normalized_table,
                                                                                      is_custom_table))) {
    LOG_WARN("fail to normalize stopword_table parser property", K(ret), K(stopword_table));
  } else if (is_custom_table
             && OB_FAIL(parser_properties.config_set_stopword_table(normalized_table))) {
    LOG_WARN("fail to set normalized stopword_table parser property", K(ret), K(normalized_table));
  } else if (FALSE_IT(has_custom_table = has_custom_table || is_custom_table)) {
  } else if (OB_FAIL(parser_properties.config_get_quantifier_table(quantifier_table))) {
    LOG_WARN("fail to get quantifier_table parser property", K(ret));
  } else if (OB_FAIL(share::ObFtsIndexBuilderUtil::normalize_fulltext_dict_table_name(default_database_name,
                                                                                      allocator_,
                                                                                      quantifier_table,
                                                                                      normalized_table,
                                                                                      is_custom_table))) {
    LOG_WARN("fail to normalize quantifier_table parser property", K(ret), K(quantifier_table));
  } else if (is_custom_table
             && OB_FAIL(parser_properties.config_set_quantifier_table(normalized_table))) {
    LOG_WARN("fail to set normalized quantifier_table parser property", K(ret), K(normalized_table));
  } else if (FALSE_IT(has_custom_table = has_custom_table || is_custom_table)) {
  } else if (has_custom_table
             && OB_FAIL(parser_properties.to_format_json(allocator_, properties_))) {
    LOG_WARN("fail to serialize normalized parser properties", K(ret), K(parser_properties));
  }

  return ret;
}

int ObExprTokenize::TokenizeParam::try_load_dictionary_for_ik()
{
  int ret = OB_SUCCESS;
  bool need_to_load_dic = false;
  ObTenantDicLoaderHandle dic_loader_handle;
  if (OB_FAIL(share::ObFtsIndexBuilderUtil::check_need_to_load_dic(parser_name_,
                                                                   need_to_load_dic))) {
    LOG_WARN("fail to check need to load dic",
        K(ret), K(parser_name_), K(need_to_load_dic));
  } else if (need_to_load_dic) {
    if (OB_FAIL(ObGenDicLoader::get_instance().get_dic_loader(
                    ObString::make_string(ObFTSLiteral::PARSER_NAME_IK), // currently only ik, use parser_name_ without version suffix
                    ObCharset::charset_type_by_coll(meta_.get_collation_type()),
                    dic_loader_handle))) {
      LOG_WARN("fail to get dic loader", K(ret));
    } else if (OB_UNLIKELY(!dic_loader_handle.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dic loader handle is not valid", K(ret), K(dic_loader_handle));
    } else if (OB_FAIL(dic_loader_handle.get_loader()->try_load_dictionary_in_trans())) {
      LOG_WARN("fail to try load dictionary", K(ret), K(dic_loader_handle));
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
