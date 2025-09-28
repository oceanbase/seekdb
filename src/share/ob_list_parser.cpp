/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SHARE

#include "ob_list_parser.h"
#include "lib/oblog/ob_log.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace share
{

int ObListParser::match(int sym)
{
  int ret = OB_SUCCESS;
  if (sym != token_) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (sym == SYM_VALUE && NULL != cb_) {
      ret = cb_->match(value_buf_);
    }
    if (OB_SUCC(ret)) {
      ret = get_token();
    }
  }
  return ret;
}

int ObListParser::get_token()
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret)) {
    if ('\0' == *cur_) {
      token_ = SYM_END;
      break;
    } else if (SYM_LIST_SEP == *cur_) {
      cur_++;
      token_ = SYM_LIST_SEP;
      break;
    } else if (isspace(*cur_)) {
      if (allow_space_) {
        cur_++; // Skip the spaces before and after the token
      } else {
        ret = OB_INVALID_ARGUMENT;
      }
    } else if (isprint(*cur_)) {
      const char *start = cur_;
      while (isprint(*cur_) && !isspace(*cur_) && SYM_LIST_SEP != *cur_ && '\0' != *cur_) {
        cur_++;
      }
      if (cur_ - start >= MAX_TOKEN_SIZE) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("token size is too large", "actual", cur_ - start, K(ret));
      } else {
        STRNCPY(value_buf_, start, cur_ - start);
        value_buf_[cur_ - start] = '\0';
        token_ = SYM_VALUE;
      }
      break;
    } else {
      // Unknown character
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}


}/* ns share*/
}/* ns oceanbase */
