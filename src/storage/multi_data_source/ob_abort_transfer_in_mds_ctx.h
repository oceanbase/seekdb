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

#ifndef SHARE_STORAGE_MULTI_DATA_SOURCE_OB_ABORT_TRANSFER_IN_MDS_CTX_H
#define SHARE_STORAGE_MULTI_DATA_SOURCE_OB_ABORT_TRANSFER_IN_MDS_CTX_H

#include "mds_ctx.h"
#include "lib/container/ob_array.h"
#include "storage/multi_data_source/runtime_utility/mds_tenant_service.h"
#include "lib/container/ob_array_serialization.h"

namespace oceanbase
{
namespace share
{
class ObLSID;
}
namespace common
{
class ObTabletID;
}
namespace storage
{
namespace mds
{
class MdsTableHandle;

struct ObAbortTransferInMdsCtxVersion
{
  enum VERSION {
    ABORT_TRANSFER_IN_MDS_CTX_VERSION_V1 = 1,
    ABORT_TRANSFER_IN_MDS_CTX_VERSION_V2 = 2,
    MAX
  };
  static const VERSION CURRENT_CTX_VERSION = ABORT_TRANSFER_IN_MDS_CTX_VERSION_V2;
  static bool is_valid(const ObAbortTransferInMdsCtxVersion::VERSION &version) {
    return version >= ABORT_TRANSFER_IN_MDS_CTX_VERSION_V1
        && version < MAX;
  }
};

class ObAbortTransferInMdsCtx : public MdsCtx
{
public:
  ObAbortTransferInMdsCtx();
  ObAbortTransferInMdsCtx(const MdsWriter &writer);
  virtual ~ObAbortTransferInMdsCtx();
  virtual int serialize(char *buf, const int64_t len, int64_t &pos) const;
  virtual int deserialize(const char *buf, const int64_t len, int64_t &pos);
  virtual int64_t get_serialize_size(void) const;

  virtual void on_redo(const share::SCN &redo_scn) override;
  share::SCN &get_redo_scn() { return redo_scn_; }

  INHERIT_TO_STRING_KV("MdsCtx", MdsCtx, K_(version), K_(redo_scn));
private:
  ObAbortTransferInMdsCtxVersion::VERSION version_;
  share::SCN redo_scn_;
  DISALLOW_COPY_AND_ASSIGN(ObAbortTransferInMdsCtx);
};


} //mds
} //storage
} //oceanbase






#endif //SHARE_STORAGE_MULTI_DATA_SOURCE_OB_FINISH_TRANSFER_IN_MDS_CTX_H
