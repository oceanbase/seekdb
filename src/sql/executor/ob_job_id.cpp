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

#include "share/ob_define.h"
#include "sql/executor/ob_job_id.h"
using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

OB_SERIALIZE_MEMBER(ObJobID, ob_execution_id_, job_id_, root_op_id_);
DEFINE_TO_YSON_KV(ObJobID, OB_ID(execution_id), ob_execution_id_,
                           OB_ID(job_id), job_id_);

}/* ns */
}/* ns oceanbase */
