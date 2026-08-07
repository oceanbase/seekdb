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

#ifndef OCEANBASE_STANDBY_OB_STANDBY_BOOTSTRAP_POLICY_H_
#define OCEANBASE_STANDBY_OB_STANDBY_BOOTSTRAP_POLICY_H_

#include "share/scn.h"

namespace oceanbase
{
namespace standby
{

class ObStandbyBootstrapPolicy
{
public:
  static bool need_located_replay_start_log(
      const share::SCN &replay_start_scn,
      const share::SCN &source_end_scn)
  {
    return replay_start_scn.is_valid()
        && source_end_scn.is_valid()
        && !(source_end_scn < replay_start_scn);
  }
};

} // namespace standby
} // namespace oceanbase

#endif /* OCEANBASE_STANDBY_OB_STANDBY_BOOTSTRAP_POLICY_H_ */
