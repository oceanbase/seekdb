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

#include <mutex>

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
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "share/ob_json_access_utils.h"
#include "data_plane/fts/ob_fts_parser_helper.h"
#include "data_plane/fts/dict/ob_gen_dic_loader.h"
#include "data_plane/fts/ob_fts_parser_property.h"

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_json_func_helper.h" // file not self-contained, there're logs inside.

namespace oceanbase
{
namespace sql
{
namespace
{
struct ObTokenizeHelperCacheEntry
{
  ObTokenizeHelperCacheEntry() : arena_(common::ObMemAttr("TokHelperCache")) {}
  void reset_entry()
  {
    helper_.reset();
    arena_.reset();
    parser_name_.reset();
    raw_properties_.reset();
    reformed_properties_.reset();
    valid_ = false;
  }
  common::ObArenaAllocator arena_;
  storage::ObFTParseHelper helper_;
  common::ObString parser_name_;
  common::ObString raw_properties_;
  common::ObString reformed_properties_;
  bool valid_ = false;
};
constexpr int64_t TOKENIZE_HELPER_CACHE_SIZE = 4;
struct ObTokenizeHelperCache
{
  ObTokenizeHelperCacheEntry entries_[TOKENIZE_HELPER_CACHE_SIZE];
  int64_t next_slot_ = 0;
};
thread_local ObTokenizeHelperCache g_tokenize_helper_cache;

ObTokenizeHelperCacheEntry *find_helper_cache(const common::ObString &parser_name,
                                              const common::ObString &raw_properties)
{
  ObTokenizeHelperCacheEntry *ret = nullptr;
  for (int64_t i = 0; i < TOKENIZE_HELPER_CACHE_SIZE; ++i) {
    ObTokenizeHelperCacheEntry &entry = g_tokenize_helper_cache.entries_[i];
    if (entry.valid_ && entry.parser_name_ == parser_name && entry.raw_properties_ == raw_properties) {
      ret = &entry;
      break;
    }
  }
  return ret;
}

ObTokenizeHelperCacheEntry &alloc_helper_cache_slot()
{
  ObTokenizeHelperCacheEntry &entry = g_tokenize_helper_cache.entries_[g_tokenize_helper_cache.next_slot_];
  g_tokenize_helper_cache.next_slot_ = (g_tokenize_helper_cache.next_slot_ + 1) % TOKENIZE_HELPER_CACHE_SIZE;
  entry.reset_entry();
  return entry;
}

struct ObTokenizeResultCacheEntry
{
  ObTokenizeResultCacheEntry() : arena_(common::ObMemAttr("TokResultCache")) {}
  void reset_entry()
  {
    arena_.reset(); parser_name_.reset(); properties_.reset(); fulltext_.reset();
    raw_binary_.reset(); key_hash_ = 0; output_mode_ = -1; collation_type_ = -1; valid_ = false;
  }
  common::ObArenaAllocator arena_;
  common::ObString parser_name_;
  common::ObString properties_;
  common::ObString fulltext_;
  common::ObString raw_binary_;
  uint64_t key_hash_ = 0;
  int32_t output_mode_ = -1;
  int32_t collation_type_ = -1;
  bool valid_ = false;
};
constexpr int64_t TOKENIZE_RESULT_MAX_LENGTH = 65536;
constexpr int64_t TOKENIZE_RESULT_CACHE_SLOTS = 32;
constexpr int64_t TOKENIZE_REPEAT_HINT_SLOTS = 64;

struct ObTokenizeResultCache
{
  std::mutex mutex_;
  ObTokenizeResultCacheEntry entries_[TOKENIZE_RESULT_CACHE_SLOTS];
  uint64_t repeat_hint_[TOKENIZE_REPEAT_HINT_SLOTS] = {};
};

ObTokenizeResultCache &tokenize_result_cache() { static ObTokenizeResultCache cache; return cache; }

uint64_t tokenize_cache_hash(const common::ObString &parser_name, const common::ObString &properties,
                             const common::ObString &fulltext, const int32_t output_mode, const int32_t collation_type)
{
  uint64_t h = 1469598103934665603ULL;
  const auto feed = [&h](const char *ptr, const int32_t len) {
    for (int32_t i = 0; i < len; ++i) { h ^= static_cast<uint64_t>(static_cast<unsigned char>(ptr[i])); h *= 1099511628211ULL; }
  };
  if (nullptr != parser_name.ptr()) feed(parser_name.ptr(), parser_name.length());
  if (nullptr != properties.ptr()) feed(properties.ptr(), properties.length());
  if (nullptr != fulltext.ptr()) feed(fulltext.ptr(), fulltext.length());
  h ^= static_cast<uint64_t>(output_mode) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  h ^= static_cast<uint64_t>(collation_type) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}

ObTokenizeResultCacheEntry *find_result_cache(ObTokenizeResultCache &cache, const uint64_t key_hash,
    const common::ObString &parser_name, const common::ObString &properties,
    const common::ObString &fulltext, const int32_t output_mode, const int32_t collation_type)
{
  ObTokenizeResultCacheEntry *ret = nullptr;
  ObTokenizeResultCacheEntry &entry = cache.entries_[key_hash % TOKENIZE_RESULT_CACHE_SLOTS];
  if (entry.valid_ && entry.key_hash_ == key_hash && entry.collation_type_ == collation_type
      && entry.output_mode_ == output_mode && entry.parser_name_ == parser_name
      && entry.properties_ == properties && entry.fulltext_ == fulltext) { ret = &entry; }
  return ret;
}

int store_result_cache(ObTokenizeResultCache &cache, const uint64_t key_hash, const common::ObString &parser_name,
    const common::ObString &properties, const common::ObString &fulltext,
    const int32_t output_mode, const int32_t collation_type, common::ObIJsonBase *json_result)
{
  int ret = OB_SUCCESS;
  ObTokenizeResultCacheEntry &entry = cache.entries_[key_hash % TOKENIZE_RESULT_CACHE_SLOTS];
  entry.reset_entry();
  common::ObString raw_binary;
  if (OB_ISNULL(json_result)) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(json_result, raw_binary, &entry.arena_))) {}
  else if (raw_binary.length() > TOKENIZE_RESULT_MAX_LENGTH) { ret = OB_SIZE_OVERFLOW; }
  else if (OB_FAIL(ob_write_string(entry.arena_, parser_name, entry.parser_name_))
      || OB_FAIL(ob_write_string(entry.arena_, properties, entry.properties_))
      || OB_FAIL(ob_write_string(entry.arena_, fulltext, entry.fulltext_))) {}
  else { entry.raw_binary_ = raw_binary; entry.key_hash_ = key_hash;
    entry.collation_type_ = collation_type; entry.output_mode_ = output_mode; entry.valid_ = true; }
  return ret;
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
  bool cache_hit = false; bool should_store = false; uint64_t key_hash = 0;

  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_param(expr, ctx, temp_allocator, param))) {
  } else {
    const int32_t om = static_cast<int32_t>(param.output_mode_);
    const int32_t ct = static_cast<int32_t>(param.meta_.get_collation_type());
    key_hash = tokenize_cache_hash(param.parser_name_, param.properties_, param.fulltext_, om, ct);
    ObTokenizeResultCache &cache = tokenize_result_cache();
    { std::lock_guard<std::mutex> g(cache.mutex_);
      ObTokenizeResultCacheEntry *ce = find_result_cache(cache, key_hash, param.parser_name_, param.properties_, param.fulltext_, om, ct);
      if (nullptr != ce && ce->raw_binary_.length() > 0) {
        cache_hit = true;
        ret = ObJsonExprHelper::pack_json_str_res(expr, ctx, expr_datum, ce->raw_binary_, &temp_allocator);
      } else {
        uint64_t &hint = cache.repeat_hint_[key_hash % TOKENIZE_REPEAT_HINT_SLOTS];
        should_store = (hint == key_hash); hint = key_hash;
      }
    }
    if (OB_SUCC(ret) && !cache_hit) {
      if (OB_FAIL(param.try_load_dictionary_for_ik())) {
      } else if (OB_FAIL(tokenize_fulltext(param, param.output_mode_, temp_allocator, json_result))) {
      } else if (OB_FAIL(ObJsonExprHelper::pack_json_res(expr, ctx, temp_allocator, json_result, expr_datum))) {
      } else if (should_store) {
        std::lock_guard<std::mutex> g(cache.mutex_);
        int tr = store_result_cache(cache, key_hash, param.parser_name_, param.properties_, param.fulltext_, om, ct, json_result);
        if (OB_SUCCESS != tr) { LOG_DEBUG("store tokenize result cache failed", K(tr)); }
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
  storage::ObFTParseHelper *hp = nullptr;
  storage::ObFTParseHelper local_helper;
  for (int64_t i = 0; i < TOKENIZE_HELPER_CACHE_SIZE; ++i) {
    auto &entry = g_tokenize_helper_cache.entries_[i];
    if (entry.valid_ && entry.parser_name_ == param.parser_name_ && entry.reformed_properties_ == param.properties_) {
      hp = &entry.helper_; hp->rebind_allocator(&allocator); break;
    }
  }
  const int64_t ft_word_bkt_cnt = MIN(MAX(param.fulltext_.length() / 2, 2), 997);
  int64_t doc_len = 0;
  storage::ObFTWordMap token_map;

  ObArenaAllocator tmp_parse_alloc(ObMemAttr("Tmp buffer"));

  if (TokenizeParam::OUTPUT_MODE::DEFAULT != mode && TokenizeParam::OUTPUT_MODE::ALL != mode) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr == hp && OB_FAIL(local_helper.init(&allocator, param.parser_name_, param.properties_))) {
  } else if (FALSE_IT(hp = (nullptr == hp ? &local_helper : hp))) {
  } else if (OB_FAIL(token_map.create(ft_word_bkt_cnt, common::ObMemAttr("FTWordMap")))) {
  } else if (
      (0 != param.fulltext_.length())
      && OB_FAIL(hp->segment(
                     param.meta_,
                     param.fulltext_.ptr(),
                     param.fulltext_.length(),
                     doc_len,
                     token_map))) {
  } else {
    switch (param.output_mode_) {
    case TokenizeParam::OUTPUT_MODE::DEFAULT: {
      if (OB_FAIL(hp->make_token_array_json(token_map, result))) {
      } else {
        // pass
      }
      break;
    }
    case TokenizeParam::OUTPUT_MODE::ALL: {
      if (OB_FAIL(hp->make_detail_json(token_map, doc_len, result))) {
      } else {
        // pass
      }
      break;
    }
    default:
      ret = OB_INVALID_ARGUMENT;
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
  } else if (ObJsonNodeType::J_OBJECT != obj->json_type()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (obj->element_count() == 0) {
    // no data
  } else if (OB_FAIL(obj->get_object_value(0, str, val))) {
  } else if (0 == str.case_compare(CASE_INDICATOR_STR)) {
    if (ObJsonNodeType::J_STRING != val->json_type()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("UPPER")) {
    } else if (0 == ObString(val->get_data_length(), val->get_data()).case_compare("LOWER")) {
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "case indentifier");
  } else if (0 == str.case_compare(OUTPUT_MODE_STR)) {
    if (ObJsonNodeType::J_STRING != val->json_type()) {
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
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser arguments");
    } else {
      ObString json_str;
      if (OB_FAIL(storage::ObFTParserJsonProps::tokenize_array_to_props_json(allocator_, val, json_str))) {
        ObSqlString message;
        message.append_fmt("format in %s form", ADDITIONAL_ARGS_STR);
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, message.ptr());
      } else {
        properties_ = json_str;
      }
    }
  } else {
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
  
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());

  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_fulltext(expr, ctx, param))) {
  } else if (OB_FAIL(parse_parser_name(expr, ctx, param))) {
  } else if (OB_FAIL(parse_parser_properties(expr, ctx, temp_allocator, param))) {
  } else {
    ObTokenizeHelperCacheEntry *entry = find_helper_cache(param.parser_name_, param.properties_);
    if (nullptr != entry) {
      param.properties_ = entry->reformed_properties_;
    } else {
      common::ObString raw_properties = param.properties_;
      if (OB_FAIL(param.reform_parser_properties(param.properties_))) {
      } else {
        int tr = OB_SUCCESS;
        ObTokenizeHelperCacheEntry &slot = alloc_helper_cache_slot();
        if (OB_SUCCESS != (tr = ob_write_string(slot.arena_, param.parser_name_, slot.parser_name_))) {
        } else if (OB_SUCCESS != (tr = ob_write_string(slot.arena_, raw_properties, slot.raw_properties_))) {
        } else if (OB_SUCCESS != (tr = ob_write_string(slot.arena_, param.properties_, slot.reformed_properties_))) {
        } else if (OB_SUCCESS != (tr = slot.helper_.init(&slot.arena_, param.parser_name_, param.properties_))) {
        } else { slot.valid_ = true; }
        if (OB_SUCCESS != tr) { slot.valid_ = false; LOG_WARN("failed to fill tokenize helper cache", K(tr)); }
      }
    }
  }
  return ret;
}

