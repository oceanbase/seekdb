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

#ifndef __OCEANBASE_ZONEMANAGER_OB_SERVER_EXT_H__
#define __OCEANBASE_ZONEMANAGER_OB_SERVER_EXT_H__

#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace common
{
class ObServerExt
{
public:
  friend class ObOcmInstance;
  ObServerExt();
  ~ObServerExt();

private:
  char hostname_[OB_MAX_HOST_NAME_LENGTH];
  ObAddr server_;
  int64_t magic_num_;
};
}
}
#endif
