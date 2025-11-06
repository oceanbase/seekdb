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

#pragma once

#include "lib/alloc/alloc_struct.h"
#include "lib/string/ob_string.h"
#include "oceanbase/ob_plugin.h"

namespace oceanbase {
namespace plugin {

extern lib::ObLabel OB_PLUGIN_MEMORY_LABEL;

constexpr int OB_PLUGIN_NAME_MAX_LENGTH = 64;

/**
 * get type name of `ObPluginType`
 */
const char *ob_plugin_type_to_string(ObPluginType type);

/**
 * A hash function for case-insensitive string
 * @details plugin name is not case-sensitive
 */
struct ObPluginNameHash
{
  int operator() (const common::ObString &name, uint64_t &res) const;
};

/**
 * An equal-to function for case-insensitive string
 * @details plugin name is not case-sensitive
 */
struct ObPluginNameEqual
{
  bool operator()(const common::ObString &name1, const ObString &name2) const;
};

} // namespace plugin
} // namespace oceanbase
