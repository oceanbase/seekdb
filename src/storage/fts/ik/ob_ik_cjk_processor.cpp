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

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/ik/ob_ik_cjk_processor.h"

#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/ik/ob_ik_char_util.h"
#include "storage/fts/ik/ob_ik_token.h"

namespace oceanbase
{
namespace storage
{
int ObIKCJKProcessor::do_process(TokenizeContext &ctx,
                                 const char *ch,
                                 const uint8_t char_len,
                                 const ObFTCharUtil::CharType type)
{
  int ret = OB_SUCCESS;

  if (ObFTCharUtil::CharType::USELESS != type) {
    // seekdb: a configured custom dict_table REPLACES the built-in main word dict for segmentation
    // (matching OceanBase): match only against the custom dict, so any multi-char word not listed
    // in it falls through to single-character tokens. Without a custom dict, use the built-in main
    // dict. The IK arbitrator resolves overlaps downstream either way.
    if (OB_NOT_NULL(dict_custom_)) {
      if (OB_FAIL(do_match(ctx, ch, char_len, *dict_custom_, custom_hits_))) {
        LOG_WARN("fail to match against custom dict", K(ret));
      }
    } else if (OB_FAIL(do_match(ctx, ch, char_len, dict_main_, hits_))) {
      LOG_WARN("fail to match against main dict", K(ret));
    }
  } else {
    // stop previous match
    hits_.clear();
    custom_hits_.clear();
  }

  if (OB_SUCC(ret)) {
    if (ctx.is_last()) {
      hits_.clear();
      custom_hits_.clear();
    }
  } else {
    hits_.clear();
    custom_hits_.clear();
  }

  return ret;
}

int ObIKCJKProcessor::do_match(TokenizeContext &ctx,
                               const char *ch,
                               const uint8_t char_len,
                               const ObIFTDict &dict,
                               ObList<ObDATrieHit, ObIAllocator> &hits)
{
  int ret = OB_SUCCESS;
  // handle previous hits first and then check from this char
  for (ObList<ObDATrieHit, ObIAllocator>::iterator iter = hits.begin();
       OB_SUCC(ret) && iter != hits.end();
       iter++) {
    ObDATrieHit &hit = *iter;
    if (OB_FAIL(dict.match_with_hit({char_len, ch}, hit, hit))) {
      LOG_WARN("fail to match with hit", K(ret));
    } else if (hit.is_match()) {
      if (OB_FAIL(ctx.add_token(ctx.fulltext(),
                                hit.start_pos_,
                                hit.end_pos_ - hit.start_pos_,
                                hit.char_cnt_,
                                ObIKTokenType::IK_CHINESE_TOKEN))) {
        LOG_WARN("Fail to add chinese token");
      } else if (hit.is_prefix()) {
        // match will record the start_cursor
      }
    } else if (hit.is_prefix()) {
      // nothing
    } else if (hit.is_unmatch()) {
      hits.erase(hit);
    } else {
      ret = OB_UNEXPECT_INTERNAL_ERROR;
      LOG_WARN("Match dict reach impossible path.", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    // Start from this char
    ObDATrieHit hit(&dict, ctx.get_cursor());
    if (OB_FAIL(dict.match({char_len, ch}, hit))) {
      LOG_WARN("Fail to match", K(ret));
    } else if (hit.is_match()) {
      // output token
      hits.push_back(hit);
      ctx.add_token(ctx.fulltext(),
                    ctx.get_cursor(),
                    char_len,
                    1,
                    ObIKTokenType::IK_CHINESE_TOKEN);
    } else if (hit.is_prefix()) {
      hits.push_back(hit);
    } else {
      // ignore mismatch
    }
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
