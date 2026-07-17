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

#ifndef OB_MICRO_BLOCK_FORMAT_VERSION_HELPER_H_
#define OB_MICRO_BLOCK_FORMAT_VERSION_HELPER_H_

#include "common/ob_store_format.h"
#include "common/ob_version_def.h"

namespace oceanbase
{
namespace storage
{
class ObMicroBlockFormatVersionHelper
{
public:
  static constexpr int64_t DEFAULT_VERSION = 1;
};
} // namespace storage
} // namespace oceanbase

#endif
