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
#include "lib/hash_func/murmur_hash.h"
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "object/ob_object.h"
#include "plugin/sys/ob_plugin_helper.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "sql/engine/expr/ob_expr_extra_info_factory.h"
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
static constexpr int64_t TOKENIZE_RESULT_CACHE_MIN_TEXT_LENGTH = 256;

OB_SERIALIZE_MEMBER(ObTokenizeFixedConfig,
                    is_valid_,
                    cacheable_,
                    output_mode_,
                    parser_version_,
                    collation_,
                    fixed_hash_,
                    parser_name_,
                    normalized_properties_);

int ObTokenizeFixedConfig::deep_copy(common::ObIAllocator &allocator,
                                     const ObExprOperatorType type,
                                     ObIExprExtraInfo *&copied_info) const
{
  int ret = OB_SUCCESS;
  ObTokenizeFixedConfig *other = nullptr;
  if (OB_FAIL(ObExprExtraInfoFactory::alloc(allocator, type, copied_info))) {
    LOG_WARN("failed to allocate tokenize fixed config", K(ret), K(type));
  } else if (OB_ISNULL(other = static_cast<ObTokenizeFixedConfig *>(copied_info))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null tokenize fixed config", K(ret));
  } else if (OB_FAIL(ob_write_string(allocator, parser_name_, other->parser_name_))) {
    LOG_WARN("failed to copy fixed parser name", K(ret), K(parser_name_));
  } else if (OB_FAIL(ob_write_string(allocator,
                                     normalized_properties_,
                                     other->normalized_properties_))) {
    LOG_WARN("failed to copy fixed parser properties", K(ret), K(normalized_properties_));
  } else {
    other->is_valid_ = is_valid_;
    other->cacheable_ = cacheable_;
    other->output_mode_ = output_mode_;
    other->parser_version_ = parser_version_;
    other->collation_ = collation_;
    other->fixed_hash_ = fixed_hash_;
  }
  return ret;
}

ObTokenizeResultCacheKey::ObTokenizeResultCacheKey(
    const uint64_t tenant_id,
    const ObString &tenant_name,
    const ObString &fulltext,
    const ObCollationType collation,
    const int8_t output_mode,
    const ObString &parser_name,
    const int64_t parser_version,
    const ObString &properties,
    const uint64_t fixed_hash)
    : tenant_id_(tenant_id),
      fulltext_hash_(murmurhash(fulltext.ptr(), fulltext.length(), 0)),
      fixed_hash_(fixed_hash),
      collation_(collation),
      output_mode_(output_mode),
      parser_version_(parser_version),
      tenant_name_(tenant_name),
      fulltext_(fulltext),
      parser_name_(parser_name),
      properties_(properties)
{
}

uint64_t ObTokenizeResultCacheKey::calc_fixed_hash(
    const ObCollationType collation,
    const int8_t output_mode,
    const ObString &parser_name,
    const int64_t parser_version,
    const ObString &properties)
{
  uint64_t hash_value = murmurhash(&collation, sizeof(collation), 0);
  hash_value = murmurhash(&output_mode, sizeof(output_mode), hash_value);
  hash_value = murmurhash(&parser_version, sizeof(parser_version), hash_value);
  hash_value = murmurhash(parser_name.ptr(), parser_name.length(), hash_value);
  hash_value = murmurhash(properties.ptr(), properties.length(), hash_value);
  return hash_value;
}

bool ObTokenizeResultCacheKey::operator==(const ObIKVCacheKey &other) const
{
  const ObTokenizeResultCacheKey &rhs =
      static_cast<const ObTokenizeResultCacheKey &>(other);
  return this == &other
         || (tenant_id_ == rhs.tenant_id_
             && fulltext_hash_ == rhs.fulltext_hash_
             && fixed_hash_ == rhs.fixed_hash_
             && collation_ == rhs.collation_
             && output_mode_ == rhs.output_mode_
             && parser_version_ == rhs.parser_version_
             && tenant_name_ == rhs.tenant_name_
             && fulltext_ == rhs.fulltext_
             && parser_name_ == rhs.parser_name_
             && properties_ == rhs.properties_);
}

