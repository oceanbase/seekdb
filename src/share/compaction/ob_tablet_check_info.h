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

#ifndef OCEANBASE_SHARE_COMPACTION_OB_TABLET_CHECK_INFO_H_
#define OCEANBASE_SHARE_COMPACTION_OB_TABLET_CHECK_INFO_H_

#include "lib/utility/ob_print_utils.h"
#include "common/ob_tablet_id.h"

namespace oceanbase
{
namespace compaction
{

class ObTabletCheckInfo
{
public:
  ObTabletCheckInfo()
    : tablet_id_(),
      check_medium_scn_(0)
  {}

  ObTabletCheckInfo(const common::ObTabletID &tablet_id, int64_t medium_scn)
    : tablet_id_(tablet_id),
      check_medium_scn_(medium_scn)
  {}

  ~ObTabletCheckInfo() {}
  bool is_valid() const;
  uint64_t hash() const;
  int hash(uint64_t &hash_val) const
  {
    hash_val = hash();
    return OB_SUCCESS;
  }
  const ObTabletID &get_tablet_id() const { return tablet_id_; }
  int64_t get_medium_scn() const { return check_medium_scn_; }
  bool operator==(const ObTabletCheckInfo &other) const;
  TO_STRING_KV(K_(tablet_id), K_(check_medium_scn));

private:
  common::ObTabletID tablet_id_;
  int64_t check_medium_scn_;
};

}  // namespace compaction
}  // namespace oceanbase

#endif // OCEANBASE_SHARE_COMPACTION_OB_TABLET_CHECK_INFO_H_
