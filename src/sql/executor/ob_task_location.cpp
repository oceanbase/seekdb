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

#include "sql/executor/ob_task_location.h"
using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

ObTaskLocation::ObTaskLocation(const ObAddr &server, const ObTaskID &ob_task_id) :
    server_(server),
    ob_task_id_(ob_task_id)
{
}

ObTaskLocation::ObTaskLocation() :
    server_(),
    ob_task_id_()
{
}

void ObTaskLocation::reset()
{
  server_.reset();
  ob_task_id_.reset();
}

ObTaskLocation& ObTaskLocation::operator=(const ObTaskLocation &task_location)
{
  server_ = task_location.server_;
  ob_task_id_ = task_location.ob_task_id_;
  return *this;
}

OB_SERIALIZE_MEMBER(ObTaskLocation, server_, ob_task_id_)

}/* ns sql*/
}/* ns oceanbase */




