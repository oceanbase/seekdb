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

#ifndef _OB_HBASE_ADAPTER_FACTORY_H
#define _OB_HBASE_ADAPTER_FACTORY_H

#include "ob_i_adapter.h"
#include "ob_hbase_normal_adapter.h"
#include "ob_hbase_series_adapter.h"
#include "observer/table/utils/ob_htable_utils.h"

namespace oceanbase
{
namespace table
{

class ObHbaseAdapterFactory
{
public:
  static int alloc_hbase_adapter(ObIAllocator &alloc, const ObTableExecCtx &exec_ctx, ObIHbaseAdapter *&adapter)
  {
    int ret = OB_SUCCESS;
    ObHbaseModeType mode_type = exec_ctx.get_schema_cache_guard().get_hbase_mode_type();
    if (mode_type == ObHbaseModeType::OB_INVALID_MODE_TYPE) {
      ret = OB_SCHEMA_ERROR;
      SERVER_LOG(WARN, "invalid hbase mode type", K(ret));
    } else {
      if (mode_type == ObHbaseModeType::OB_HBASE_NORMAL_TYPE) {
        if (OB_ISNULL(adapter = OB_NEWx(ObHNormalAdapter, &alloc))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          SERVER_LOG(WARN, "fail to alloc hbase normal adapter", K(ret));
        }
      } else if (mode_type == ObHbaseModeType::OB_HBASE_SERIES_TYPE) {
        if (OB_ISNULL(adapter = OB_NEWx(ObHSeriesAdapter, &alloc))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          SERVER_LOG(WARN, "fail to alloc hbase series adapter", K(ret));
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "unexpected hbase mode type", K(ret), K(mode_type));
      }
    }

    return ret;
  }
};

class ObHbaseAdapterGuard
{
public:
  ObHbaseAdapterGuard(ObIAllocator &alloc, const ObTableExecCtx &exec_ctx) 
    : allocator_(alloc), exec_ctx_(exec_ctx), hbase_adapter_(nullptr)
  {}
  ~ObHbaseAdapterGuard()
  {
    OB_DELETEx(ObIHbaseAdapter, &allocator_, hbase_adapter_);
  }
  int get_hbase_adapter(ObIHbaseAdapter *&hbase_adapter);
private:
  common::ObIAllocator &allocator_;
  const ObTableExecCtx &exec_ctx_;
  ObIHbaseAdapter *hbase_adapter_;
};


} // end of namespace table
} // end of namespace oceanbase

#endif
