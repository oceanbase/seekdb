/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_SHARE_OB_EST_ROW_COUNT_RECORD_H_
#define OCEANBASE_SHARE_OB_EST_ROW_COUNT_RECORD_H_

#include "common/ob_range.h"

namespace oceanbase
{
namespace common
{

struct ObEstRowCountRecord
{
  int64_t table_id_;
  int64_t table_type_;
  ObVersionRange version_range_;
  int64_t logical_row_count_;
  int64_t physical_row_count_;
  TO_STRING_KV(K_(table_id), K_(table_type), K_(version_range), K_(logical_row_count), K_(physical_row_count));
  OB_UNIS_VERSION(1);
};

} // namespace common
} // namespace oceanbase

#endif /* OCEANBASE_SHARE_OB_EST_ROW_COUNT_RECORD_H_ */
