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

#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "oceanbase/ob_plugin.h"

namespace oceanbase {
namespace plugin {

class ObPluginHandle;
class ObPluginMgr;

class ObPluginParam final
{
public:
  ObPluginParam() = default;
  ~ObPluginParam() = default;

  TO_STRING_KV(KP_(plugin_handle), KP_(plugin_mgr), KP_(plugin_user_data));

public:
  ObPluginHandle *plugin_handle_  = nullptr;
  ObPluginMgr *   plugin_mgr_     = nullptr;
  ObPluginDatum plugin_user_data_ = nullptr;
};

class ObIPluginDescriptor
{
public:
  virtual ~ObIPluginDescriptor() = default;

  virtual int init(ObPluginParam *param) { return OB_SUCCESS; }
  virtual int deinit(ObPluginParam *param) { return OB_SUCCESS; }
};
} // namespace plugin
} // namespace oceanbase
