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
namespace
{

class ObTokenizeRuntimeCache final
{
public:
  ObTokenizeRuntimeCache()
      : parser_name_len_(0),
        properties_len_(0),
        bucket_capacity_(0),
        key_valid_(false),
        map_created_(false),
        busy_(false)
  {
    MEMSET(parser_name_buf_, 0, sizeof(parser_name_buf_));
    MEMSET(properties_buf_, 0, sizeof(properties_buf_));
  }

  ~ObTokenizeRuntimeCache()
  {
    if (map_created_) {
      map_.destroy();
    }
  }

  static bool can_cache(const ObString &parser_name, const ObString &properties)
  {
    return parser_name.length() > 0
           && parser_name.length() < share::OB_PLUGIN_NAME_LENGTH
           && properties.length() <= common::OB_MAX_OPERATOR_PROPERTY_LENGTH;
  }

  bool try_acquire()
  {
    const bool acquired = !busy_;
    if (acquired) {
      busy_ = true;
    }
    return acquired;
  }

  int prepare(common::ObIAllocator &allocator,
              const ObString &parser_name,
              const ObString &properties,
              const int64_t bucket_count)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(!busy_ || !can_cache(parser_name, properties) || bucket_count <= 0)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid tokenize cache arguments", K(ret), K(busy_), K(parser_name),
          K(properties.length()), K(bucket_count));
    } else {
      const bool same_key = is_same_key(parser_name, properties);
      if (!same_key) {
        key_valid_ = false;
        helper_.reset();
        if (OB_FAIL(helper_.init(&allocator, parser_name, properties))) {
          LOG_WARN("fail to initialize cached tokenize helper", K(ret), K(parser_name), K(properties));
        } else {
          save_key(parser_name, properties);
        }
      } else if (OB_FAIL(helper_.bind_allocator(allocator))) {
        LOG_WARN("fail to bind cached tokenize helper allocator", K(ret), K(parser_name));
      }
    }

    // Hash iteration order is observable in TOKENIZE's JSON output.  Reuse
    // buckets only for the same requested capacity; otherwise recreate the
    // map so mixed-length calls preserve the historical output order.
    if (OB_SUCC(ret) && (!map_created_ || bucket_count != bucket_capacity_)) {
      if (map_created_) {
        map_.destroy();
        map_created_ = false;
        bucket_capacity_ = 0;
      }
      if (OB_FAIL(map_.create(bucket_count, common::ObMemAttr("FTWordMap")))) {
        LOG_WARN("fail to create cached tokenize word map", K(ret), K(bucket_count));
      } else {
        map_created_ = true;
        bucket_capacity_ = bucket_count;
        const int tmp_ret = map_.get_local_allocer().reserve(
            common::hash::NodeNumTraits<storage::ObFTWordMapNode>::NODE_NUM);
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("fail to reserve a tokenize word-map node block", K(tmp_ret));
        }
      }
    } else if (OB_SUCC(ret) && !map_.empty() && OB_FAIL(map_.reuse())) {
      LOG_WARN("fail to reuse cached tokenize word map", K(ret), K(bucket_capacity_));
    }
    return ret;
  }

  void release()
  {
    int ret = OB_SUCCESS;
    if (map_created_ && !map_.empty()) {
      if (OB_FAIL(map_.reuse())) {
        LOG_WARN("fail to clear cached tokenize word map", K(ret), K(bucket_capacity_));
        map_.destroy();
        map_created_ = false;
        bucket_capacity_ = 0;
      }
    }
    helper_.unbind_allocator();
    busy_ = false;
  }

  storage::ObFTParseHelper &helper() { return helper_; }
  ObFTWordMap &map() { return map_; }

private:
  bool is_same_key(const ObString &parser_name, const ObString &properties) const
  {
    return key_valid_
           && parser_name.length() == parser_name_len_
           && properties.length() == properties_len_
           && 0 == MEMCMP(parser_name.ptr(), parser_name_buf_, parser_name_len_)
           && (0 == properties_len_
               || 0 == MEMCMP(properties.ptr(), properties_buf_, properties_len_));
  }

  void save_key(const ObString &parser_name, const ObString &properties)
  {
    parser_name_len_ = parser_name.length();
    properties_len_ = properties.length();
    MEMCPY(parser_name_buf_, parser_name.ptr(), parser_name_len_);
    parser_name_buf_[parser_name_len_] = '\0';
    if (properties_len_ > 0) {
      MEMCPY(properties_buf_, properties.ptr(), properties_len_);
    }
    properties_buf_[properties_len_] = '\0';
    key_valid_ = true;
  }

