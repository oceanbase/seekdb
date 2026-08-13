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

#ifndef OCEANBASE_STANDBY_OB_STANDBY_BOOTSTRAP_SERVICE_H_
#define OCEANBASE_STANDBY_OB_STANDBY_BOOTSTRAP_SERVICE_H_

#include "lib/net/ob_addr.h"
#include "share/log/palf/palf_base_info.h"
#include "share/scn.h"

namespace oceanbase
{
namespace common
{
class ObInOutBandwidthThrottle;
}
namespace standby
{

struct StandbyConfig;

struct ObStandbyBootstrapParam final
{
  ObStandbyBootstrapParam();
  bool is_valid() const;

  bool is_standby_cluster_;
  common::ObString source_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  const StandbyConfig *restore_config_;
};

class ObStandbyBootstrapService final
{
public:
  static int bootstrap(
      const ObStandbyBootstrapParam &param,
      share::SCN &source_end_scn);
  static int check_bootstrap_source(
      const ObStandbyBootstrapParam &param,
      common::ObAddr &primary_addr);
private:
  static int create_sys_ls_(
      const ObStandbyBootstrapParam &param,
      const palf::PalfBaseInfo &palf_base_info,
      const share::SCN &restore_checkpoint_scn);
};

} // namespace standby
} // namespace oceanbase

#endif /* OCEANBASE_STANDBY_OB_STANDBY_BOOTSTRAP_SERVICE_H_ */
