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

#include "storage/multi_data_source/ob_tablet_create_mds_ctx.h"
#include "share/rc/ob_server_runtime.h"
#include "src/storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

#define USING_LOG_PREFIX MDS

namespace oceanbase
{
namespace storage
{
namespace mds
{
ObTabletCreateMdsCtx::ObTabletCreateMdsCtx()
  : MdsCtx()
{
}

ObTabletCreateMdsCtx::ObTabletCreateMdsCtx(const MdsWriter &writer)
  : MdsCtx(writer)
{
}

void ObTabletCreateMdsCtx::on_abort(const share::SCN &abort_scn)
{
  mds::MdsCtx::on_abort(abort_scn);

  int ret = OB_SUCCESS;
  ObLS *tenant_ls = nullptr;

  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(tenant_ls))) {
  } else {
    checkpoint::ObTabletEmptyShellHandler *handler =
        tenant_ls->get_tablet_empty_shell_handler();
    handler->set_empty_shell_trigger(true/*is_trigger*/);

    LOG_INFO("tablet create tx aborted", K(ret), K(abort_scn));
  }
}
} // namespace mds
} // namespace storage
} // namespace oceanbase
