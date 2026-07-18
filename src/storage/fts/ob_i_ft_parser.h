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

#ifndef OB_I_FT_PARSER_H_
#define OB_I_FT_PARSER_H_

#include "plugin/interface/ob_plugin_ftparser_intf.h"

namespace oceanbase
{
namespace storage
{

class ObIFTParser : public plugin::ObITokenIterator
{
public:
  ObIFTParser() = default;
  virtual ~ObIFTParser() = default;
  virtual int reuse_parser(const char *fulltext, const int64_t fulltext_len) = 0;
};

} // namespace storage
} // namespace oceanbase

#endif // OB_I_FT_PARSER_H_
