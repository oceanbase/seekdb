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

#ifndef OCEANBASE_TABLE_SCHEMA_ITERATOR_H_
#define OCEANBASE_TABLE_SCHEMA_ITERATOR_H_

#include "lib/container/ob_array.h"
#include "share/ob_define.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
class ObSchemaGetterGuard;

class ObITableIterator
{
public:
  ObITableIterator() {}
  virtual ~ObITableIterator() {}

  virtual int next(uint64_t &table_id) = 0;
};

}//end of schema
}//end of share
}//end of oceanbase
#endif // OCEANBASE_TABLE_SCHEMA_ITERATOR_H_
