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

#include "lib/alloc/alloc_func.h"
#include "lib/alloc/alloc_struct.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "lib/hash/ob_hashutils.h"
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/ob_errno.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "object/ob_object.h"
#include "plugin/sys/ob_plugin_helper.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "sql/session/ob_sql_session_info.h"
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

enum class ObBuiltinTokenizer : uint8_t
{
  INVALID = 0,
  IK,
  BENG,
};

struct ObTokenizeCacheKey
{
  ObTokenizeCacheKey()
    : collation_(CS_TYPE_INVALID), parser_(ObBuiltinTokenizer::INVALID),
      tenant_name_(), text_(), hash_(0)
  {}

  ObCollationType collation_;
  ObBuiltinTokenizer parser_;
  ObString tenant_name_;
  ObString text_;
  uint64_t hash_;
};

class ObTokenizeThreadCache final
{
public:
  static const int64_t MAX_RESULT_LENGTH = 64 * 1024;

  ObTokenizeThreadCache()
    : collation_(CS_TYPE_INVALID), parser_(ObBuiltinTokenizer::INVALID),
      tenant_name_length_(0), text_length_(0), result_length_(0),
      capacity_(0), buffer_(nullptr)
  {}

  ~ObTokenizeThreadCache()
  {
    if (OB_NOT_NULL(buffer_)) {
      ob_free(buffer_);
      buffer_ = nullptr;
    }
  }

  bool get(const ObTokenizeCacheKey &key,
           const ObExpr &expr,
           ObEvalCtx &ctx,
           ObDatum &result) const
  {
    bool found = false;
    if (matches(key)) {
      char *result_buffer = expr.get_str_res_mem(ctx, result_length_);
      if (OB_NOT_NULL(result_buffer)) {
        MEMCPY(result_buffer, result_ptr(), result_length_);
        result.set_string(result_buffer, result_length_);
        found = true;
      }
    }
    return found;
  }

  void put(const ObTokenizeCacheKey &key, const ObDatum &result)
  {
    if (key.tenant_name_.empty()
        || key.text_.empty()
        || result.is_null()
        || result.len_ <= 0
        || result.len_ > MAX_RESULT_LENGTH) {
      return;
    }

    const int64_t required_size = key.tenant_name_.length() + key.text_.length() + result.len_;
    if (required_size > capacity_) {
      char *new_buffer =
          static_cast<char *>(ob_malloc(required_size, ObMemAttr("TokenizeHot")));
      if (OB_ISNULL(new_buffer)) {
        return;
      }
      if (OB_NOT_NULL(buffer_)) {
        ob_free(buffer_);
      }
      buffer_ = new_buffer;
      capacity_ = required_size;
    }

    MEMCPY(buffer_, key.tenant_name_.ptr(), key.tenant_name_.length());
    MEMCPY(buffer_ + key.tenant_name_.length(), key.text_.ptr(), key.text_.length());
    MEMCPY(buffer_ + key.tenant_name_.length() + key.text_.length(), result.ptr_, result.len_);
    collation_ = key.collation_;
    parser_ = key.parser_;
    tenant_name_length_ = key.tenant_name_.length();
    text_length_ = key.text_.length();
    result_length_ = result.len_;
  }

private:
  bool matches(const ObTokenizeCacheKey &key) const
  {
    return OB_NOT_NULL(buffer_)
        && collation_ == key.collation_
        && parser_ == key.parser_
        && tenant_name_length_ == key.tenant_name_.length()
        && text_length_ == key.text_.length()
        && 0 == MEMCMP(buffer_, key.tenant_name_.ptr(), tenant_name_length_)
        && 0 == MEMCMP(buffer_ + tenant_name_length_, key.text_.ptr(), text_length_);
  }

  const char *result_ptr() const
  {
    return buffer_ + tenant_name_length_ + text_length_;
  }

  ObCollationType collation_;
  ObBuiltinTokenizer parser_;
  int64_t tenant_name_length_;
  int64_t text_length_;
  int64_t result_length_;
  int64_t capacity_;
  char *buffer_;
};

ObTokenizeThreadCache &get_tokenize_thread_cache()
{
  static thread_local ObTokenizeThreadCache cache;
  return cache;
}

