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

#ifndef OCEANBASE_STORAGE_META_STORE_OB_SERVER_RUNTIME_META_H_
#define OCEANBASE_STORAGE_META_STORE_OB_SERVER_RUNTIME_META_H_

#include "share/resource/ob_server_runtime_config.h"

namespace oceanbase
{
namespace omt
{

struct ObServerRuntimeMeta final
{
public:
  ObServerRuntimeMeta()
    : runtime_config_(),
      is_hidden_(false) {}
  ObServerRuntimeMeta(const ObServerRuntimeMeta &) = default;
  ObServerRuntimeMeta &operator=(const ObServerRuntimeMeta &) = default;

  ~ObServerRuntimeMeta() = default;

  bool is_valid() const
  {
    return runtime_config_.is_valid();
  }

  int build(const share::ObServerRuntimeConfig &runtime_config,
            bool is_hidden);

  TO_STRING_KV(K_(runtime_config), K_(is_hidden));

public:
  share::ObServerRuntimeConfig runtime_config_;
  bool is_hidden_;
};

}  // end namespace omt
}  // end namespace oceanbase

#endif  // OCEANBASE_STORAGE_META_STORE_OB_SERVER_RUNTIME_META_H_
