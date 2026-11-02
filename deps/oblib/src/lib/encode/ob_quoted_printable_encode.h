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

#ifndef OB_QUOTED_PRINTABLE_ENCODE_H_
#define OB_QUOTED_PRINTABLE_ENCODE_H_

#include <cctype>
#include <cstdint>

namespace oceanbase
{
namespace common
{

class ObQuotedPrintableEncoder
{
private:
public:
  static char hex[];
  const static uint8_t QP_ESCAPE_CHAR = 61;
  const static uint8_t QP_TAB = 9;
  const static uint8_t QP_SPACE = 32;
  const static uint8_t QP_CR = 13;
  const static uint8_t QP_LF = 10;
  const static uint8_t QP_QUESTIONMARK = 63;
  const static uint8_t QP_UNDERLINE = 95;
  const static int BYTE_PER_LINE = 76;
  const static uint8_t PRINTABLE_START = 33;
  const static uint8_t PRINTABLE_END = 126;
  ObQuotedPrintableEncoder ();
  ~ObQuotedPrintableEncoder();


};



} //namespace common
} // namespace oceanbase
#endif //OB_QUOTED_PRINTABLE_ENCODE_H_
