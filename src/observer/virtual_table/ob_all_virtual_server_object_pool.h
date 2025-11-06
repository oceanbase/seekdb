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

#ifndef OB_ALL_VIRTUAL_SERVER_OBJECT_POOL_H_
#define OB_ALL_VIRTUAL_SERVER_OBJECT_POOL_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"
#include <lib/objectpool/ob_server_object_pool.h>

namespace oceanbase
{
namespace observer
{
class ObAllVirtualServerObjectPool: public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualServerObjectPool();
  virtual ~ObAllVirtualServerObjectPool() { reset(); }
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  void set_addr(common::ObAddr &addr) { addr_ = &addr; }
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualServerObjectPool);
private:
  common::ObAddr *addr_;
};

}
}
#endif /* OB_ALL_VIRTUAL_SERVER_OBJECT_POOL_H_ */
