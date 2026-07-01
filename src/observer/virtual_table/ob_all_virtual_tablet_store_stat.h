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

#ifndef OB_ALL_VIRTUAL_TABLET_STORE_STAT_H_
#define OB_ALL_VIRTUAL_TABLET_STORE_STAT_H_
#include "share/ob_virtual_table_scanner_iterator.h"
#include "storage/ob_table_store_stat_mgr.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualTabletStoreStat : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualTabletStoreStat();
  virtual ~ObAllVirtualTabletStoreStat();
  int init();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
protected:
  int fill_cells(const storage::ObTableStoreStat &stat);
private:
  char ip_buf_[common::OB_IP_STR_BUFF];
  char rowkey_prefix_info_[common::COLUMN_DEFAULT_LENGTH]; //json format
  storage::ObTableStoreStat stat_;
  storage::ObTableStoreStatIterator stat_iter_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTabletStoreStat);
};

} /* namespace observer */
} /* namespace oceanbase */
#endif /* OB_ALL_VIRTUAL_TABLET_STORE_STAT_H_ */
