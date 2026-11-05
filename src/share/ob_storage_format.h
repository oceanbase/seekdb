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

#ifndef OB_STORAGE_FORMAT_H_
#define OB_STORAGE_FORMAT_H_

#include "lib/ob_define.h"

namespace oceanbase
{
namespace common
{

enum ObStorageFormatVersion
{
  OB_STORAGE_FORMAT_VERSION_INVALID = 0,
  OB_STORAGE_FORMAT_VERSION_V1 = 1, // supports micro block compaction
  OB_STORAGE_FORMAT_VERSION_V2 = 2, // supports encoding, not used any more
  OB_STORAGE_FORMAT_VERSION_V3 = 3, // supports micro block compaction optimization
  OB_STORAGE_FORMAT_VERSION_V4 = 4, // supports optimize ObNumber integer store
  OB_STORAGE_FORMAT_VERSION_MAX = 5, // update MAX each time add new version
};

}  // end namespace common
}  // end namespace oceanbase

#endif  // OB_STORAGE_INTERNAL_FORMAT_H_
