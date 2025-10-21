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

#ifndef OB_BASE64_ENCODE_H_
#define OB_BASE64_ENCODE_H_

#include <cctype>
#include <cstdint>

namespace oceanbase
{
namespace common
{
class ObBase64Encoder
{
private:
template<int N>
friend struct InitBase64Values;
static char BASE64_CHARS[];
static int FROM_BASE64_TABLE[];

  static uint8_t BASE64_VALUES[256];

  static inline bool is_base64_char(char c)
  {
    return std::isalnum(c) || c == '+' || c == '/';
  }

  static const int64_t SOFT_NEW_LINE_STR_POS = 19;
public:
  static constexpr int64_t needed_encoded_length(const int64_t buf_size)
  {
    return (buf_size / 3) * 4 + (buf_size % 3 == 0 ? 0 : 4);
  }

  static constexpr int64_t needed_decoded_length(const int64_t buf_size)
  {
    return (buf_size / 4) * 3;
  }

  static inline bool my_base64_decoder_skip_spaces(char c)
  {
    if (FROM_BASE64_TABLE[(uint8_t) c] != -2) {
      return false;
    }

    return true;
  }

  static int encode(const uint8_t* input, const int64_t input_len,
                    char* output, const int64_t output_len,
                    int64_t &pos, const int16_t wrap = 0);

  static int decode(const char* input, const int64_t input_len,
                    uint8_t* output, const int64_t output_len,
                    int64_t &pos, bool skip_spaces = false);
};
} // end namespace common
} // end namespace oceanbase
#endif // !OB_BASE64_ENCODE_H_
