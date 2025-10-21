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

#ifndef OCEANBASE_COMMON_OB_REGION_H_
#define OCEANBASE_COMMON_OB_REGION_H_

#include "lib/string/ob_fixed_length_string.h"

namespace oceanbase
{
namespace common
{
typedef ObFixedLengthString<MAX_REGION_LENGTH> ObRegion;
}//end namespace common
}//end namespace oceanbase
#endif //OCEANBASE_COMMON_OB_REGION_H_

