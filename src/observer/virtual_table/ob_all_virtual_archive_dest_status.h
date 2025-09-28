/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
