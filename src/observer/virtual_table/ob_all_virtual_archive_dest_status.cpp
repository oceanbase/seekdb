/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
