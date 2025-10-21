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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SHARED_STORAGE_QUOTA_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SHARED_STORAGE_QUOTA_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "src/observer/ob_server_struct.h"
namespace oceanbase
{
namespace obrpc
{
struct ObSharedDeviceResource;
}
namespace observer
{
class ObVirtualSharedStorageQuota : public common::ObVirtualTableScannerIterator
{
  enum COLUMN_ID_LIST {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    MODULE,
    CLASS_ID,
    STORAGE_ID,
    TYPE,
    REQUIRE,
    ASSIGN
  };

public:
  ObVirtualSharedStorageQuota();
  virtual ~ObVirtualSharedStorageQuota() override;
  virtual int inner_open() override;
  virtual void reset() override;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;

private:
  bool is_inited_;
  char ip_buf_[common::OB_IP_STR_BUFF];

private:
  int add_one_storage_batch_row();
  int add_row(const obrpc::ObSharedDeviceResource &usage, const obrpc::ObSharedDeviceResource &limit);
  DISALLOW_COPY_AND_ASSIGN(ObVirtualSharedStorageQuota);
};

}  // namespace observer
}  // namespace oceanbase
#endif /* SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SHARED_STORAGE_QUOTA_H_ */
