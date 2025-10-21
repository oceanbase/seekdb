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

#ifndef OB_ALL_VIRTUAL_DTL_FIRST_CACHED_BUFFER_H
#define OB_ALL_VIRTUAL_DTL_FIRST_CACHED_BUFFER_H

#include "sql/dtl/ob_dtl_channel.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "common/row/ob_row.h"
#include "sql/dtl/ob_dtl_fc_server.h"


namespace oceanbase
{
namespace observer
{


class ObAllVirtualDtlFirstBufferInfo
{
public:
  ObAllVirtualDtlFirstBufferInfo() :
    tenant_id_(0), channel_id_(0), calced_val_(0), buffer_pool_id_(0), timeout_ts_(0)
  {}

  TO_STRING_KV(K(tenant_id_), K(channel_id_));

public:
  uint64_t tenant_id_;                // 1
  int64_t channel_id_;
  int64_t calced_val_;
  int64_t buffer_pool_id_;
  int64_t timeout_ts_;                // 5
};

class ObAllVirtualDtlFirstCachedBufferIterator
{
public:
  ObAllVirtualDtlFirstCachedBufferIterator(common::ObArenaAllocator *iter_allocator);
  virtual ~ObAllVirtualDtlFirstCachedBufferIterator();

  void destroy();
  void reset();

  int init();
  int get_tenant_ids();
  int get_next_tenant_buffer_infos();
  int get_tenant_buffer_infos(uint64_t tenant_id);

  int get_all_first_cached_buffer(int64_t tenant_id, sql::dtl::ObTenantDfc *tenant_dfc);
  int get_all_first_cached_buffer_old(int64_t tenant_id, sql::dtl::ObTenantDfc *tenant_dfc);

private:
  static const int64_t MAX_BUFFER_CAPCITY = 1000;
  int64_t cur_tenant_idx_;
  int64_t cur_buffer_idx_;
  common::ObArenaAllocator *iter_allocator_;
  common::ObArray<uint64_t> tenant_ids_;
  common::ObArray<ObAllVirtualDtlFirstBufferInfo, common::ObWrapperAllocator> buffer_infos_;
};

class ObAllVirtualDtlFirstCachedBuffer : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualDtlFirstCachedBuffer();
  virtual ~ObAllVirtualDtlFirstCachedBuffer();

  void destroy();
  void reset();
  int inner_open();
  int inner_get_next_row(common::ObNewRow *&row);

private:
private:
  enum STORAGE_COLUMN
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    CHANNEL_ID,
    CALCED_VAL,
    BUFFER_POOL_ID,         // OB_APP_MIN_COLUMN_ID + 5
    TIMEOUT_TS,
  };

private:
  common::ObString ipstr_;
  int32_t port_;
  common::ObArenaAllocator arena_allocator_;
  ObAllVirtualDtlFirstCachedBufferIterator iter_;
};


} /* namespace observer */
} /* namespace oceanbase */

#endif /* OB_ALL_VIRTUAL_DTL_FIRST_CACHED_BUFFER_H */
