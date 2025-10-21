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

#ifndef OCEANBASE_STORAGE_OB_BATCH_CREATE_TABLET_PRETTY_ARG
#define OCEANBASE_STORAGE_OB_BATCH_CREATE_TABLET_PRETTY_ARG

#include <stdint.h>

namespace oceanbase
{
namespace obrpc
{
struct ObBatchCreateTabletArg;
}

namespace storage
{
class ObBatchCreateTabletPrettyArg
{
public:
  ObBatchCreateTabletPrettyArg(const obrpc::ObBatchCreateTabletArg &arg);
  ~ObBatchCreateTabletPrettyArg() = default;
  ObBatchCreateTabletPrettyArg(const ObBatchCreateTabletPrettyArg&) = delete;
  ObBatchCreateTabletPrettyArg &operator=(const ObBatchCreateTabletPrettyArg&) = delete;
public:
  int64_t to_string(char *buf, const int64_t buf_len) const;
private:
  const obrpc::ObBatchCreateTabletArg &arg_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_BATCH_CREATE_TABLET_PRETTY_ARG
