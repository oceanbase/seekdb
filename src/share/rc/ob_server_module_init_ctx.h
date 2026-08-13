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

#ifndef OCEANBASE_SHARE_OB_SERVER_MODULE_INIT_CTX_H_
#define OCEANBASE_SHARE_OB_SERVER_MODULE_INIT_CTX_H_

#include "lib/ob_define.h"
#include "share/log/palf/palf_options.h"

namespace oceanbase
{
namespace share
{
class ObServerModuleInitCtx
{
public:
  ObServerModuleInitCtx() : palf_options_()
  {}

  palf::PalfOptions palf_options_;
  char clog_dir_[common::MAX_PATH_SIZE] = {'\0'};
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_SERVER_MODULE_INIT_CTX_H_
