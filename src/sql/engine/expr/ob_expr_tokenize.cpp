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
#include "share/ob_json_access_utils.h"
#include "storage/fts/dict/ob_ft_cache.h"
#include "storage/fts/dict/ob_ft_dict_hub.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_plugin_helper.h"

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_json_func_helper.h" // file not self-contained, there're logs inside.

namespace oceanbase
{
namespace sql
{
namespace
{
class ObBuiltinTokenizeParserCache final
{
public:
  ObBuiltinTokenizeParserCache()
      : allocator_(ObMemAttr("TokParserCache")),
        helper_(),
        dictionary_epoch_(0),
        parser_name_length_(0)
  {
    MEMSET(parser_name_, 0, sizeof(parser_name_));
  }

  int get_helper(const ObString &parser_name,
                 const uint64_t dictionary_epoch,
                 storage::ObFTParseHelper *&helper)
  {
    int ret = OB_SUCCESS;
    helper = nullptr;
    if (OB_UNLIKELY(parser_name.empty()
                    || parser_name.length() >= static_cast<int64_t>(sizeof(parser_name_)))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid cached parser name", K(ret), K(parser_name.length()));
    } else if (!matches(parser_name, dictionary_epoch)) {
      helper_.reset();
      allocator_.reset();
      parser_name_length_ = 0;
      if (OB_FAIL(helper_.init(&allocator_, parser_name, ObString()))) {
        LOG_WARN("failed to initialize cached tokenize parser", K(ret), K(parser_name));
      } else {
        MEMCPY(parser_name_, parser_name.ptr(), parser_name.length());
        dictionary_epoch_ = dictionary_epoch;
        parser_name_length_ = parser_name.length();
      }
    }
    if (OB_SUCC(ret)) {
      helper = &helper_;
    }
    return ret;
  }

private:
  bool matches(const ObString &parser_name, const uint64_t dictionary_epoch) const
  {
    return dictionary_epoch_ == dictionary_epoch
        && parser_name.length() == parser_name_length_
        && 0 == MEMCMP(parser_name.ptr(), parser_name_, parser_name_length_);
  }

  ObArenaAllocator allocator_;
  storage::ObFTParseHelper helper_;
  uint64_t dictionary_epoch_;
  char parser_name_[OB_PLUGIN_NAME_LENGTH];
  int64_t parser_name_length_;

  DISALLOW_COPY_AND_ASSIGN(ObBuiltinTokenizeParserCache);
};

class ObTokenizeResultCache final
{
public:
  ObTokenizeResultCache()
      : allocator_(ObMemAttr("TokResultCache")),
        dictionary_epoch_(0),
        collation_type_(CS_TYPE_INVALID),
        output_mode_(0),
        parser_name_(),
        parser_properties_(),
        fulltext_(),
        result_(),
        is_valid_(false)
  {
  }

  bool matches(const ObString &parser_name,
               const ObString &parser_properties,
               const ObString &fulltext,
               const ObCollationType collation_type,
               const int64_t output_mode,
               const uint64_t dictionary_epoch) const
  {
    return is_valid_
        && dictionary_epoch_ == dictionary_epoch
        && collation_type_ == collation_type
        && output_mode_ == output_mode
        && string_equal(parser_name_, parser_name)
        && string_equal(parser_properties_, parser_properties)
        && string_equal(fulltext_, fulltext);
  }

  bool matches_default_raw(const ObString &raw_parser_name,
                           const ObString &fulltext,
                           const ObCollationType collation_type,
                           const uint64_t dictionary_epoch) const
  {
    const bool parser_matches = 0 == parser_name_.case_compare(raw_parser_name)
        || (parser_name_.length() > raw_parser_name.length()
            && parser_name_.prefix_match_ci(raw_parser_name)
            && '.' == parser_name_.ptr()[raw_parser_name.length()]);
    return is_valid_
        && dictionary_epoch_ == dictionary_epoch
        && collation_type_ == collation_type
        && 0 == output_mode_
        && parser_properties_.empty()
        && parser_matches
        && string_equal(fulltext_, fulltext);
  }

