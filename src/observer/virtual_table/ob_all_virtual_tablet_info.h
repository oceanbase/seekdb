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

#ifndef OB_ALL_VIRTUAL_TABLET_INFO_H_
#define OB_ALL_VIRTUAL_TABLET_INFO_H_

#include "common/row/ob_row.h"
#include "lib/container/ob_se_array.h"
#include "lib/guard/ob_shared_guard.h"
#include "sql/ob_scanner.h"
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "sql/ob_scanner.h"
#include "storage/tablet/ob_tablet_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
}
namespace observer
{
class ObAllVirtualTabletInfo : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualTabletInfo();
  virtual ~ObAllVirtualTabletInfo();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int get_next_tablet(storage::ObTabletHandle &tablet_handle);
private:
  storage::ObLS *ls_;
  storage::ObLSTabletIterator tablet_iter_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTabletInfo);
};

}
}
#endif /* OB_ALL_VIRTUAL_TABLET_INFO_H */
