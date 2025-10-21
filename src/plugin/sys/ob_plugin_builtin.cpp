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

#include "lib/ob_errno.h"
#include "plugin/sys/ob_plugin_builtin.h"

using namespace oceanbase::common;
using namespace oceanbase::plugin;

#define OB_DECLARE_BUILTIN_PLUGIN(name)                                                     \
  do {                                                                                      \
    extern ObPlugin OBP_BUILTIN_PLUGIN_VAR(name);                                           \
    const char *plugin_name = OBP_STRINGIZE(OBP_BUILTIN_PLUGIN_VAR(name));                  \
    ObPlugin *plugin = &OBP_BUILTIN_PLUGIN_VAR(name);                                       \
    if (OB_SUCC(ret) && OB_FAIL(plugins.push_back(ObBuiltinPlugin(plugin_name, plugin)))) { \
      LOG_WARN("failed to push back builtin plugin", KCSTRING(#name), K(ret));              \
    }                                                                                       \
  } while (false)

extern "C" {
static int __plugin_register_global_plugins(ObIArray<ObBuiltinPlugin> &plugins)
{
  int ret = OB_SUCCESS;
  /// Append builtin plugins here
  OB_DECLARE_BUILTIN_PLUGIN(fts_parser);
  return ret;
}

} // extern "C"

namespace oceanbase {
namespace plugin {

int plugin_register_global_plugins(ObIArray<ObBuiltinPlugin> &plugins)
{
  return __plugin_register_global_plugins(plugins);
}

} // namespace plugin
} // namespace oceanbase
