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
