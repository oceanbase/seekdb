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

#ifndef OCEANBASE_PLUGIN_BUILTIN_H
#define OCEANBASE_PLUGIN_BUILTIN_H

#include "oceanbase/ob_plugin.h"
#include "lib/container/ob_iarray.h"
#include "plugin/adaptor/ob_plugin_adaptor.h"

namespace oceanbase {
namespace plugin {

struct ObBuiltinPlugin final
{
  ObString        name;
  ObPluginAdaptor plugin;

  ObBuiltinPlugin() = default;
  ObBuiltinPlugin(const char *_name, ObPlugin *_plugin) : name(_name), plugin(_plugin)
  {}

  TO_STRING_KV(K(name), K(plugin));
};

int plugin_register_global_plugins(common::ObIArray<ObBuiltinPlugin> &plugins);

} // namespace plugin
} // namespace oceanbase

#endif // OCEANBASE_PLUGIN_BUILTIN_H
