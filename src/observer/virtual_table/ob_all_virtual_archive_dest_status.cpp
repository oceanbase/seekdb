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

#include "ob_all_virtual_archive_dest_status.h"
#include "observer/ob_sql_client_decorator.h"

using namespace oceanbase::share; 
using namespace oceanbase::common::sqlclient;

namespace oceanbase
{  
namespace observer
{

ObVirtualArchiveDestStatus::ObVirtualArchiveDestStatus():
  is_inited_(false)
{}

ObVirtualArchiveDestStatus::~ObVirtualArchiveDestStatus()
{
  destroy();
}

int ObVirtualArchiveDestStatus::init(ObMySQLProxy *sql_proxy)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    SERVER_LOG(WARN, "init twice", K(ret));
  } else if (OB_ISNULL(sql_proxy)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "sql proxy is NULL", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObVirtualArchiveDestStatus::inner_get_next_row(common::ObNewRow *&row)
{
  return OB_ITER_END;
}

void ObVirtualArchiveDestStatus::destroy() {}
}// end namespace observer
}// end namespace oceanbase