class ObTokenizeResultCache final
{
public:
  static const int64_t SHARD_COUNT = 16;
  static const int64_t ENTRIES_PER_SHARD = 4;
  static const int64_t MAX_TEXT_LENGTH = 4096;
  static const int64_t MAX_RESULT_LENGTH = 64 * 1024;

  static bool is_cacheable_text(const ObString &text)
  {
    return !text.empty() && text.length() <= MAX_TEXT_LENGTH;
  }

  static ObTokenizeResultCache &instance()
  {
    static ObTokenizeResultCache cache;
    return cache;
  }

  bool get(const ObTokenizeCacheKey &key,
           const ObExpr &expr,
           ObEvalCtx &ctx,
           ObDatum &result)
  {
    bool found = false;
    Shard &shard = get_shard(key);
    common::ObSpinLockGuard guard(shard.lock_);
    for (int64_t i = 0; !found && i < ENTRIES_PER_SHARD; ++i) {
      Entry &entry = shard.entries_[i];
      if (entry.matches(key)) {
        char *result_buffer = expr.get_str_res_mem(ctx, entry.result_length_);
        if (OB_NOT_NULL(result_buffer)) {
          MEMCPY(result_buffer, entry.result(), entry.result_length_);
          result.set_string(result_buffer, entry.result_length_);
          entry.last_access_ = ++shard.clock_;
          found = true;
        }
      }
    }
    return found;
  }

  void put(const ObTokenizeCacheKey &key, const ObDatum &result)
  {
    if (!is_cacheable_text(key.text_)
        || key.tenant_name_.empty()
        || result.is_null()
        || result.len_ <= 0
        || result.len_ > MAX_RESULT_LENGTH) {
      return;
    }

    const int64_t allocation_size = key.tenant_name_.length() + key.text_.length() + result.len_;
    char *new_buffer = static_cast<char *>(ob_malloc(allocation_size, ObMemAttr("TokenizeCache")));
    if (OB_ISNULL(new_buffer)) {
      return;
    }
    MEMCPY(new_buffer, key.tenant_name_.ptr(), key.tenant_name_.length());
    MEMCPY(new_buffer + key.tenant_name_.length(), key.text_.ptr(), key.text_.length());
    MEMCPY(new_buffer + key.tenant_name_.length() + key.text_.length(), result.ptr_, result.len_);

    Shard &shard = get_shard(key);
    char *old_buffer = nullptr;
    bool insert_new_entry = true;
    {
      common::ObSpinLockGuard guard(shard.lock_);
      int64_t target = 0;
      uint64_t oldest = UINT64_MAX;
      for (int64_t i = 0; i < ENTRIES_PER_SHARD; ++i) {
        if (shard.entries_[i].matches(key)) {
          shard.entries_[i].last_access_ = ++shard.clock_;
          insert_new_entry = false;
          break;
        } else if (OB_ISNULL(shard.entries_[i].buffer_)) {
          target = i;
          oldest = 0;
          break;
        } else if (shard.entries_[i].last_access_ < oldest) {
          target = i;
          oldest = shard.entries_[i].last_access_;
        }
      }
      if (insert_new_entry) {
        Entry &entry = shard.entries_[target];
        old_buffer = entry.buffer_;
        entry.collation_ = key.collation_;
        entry.parser_ = key.parser_;
        entry.hash_ = key.hash_;
        entry.tenant_name_length_ = key.tenant_name_.length();
        entry.text_length_ = key.text_.length();
        entry.result_length_ = result.len_;
        entry.last_access_ = ++shard.clock_;
        entry.buffer_ = new_buffer;
      }
    }
    if (!insert_new_entry) {
      ob_free(new_buffer);
    } else if (OB_NOT_NULL(old_buffer)) {
      ob_free(old_buffer);
    }
  }

private:
  struct Entry
  {
    Entry()
      : collation_(CS_TYPE_INVALID), parser_(ObBuiltinTokenizer::INVALID), hash_(0),
        tenant_name_length_(0), text_length_(0), result_length_(0),
        last_access_(0), buffer_(nullptr)
    {}

    bool matches(const ObTokenizeCacheKey &key) const
    {
      return OB_NOT_NULL(buffer_)
          && hash_ == key.hash_
          && collation_ == key.collation_
          && parser_ == key.parser_
          && tenant_name_length_ == key.tenant_name_.length()
          && text_length_ == key.text_.length()
          && 0 == MEMCMP(buffer_, key.tenant_name_.ptr(), tenant_name_length_)
          && 0 == MEMCMP(buffer_ + tenant_name_length_, key.text_.ptr(), text_length_);
    }

