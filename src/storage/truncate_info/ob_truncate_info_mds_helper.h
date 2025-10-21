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
#ifndef OB_STORAGE_TRUNCATE_INFO_TRUNCATE_INFO_MDS_HELPER_H_
#define OB_STORAGE_TRUNCATE_INFO_TRUNCATE_INFO_MDS_HELPER_H_
#include "/usr/include/stdint.h"
#include "src/share/scn.h"
namespace oceanbase
{
namespace storage
{
namespace mds
{
struct BufferCtx;
}
class ObTruncateInfoMdsHelper
{
public:
  static int on_register(
      const char* buf,
      const int64_t len,
      mds::BufferCtx &ctx);
  static int on_replay(
      const char* buf,
      const int64_t len,
      const share::SCN &scn,
      mds::BufferCtx &ctx);
};

} // namespace storage
} // namespace oceanbase

#endif // OB_STORAGE_TRUNCATE_INFO_TRUNCATE_INFO_MDS_HELPER_H_
