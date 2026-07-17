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

#ifndef OCEANBASE_TRANSACTION_OB_MULTI_DATA_SOURCE_PRINTER_H
#define OCEANBASE_TRANSACTION_OB_MULTI_DATA_SOURCE_PRINTER_H

#include <stdint.h>

namespace oceanbase
{
namespace transaction
{
enum class ObTxDataSourceType : int64_t;
enum class NotifyType : int64_t;

class ObMultiDataSourcePrinter
{
public:
  static const char *to_str_mds_type(const ObTxDataSourceType &mds_type);
  static const char *to_str_notify_type(const NotifyType &notify_type);
};
} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_TRANSACTION_OB_MULTI_DATA_SOURCE_PRINTER_H