  int copy_result(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result) const
  {
    int ret = OB_SUCCESS;
    char *result_buf = nullptr;
    if (OB_UNLIKELY(!is_valid_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("tokenize result cache is not initialized", K(ret));
    } else if (OB_ISNULL(result_buf = expr.get_str_res_mem(ctx, result_.length()))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate tokenize result", K(ret), K(result_.length()));
    } else {
      MEMCPY(result_buf, result_.ptr(), result_.length());
      result.set_string(result_buf, result_.length());
    }
    return ret;
  }

  int store(const ObString &parser_name,
            const ObString &parser_properties,
            const ObString &fulltext,
            const ObCollationType collation_type,
            const int64_t output_mode,
            const uint64_t dictionary_epoch,
            const ObDatum &result)
  {
    int ret = OB_SUCCESS;
    allocator_.reset();
    is_valid_ = false;
    if (OB_FAIL(ob_write_string(allocator_, parser_name, parser_name_))) {
      LOG_WARN("failed to cache tokenize parser name", K(ret));
    } else if (OB_FAIL(ob_write_string(allocator_, parser_properties, parser_properties_))) {
      LOG_WARN("failed to cache tokenize parser properties", K(ret));
    } else if (OB_FAIL(ob_write_string(allocator_, fulltext, fulltext_))) {
      LOG_WARN("failed to cache tokenize fulltext", K(ret));
    } else if (OB_FAIL(ob_write_string(allocator_, result.get_string(), result_))) {
      LOG_WARN("failed to cache tokenize result", K(ret));
    } else {
      dictionary_epoch_ = dictionary_epoch;
      collation_type_ = collation_type;
      output_mode_ = output_mode;
      is_valid_ = true;
    }
    return ret;
  }

private:
  static bool string_equal(const ObString &left, const ObString &right)
  {
    return left.length() == right.length()
        && (left.empty() || 0 == MEMCMP(left.ptr(), right.ptr(), left.length()));
  }

  ObArenaAllocator allocator_;
  uint64_t dictionary_epoch_;
  ObCollationType collation_type_;
  int64_t output_mode_;
  ObString parser_name_;
  ObString parser_properties_;
  ObString fulltext_;
  ObString result_;
  bool is_valid_;

  DISALLOW_COPY_AND_ASSIGN(ObTokenizeResultCache);
};

ObBuiltinTokenizeParserCache &get_builtin_tokenize_parser_cache()
{
  static thread_local ObBuiltinTokenizeParserCache parser_cache;
  return parser_cache;
}

ObTokenizeResultCache &get_tokenize_result_cache()
{
  static thread_local ObTokenizeResultCache result_cache;
  return result_cache;
}

int get_dictionary_epoch(uint64_t &dictionary_epoch)
{
  int ret = OB_SUCCESS;
  storage::ObFTDictHub *dict_hub = nullptr;
  if (OB_FAIL(storage::ObFTParsePluginData::instance().get_dict_hub(dict_hub))) {
    LOG_WARN("failed to get fulltext dictionary hub", K(ret));
  } else {
    dictionary_epoch = dict_hub->get_dictionary_epoch();
  }
  return ret;
}

int try_copy_cached_default_result(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   const uint64_t dictionary_epoch,
                                   ObDatum &result,
                                   bool &cache_hit)
{
  int ret = OB_SUCCESS;
  ObDatum *fulltext_datum = nullptr;
  ObDatum *parser_datum = nullptr;
  ObString fulltext;
  ObString parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);
  cache_hit = false;
  if (expr.arg_cnt_ > 2) {
    // Parser properties can change output semantics, so use the canonical cache path.
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, fulltext_datum))) {
    LOG_WARN("failed to evaluate cached tokenize fulltext", K(ret));
  } else if (OB_ISNULL(fulltext_datum)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tokenize fulltext datum is null", K(ret));
  } else {
    if (!fulltext_datum->is_null()) {
      fulltext = fulltext_datum->get_string();
    }
    if (expr.arg_cnt_ >= 2) {
      if (OB_FAIL(expr.args_[1]->eval(ctx, parser_datum))) {
        LOG_WARN("failed to evaluate cached tokenize parser", K(ret));
      } else if (OB_ISNULL(parser_datum)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tokenize parser datum is null", K(ret));
      } else if (!parser_datum->is_null()) {
        parser_name = parser_datum->get_string().trim();
      }
    }
    if (OB_SUCC(ret)
        && get_tokenize_result_cache().matches_default_raw(
            parser_name,
            fulltext,
            expr.args_[0]->obj_meta_.get_collation_type(),
            dictionary_epoch)) {
      if (OB_FAIL(get_tokenize_result_cache().copy_result(expr, ctx, result))) {
        LOG_WARN("failed to copy raw cached tokenize result", K(ret));
      } else {
        cache_hit = true;
      }
    }
  }
  return ret;
}

bool is_cacheable_builtin_parser(const ObString &parser_name, const ObString &properties)
{
  return properties.empty()
      && (parser_name.prefix_match_ci(ObString::make_string("ik."))
          || parser_name.prefix_match_ci(ObString::make_string("beng.")));
}

} // namespace

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

  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &temp_allocator = tmp_alloc_g.get_allocator();

  ObIJsonBase *json_result = nullptr;
  TokenizeParam param;
  uint64_t dictionary_epoch = 0;
  bool cache_hit = false;

  // check param num, which is checked in ObExprOperator::calc_result_typeN.
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(get_dictionary_epoch(dictionary_epoch))) {
    LOG_WARN("failed to get fulltext dictionary epoch", K(ret));
  } else if (OB_FAIL(try_copy_cached_default_result(expr,
                                                    ctx,
                                                    dictionary_epoch,
                                                    expr_datum,
                                                    cache_hit))) {
    LOG_WARN("failed to check raw tokenize result cache", K(ret));
  } else if (cache_hit) {
    // Cached result is already projected.
  } else if (OB_FAIL(parse_param(expr, ctx, temp_allocator, param))) {
    LOG_WARN("Fail to parse param", K(ret));
  } else if (get_tokenize_result_cache().matches(param.parser_name_,
                                                  param.properties_,
                                                  param.fulltext_,
                                                  param.meta_.get_collation_type(),
                                                  static_cast<int64_t>(param.output_mode_),
                                                  dictionary_epoch)) {
    if (OB_FAIL(get_tokenize_result_cache().copy_result(expr, ctx, expr_datum))) {
      LOG_WARN("failed to copy cached tokenize result", K(ret));
    }
  } else if (OB_FAIL(tokenize_fulltext(param,
                                       param.output_mode_,
                                       temp_allocator,
                                       dictionary_epoch,
                                       json_result))) {
    LOG_WARN("Fail to tokenize fulltext", K(ret));
  } else if (OB_FAIL(ObJsonExprHelper::pack_json_res(expr,
                                                     ctx,
                                                     temp_allocator,
                                                     json_result,
                                                     expr_datum))) {
    LOG_WARN("fail to pack json result", K(ret));
  } else {
    const int cache_ret = get_tokenize_result_cache().store(param.parser_name_,
                                                            param.properties_,
                                                            param.fulltext_,
                                                            param.meta_.get_collation_type(),
                                                            static_cast<int64_t>(param.output_mode_),
                                                            dictionary_epoch,
                                                            expr_datum);
    if (OB_SUCCESS != cache_ret) {
      LOG_WARN("failed to store tokenize result cache", K(cache_ret));
    }
  }

  return ret;
}

