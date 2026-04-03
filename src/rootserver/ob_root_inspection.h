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

#ifndef OCEANBASE_ROOTSERVER_OB_ROOT_INSPECTION_H_
#define OCEANBASE_ROOTSERVER_OB_ROOT_INSPECTION_H_

#include "lib/list/ob_dlink_node.h"
#include "lib/list/ob_dlist.h"
#include "lib/container/ob_iarray.h"
#include "lib/string/ob_sql_string.h"
#include "lib/string/ob_fixed_length_string.h"
#include "lib/thread/ob_work_queue.h"
#include "share/ob_virtual_table_projector.h"
#include "share/ob_common_rpc_proxy.h"
#include "share/ob_schema_status_proxy.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}

namespace share
{
namespace schema
{
class ObTableSchema;
class ObColumnSchemaV2;
class ObMultiVersionSchemaService;
}
}

namespace rootserver
{
class ObRootService;

////////////////////////////////////////////////////////////////
// Class I: purge recyclebin in the background
class ObPurgeRecyclebinTask: public common::ObAsyncTimerTask
{
public:
  explicit ObPurgeRecyclebinTask(ObRootService &rs);
  virtual ~ObPurgeRecyclebinTask() {}

  virtual int process() override;
  virtual int64_t get_deep_copy_size() const override { return sizeof(*this); }
  virtual ObAsyncTask *deep_copy(char *buf, const int64_t buf_size) const override;
private:
  ObRootService &root_service_;
};

}//end namespace rootserver
}//end namespace oceanbase

#endif //OCEANBASE_ROOTSERVER_OB_ROOT_INSPECTION_H_
