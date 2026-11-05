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

#ifdef ENABLE_SERIALIZATION_CHECK
#include "ob_unify_serialize.h"
namespace oceanbase
{
namespace lib
{
RLOCAL(SerializeDiagnoseRecord, ser_diag_record);
void begin_record_serialization()
{
  ser_diag_record.count = 0;
  ser_diag_record.check_index = 0;
  ser_diag_record.flag = CHECK_STATUS_RECORDING;
}

void finish_record_serialization()
{
  ser_diag_record.flag = CHECK_STATUS_WATING;
}

void begin_check_serialization()
{
  ser_diag_record.check_index = 0;
  if (ser_diag_record.count > 0) {
    ser_diag_record.flag = CHECK_STATUS_COMPARING;
  }
}

void finish_check_serialization()
{
  ser_diag_record.count = -1;
  ser_diag_record.check_index = -1;
  ser_diag_record.flag = CHECK_STATUS_WATING;
}

}  // namespace lib
}  // namespace oceanbase
#endif