int ObTokenizeResultCacheKey::equal(const ObIKVCacheKey &other, bool &equal) const
{
  equal = *this == other;
  return OB_SUCCESS;
}

int ObTokenizeResultCacheKey::hash(uint64_t &hash_value) const
{
  hash_value = murmurhash(&tenant_id_, sizeof(tenant_id_), 0);
  hash_value = murmurhash(&fulltext_hash_, sizeof(fulltext_hash_), hash_value);
  hash_value = murmurhash(tenant_name_.ptr(), tenant_name_.length(), hash_value);
  hash_value = murmurhash(&fixed_hash_, sizeof(fixed_hash_), hash_value);
  return OB_SUCCESS;
}

int64_t ObTokenizeResultCacheKey::size() const
{
  return sizeof(*this) + tenant_name_.length() + fulltext_.length()
         + parser_name_.length() + properties_.length();
}

int ObTokenizeResultCacheKey::deep_copy(char *buf,
                                        const int64_t buf_len,
                                        ObIKVCacheKey *&key) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tokenize cache key buffer", K(ret), K(buf_len), K(size()));
  } else {
    char *pos = buf + sizeof(*this);
    ObString tenant_name(tenant_name_.length(), pos);
    MEMCPY(pos, tenant_name_.ptr(), tenant_name_.length());
    pos += tenant_name_.length();
    ObString fulltext(fulltext_.length(), pos);
    MEMCPY(pos, fulltext_.ptr(), fulltext_.length());
    pos += fulltext_.length();
    ObString parser_name(parser_name_.length(), pos);
    MEMCPY(pos, parser_name_.ptr(), parser_name_.length());
    pos += parser_name_.length();
    ObString properties(properties_.length(), pos);
    MEMCPY(pos, properties_.ptr(), properties_.length());
    key = new (buf) ObTokenizeResultCacheKey(tenant_id_,
                                             tenant_name,
                                             fulltext,
                                             collation_,
                                             output_mode_,
                                             parser_name,
                                             parser_version_,
                                             properties,
                                             fixed_hash_);
  }
  return ret;
}

int64_t ObTokenizeResultCacheValue::size() const
{
  return sizeof(*this) + json_.length();
}

int ObTokenizeResultCacheValue::deep_copy(char *buf,
                                          const int64_t buf_len,
                                          ObIKVCacheValue *&value) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tokenize cache value buffer", K(ret), K(buf_len), K(size()));
  } else {
    char *json_buf = buf + sizeof(*this);
    MEMCPY(json_buf, json_.ptr(), json_.length());
    value = new (buf) ObTokenizeResultCacheValue(ObString(json_.length(), json_buf));
  }
  return ret;
}

ObTokenizeResultCache &ObTokenizeResultCache::get_instance()
{
  static ObTokenizeResultCache cache;
  return cache;
}

