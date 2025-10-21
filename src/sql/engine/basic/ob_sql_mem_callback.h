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

#ifndef OCEANBASE_BASIC_OB_SQL_MEMM_CALLBACK_H_
#define OCEANBASE_BASIC_OB_SQL_MEMM_CALLBACK_H_

#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{

class ObSqlMemoryCallback
{
public:
  virtual ~ObSqlMemoryCallback() = default;

  virtual void alloc(int64_t size) = 0;
  virtual void free(int64_t size) = 0;
  virtual void dumped(int64_t size) = 0;
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_BASIC_OB_SQL_MEMM_CALLBACK_H_
