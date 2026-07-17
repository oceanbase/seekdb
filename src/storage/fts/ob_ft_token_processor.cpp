/*
 * Copyright (c) 2026 OceanBase.
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

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/ob_ft_token_processor.h"

#include "share/datum/ob_datum_funcs.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_plugin_helper.h"
#include "storage/ob_storage_util.h"

namespace oceanbase
{
namespace storage
{

int ObFTTokenProcessor::init(const ObFTParserProperty &property,
                             const ObObjMeta &meta,
                             const ObProcessTokenFlag &flag,
                             ObFTTokenMap *token_map)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(token_map)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObFTParsePluginData::instance().get_stop_token_checker(
                         meta.get_collation_type(), stop_token_checker_))) {
    LOG_WARN("failed to get stop token checker", K(ret), K(meta));
  } else {
    sql::ObExprBasicFuncs *basic_funcs =
        ObDatumFuncs::get_basic_func(meta.get_type(), meta.get_collation_type());
    ObDatumCmpFuncType cmp_func = get_datum_cmp_func(meta, meta);
    if (OB_ISNULL(basic_funcs) || OB_ISNULL(basic_funcs->default_hash_) || OB_ISNULL(cmp_func)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to cache token datum functions", K(ret), K(meta), KP(basic_funcs), KP(cmp_func));
    } else {
      token_meta_ = meta;
      token_map_ = token_map;
      min_max_token_cnt_ = 0;
      non_stop_token_cnt_ = 0;
      stop_token_cnt_ = 0;
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
  min_max_token_cnt_ = 0;
  non_stop_token_cnt_ = 0;
  stop_token_cnt_ = 0;
  min_token_size_ = 0;
  max_token_size_ = 0;
  flag_.reset();
  hash_func_ = nullptr;
  cmp_func_ = nullptr;
  stop_token_checker_.reset();
  is_inited_ = false;
}

void ObFTTokenProcessor::reuse()
{
  min_max_token_cnt_ = 0;
  non_stop_token_cnt_ = 0;
  stop_token_cnt_ = 0;
}

int ObFTTokenProcessor::process_token(const bool need_pos_list,
                                      const char *token,
                                      const int64_t token_len,
                                      const int64_t char_cnt,
                                      const int64_t position)
{
  int ret = OB_SUCCESS;
  bool is_stop_token = false;
  ObFTToken src_token;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(nullptr == token || token_len <= 0 || char_cnt <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(src_token.init(token, token_len, token_meta_, hash_func_, cmp_func_))) {
    LOG_WARN("failed to initialize token", K(ret), KP(token), K(token_len), K(token_meta_));
  } else if (OB_UNLIKELY(is_min_max_token_(char_cnt))) {
    ++min_max_token_cnt_;
  } else if (flag_.stop_token()
             && OB_FAIL(stop_token_checker_.check_is_stop_token(src_token, is_stop_token))) {
    LOG_WARN("failed to check stop token", K(ret), K(src_token));
  } else if (OB_UNLIKELY(is_stop_token)) {
    ++stop_token_cnt_;
  } else if (OB_FAIL(groupby_token_(need_pos_list, src_token, position))) {
    LOG_WARN("failed to aggregate token", K(ret), K(src_token), K(position));
  } else {
    ++non_stop_token_cnt_;
  }
  return ret;
}

bool ObFTTokenProcessor::is_min_max_token_(const int64_t char_cnt) const
{
  return char_cnt > MAX_CHAR_COUNT_PER_TOKEN
      || (flag_.min_max_token() && (char_cnt < min_token_size_ || char_cnt > max_token_size_));
}

int ObFTTokenProcessor::groupby_token_(const bool need_pos_list,
                                       const ObFTToken &token,
                                       const int64_t position)
{
  int ret = OB_SUCCESS;
  if (flag_.groupby_token()) {
    ObFTTokenInfo token_info;
    if (need_pos_list) {
      UpdateTokenCallBack callback(scratch_allocator_, position);
      if (OB_FAIL(token_info.update_one_position(scratch_allocator_, position))) {
        LOG_WARN("failed to initialize token position", K(ret), K(token), K(position));
      } else if (OB_FAIL(token_map_->set_or_update(token, token_info, callback))) {
        LOG_WARN("failed to aggregate token position", K(ret), K(token), K(position));
      }
    } else {
      UpdateTokenWithoutPosListCallBack callback;
      if (OB_FAIL(token_info.update_without_pos_list())) {
        LOG_WARN("failed to initialize token count", K(ret), K(token));
      } else if (OB_FAIL(token_map_->set_or_update(token, token_info, callback))) {
        LOG_WARN("failed to aggregate token count", K(ret), K(token));
      }
    }
  }
  return ret;
}

int ObFTTokenProcessor::UpdateTokenCallBack::operator()(
    common::hash::HashMapPair<ObFTToken, ObFTTokenInfo> &pair)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(pair.second.update_one_position(allocator_, position_))) {
    LOG_WARN("failed to update token position", K(ret), K(position_), K(pair.first));
  }
  return ret;
}

int ObFTTokenProcessor::UpdateTokenWithoutPosListCallBack::operator()(
    common::hash::HashMapPair<ObFTToken, ObFTTokenInfo> &pair)
{
  return pair.second.update_without_pos_list();
}

} // namespace storage
} // namespace oceanbase
