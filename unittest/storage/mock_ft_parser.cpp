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

#include "mock_ft_parser.h"

OB_DECLARE_PLUGIN(mock_ft_parser)
{
  oceanbase::lib::ObPluginType::OB_FT_PARSER_PLUGIN,
  "mock_ft_parser",
  OB_PLUGIN_AUTHOR_OCEANBASE,
  "This is mock fulltext parser plugin.",
  0x00001,
  oceanbase::lib::ObPluginLicenseType::OB_Mulan_PubL_V2_LICENSE,
  &oceanbase::storage::mock_ft_parser,
};