    const char *result() const
    {
      return buffer_ + tenant_name_length_ + text_length_;
    }

    ObCollationType collation_;
    ObBuiltinTokenizer parser_;
    uint64_t hash_;
    int64_t tenant_name_length_;
    int64_t text_length_;
    int64_t result_length_;
    uint64_t last_access_;
    char *buffer_;
  };

  struct Shard
  {
    Shard() : lock_(), clock_(0), entries_() {}
    common::ObSpinLock lock_;
    uint64_t clock_;
    Entry entries_[ENTRIES_PER_SHARD];
  };

  ObTokenizeResultCache() : shards_() {}
  ~ObTokenizeResultCache()
  {
    for (int64_t shard_idx = 0; shard_idx < SHARD_COUNT; ++shard_idx) {
      for (int64_t entry_idx = 0; entry_idx < ENTRIES_PER_SHARD; ++entry_idx) {
        if (OB_NOT_NULL(shards_[shard_idx].entries_[entry_idx].buffer_)) {
          ob_free(shards_[shard_idx].entries_[entry_idx].buffer_);
          shards_[shard_idx].entries_[entry_idx].buffer_ = nullptr;
        }
      }
    }
  }

  Shard &get_shard(const ObTokenizeCacheKey &key)
  {
    return shards_[key.hash_ & (SHARD_COUNT - 1)];
  }

  Shard shards_[SHARD_COUNT];
  DISALLOW_COPY_AND_ASSIGN(ObTokenizeResultCache);
};

ObBuiltinTokenizer parse_builtin_tokenizer(const ObString &parser_name)
{
  ObBuiltinTokenizer parser = ObBuiltinTokenizer::INVALID;
  if (2 == parser_name.length()
      && ('i' == parser_name.ptr()[0] || 'I' == parser_name.ptr()[0])
      && ('k' == parser_name.ptr()[1] || 'K' == parser_name.ptr()[1])) {
    parser = ObBuiltinTokenizer::IK;
  } else if (4 == parser_name.length()
      && ('b' == parser_name.ptr()[0] || 'B' == parser_name.ptr()[0])
      && ('e' == parser_name.ptr()[1] || 'E' == parser_name.ptr()[1])
      && ('n' == parser_name.ptr()[2] || 'N' == parser_name.ptr()[2])
      && ('g' == parser_name.ptr()[3] || 'G' == parser_name.ptr()[3])) {
    parser = ObBuiltinTokenizer::BENG;
  }
  return parser;
}

void calc_tokenize_cache_hash(ObTokenizeCacheKey &key)
{
  key.hash_ = murmurhash(key.tenant_name_.ptr(), key.tenant_name_.length(), 0);
  key.hash_ = murmurhash(&key.collation_, sizeof(key.collation_), key.hash_);
  key.hash_ = murmurhash(&key.parser_, sizeof(key.parser_), key.hash_);
  key.hash_ = murmurhash(key.text_.ptr(), key.text_.length(), key.hash_);
}

