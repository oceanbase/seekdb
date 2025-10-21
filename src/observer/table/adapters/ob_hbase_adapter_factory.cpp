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


#define USING_LOG_PREFIX SERVER
#include "ob_hbase_adapter_factory.h"

namespace oceanbase
{
namespace table
{
int ObHbaseAdapterGuard::get_hbase_adapter(ObIHbaseAdapter *&hbase_adapter)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(hbase_adapter_)) {
    if (OB_FAIL(ObHbaseAdapterFactory::alloc_hbase_adapter(allocator_, 
                                                           exec_ctx_,
                                                           hbase_adapter_))) {
      LOG_WARN("failed to alloc hbase adapter", K(ret));
    } else if (OB_ISNULL(hbase_adapter_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null hbase adapter", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    hbase_adapter = hbase_adapter_;
  }
  return ret;
}
} // end of namespace table
} // end of namespace oceanbase