int ObTokenizeResultCache::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(cache_.init("tokenize_result", 1))) {
    LOG_WARN("failed to initialize tokenize result cache", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObTokenizeResultCache::destroy()
{
  if (is_inited_) {
    cache_.destroy();
    is_inited_ = false;
  }
}

int ObTokenizeResultCache::get(const ObTokenizeResultCacheKey &key,
                               const ObTokenizeResultCacheValue *&value,
                               ObKVCacheHandle &handle)
{
  return is_inited_ ? cache_.get(key, value, handle) : OB_NOT_INIT;
}

int ObTokenizeResultCache::evict_one(const uint64_t tenant_id, const bool tenant_only)
{
  int ret = OB_SUCCESS;
  ObKVCacheIterator iter;
  if (OB_FAIL(cache_.get_iterator(iter))) {
    LOG_WARN("failed to create tokenize cache iterator", K(ret));
  } else {
    const ObTokenizeResultCacheKey *key = nullptr;
    const ObTokenizeResultCacheValue *value = nullptr;
    ObKVCacheHandle handle;
    bool erased = false;
    while (OB_SUCC(ret) && !erased) {
      if (OB_FAIL(iter.get_next_kvpair(key, value, handle))) {
        ret = OB_ITER_END == ret ? OB_ENTRY_NOT_EXIST : ret;
      } else if (!tenant_only || key->tenant_id() == tenant_id) {
        if (OB_FAIL(cache_.erase(*key))) {
          LOG_WARN("failed to evict tokenize cache entry", K(ret));
        } else {
          erased = true;
        }
      }
    }
  }
  return ret;
}

int ObTokenizeResultCache::put(const ObTokenizeResultCacheKey &key,
                               const ObTokenizeResultCacheValue &value)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    ObSpinLockGuard guard(lock_);
    if (cache_.count() >= MAX_ENTRY_COUNT) {
      if (OB_FAIL(evict_one(key.tenant_id(), true))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          ret = evict_one(key.tenant_id(), false);
        }
        if (OB_FAIL(ret)) {
          LOG_WARN("failed to bound tokenize result cache", K(ret));
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(cache_.put(key, value))) {
      LOG_WARN("failed to put tokenize result cache", K(ret));
    }
  }
  return ret;
}

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
  const ObTokenizeFixedConfig *fixed_config =
      static_cast<const ObTokenizeFixedConfig *>(expr.extra_info_);

  // check param num, which is checked in ObExprOperator::calc_result_typeN.
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_NOT_NULL(fixed_config)
             && fixed_config->is_valid_) {
    ObDatum *fulltext_datum = nullptr;
    ObString fulltext;
    if (OB_FAIL(expr.args_[0]->eval(ctx, fulltext_datum))) {
      LOG_WARN("failed to evaluate tokenize fulltext", K(ret));
    } else {
      fulltext = fulltext_datum->is_null()
                     ? ObString::make_empty_string()
                     : fulltext_datum->get_string();
    }

    if (OB_SUCC(ret)) {
      const bool use_result_cache = fixed_config->cacheable_
                                    && fulltext.length() >= TOKENIZE_RESULT_CACHE_MIN_TEXT_LENGTH;
      bool cache_hit = false;
      if (use_result_cache) {
        const ObBasicSessionInfo *session = ctx.exec_ctx_.get_my_session();
        const ObString tenant_name = OB_ISNULL(session) ? ObString() : session->get_tenant_name();
        const uint64_t tenant_id = murmurhash(tenant_name.ptr(), tenant_name.length(), 0);
        ObTokenizeResultCacheKey key(tenant_id,
                                     tenant_name,
                                     fulltext,
                                     fixed_config->collation_,
                                     fixed_config->output_mode_,
                                     fixed_config->parser_name_,
                                     fixed_config->parser_version_,
                                     fixed_config->normalized_properties_,
                                     fixed_config->fixed_hash_);
        const ObTokenizeResultCacheValue *cached_value = nullptr;
        ObKVCacheHandle cache_handle;
        const int cache_ret = ObTokenizeResultCache::get_instance().get(key,
                                                                        cached_value,
                                                                        cache_handle);
        if (OB_SUCCESS == cache_ret && OB_NOT_NULL(cached_value)) {
          const ObString &cached_json = cached_value->json();
          char *buf = expr.get_str_res_mem(ctx, cached_json.length());
          if (OB_ISNULL(buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate cached tokenize result", K(ret), K(cached_json.length()));
          } else {
            MEMCPY(buf, cached_json.ptr(), cached_json.length());
            expr_datum.set_string(buf, cached_json.length());
            cache_hit = true;
          }
        } else if (OB_ENTRY_NOT_EXIST != cache_ret && OB_NOT_INIT != cache_ret) {
          LOG_WARN("failed to lookup tokenize result cache", K(cache_ret));
        }
      }

      if (OB_SUCC(ret) && !cache_hit) {
        ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
        common::ObArenaAllocator &temp_allocator = tmp_alloc_g.get_allocator();
        ObIJsonBase *json_result = nullptr;
        TokenizeParam param;
        param.parser_name_ = fixed_config->parser_name_;
        param.properties_ = fixed_config->normalized_properties_;
        param.meta_.set_varchar();
        param.meta_.set_collation_type(fixed_config->collation_);
        param.fulltext_ = fulltext;
        param.output_mode_ = static_cast<TokenizeParam::OUTPUT_MODE>(fixed_config->output_mode_);
        if (OB_FAIL(param.try_load_dictionary_for_ik())) {
          LOG_WARN("failed to load dictionary for fixed tokenize config", K(ret));
        } else if (OB_FAIL(tokenize_fulltext(param,
                                             param.output_mode_,
                                             temp_allocator,
                                             json_result))) {
          LOG_WARN("failed to tokenize fulltext with fixed config", K(ret));
        } else if (OB_FAIL(ObJsonExprHelper::pack_json_res(expr,
                                                           ctx,
                                                           temp_allocator,
                                                           json_result,
                                                           expr_datum))) {
          LOG_WARN("failed to pack tokenize json result", K(ret));
        } else if (use_result_cache) {
          const ObBasicSessionInfo *session = ctx.exec_ctx_.get_my_session();
          const ObString tenant_name = OB_ISNULL(session) ? ObString() : session->get_tenant_name();
          const uint64_t tenant_id = murmurhash(tenant_name.ptr(), tenant_name.length(), 0);
          ObTokenizeResultCacheKey key(tenant_id,
                                       tenant_name,
                                       fulltext,
                                       fixed_config->collation_,
                                       fixed_config->output_mode_,
                                       fixed_config->parser_name_,
                                       fixed_config->parser_version_,
                                       fixed_config->normalized_properties_,
                                       fixed_config->fixed_hash_);
          ObTokenizeResultCacheValue value(expr_datum.get_string());
          const int cache_ret = ObTokenizeResultCache::get_instance().put(key, value);
          if (OB_SUCCESS != cache_ret && OB_NOT_INIT != cache_ret) {
            LOG_WARN("failed to cache tokenize result", K(cache_ret));
          }
        }
      }
    }
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    common::ObArenaAllocator &temp_allocator = tmp_alloc_g.get_allocator();
    ObIJsonBase *json_result = nullptr;
    TokenizeParam param;
    int64_t parser_version = -1;
    bool cache_hit = false;
    bool cacheable = false;
    if (OB_FAIL(parse_param(expr, ctx, temp_allocator, param))) {
      LOG_WARN("Fail to parse param", K(ret));
    } else {
      cacheable = can_use_result_cache(expr, param, parser_version);
    }

    if (OB_SUCC(ret)) {
      const ObBasicSessionInfo *session = ctx.exec_ctx_.get_my_session();
      const ObString tenant_name = OB_ISNULL(session) ? ObString() : session->get_tenant_name();
      const uint64_t tenant_id = murmurhash(tenant_name.ptr(), tenant_name.length(), 0);
      const uint64_t fixed_hash = ObTokenizeResultCacheKey::calc_fixed_hash(
          param.meta_.get_collation_type(),
          static_cast<int8_t>(param.output_mode_),
          param.parser_name_,
          parser_version,
          param.properties_);
      ObTokenizeResultCacheKey key(tenant_id,
                                   tenant_name,
                                   param.fulltext_,
                                   param.meta_.get_collation_type(),
                                   static_cast<int8_t>(param.output_mode_),
                                   param.parser_name_,
                                   parser_version,
                                   param.properties_,
                                   fixed_hash);
      const ObTokenizeResultCacheValue *cached_value = nullptr;
      ObKVCacheHandle cache_handle;
      int cache_ret = cacheable
                          ? ObTokenizeResultCache::get_instance().get(key,
                                                                      cached_value,
                                                                      cache_handle)
                          : OB_ENTRY_NOT_EXIST;
      if (cacheable && OB_SUCCESS == cache_ret && OB_NOT_NULL(cached_value)) {
        const ObString &cached_json = cached_value->json();
        char *buf = expr.get_str_res_mem(ctx, cached_json.length());
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate cached tokenize result", K(ret), K(cached_json.length()));
        } else {
          MEMCPY(buf, cached_json.ptr(), cached_json.length());
          expr_datum.set_string(buf, cached_json.length());
          cache_hit = true;
        }
      } else if (cacheable && OB_ENTRY_NOT_EXIST != cache_ret && OB_NOT_INIT != cache_ret) {
        LOG_WARN("failed to lookup tokenize result cache", K(cache_ret));
      }

      if (OB_SUCC(ret) && !cache_hit) {
        if (OB_FAIL(tokenize_fulltext(param, param.output_mode_, temp_allocator, json_result))) {
          LOG_WARN("Fail to tokenize fulltext", K(ret));
        } else if (OB_FAIL(ObJsonExprHelper::pack_json_res(expr,
                                                           ctx,
                                                           temp_allocator,
                                                           json_result,
                                                           expr_datum))) {
          LOG_WARN("fail to pack json result", K(ret));
        } else if (cacheable) {
          ObTokenizeResultCacheValue value(expr_datum.get_string());
          cache_ret = ObTokenizeResultCache::get_instance().put(key, value);
          if (OB_SUCCESS != cache_ret && OB_NOT_INIT != cache_ret) {
            LOG_WARN("failed to cache tokenize result", K(cache_ret));
          }
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

bool ObExprTokenize::can_use_result_cache(const ObExpr &expr,
                                          const TokenizeParam &param,
                                          int64_t &parser_version)
{
  bool can_cache = false;
  storage::ObFTParser parser;
  storage::ObFTParserJsonProps properties;
  const bool parser_is_constant = expr.arg_cnt_ < 2 || expr.args_[1]->is_const_expr();
  const bool properties_are_constant = expr.arg_cnt_ < 3 || expr.args_[2]->is_const_expr();
  if (parser_is_constant && properties_are_constant
      && OB_SUCCESS == parser.parse_from_str(param.parser_name_.ptr(), param.parser_name_.length())
      && OB_SUCCESS == properties.init()
      && OB_SUCCESS == properties.parse_from_valid_str(param.properties_)) {
    can_cache = is_result_cacheable_parser(parser, properties);
    parser_version = parser.get_parser_version();
  }
  return can_cache;
}

bool ObExprTokenize::is_result_cacheable_parser(
    const storage::ObFTParser &parser,
    const storage::ObFTParserJsonProps &properties)
{
  bool can_cache = false;
  if (parser.is_beng()) {
    can_cache = true;
  } else if (parser.is_ik()) {
    ObString dict_table;
    ObString quan_table;
    ObString stopword_table;
    const int dict_ret = properties.config_get_dict_table(dict_table);
    const int quan_ret = properties.config_get_quantifier_table(quan_table);
    const int stop_ret = properties.config_get_stopword_table(stopword_table);
    const bool no_dict = OB_SEARCH_NOT_FOUND == dict_ret
                         || (OB_SUCCESS == dict_ret && dict_table.empty());
    const bool no_quan = OB_SEARCH_NOT_FOUND == quan_ret
                         || (OB_SUCCESS == quan_ret && quan_table.empty());
    const bool no_stop = OB_SEARCH_NOT_FOUND == stop_ret
                         || (OB_SUCCESS == stop_ret && stopword_table.empty());
    can_cache = no_dict && no_quan && no_stop;
  }
  return can_cache;
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
  } else if (OB_FAIL(param.reform_parser_properties(param.properties_))) {
    LOG_WARN("Fail to reform parser params.", K(ret));
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
  ObTokenizeFixedConfig *fixed_config = nullptr;
  CK((rt_expr.arg_cnt_ >= 1 && rt_expr.arg_cnt_ <= 3));
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(build_fixed_config(op_cg_ctx, raw_expr, rt_expr, fixed_config))) {
    LOG_WARN("failed to build fixed tokenize config", K(ret));
  } else {
    // do register
    rt_expr.extra_info_ = fixed_config;
    rt_expr.eval_func_ = eval_tokenize;
  }
  return ret;
}

int ObExprTokenize::build_fixed_config(ObExprCGCtx &op_cg_ctx,
                                       const ObRawExpr &raw_expr,
                                       ObExpr &rt_expr,
                                       ObTokenizeFixedConfig *&fixed_config)
{
  int ret = OB_SUCCESS;
  fixed_config = nullptr;
  const ObRawExpr *parser_expr = nullptr;
  ObString raw_parser_name;
  storage::ObFTParser parser;

  // The fixed fast path deliberately excludes the third argument. It can
  // affect output mode, stopwords and parser properties, all of which remain
  // on the complete runtime parsing path.
  if (2 != raw_expr.get_param_count() || 2 != rt_expr.arg_cnt_) {
    // no fixed config
  } else if (OB_ISNULL(parser_expr = raw_expr.get_param_expr(1))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null tokenize parser expression", K(ret));
  } else {
    while (T_FUN_SYS_CAST == parser_expr->get_expr_type()
           && parser_expr->has_flag(IS_OP_OPERAND_IMPLICIT_CAST)
           && 1 == parser_expr->get_param_count()
           && OB_NOT_NULL(parser_expr->get_param_expr(0))) {
      parser_expr = parser_expr->get_param_expr(0);
    }
    const ObObj *parser_obj = parser_expr->is_const_raw_expr()
                                  ? &static_cast<const ObConstRawExpr *>(parser_expr)->get_value()
                                  : nullptr;
    // Bind variables and computed expressions must remain dynamic.
    if (OB_ISNULL(parser_obj)) {
    } else if (parser_obj->is_null()) {
      raw_parser_name = ObString::make_string(OB_DEFAULT_FULLTEXT_PARSER_NAME);
    } else if (!ob_is_string_type(parser_obj->get_type())) {
      // Preserve the existing implicit-cast evaluation path.
    } else {
      raw_parser_name = parser_obj->get_string().trim();
    }

    if (!raw_parser_name.empty()
        && OB_SUCCESS == plugin::ObPluginHelper::find_ftparser(raw_parser_name, parser)
        && (parser.is_ik() || parser.is_beng())) {
      char parser_name_buf[share::OB_PLUGIN_NAME_LENGTH] = {'\0'};
      storage::ObFTParserJsonProps properties;
      ObString normalized_properties;
      const ObCollationType collation = rt_expr.args_[0]->obj_meta_.get_collation_type();
      if (OB_FAIL(parser.serialize_to_str(parser_name_buf, sizeof(parser_name_buf)))) {
        LOG_WARN("failed to serialize fixed tokenize parser", K(ret), K(parser));
      } else {
        const ObString parser_name = ObString::make_string(parser_name_buf);
        if (OB_FAIL(properties.init())) {
          LOG_WARN("failed to initialize fixed tokenize properties", K(ret));
        } else if (OB_FAIL(properties.parse_from_valid_str(ObString()))) {
          LOG_WARN("failed to parse empty fixed tokenize properties", K(ret));
        } else if (OB_FAIL(properties.rebuild_props_for_ddl(parser_name,
                                                            collation,
                                                            false))) {
          LOG_WARN("failed to normalize fixed tokenize properties", K(ret), K(parser_name));
        } else if (OB_FAIL(properties.to_format_json(*op_cg_ctx.allocator_,
                                                     normalized_properties))) {
          LOG_WARN("failed to serialize fixed tokenize properties", K(ret));
        } else {
          ObIExprExtraInfo *extra_info = nullptr;
          if (OB_FAIL(ObExprExtraInfoFactory::alloc(*op_cg_ctx.allocator_,
                                                    rt_expr.type_,
                                                    extra_info))) {
            LOG_WARN("failed to allocate fixed tokenize config", K(ret));
          } else if (OB_ISNULL(fixed_config =
                                   static_cast<ObTokenizeFixedConfig *>(extra_info))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null fixed tokenize config", K(ret));
          } else if (OB_FAIL(ob_write_string(*op_cg_ctx.allocator_,
                                             parser_name,
                                             fixed_config->parser_name_))) {
            LOG_WARN("failed to copy fixed tokenize parser name", K(ret));
          } else {
            fixed_config->normalized_properties_ = normalized_properties;
            fixed_config->is_valid_ = true;
            fixed_config->cacheable_ = is_result_cacheable_parser(parser, properties);
            fixed_config->output_mode_ = TokenizeParam::DEFAULT;
            fixed_config->parser_version_ = parser.get_parser_version();
            fixed_config->collation_ = collation;
            fixed_config->fixed_hash_ = ObTokenizeResultCacheKey::calc_fixed_hash(
                collation,
                fixed_config->output_mode_,
                fixed_config->parser_name_,
                fixed_config->parser_version_,
                fixed_config->normalized_properties_);
          }
        }
      }
    }
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
