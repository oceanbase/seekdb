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

#ifndef OB_ALL_VIRTUAL_TABLET_DDL_KV_INFO_H_
#define OB_ALL_VIRTUAL_TABLET_DDL_KV_INFO_H_

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "storage/tablet/ob_tablet_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
}
namespace observer
{
class ObAllVirtualTabletDDLKVInfo : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualTabletDDLKVInfo();
  virtual ~ObAllVirtualTabletDDLKVInfo();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int get_next_ddl_kv_mgr(storage::ObDDLKvMgrHandle &ddl_kv_mgr_handle);
  int get_next_ddl_kv(ObDDLKV *&ddl_kv);
private:
  storage::ObLS *ls_;
  storage::ObLSTabletIterator tablet_iter_;
  ObArray<ObDDLKVHandle> ddl_kvs_handle_;
  common::ObTabletID curr_tablet_id_;
  int64_t ddl_kv_idx_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTabletDDLKVInfo);
};

}
}
#endif /* OB_ALL_VIRTUAL_TABLET_DDL_KV_INFO_H_ */
