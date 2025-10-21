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

#ifndef OCEANBASE_SQL_ENGINE_TABLE_OB_I_VIRTUAL_TABLE_ITERATOR_FACTORY_
#define OCEANBASE_SQL_ENGINE_TABLE_OB_I_VIRTUAL_TABLE_ITERATOR_FACTORY_

#include "common/ob_range.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace common
{
class ObVirtualTableIterator;
class ObVTableScanParam;
}
namespace sql
{
class ObExecContext;
class ObCreateVirtualTableParams
{
public:
  ObCreateVirtualTableParams() : table_id_(common::OB_INVALID_ID), key_ranges_() {}
  virtual ~ObCreateVirtualTableParams() {}

  uint64_t table_id_;
  common::ObSEArray<common::ObNewRange, 16> key_ranges_;
  TO_STRING_KV(K_(table_id),
               K_(key_ranges));
};

class ObIVirtualTableIteratorFactory
{
public:
  ObIVirtualTableIteratorFactory() {}
  virtual ~ObIVirtualTableIteratorFactory() {}

  virtual int create_virtual_table_iterator(common::ObVTableScanParam &params,
                                            common::ObVirtualTableIterator *&vt_iter) = 0;
  virtual int revert_virtual_table_iterator(common::ObVirtualTableIterator *vt_iter) = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObIVirtualTableIteratorFactory);
};
}
}
#endif /* OCEANBASE_SQL_ENGINE_TABLE_OB_I_VIRTUAL_TABLE_ITERATOR_FACTORY_ */
