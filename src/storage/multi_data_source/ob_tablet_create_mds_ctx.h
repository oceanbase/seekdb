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

#ifndef OCEANBASE_STORAGE_OB_CREATE_TABLET_MDS_CTX
#define OCEANBASE_STORAGE_OB_CREATE_TABLET_MDS_CTX

#include "storage/multi_data_source/mds_ctx.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{
class ObTabletCreateMdsCtx : public MdsCtx
{
public:
  ObTabletCreateMdsCtx();
  explicit ObTabletCreateMdsCtx(const MdsWriter &writer);
  virtual ~ObTabletCreateMdsCtx() = default;
public:
  virtual void on_abort(const share::SCN &abort_scn) override;
};
} // namespace mds
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_CREATE_TABLET_MDS_CTX
