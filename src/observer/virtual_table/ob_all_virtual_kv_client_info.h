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
#ifndef OB_ALL_VIRTUAL_KV_CLIENT_INFO_H_
#define OB_ALL_VIRTUAL_KV_CLIENT_INFO_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "observer/omt/ob_multi_tenant_operator.h"
#include "observer/table/ob_table_client_info_mgr.h"
namespace oceanbase
{
namespace observer
{
struct ObGetAllClientInfoOp {
  explicit ObGetAllClientInfoOp(common::ObIArray<table::ObTableClientInfo>& cli_infos)
    : allocator_(nullptr),
      cli_infos_(cli_infos)
  {}
  int operator()(common::hash::HashMapPair<uint64_t, table::ObTableClientInfo*> &entry);
  void set_allocator(ObIAllocator *allocator) { allocator_ = allocator; }
private:
  ObIAllocator *allocator_;
  common::ObIArray<table::ObTableClientInfo>& cli_infos_;
};

class ObAllVirtualKvClientInfo : public common::ObVirtualTableScannerIterator,
                                 public omt::ObMultiTenantOperator
{
public:
  ObAllVirtualKvClientInfo()
    : ObVirtualTableScannerIterator(),
      cur_idx_(0),
      svr_addr_()
  {
    MEMSET(svr_ip_buf_, 0, common::OB_IP_STR_BUFF);
  }
  virtual ~ObAllVirtualKvClientInfo() { reset(); }
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  int set_svr_addr(common::ObAddr &addr);
private:
  virtual bool is_need_process(uint64_t tenant_id) override;
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  virtual void release_last_tenant() override;
private:
  enum CLI_INFO_COLUMN
  {
    CLIENT_ID = common::OB_APP_MIN_COLUMN_ID,
    CLIENT_IP,
    CLIENT_PORT,
    SVR_IP,
    SVR_PORT,
    TENANT_ID,
    USER_NAME,
    FIRST_LOGIN_TS,
    LAST_LOGIN_TS,
    CLIENT_INFO,
  };
  int64_t cur_idx_;
  ObAddr svr_addr_;
  char svr_ip_buf_[common::OB_IP_STR_BUFF];
  ObSEArray<table::ObTableClientInfo, 128> cli_infos_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualKvClientInfo);
};

} /* namespace observer */
} /* namespace oceanbase */
#endif
