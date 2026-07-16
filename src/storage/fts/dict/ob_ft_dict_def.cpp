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

#include "storage/fts/dict/ob_ft_dict_def.h"

namespace oceanbase
{
namespace storage
{

bool ObFTSingleToken::operator==(const ObFTSingleToken &other) const
{
  return (this == &other)
         || (token_char_len_ == other.token_char_len_ && 0 == memcmp(token_, other.token_, token_char_len_));
}

int ObFTSingleToken::set_token(const char *token, int32_t token_len)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == token || token_len <= 0 || token_len > ObCharset::MAX_MB_LEN)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    memcpy(this->token_, token, token_len);
    this->token_char_len_ = token_len;
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
