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

#ifndef OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_ARCHIVE_DEST_STATUS_H_
#define OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_ARCHIVE_DEST_STATUS_H_

#include "share/ob_virtual_table_projector.h"
#include "share/backup/ob_backup_struct.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
class ObTableSchema;
}
}

namespace observer
{
class ObVirtualArchiveDestStatus : public common::ObVirtualTableProjector
{
public:
  static const int64_t MAX_SYNC_TYPE_LENTH = 10;
  ObVirtualArchiveDestStatus();
  virtual ~ObVirtualArchiveDestStatus();
  int init(common::ObMySQLProxy *sql_proxy);
  virtual int inner_get_next_row(common::ObNewRow *&row);
  void destroy(); 
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObVirtualArchiveDestStatus);
};
}//end namespace observer
}//end namespace oceanbase
#endif //OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_ARCHIVE_DEST_STATUS_H_
