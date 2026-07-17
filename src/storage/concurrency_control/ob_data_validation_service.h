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

#ifndef OCEANBASE_STORAGE_CONCURRENCY_CONTROL_OB_DATA_VALIDATION_SERVICE
#define OCEANBASE_STORAGE_CONCURRENCY_CONTROL_OB_DATA_VALIDATION_SERVICE

#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace concurrency_control
{

class ObDataValidationService
{
public:
  static void set_delay_resource_recycle(const ObLSID ls_id);
};

} // namespace concurrency_control
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_CONCURRENCY_CONTROL_OB_DATA_VALIDATION_SERVICE
