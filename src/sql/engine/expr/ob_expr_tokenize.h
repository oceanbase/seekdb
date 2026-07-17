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

#ifndef _OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_TOKENIZE_H_
#define _OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_TOKENIZE_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/string/ob_string.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "storage/fts/ob_fts_parser_property.h"

namespace oceanbase
{
namespace sql
{
class MultimodeAlloctor;
class ObExprTokenize : public ObStringExprOperator
{
public:
  explicit ObExprTokenize(common::ObIAllocator &alloc);
  ~ObExprTokenize() override;
  /**
   * @brief evaluate function
   * @param expr expression
   * @param ctx expression evaluation context
   * @param expr_datum expression result
   * @note see cg_expr REG_OP and g_expr_eval_functions
   */
  static int eval_tokenize(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);

  int calc_result_typeN(ObExprResType &type,
                        ObExprResType *types,
                        int64_t param_num,
                        common::ObExprTypeCtx &type_ctx) const override;
  int cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const override;

private:
  struct TokenizeParam
  {
  public:
    static constexpr const char *CASE_INDICATOR_STR = "case";
    static constexpr const char *OUTPUT_MODE_STR = "output";
    static constexpr const char *STOPWORDS_LIST_STR = "stopwords";
    static constexpr const char *ADDITIONAL_ARGS_STR = "additional_args";

  public:
    TokenizeParam();

    int parse_json_param(const ObIJsonBase *obj);

    // check and reform parser properties to standard format
    int reform_parser_properties(const ObString &properties);
    int try_load_dictionary_for_ik();

  public:
    // for property and tmp json string
    mutable ObArenaAllocator allocator_;
    ObString parser_name_;
    ObString properties_;
    ObObjMeta meta_;
    ObString fulltext_;
    enum OUTPUT_MODE
    {
      DEFAULT,
      ALL,
    } output_mode_;
  };

private:
  static int parse_param(const ObExpr &expr,
                         ObEvalCtx &ctx,
                         common::ObArenaAllocator &allocator,
                         TokenizeParam &param);

  static int parse_fulltext(const ObExpr &expr, ObEvalCtx &ctx, TokenizeParam &param);
  static int parse_parser_name(const ObExpr &expr, ObEvalCtx &ctx, TokenizeParam &param);
  static int parse_parser_properties(const ObExpr &expr,
                                     ObEvalCtx &ctx,
                                     MultimodeAlloctor &mm_alloc,
                                     TokenizeParam &param);

  static int tokenize_fulltext(const TokenizeParam &param,
                               TokenizeParam::OUTPUT_MODE mode,
                               common::ObIAllocator &allocator,
                               ObIJsonBase *&result);

  static int construct_ft_parser_inner_name(const ObString &input_str, TokenizeParam &param);

private:
  // ----- Content-dedup cache -----
  // When the same (parser_name, properties, fulltext) tuple is tokenized
  // repeatedly (e.g. a hot MATCH(...) loop or a derived table referenced
  // many times in one plan), the JSON output is identical. We cache the
  // serialized JSON bytes per tuple and skip segment() on cache hit.
  //
  // The cache is process-wide (static) — tokenize_fulltext is on a SQL hot
  // path, and we'd rather have one shared LRU than per-call/per-allocator
  // state. Mutex is held only on insert/lookup; reads after the lookup
  // return a borrowed bytes pointer that the caller's arena allocator
  // copies out.
  struct DedupCacheKey
  {
    DedupCacheKey() : fulltext_hash_(0), parser_hash_(0), props_hash_(0) {}
    DedupCacheKey(uint64_t ft_h, uint64_t p_h, uint64_t pr_h)
        : fulltext_hash_(ft_h), parser_hash_(p_h), props_hash_(pr_h) {}
    uint64_t fulltext_hash_;
    uint64_t parser_hash_;
    uint64_t props_hash_;
    bool operator==(const DedupCacheKey &other) const
    {
      return fulltext_hash_ == other.fulltext_hash_
          && parser_hash_ == other.parser_hash_
          && props_hash_  == other.props_hash_;
    }
    // Required by hash::ObHashMap / hash::hash_func. The argument is the
    // output seed: we mix our three component hashes into it via FNV-like
    // combines. The return value must be OB_SUCCESS (or any signed int ≤ 0
    // is treated as an error by hash_func). Always returns OB_SUCCESS and
    // writes the actual hash through `res`.
    int hash(uint64_t &res) const
    {
      uint64_t h = fulltext_hash_;
      h ^= parser_hash_ + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      h ^= props_hash_  + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      res = h;
      return OB_SUCCESS;
    }
    TO_STRING_KV(K_(fulltext_hash), K_(parser_hash), K_(props_hash));
  };
  struct DedupCacheValue
  {
    DedupCacheValue() : json_buf_(nullptr), json_len_(0) {}
    char *json_buf_;
    int64_t json_len_;
  };
  static constexpr int64_t DEDUP_CACHE_BUCKET_CNT = 128;
  // Process-wide cache + mutex. Sized small to keep memory bounded; the
  // hot workload (same content tokenized many times) fits comfortably.
  static hash::ObHashMap<DedupCacheKey, DedupCacheValue> &get_dedup_cache();
  static lib::ObMutex &get_dedup_mutex();
  static uint64_t hash_bytes(const char *data, int64_t len);

  // Look up a cached JSON blob by (parser_name, properties, fulltext).
  // On hit, allocates a copy into `out_buf` / sets `out_len` and returns
  // OB_SUCCESS. On miss, returns OB_HASH_NOT_EXIST.
  static int dedup_cache_lookup(const ObString &parser_name,
                                const ObString &properties,
                                const ObString &fulltext,
                                const char *&out_buf,
                                int64_t &out_len);
  // Store a serialized JSON blob keyed by (parser_name, properties, fulltext).
  // Caller passes the bytes; we copy them into the cache's allocator.
  static int dedup_cache_store(const ObString &parser_name,
                               const ObString &properties,
                               const ObString &fulltext,
                               const char *json_buf,
                               int64_t json_len);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprTokenize);
};

} // namespace sql
} // namespace oceanbase

#endif // _OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_TOKENIZE_H_
