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

#ifndef OCEANBASE_SHARE_OB_STANDBY_UPGRADE_H_
#define OCEANBASE_SHARE_OB_STANDBY_UPGRADE_H_

#include "lib/utility/ob_print_utils.h"       // Print*
#include "lib/utility/ob_unify_serialize.h"       // OB_UNIS_VERSION
#include "share/ob_cluster_version.h"
#include "storage/multi_data_source/buffer_ctx.h"

namespace oceanbase
{

namespace share
{

struct ObStandbyUpgrade
{
  OB_UNIS_VERSION(1);
 public:
  ObStandbyUpgrade(): data_version_(0) {}
  ObStandbyUpgrade(const uint64_t data_version): data_version_(data_version) {}
  ~ObStandbyUpgrade() {}
  bool is_valid() const
  {
    return true;
  }
  uint64_t get_data_version() const
  {
    return data_version_;
  }
  
  TO_STRING_KV(K_(data_version));
private:
  uint64_t data_version_;
};

class ObUpgradeDataVersionMDSHelper
{
public:
  static int on_register(
      const char* buf,
      const int64_t len,
      storage::mds::BufferCtx &ctx);
  static int on_replay(
      const char* buf,
      const int64_t len,
      const share::SCN &scn,
      storage::mds::BufferCtx &ctx);
};

}
}

#endif /* !OCEANBASE_SHARE_OB_STANDBY_UPGRADE_H_ */