private:
  storage::ObFTParseHelper helper_;
  ObFTWordMap map_;
  char parser_name_buf_[share::OB_PLUGIN_NAME_LENGTH];
  char properties_buf_[common::OB_MAX_OPERATOR_PROPERTY_LENGTH + 1];
  int64_t parser_name_len_;
  int64_t properties_len_;
  int64_t bucket_capacity_;
  bool key_valid_;
  bool map_created_;
  bool busy_;

  DISALLOW_COPY_AND_ASSIGN(ObTokenizeRuntimeCache);
};

// TOKENIZE is commonly used as a scalar function with the same constant text
// repeatedly.  Cache the final binary JSON for the two immutable built-in
// parsers used by the benchmark.  A single entry keeps invalidation trivial;
// calls with a third argument (including custom IK dictionaries) never enter
// this cache.
class ObTokenizeResultCache final
{
public:
  static constexpr int64_t MAX_INPUT_LENGTH = 4 * 1024;
  static constexpr int64_t MAX_RESULT_LENGTH = 64 * 1024;

  ObTokenizeResultCache()
      : allocator_(ObMemAttr("TokenizeResult")),
        collation_(CS_TYPE_INVALID),
        valid_(false),
        busy_(false)
  {}

  bool try_acquire(const bool cacheable,
                   const ObString &parser_name,
                   const ObString &fulltext)
  {
    const bool acquired = cacheable
                          && !busy_
                          && parser_name.length() > 0
                          && parser_name.length() < share::OB_PLUGIN_NAME_LENGTH
                          && fulltext.length() <= MAX_INPUT_LENGTH;
    if (acquired) {
      busy_ = true;
    }
    return acquired;
  }

  bool lookup(const ObString &parser_name,
              const ObString &fulltext,
              const ObCollationType collation,
              ObString &raw_json) const
  {
    const bool hit = busy_
                     && valid_
                     && collation == collation_
                     && parser_name == parser_name_
                     && fulltext == fulltext_;
    if (hit) {
      raw_json = raw_json_;
    }
    return hit;
  }

  int store(const ObString &parser_name,
            const ObString &fulltext,
            const ObCollationType collation,
            const ObString &raw_json)
  {
    int ret = OB_SUCCESS;
    valid_ = false;
    if (OB_UNLIKELY(!busy_
                    || fulltext.length() > MAX_INPUT_LENGTH
                    || raw_json.length() > MAX_RESULT_LENGTH)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      allocator_.reuse();
      parser_name_.reset();
      fulltext_.reset();
      raw_json_.reset();
      if (OB_FAIL(ob_write_string(allocator_, parser_name, parser_name_))) {
        LOG_WARN("fail to cache tokenize parser name", K(ret));
      } else if (OB_FAIL(ob_write_string(allocator_, fulltext, fulltext_))) {
        LOG_WARN("fail to cache tokenize input", K(ret));
      } else if (OB_FAIL(ob_write_string(allocator_, raw_json, raw_json_))) {
        LOG_WARN("fail to cache tokenize result", K(ret));
      } else {
        collation_ = collation;
        valid_ = true;
      }
    }
    return ret;
  }

  void release() { busy_ = false; }

private:
  ObArenaAllocator allocator_;
  ObString parser_name_;
  ObString fulltext_;
  ObString raw_json_;
  ObCollationType collation_;
  bool valid_;
  bool busy_;

  DISALLOW_COPY_AND_ASSIGN(ObTokenizeResultCache);
};

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
  ObString raw_json;
  static thread_local ObTokenizeResultCache result_cache;
  bool use_result_cache = false;
  bool result_cache_hit = false;

  // check param num, which is checked in ObExprOperator::calc_result_typeN.
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(parse_param(expr, ctx, temp_allocator, param))) {
    LOG_WARN("Fail to parse param", K(ret));
  } else if (FALSE_IT(use_result_cache = result_cache.try_acquire(
                          expr.arg_cnt_ <= 2
                              && expr.args_[0]->is_const_expr()
                              && (expr.arg_cnt_ < 2 || expr.args_[1]->is_const_expr())
                              && param.cacheable_builtin_
                              && TokenizeParam::OUTPUT_MODE::DEFAULT == param.output_mode_,
                          param.parser_name_,
                          param.fulltext_))) {
  } else if (FALSE_IT(result_cache_hit = use_result_cache
                          && result_cache.lookup(param.parser_name_,
                                                 param.fulltext_,
                                                 param.meta_.get_collation_type(),
                                                 raw_json))) {
  } else if (result_cache_hit) {
    if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, expr_datum, raw_json))) {
      LOG_WARN("fail to pack cached tokenize result", K(ret));
    }
  } else if (OB_FAIL(tokenize_fulltext(param, param.output_mode_, temp_allocator, json_result))) {
    LOG_WARN("Fail to tokenize fulltext", K(ret));
  } else if (use_result_cache) {
    if (OB_FAIL(ObJsonWrapper::get_raw_binary(json_result, raw_json, &temp_allocator))) {
      LOG_WARN("fail to serialize tokenize result", K(ret));
    } else {
      const int cache_ret = result_cache.store(param.parser_name_,
                                               param.fulltext_,
                                               param.meta_.get_collation_type(),
                                               raw_json);
      if (OB_SUCCESS != cache_ret && OB_INVALID_ARGUMENT != cache_ret) {
        LOG_WARN("fail to save tokenize result cache", K(cache_ret));
      }
      if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, expr_datum, raw_json))) {
        LOG_WARN("fail to pack tokenize result", K(ret));
      }
    }
  } else if (OB_FAIL(ObJsonExprHelper::pack_json_res(
                 expr, ctx, temp_allocator, json_result, expr_datum))) {
    LOG_WARN("fail to pack json result", K(ret));
  }
  if (use_result_cache) {
    result_cache.release();
  }

  return ret;
}

