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

#ifndef OCEANBASE_DATA_PLANE_REPORT_OB_I_DISK_REPORT_H_
#define OCEANBASE_DATA_PLANE_REPORT_OB_I_DISK_REPORT_H_

#include <stdint.h>

namespace oceanbase
{
namespace data_plane
{

class ObIDiskReport
{
public:
  ObIDiskReport() = default;
  virtual ~ObIDiskReport() = default;

  virtual int delete_usage_stat() = 0;
  virtual int get_data_disk_used_size(int64_t &used_size) = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_REPORT_OB_I_DISK_REPORT_H_