int ObExprTokenize::construct_ft_parser_inner_name(const ObString &input_str, TokenizeParam &param)
{
  int ret = OB_SUCCESS;
  storage::ObFTParser parser;

  char *parser_name_buf = nullptr;
  if (OB_ISNULL(parser_name_buf
                = static_cast<char *>(param.allocator_.alloc(storage::OB_FT_PARSER_NAME_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(parser.init(input_str))) {
  } else if (OB_FAIL(parser.serialize_to_str(parser_name_buf, storage::OB_FT_PARSER_NAME_LENGTH))) {
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
    } else {
      if (ObJsonNodeType::J_ARRAY != base->json_type()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser args should be in an array.");
      } else {
        for (uint64_t i = 0; OB_SUCC(ret) && i < base->element_count(); ++i) {
          ObIJsonBase *node = nullptr;
          if (OB_FAIL(base->get_array_element(i, node))) {
          } else if (ObJsonNodeType::J_OBJECT != (node->json_type())) {
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(param.parse_json_param(node))) {
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
  } else if (OB_FAIL(parser_properties.parse_from_valid_str(properties))) {
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parser properties invalid.");
  } else if (OB_FAIL(parser_properties.rebuild_props_for_ddl(parser_name_,
                                                             ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                                             true))) {
  } else if (OB_FAIL(parser_properties.to_format_json(allocator_, properties_))) {
  }

  return ret;
}

int ObExprTokenize::TokenizeParam::try_load_dictionary_for_ik()
{
  int ret = OB_SUCCESS;
  bool need_to_load_dic = false;
  storage::ObDicLoaderHandle dic_loader_handle;
  if (OB_FAIL(share::ObFtsIndexBuilderUtil::check_need_to_load_dic(
          parser_name_, need_to_load_dic))) {
  } else if (need_to_load_dic) {
    if (OB_FAIL(storage::ObGenDicLoader::get_instance().get_dic_loader(
                    ObString::make_string(storage::ObFTSLiteral::PARSER_NAME_IK), // currently only ik, use parser_name_ without version suffix
                    ObCharset::charset_type_by_coll(meta_.get_collation_type()),
                    dic_loader_handle))) {
    } else if (OB_UNLIKELY(!dic_loader_handle.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(dic_loader_handle.get_loader()->try_load_dictionary_in_trans())) {
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
