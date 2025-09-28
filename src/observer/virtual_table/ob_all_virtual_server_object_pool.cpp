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

#include "observer/virtual_table/ob_all_virtual_server_object_pool.h"
#include <observer/ob_server_utils.h>

using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::transaction;

ObAllVirtualServerObjectPool::ObAllVirtualServerObjectPool() : addr_(NULL)
{
}

void ObAllVirtualServerObjectPool::reset()
{
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualServerObjectPool::inner_get_next_row(ObNewRow *&row)
{
  return OB_ITER_END;
}
