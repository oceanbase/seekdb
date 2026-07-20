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

#ifndef OCEANBASE_TABLET_TO_LS_CACHE_
#define OCEANBASE_TABLET_TO_LS_CACHE_

#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{
namespace transaction
{

class ObTabletToLSCache final
{
public:
  ObTabletToLSCache() : is_inited_(false), tx_ctx_mgr_(NULL) { }
  ~ObTabletToLSCache() { destroy(); }

  int init(ObTxCtxMgr *tx_ctx_mgr);
  void destroy();
  int create_tablet(const common::ObTabletID &tablet_id, const share::ObLSID &ls_id);
  int remove_tablet(const common::ObTabletID &tablet_id, const share::ObLSID &ls_id);
  int remove_ls_tablets(const share::ObLSID &ls_id);
  int check_and_get_ls_info(const common::ObTabletID &tablet_id,
                            share::ObLSID &ls_id,
                            bool &is_local_leader);
  int64_t size();
  TO_STRING_KV(K_(is_inited),KP_(tx_ctx_mgr),KP(this));

private:
  bool is_inited_;
  ObTxCtxMgr *tx_ctx_mgr_;
};

} // transaction
} // oceanbase

#endif // OCEANBASE_TABLET_TO_LS_CACHE_