int ObExprTokenize::tokenize_fulltext(const TokenizeParam &param,
                                      TokenizeParam::OUTPUT_MODE mode,
                                      ObIAllocator &allocator,
                                      const uint64_t dictionary_epoch,
                                      ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  storage::ObFTParseHelper local_tokenize_helper;
  storage::ObFTParseHelper *tokenize_helper = &local_tokenize_helper;
  const bool use_cached_parser = is_cacheable_builtin_parser(param.parser_name_, param.properties_);
  const int64_t ft_word_bkt_cnt = MIN(MAX(param.fulltext_.length() / 2, 2), 997);
  int64_t doc_len = 0;
  ObFTWordMap token_map;
  const storage::ObFTTokenCacheValue *cached_value = nullptr;
  common::ObKVCacheHandle cache_handle;
  const storage::ObFTTokenCacheKey cache_key(1UL,
                                             dictionary_epoch,
                                             param.meta_.get_collation_type(),
                                             param.parser_name_,
                                             param.properties_,
                                             param.fulltext_);

  if (TokenizeParam::OUTPUT_MODE::DEFAULT != mode && TokenizeParam::OUTPUT_MODE::ALL != mode) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid output mode", K(ret), K(mode));
  } else if (use_cached_parser) {
    if (OB_FAIL(get_builtin_tokenize_parser_cache().get_helper(param.parser_name_,
                                                               dictionary_epoch,
                                                               tokenize_helper))) {
      LOG_WARN("Fail to get cached tokenize helper", K(ret), K(param.parser_name_));
    }
  } else if (OB_FAIL(local_tokenize_helper.init(&allocator,
                                                param.parser_name_,
                                                param.properties_))) {
    LOG_WARN("Fail to init tokenize helper", K(ret));
  }
  if (OB_SUCC(ret)
      && OB_FAIL(token_map.create(ft_word_bkt_cnt, common::ObMemAttr("FTWordMap")))) {
    LOG_WARN("Fail to create token map", K(ret));
  } else if (OB_SUCC(ret) && 0 != param.fulltext_.length()) {
    const int cache_ret = storage::ObFTTokenCache::get_instance().get_token(
        cache_key, cached_value, cache_handle);
    if (OB_SUCCESS == cache_ret) {
      doc_len = cached_value->get_document_length();
      if (OB_FAIL(cached_value->deserialize(allocator, param.meta_, token_map))) {
        LOG_WARN("failed to deserialize cached tokenize tokens", K(ret), K(cache_key));
      }
    } else if (OB_ENTRY_NOT_EXIST == cache_ret || OB_NOT_INIT == cache_ret) {
      if ((use_cached_parser
           && OB_FAIL(tokenize_helper->segment(param.meta_,
                                               param.fulltext_.ptr(),
                                               param.fulltext_.length(),
                                               allocator,
                                               doc_len,
                                               token_map)))
          || (!use_cached_parser
              && OB_FAIL(tokenize_helper->segment(param.meta_,
                                                  param.fulltext_.ptr(),
                                                  param.fulltext_.length(),
                                                  doc_len,
                                                  token_map)))) {
        LOG_WARN("Fail to segment fulltext", K(ret));
      } else {
        storage::ObFTTokenCacheValue cache_value;
        int tmp_ret = storage::ObFTTokenCacheValue::serialize(
            allocator, doc_len, token_map, cache_value);
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("failed to serialize tokenize tokens for cache", K(tmp_ret), K(doc_len));
        } else if (OB_SUCCESS != (tmp_ret = storage::ObFTTokenCache::get_instance().put_token(
                                      cache_key, cache_value))) {
          LOG_WARN("failed to cache tokenize tokens", K(tmp_ret), K(cache_key));
        }
      }
    } else {
      ret = cache_ret;
      LOG_WARN("failed to read tokenize token cache", K(ret), K(cache_key));
    }
  }
  if (OB_SUCC(ret)) {
    switch (param.output_mode_) {
    case TokenizeParam::OUTPUT_MODE::DEFAULT: {
      if (use_cached_parser
          && OB_FAIL(tokenize_helper->make_token_array_json(token_map, allocator, result))) {
        LOG_WARN("Fail to construct json array", K(ret));
      } else if (!use_cached_parser
                 && OB_FAIL(tokenize_helper->make_token_array_json(token_map, result))) {
        LOG_WARN("Fail to construct json array", K(ret));
      } else {
        // pass
      }
      break;
    }
    case TokenizeParam::OUTPUT_MODE::ALL: {
      if (use_cached_parser
          && OB_FAIL(tokenize_helper->make_detail_json(token_map, doc_len, allocator, result))) {
        LOG_WARN("Fail to construct detaild json", K(ret));
      } else if (!use_cached_parser
                 && OB_FAIL(tokenize_helper->make_detail_json(token_map, doc_len, result))) {
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
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();

  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(parse_fulltext(expr, ctx, param))) {
    LOG_WARN("Fail to parse fulltext.", K(ret));
  } else if (OB_FAIL(parse_parser_name(expr, ctx, param))) {
    LOG_WARN("Fail to parse parser params.", K(ret));
  } else if (OB_FAIL(parse_parser_properties(expr, ctx, param))) {
    LOG_WARN("Fail to parse parser params.", K(ret));
  } else if (!param.properties_.empty() && OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is null", K(ret));
  } else if (!param.properties_.empty()
             && OB_FAIL(param.reform_parser_properties(param.properties_,
                                                       session->get_database_name()))) {
    LOG_WARN("Fail to reform parser params.", K(ret));
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
                                            TokenizeParam &param)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *base = nullptr;

  if (expr.arg_cnt_ < 3) {
    // do nothing
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator(), expr.type_, ret);
    bool is_null = false;
    if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, 2, base, is_null))) {
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

int ObExprTokenize::TokenizeParam::reform_parser_properties(const ObString &properties,
                                                            const ObString &database_name)
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
  } else {
    storage::ObFTParser parser;
    if (OB_FAIL(parser.parse_from_str(parser_name_.ptr(), parser_name_.length()))) {
      LOG_WARN("fail to parse parser name", K(ret), K(parser_name_));
    } else if (parser.is_ik()
               && OB_FAIL(parser_properties.qualify_ik_dict_tables(database_name))) {
      LOG_WARN("fail to qualify IK dictionary table names", K(ret), K(database_name));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(parser_properties.to_format_json(allocator_, properties_))) {
      LOG_WARN("fail to serialize to string", K(ret), K(parser_properties));
    }
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase
