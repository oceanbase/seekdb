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

#ifndef OCEANBASE_STORAGE_OB_TABLET_FORK_MDS_HELPER
#define OCEANBASE_STORAGE_OB_TABLET_FORK_MDS_HELPER

#include "common/ob_tablet_id.h"
#include "lib/allocator/page_arena.h"
#include "lib/mysqlclient/ob_mysql_transaction.h"
#include "share/ob_ls_id.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "rootserver/truncate_info/ob_truncate_tablet_arg.h"

namespace oceanbase
{
namespace obrpc
{
struct ObBatchSetTabletAutoincSeqArg;
}

namespace share
{
class SCN;
}

namespace storage
{
class ObLS;

namespace mds
{
struct BufferCtx;
}

class ObTabletForkMdsArg final
{
OB_UNIS_VERSION(1);
public:
  ObTabletForkMdsArg();
  ~ObTabletForkMdsArg();
  bool is_valid() const;
  void reset();
  int set_autoinc_seq_arg(const obrpc::ObBatchSetTabletAutoincSeqArg &arg);
  int set_truncate_arg(const rootserver::ObTruncateTabletArg &arg);

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(autoinc_seq_arg), K_(truncate_arg));

public:
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  obrpc::ObBatchSetTabletAutoincSeqArg autoinc_seq_arg_;
  rootserver::ObTruncateTabletArg truncate_arg_;

private:
  common::ObArenaAllocator allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletForkMdsArg);
};

class ObTabletForkMdsHelper
{
public:
  static int on_register(const char* buf, const int64_t len, mds::BufferCtx &ctx);
  static int on_replay(const char* buf, const int64_t len, const share::SCN &scn, mds::BufferCtx &ctx);
  static int register_mds(const ObTabletForkMdsArg &arg, const bool need_flush_redo, ObMySQLTransaction &trans);

private:
  static int modify(const ObTabletForkMdsArg &arg, const share::SCN &scn, mds::BufferCtx &ctx);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_FORK_MDS_HELPER