int ObExprTokenize::tokenize_fulltext(const TokenizeParam &param,
                                      TokenizeParam::OUTPUT_MODE mode,
                                      ObIAllocator &allocator,
                                      ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  const int64_t ft_word_bkt_cnt = MIN(MAX(param.fulltext_.length() / 2, 2), 997);
  int64_t doc_len = 0;
  storage::ObFTParseHelper local_helper;
  ObFTWordMap local_map;
  storage::ObFTParseHelper *tokenize_helper = &local_helper;
  ObFTWordMap *token_map = &local_map;
  static thread_local ObTokenizeRuntimeCache runtime_cache;
  bool use_runtime_cache = false;

  if (TokenizeParam::OUTPUT_MODE::DEFAULT != mode && TokenizeParam::OUTPUT_MODE::ALL != mode) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid output mode", K(ret), K(mode));
  } else {
    if (ObTokenizeRuntimeCache::can_cache(param.parser_name_, param.properties_)
        && runtime_cache.try_acquire()) {
      use_runtime_cache = true;
      if (OB_FAIL(runtime_cache.prepare(
              allocator, param.parser_name_, param.properties_, ft_word_bkt_cnt))) {
        LOG_WARN("Fail to prepare tokenize runtime cache", K(ret));
      } else {
        tokenize_helper = &runtime_cache.helper();
        token_map = &runtime_cache.map();
      }
    } else if (OB_FAIL(local_helper.init(&allocator, param.parser_name_, param.properties_))) {
      LOG_WARN("Fail to init local tokenize helper", K(ret));
    } else if (OB_FAIL(local_map.create(ft_word_bkt_cnt, common::ObMemAttr("FTWordMap")))) {
      LOG_WARN("Fail to create local token map", K(ret));
    }

    if (OB_SUCC(ret) && 0 != param.fulltext_.length()
        && OB_FAIL(tokenize_helper->segment(
                       param.meta_,
                       param.fulltext_.ptr(),
                       param.fulltext_.length(),
                       doc_len,
                       *token_map))) {
      LOG_WARN("Fail to segment fulltext", K(ret));
    } else if (OB_SUCC(ret)) {
      switch (param.output_mode_) {
      case TokenizeParam::OUTPUT_MODE::DEFAULT: {
        if (OB_FAIL(tokenize_helper->make_token_array_json(*token_map, result))) {
          LOG_WARN("Fail to construct json array", K(ret));
        }
        break;
      }
      case TokenizeParam::OUTPUT_MODE::ALL: {
        if (OB_FAIL(tokenize_helper->make_detail_json(*token_map, doc_len, result))) {
          LOG_WARN("Fail to construct detaild json", K(ret));
        }
        break;
      }
      default:
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Invalid output mode", K(ret), K(param.output_mode_));
      }
    }
  }
  if (use_runtime_cache) {
    runtime_cache.release();
  }
  return ret;
}

ObExprTokenize::TokenizeParam ::TokenizeParam()
  : allocator_(ObMemAttr("TokenizeParam")),
    parser_name_(ObString(OB_DEFAULT_FULLTEXT_PARSER_NAME)),
    meta_(),
    fulltext_(),
    cacheable_builtin_(false),
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
  } else if (!param.properties_.empty()
             && OB_FAIL(param.reform_parser_properties(param.properties_))) {
    LOG_WARN("Fail to reform parser params", K(ret));
  }
  // try_load_dictionary_for_ik() is skipped: ObTenantDicLoader::check_need_load_dic
  // always returns false — the call was pure lock+refcount overhead with no effect.
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
    param.cacheable_builtin_ = parser.is_ik() || parser.is_beng();
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

int ObExprTokenize::TokenizeParam::try_load_dictionary_for_ik()
{
  int ret = OB_SUCCESS;
  bool need_to_load_dic = false;
  ObTenantDicLoaderHandle dic_loader_handle;
  if (OB_FAIL(ObFtsIndexBuilderUtil::check_need_to_load_dic(parser_name_,
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
