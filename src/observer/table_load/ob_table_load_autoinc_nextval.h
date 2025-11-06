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

#pragma once

#include "common/object/ob_object.h"
#include "share/ob_autoincrement_param.h"
#include "share/ob_autoincrement_service.h"
#include "sql/engine/expr/ob_expr_autoinc_nextval.h"
#include "storage/blocksstable/ob_storage_datum.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadAutoincNextval
{
public:
  static int eval_nextval(share::AutoincParam *autoinc_param, blocksstable::ObStorageDatum &datum,
                          const common::ObObjTypeClass &tc, const uint64_t &sql_mode);

private:
  static int get_uint_value(blocksstable::ObStorageDatum &datum, bool &is_zero,
                            uint64_t &casted_value, const common::ObObjTypeClass &tc);
  static int get_input_value(blocksstable::ObStorageDatum &datum,
                             share::AutoincParam &autoinc_param, bool &is_to_generate,
                             uint64_t &casted_value, const common::ObObjTypeClass &tc,
                             const uint64_t &sql_mode);
  static int generate_autoinc_value(share::ObAutoincrementService &auto_service,
                                    share::AutoincParam *autoinc_param, uint64_t &new_val);
};
} // namespace observer
} // namespace oceanbase
