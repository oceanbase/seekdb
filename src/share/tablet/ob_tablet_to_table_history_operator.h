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

#ifndef OCEANBASE_SHARE_OB_TABLET_TO_TABLE_HISTORY_OPERATOR
#define OCEANBASE_SHARE_OB_TABLET_TO_TABLE_HISTORY_OPERATOR

#include "lib/container/ob_iarray.h"     // ObIArray
#include "common/ob_tablet_id.h"         // ObTabletID
#include "share/tablet/ob_tablet_info.h" // ObTabletTablePair

namespace oceanbase
{
namespace common
{
class ObISQLClient;

} // end nampspace common

namespace share
{
class ObTabletToTableHistoryOperator
{
public:
  ObTabletToTableHistoryOperator() {}
  virtual ~ObTabletToTableHistoryOperator() {}

  static int create_tablet_to_table_history(
             common::ObISQLClient &sql_proxy,
             const uint64_t tenant_id,
             const int64_t schema_version,
             const common::ObIArray<ObTabletTablePair> &pairs);
  static int drop_tablet_to_table_history(
             common::ObISQLClient &sql_proxy,
             const uint64_t tenant_id,
             const int64_t schema_version,
             const common::ObIArray<ObTabletID> &tablet_ids);
};

} // end namespace share
} // end namespace oceanbase
#endif // OCEANBASE_SHARE_OB_TABLET_TO_TABLE_HISTORY_OPERATOR
