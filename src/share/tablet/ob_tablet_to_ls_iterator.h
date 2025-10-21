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

#ifndef OCEANBASE_SHARE_TABLET_OB_TABLET_TO_LS_ITERATOR_H
#define OCEANBASE_SHARE_TABLET_OB_TABLET_TO_LS_ITERATOR_H

#include "share/tablet/ob_tablet_to_ls_operator.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
namespace share
{
class ObTabletToLSTableOperator;

// ObTenantTabletToLSIterator is used to iterate __all_tablet_to_ls according to target tenant.
class ObTenantTabletToLSIterator
{
public:
  ObTenantTabletToLSIterator();
  virtual ~ObTenantTabletToLSIterator() {}
  int init(
      common::ObISQLClient &sql_proxy,
      const uint64_t tenant_id);
  // init with LS white list
  // ls_white_list: LS white list that only output tablets on the LS white list.
  //                If list is empty, it means ALL LS are in white list.
  int init(
      common::ObISQLClient &sql_proxy,
      const uint64_t tenant_id,
      const common::ObIArray<ObLSID> &ls_white_list);
  int next(ObTabletLSPair &pair);
  int next(ObTabletToLSInfo &info);
private:
  int prefetch_();

  bool inited_;
  uint64_t tenant_id_;
  int64_t inner_idx_;
  common::ObSEArray<ObLSID, 1> ls_white_list_;
  common::ObArray<ObTabletToLSInfo> inner_tablet_infos_;
  common::ObISQLClient *sql_proxy_;
};

} // end namespace
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_TABLET_OB_TABLET_TO_LS_ITERATOR_H
