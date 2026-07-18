/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/ob_ft_token_processor.h"

#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_plugin_helper.h"

namespace oceanbase
{
namespace storage
{

int ObFTTokenProcessor::init(
    const ObFTParserProperty &property,
    const ObObjMeta &meta,
    const ObAddWordFlag &flag,
    ObFTTokenMap *token_map)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("fulltext token processor initialized twice", K(ret));
  } else if (OB_ISNULL(token_map)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fulltext token map is null", K(ret));
  } else {
    sql::ObExprBasicFuncs *basic_funcs =
        ObDatumFuncs::get_basic_func(meta.get_type(), meta.get_collation_type());
    ObDatumCmpFuncType cmp_func = get_datum_cmp_func(meta, meta);
    if (OB_ISNULL(basic_funcs) || OB_ISNULL(basic_funcs->default_hash_)
        || OB_ISNULL(cmp_func)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get token datum functions", K(ret), K(meta));
    } else if (flag.stopword()
        && OB_FAIL(ObFTParsePluginData::instance().get_stop_token_checker(
            meta.get_collation_type(), stop_token_checker_))) {
      LOG_WARN("failed to get stop token checker", K(ret), K(meta));
    } else {
      token_meta_ = meta;
      token_map_ = token_map;
      non_stop_token_cnt_ = 0;
      min_token_size_ = property.min_token_size_;
      max_token_size_ = property.max_token_size_;
      flag_ = flag;
      hash_func_ = basic_funcs->default_hash_;
      cmp_func_ = cmp_func;
      is_inited_ = true;
    }
  }
  return ret;
}

void ObFTTokenProcessor::reset()
{
  token_meta_.reset();
  token_map_ = nullptr;
  non_stop_token_cnt_ = 0;
  min_token_size_ = 0;
  max_token_size_ = 0;
  flag_.clear();
  hash_func_ = nullptr;
  cmp_func_ = nullptr;
  stop_token_checker_.reset();
  is_inited_ = false;
}

void ObFTTokenProcessor::reuse()
{
  non_stop_token_cnt_ = 0;
}

int ObFTTokenProcessor::process_token(
    const char *token,
    const int64_t token_len,
    const int64_t char_cnt,
    const int64_t token_freq)
{
  int ret = OB_SUCCESS;
  bool is_stop_token = false;
  ObFTToken ft_token;
  ObString source(token_len, token);
  ObString regularized;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("fulltext token processor is not initialized", K(ret));
  } else if (OB_ISNULL(token) || OB_UNLIKELY(token_len <= 0 || char_cnt <= 0 || token_freq <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext token", K(ret), KP(token), K(token_len), K(char_cnt), K(token_freq));
  } else if (flag_.casedown()
      && OB_FAIL(ObCharset::tolower(token_meta_.get_collation_type(),
                                    source,
                                    regularized,
                                    scratch_allocator_))) {
    LOG_WARN("failed to lowercase fulltext token", K(ret), K(token_meta_), K(token_len));
  } else if (flag_.casedown() && regularized.empty()) {
    // Invalid token text is filtered, matching the legacy token processor.
  } else if (OB_FAIL(ft_token.init(flag_.casedown() ? regularized.ptr() : token,
                                   flag_.casedown() ? regularized.length() : token_len,
                                   token_meta_,
                                   hash_func_,
                                   cmp_func_))) {
    LOG_WARN("failed to initialize fulltext token", K(ret), K(token_len), K(char_cnt));
  } else if (!ft_token.empty() && is_min_max_token(char_cnt)) {
    // Filtered by configured token length.
  } else if (!ft_token.empty() && flag_.stopword()
      && OB_FAIL(stop_token_checker_.check_is_stop_token(ft_token, is_stop_token))) {
    LOG_WARN("failed to check stop token", K(ret), K(ft_token));
  } else if (!ft_token.empty() && is_stop_token) {
    // Filtered by the immutable per-collation stop-token table.
  } else if (!ft_token.empty() && OB_FAIL(groupby_token(ft_token, token_freq))) {
    LOG_WARN("failed to aggregate fulltext token", K(ret), K(ft_token), K(token_freq));
  } else if (!ft_token.empty()) {
    non_stop_token_cnt_ += token_freq;
  }
  return ret;
}

bool ObFTTokenProcessor::is_min_max_token(const int64_t char_cnt) const
{
  return char_cnt > MAX_CHAR_COUNT_PER_TOKEN
      || (flag_.min_max_word()
          && (char_cnt < min_token_size_ || char_cnt > max_token_size_));
}

int ObFTTokenProcessor::groupby_token(const ObFTToken &token, const int64_t token_freq)
{
  int ret = OB_SUCCESS;
  if (flag_.groupby_word()) {
    ObFTTokenInfo token_info;
    UpdateTokenCallback callback(token_freq);
    token_info.count_ = token_freq;
    if (OB_FAIL(token_map_->set_or_update(token, token_info, callback))) {
      LOG_WARN("failed to set or update token", K(ret), K(token), K(token_freq));
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
