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

#include "lib/utility/ob_print_utils.h"
#include "oceanbase/ob_plugin.h"
#include "lib/alloc/alloc_struct.h"

namespace oceanbase {
namespace plugin {

extern lib::ObLabel default_plugin_memory_label;

class ObPluginVersionAdaptor final
{
public:
  ObPluginVersionAdaptor() = default;
  explicit ObPluginVersionAdaptor(int64_t version);
  explicit ObPluginVersionAdaptor(ObPluginVersion version);
  ObPluginVersionAdaptor(uint16_t major, uint16_t minor, uint16_t patch);
  ~ObPluginVersionAdaptor() = default;

  
  ObPluginVersion major() const;
  ObPluginVersion minor() const;
  ObPluginVersion patch() const;

  int64_t to_string(char buf[], int64_t buf_len) const;
  
private:
  ObPluginVersion version_;
};

class ObPluginPrinter final
{
public:
  ObPluginPrinter(const ObPlugin &plugin) : plugin_(plugin)
  {}

  TO_STRING_KV("author",  plugin_.author,
               "version", ObPluginVersionAdaptor(plugin_.version),
               "license", plugin_.license,
               "init",    plugin_.init,
               "deinit",  plugin_.deinit
               );
private:
  const ObPlugin &plugin_;
};

class ObPluginAdaptor final
{
public:
  ObPluginAdaptor() = default;
  explicit ObPluginAdaptor(ObPlugin *plugin) : plugin_(plugin)
  {}

  ObPlugin *plugin() const { return plugin_; }

  int64_t to_string(char buf[], int64_t buf_len) const
  {
    int64_t ret = 0;
    if (OB_ISNULL(plugin_)) {
      ret = snprintf(buf, buf_len, "nullptr");
    } else {
      ret = ObPluginPrinter(*plugin_).to_string(buf, buf_len);
    }
    return ret;
  }

private:
  ObPlugin *plugin_ = nullptr;
};

} // plugin
} // oceanbase
