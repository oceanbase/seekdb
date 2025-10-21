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

#ifndef _OB_TABLE_SERVICE_H
#define _OB_TABLE_SERVICE_H 1
#include "observer/ob_server_struct.h"
#include "object_pool/ob_table_object_pool.h"
namespace oceanbase
{
namespace observer
{
/// table service
class ObTableService
{
public:
  ObTableService() {}
  virtual ~ObTableService() = default;
  int init();
  void stop();
  void wait();
  void destroy();
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTableService);
};

} // end namespace observer
} // end namespace oceanbase

#endif /* _OB_TABLE_SERVICE_H */
