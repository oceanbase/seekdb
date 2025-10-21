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

#ifndef OCEANBASE_COMMON_ZEROFILL_INFO_
#define OCEANBASE_COMMON_ZEROFILL_INFO_

#include "common/ob_accuracy.h"

namespace oceanbase
{
namespace common
{
struct ObZerofillInfo
{
public:
  ObZerofillInfo(const bool zf, const ObLength len) : max_length_(len), need_zerofill_(zf) {}
  ObZerofillInfo() : max_length_(0), need_zerofill_(false) {}
public:
  ObLength max_length_;
  bool need_zerofill_;
};

}/* ns common*/
}/* ns oceanbase */

#endif /* OCEANBASE_COMMON_ZEROFILL_INFO_ */
//// end of header file

