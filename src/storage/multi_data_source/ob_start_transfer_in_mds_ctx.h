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

#ifndef SHARE_STORAGE_MULTI_DATA_SOURCE_OB_START_TRANSFER_IN_MDS_CTX_H
#define SHARE_STORAGE_MULTI_DATA_SOURCE_OB_START_TRANSFER_IN_MDS_CTX_H

#include "mds_ctx.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_array_serialization.h"
#include "share/ob_ls_id.h"
#include "storage/multi_data_source/runtime_utility/mds_tenant_service.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{

class MdsTableHandle;

struct ObStartTransferInMdsCtxVersion
{
  enum VERSION {
    START_TRANSFER_IN_MDS_CTX_VERSION_V1 = 1,
    START_TRANSFER_IN_MDS_CTX_VERSION_V2 = 2,
    START_TRANSFER_IN_MDS_CTX_VERSION_V3 = 3,
    MAX
  };
  static const VERSION CURRENT_CTX_VERSION = START_TRANSFER_IN_MDS_CTX_VERSION_V3;
  static bool is_valid(const ObStartTransferInMdsCtxVersion::VERSION &version) {
    return version >= START_TRANSFER_IN_MDS_CTX_VERSION_V1
        && version < MAX;
  }
};

class ObStartTransferInMdsCtx : public MdsCtx
{
public:
  ObStartTransferInMdsCtx();
  explicit ObStartTransferInMdsCtx(const MdsWriter &writer);
  virtual ~ObStartTransferInMdsCtx();
public:
  virtual void on_prepare(const share::SCN &prepare_version) override;
  virtual void on_abort(const share::SCN &abort_scn) override;

  virtual int serialize(char *buf, const int64_t len, int64_t &pos) const override;
  virtual int deserialize(const char *buf, const int64_t len, int64_t &pos) override;
  virtual int64_t get_serialize_size(void) const override;
public:
  void set_ls_id(const share::ObLSID &ls_id) { ls_id_ = ls_id; }
private:
  ObStartTransferInMdsCtxVersion::VERSION version_;
  share::ObLSID ls_id_;
  DISALLOW_COPY_AND_ASSIGN(ObStartTransferInMdsCtx);
};

} //mds
} //storage
} //oceanbase

#endif //SHARE_STORAGE_MULTI_DATA_SOURCE_OB_TRANSFER_IN_MDS_CTX_H
