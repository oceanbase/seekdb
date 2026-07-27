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

#ifndef OCEANBASE_SHARE_OB_TABLET_TABLE_OPERATOR
#define OCEANBASE_SHARE_OB_TABLET_TABLE_OPERATOR

#include "lib/container/ob_iarray.h" //ObIArray
#include "storage/compaction/ob_tablet_check_info.h"
#include "share/tablet/ob_tablet_info.h"
#include "share/compaction/ob_array_with_map.h"
#include "share/tablet/ob_tablet_meta_table_storage.h"

namespace oceanbase
{
namespace common
{
class ObTabletID;
}
namespace share
{

// Local persistent tablet runtime metadata operator.
class ObTabletTableOperator
{
public:
  ObTabletTableOperator();
  virtual ~ObTabletTableOperator();
  // Initialize with SQLite storage
  int init(share::ObSQLiteConnectionPool *pool);
  void reset();
  int batch_get(
      const ObIArray<common::ObTabletID> &tablet_ids,
      ObIArray<ObTabletRuntimeInfo> &tablet_infos);
  int batch_update(
      const ObIArray<ObTabletRuntimeInfo> &tablet_infos);
  int batch_update(
      ObSQLiteConnection *conn,
      const ObIArray<ObTabletRuntimeInfo> &tablet_infos);
  int batch_remove(
      const ObIArray<ObTabletRuntimeInfo> &tablet_infos);
  int batch_remove(
      ObSQLiteConnection *conn,
      const ObIArray<ObTabletRuntimeInfo> &tablet_infos);
  int batch_get_tablet_info(
      const ObIArray<compaction::ObTabletCheckInfo> &tablet_ls_infos,
      ObArrayWithMap<ObTabletRuntimeInfo> &tablet_infos);
private:
  bool inited_;
  ObTabletMetaTableStorage storage_;
};
} // end namespace share
} // end namespace oceanbase
#endif