int build_tokenize_cache_key(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             ObTokenizeCacheKey &key,
                             bool &cacheable)
{
  int ret = OB_SUCCESS;
  cacheable = false;
  ObDatum *text_datum = nullptr;
  ObDatum *parser_datum = nullptr;
  ObString parser_name;
  const ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  if (2 != expr.arg_cnt_
      || !expr.args_[0]->is_const_expr()
      || !expr.args_[1]->is_const_expr()
      || OB_ISNULL(session)) {
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, text_datum))) {
    LOG_WARN("fail to evaluate tokenize cache text", K(ret));
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, parser_datum))) {
    LOG_WARN("fail to evaluate tokenize cache parser", K(ret));
  } else if (OB_ISNULL(text_datum) || text_datum->is_null()
      || OB_ISNULL(parser_datum) || parser_datum->is_null()) {
  } else if (FALSE_IT(parser_name = parser_datum->get_string().trim())) {
  } else {
    key.parser_ = parse_builtin_tokenizer(parser_name);
  }

  if (OB_SUCC(ret) && ObBuiltinTokenizer::INVALID != key.parser_) {
    key.collation_ = expr.args_[0]->obj_meta_.get_collation_type();
    key.tenant_name_ = session->get_tenant_name();
    key.text_ = text_datum->get_string();
    cacheable = !key.tenant_name_.empty()
        && ObTokenizeResultCache::is_cacheable_text(key.text_);
  }
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
  ObTokenizeCacheKey cache_key;
  bool cacheable = false;
  bool cache_hit = false;
  ObTokenizeThreadCache &thread_cache = get_tokenize_thread_cache();

  // check param num, which is checked in ObExprOperator::calc_result_typeN.
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(build_tokenize_cache_key(expr, ctx, cache_key, cacheable))) {
    LOG_WARN("fail to build tokenize cache key", K(ret));
  } else if (cacheable
      && FALSE_IT(cache_hit = thread_cache.get(cache_key, expr, ctx, expr_datum))) {
  } else if (cache_hit) {
  } else if (cacheable
      && FALSE_IT(calc_tokenize_cache_hash(cache_key))
      && FALSE_IT(cache_hit = ObTokenizeResultCache::instance().get(
          cache_key, expr, ctx, expr_datum))) {
  } else if (cache_hit) {
    thread_cache.put(cache_key, expr_datum);
  } else if (OB_FAIL(parse_param(expr, ctx, temp_allocator, param))) {
    LOG_WARN("Fail to parse param", K(ret));
  } else if (OB_FAIL(tokenize_fulltext(param, param.output_mode_, temp_allocator, json_result))) {
    LOG_WARN("Fail to tokenize fulltext", K(ret));
  } else if (OB_FAIL(ObJsonExprHelper::pack_json_res(expr,
                                                     ctx,
                                                     temp_allocator,
                                                     json_result,
                                                     expr_datum))) {
    LOG_WARN("fail to pack json result", K(ret));
  } else if (cacheable) {
    ObTokenizeResultCache::instance().put(cache_key, expr_datum);
    thread_cache.put(cache_key, expr_datum);
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
  storage::ObFTTokenMap token_map;

  if (TokenizeParam::OUTPUT_MODE::DEFAULT != mode && TokenizeParam::OUTPUT_MODE::ALL != mode) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid output mode", K(ret), K(mode));
  } else if (OB_FAIL(tokenize_helper.init(&allocator, param.parser_name_, param.properties_))) {
    LOG_WARN("Fail to init tokenize helper", K(ret));
  } else if (OB_FAIL(token_map.create(ft_word_bkt_cnt, common::ObMemAttr("FTTokenMap")))) {
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

int ObExprTokenize::TokenizeParam::parse_json_param(const ObIJsonBase *obj,
                                                    const ObString &database_name,
                                                    const uint64_t tenant_id)
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
      if (OB_FAIL(ObFTParserJsonProps::tokenize_array_to_props_json(
                      allocator_, val, database_name, tenant_id, json_str))) {
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
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  const uint64_t tenant_id = common::OB_SERVER_TENANT_ID;
  const ObString database_name = OB_ISNULL(session)
      ? ObString() : session->get_database_name();

  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Args count invalid.", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(parse_fulltext(expr, ctx, param))) {
    LOG_WARN("Fail to parse fulltext.", K(ret));
  } else if (OB_FAIL(parse_parser_name(expr, ctx, param))) {
    LOG_WARN("Fail to parse parser params.", K(ret));
  } else if (OB_FAIL(parse_parser_properties(
                         expr, database_name, tenant_id, ctx, temp_allocator, param))) {
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
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
        param.allocator_,
        *fulltext_datum,
        expr.args_[0]->datum_meta_,
        expr.args_[0]->obj_meta_.has_lob_header(),
        param.fulltext_,
        &ctx.exec_ctx_))) {
      LOG_WARN("Fail to read fulltext datum.", K(ret),
          K(expr.args_[0]->datum_meta_), K(expr.args_[0]->obj_meta_));
    }
    if (OB_SUCC(ret)) {
      param.meta_.set_varchar(); // as we hardcoded in fts_index
      param.meta_.set_collation_type(expr.args_[0]->obj_meta_.get_collation_type());
    }
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
                                            const ObString &database_name,
                                            const uint64_t tenant_id,
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
          } else if (OB_FAIL(param.parse_json_param(node, database_name, tenant_id))) {
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
