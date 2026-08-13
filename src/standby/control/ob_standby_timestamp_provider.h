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

#ifndef OCEANBASE_STANDBY_CONTROL_OB_STANDBY_TIMESTAMP_PROVIDER_H_
#define OCEANBASE_STANDBY_CONTROL_OB_STANDBY_TIMESTAMP_PROVIDER_H_

#include <stdint.h>

namespace oceanbase
{
namespace standby
{

class ObStandbyTimestampProvider final
{
public:
  static int enable();
  static int disable();
  static int prepare_for_startup();

private:
  static int get_timestamp_(int64_t &timestamp);
};

} // namespace standby
} // namespace oceanbase

#endif // OCEANBASE_STANDBY_CONTROL_OB_STANDBY_TIMESTAMP_PROVIDER_H_
