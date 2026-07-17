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
#include "lib/lock/ob_spin_lock.h"
#include "lib/string/ob_string.h"
#include "share/cache/ob_kv_storecache.h"
#include "share/cache/ob_kvcache_struct.h"
#include "sql/engine/expr/ob_i_expr_extra_info.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "storage/fts/ob_fts_parser_property.h"

namespace oceanbase
{
namespace sql
{
class MultimodeAlloctor;

struct ObTokenizeFixedConfig final : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  ObTokenizeFixedConfig(common::ObIAllocator &allocator,
                        const ObExprOperatorType type)
      : ObIExprExtraInfo(allocator, type),
        is_valid_(false),
        cacheable_(false),
        output_mode_(0),
        parser_version_(-1),
        collation_(common::CS_TYPE_INVALID),
        fixed_hash_(0),
        parser_name_(),
        normalized_properties_()
  {}
  ~ObTokenizeFixedConfig() = default;

  int deep_copy(common::ObIAllocator &allocator,
                const ObExprOperatorType type,
                ObIExprExtraInfo *&copied_info) const override;

  bool is_valid_;
  bool cacheable_;
  int8_t output_mode_;
  int64_t parser_version_;
  common::ObCollationType collation_;
  uint64_t fixed_hash_;
  common::ObString parser_name_;
  common::ObString normalized_properties_;
};

class ObTokenizeResultCacheKey : public common::ObIKVCacheKey
{
public:
  ObTokenizeResultCacheKey(const uint64_t tenant_id,
                           const common::ObString &tenant_name,
                           const common::ObString &fulltext,
                           const common::ObCollationType collation,
                           const int8_t output_mode,
                           const common::ObString &parser_name,
                           const int64_t parser_version,
                           const common::ObString &properties,
                           const uint64_t fixed_hash);
  ~ObTokenizeResultCacheKey() override = default;

  bool operator==(const common::ObIKVCacheKey &other) const override;
  int equal(const common::ObIKVCacheKey &other, bool &equal) const override;
  int hash(uint64_t &hash_value) const override;
  int64_t size() const override;
  int deep_copy(char *buf,
                const int64_t buf_len,
                common::ObIKVCacheKey *&key) const override;

  uint64_t tenant_id() const { return tenant_id_; }
  static uint64_t calc_fixed_hash(const common::ObCollationType collation,
                                  const int8_t output_mode,
                                  const common::ObString &parser_name,
                                  const int64_t parser_version,
                                  const common::ObString &properties);

private:
  uint64_t tenant_id_;
  uint64_t fulltext_hash_;
  uint64_t fixed_hash_;
  common::ObCollationType collation_;
  int8_t output_mode_;
  int64_t parser_version_;
  common::ObString tenant_name_;
  common::ObString fulltext_;
  common::ObString parser_name_;
  common::ObString properties_;
};

class ObTokenizeResultCacheValue : public common::ObIKVCacheValue
{
public:
  explicit ObTokenizeResultCacheValue(const common::ObString &json) : json_(json) {}
  ~ObTokenizeResultCacheValue() override = default;
  int64_t size() const override;
  int deep_copy(char *buf,
                const int64_t buf_len,
                common::ObIKVCacheValue *&value) const override;
  const common::ObString &json() const { return json_; }

private:
  common::ObString json_;
};

class ObTokenizeResultCache
{
public:
  static ObTokenizeResultCache &get_instance();
  int init();
  void destroy();
  int get(const ObTokenizeResultCacheKey &key,
          const ObTokenizeResultCacheValue *&value,
          common::ObKVCacheHandle &handle);
  int put(const ObTokenizeResultCacheKey &key,
          const ObTokenizeResultCacheValue &value);

private:
  ObTokenizeResultCache() : cache_(), lock_(), is_inited_(false) {}
  ~ObTokenizeResultCache() { destroy(); }
  int evict_one(const uint64_t tenant_id, const bool tenant_only);

private:
  static constexpr int64_t MAX_ENTRY_COUNT = 128;
  common::ObKVCache<ObTokenizeResultCacheKey, ObTokenizeResultCacheValue> cache_;
  common::ObSpinLock lock_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObTokenizeResultCache);
};

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

  static bool can_use_result_cache(const ObExpr &expr,
                                   const TokenizeParam &param,
                                   int64_t &parser_version);
  static bool is_result_cacheable_parser(const storage::ObFTParser &parser,
                                         const storage::ObFTParserJsonProps &properties);
  static int build_fixed_config(ObExprCGCtx &op_cg_ctx,
                                const ObRawExpr &raw_expr,
                                ObExpr &rt_expr,
                                ObTokenizeFixedConfig *&fixed_config);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprTokenize);
};

} // namespace sql
} // namespace oceanbase

#endif // _OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_TOKENIZE_H_
