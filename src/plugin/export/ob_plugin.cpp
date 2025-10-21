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
#include "lib/string/ob_string.h"
#include "plugin/sys/ob_plugin_entry_handle.h"
#include "plugin/sys/ob_plugin_handle.h"
#include "plugin/sys/ob_plugin_mgr.h"
#include "plugin/adaptor/ob_plugin_adaptor.h"
#include "oceanbase/ob_plugin.h"

using namespace oceanbase::common;
using namespace oceanbase::plugin;

namespace oceanbase {
namespace plugin {

oceanbase::lib::ObLabel default_plugin_memory_label = oceanbase::lib::ObLabel("LibPlugin");

} // namespace plugin
} // namespace oceanbase

#ifdef __cplusplus
extern "C" {
#endif

inline static ObPluginParam *get_plugin_param(ObPluginParamPtr param)
{
  return (ObPluginParam *)param;
}

OBP_PUBLIC_API ObPlugin *obp_param_plugin(ObPluginParamPtr param)
{
  ObPlugin *ptr = nullptr;
  if (OB_NOT_NULL(param) && OB_NOT_NULL(get_plugin_param(param)->plugin_handle_)) {
    ObPluginHandle *plugin_handle = get_plugin_param(param)->plugin_handle_;
    ptr = plugin_handle->plugin();
  }
  return ptr;
}

OBP_PUBLIC_API ObPluginDatum obp_param_plugin_user_data(ObPluginParamPtr param)
{
  ObPluginDatum ptr = nullptr;
  if (OB_NOT_NULL(param)) {
    ptr = get_plugin_param(param)->plugin_user_data_;
  }
  return ptr;
}

OBP_PUBLIC_API void obp_param_set_plugin_user_data(ObPluginParamPtr param, ObPluginDatum user_data)
{
  if (OB_NOT_NULL(param)) {
    get_plugin_param(param)->plugin_user_data_ = user_data;
  }
}

#ifdef __cplusplus
} // extern "C"
#endif
