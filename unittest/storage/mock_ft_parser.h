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

#ifndef MOCK_FT_PARSER_H_
#define MOCK_FT_PARSER_H_

#include "plugin/interface/ob_plugin_ftparser_intf.h"

namespace oceanbase
{
namespace storage
{

class ObMockFTParserDesc final : public plugin::ObIFTParserDesc
{
public:
  ObMockFTParserDesc() = default;
  virtual ~ObMockFTParserDesc() = default;
  virtual int init(plugin::ObPluginParam *param) override;
  virtual int deinit(plugin::ObPluginParam *param) override;
  virtual int segment(plugin::ObFTParserParam *param, plugin::ObITokenIterator *&iter) const override;
};

int ObMockFTParserDesc::init(plugin::ObPluginParam *param)
{
  UNUSEDx(param);
  return OB_SUCCESS;
}

int ObMockFTParserDesc::deinit(plugin::ObPluginParam *param)
{
  UNUSED(param);
  return OB_SUCCESS;
}

int ObMockFTParserDesc::segment(plugin::ObFTParserParam *param, plugin::ObITokenIterator *&iter) const
{
  UNUSED(param);
  return OB_SUCCESS;
}

static ObMockFTParserDesc mock_ft_parser;

} // end storage
} // end oceanbase

#endif // MOCK_FT_PARSER_H_
