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
    // handle previous hits_ first and then check from this char
    int64_t alive_cnt = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < hits_.count(); ++i) {
      ObDATrieHit &hit = hits_.at(i);
      bool keep = true;
      if (OB_FAIL(dict_main_.match_with_hit({char_len, ch}, hit, hit))) {
      } else if (hit.is_match()) {
        if (OB_FAIL(ctx.add_token(ctx.fulltext(),
                                  hit.start_pos_,
                                  hit.end_pos_ - hit.start_pos_,
                                  hit.char_cnt_,
                                  ObIKTokenType::IK_CHINESE_TOKEN))) {
        } else if (hit.is_prefix()) {
          // match will record the start_cursor
        }
      } else if (hit.is_prefix()) {
        // nothing
      } else if (hit.is_unmatch()) {
        keep = false;
      } else {
        ret = OB_UNEXPECT_INTERNAL_ERROR;
      }
      if (OB_SUCC(ret) && keep) {
        if (alive_cnt != i) {
          hits_.at(alive_cnt) = hit;
        }
        ++alive_cnt;
      }
    }
    if (OB_SUCC(ret)) {
      while (hits_.count() > alive_cnt) {
        hits_.pop_back();
      }
    }
    if (OB_SUCC(ret)) {
      // Start from this char
      // find range of dict
      ObDATrieHit hit(&dict_main_, ctx.get_cursor());
      if (OB_FAIL(dict_main_.match({char_len, ch}, hit))) {
      } else if (hit.is_match()) {
        // output token
        hits_.push_back(hit);
        ctx.add_token(ctx.fulltext(),
                      ctx.get_cursor(),
                      char_len,
                      1,
                      ObIKTokenType::IK_CHINESE_TOKEN);
      } else if (hit.is_prefix()) {
        hits_.push_back(hit);
      } else {
        // ignore mismatch
      }
    }
  } else {
    // stop previous match
    hits_.reuse();
  }

  if (OB_SUCC(ret)) {
    if (ctx.is_last()) {
      hits_.reuse();
    }
  } else {
    hits_.reuse();
  }

  return ret;
}

} //  namespace storage
} //  namespace oceanbase
