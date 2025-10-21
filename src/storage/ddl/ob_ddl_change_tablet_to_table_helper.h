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

#ifndef OCEANBASE_STORAGE_OB_CHANGE_TABLET_TO_TABLE_HELPER
#define OCEANBASE_STORAGE_OB_CHANGE_TABLET_TO_TABLE_HELPER

#include "common/ob_tablet_id.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_array_serialization.h"
#include "lib/ob_define.h"
#include "share/ob_ls_id.h"

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace storage
{
namespace mds
{
struct BufferCtx;
}

class ObChangeTabletToTableHelper final
{
public:
  static int on_register(const char* buf,
                         const int64_t len,
                         mds::BufferCtx &ctx); // out parameter, will record corresponding modifications in Ctx

  static int on_replay(const char* buf,
                       const int64_t len,
                       const share::SCN &scn, // log scn
                       mds::BufferCtx &ctx); // standby replay
};

inline int ObChangeTabletToTableHelper::on_register(const char* buf,
                                            const int64_t len,
                                            mds::BufferCtx &ctx)
{
  int ret = OB_SUCCESS;
  return ret;
}

inline int ObChangeTabletToTableHelper::on_replay(const char* buf,
                                                  const int64_t len,
                                                  const share::SCN &scn, // log scn
                                                  mds::BufferCtx &ctx)
{
  int ret = OB_SUCCESS;
  return ret;
}
} // namespace storage
} // namespace oceanbase
#endif
