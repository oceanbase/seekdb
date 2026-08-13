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
#define USING_LOG_PREFIX MDS
#include "storage/truncate_info/ob_truncate_info_mds_helper.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/truncate_info/ob_truncate_info.h"
#include "storage/truncate_info/ob_truncate_tablet_arg.h"
#include "storage/tablet/ob_tablet_replay_executor.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h"
namespace oceanbase
{
namespace storage
{
using namespace mds;
class ObTruncateInfoClogReplayExecutor final : public storage::ObTabletReplayExecutor
{
public:
  ObTruncateInfoClogReplayExecutor(ObTruncateTabletArg &truncate_arg);
  int init(mds::BufferCtx &user_ctx, const share::SCN &scn);
protected:
  bool is_replay_update_tablet_status_() const override
  {
    return false;
  }
  int do_replay_(ObTabletHandle &tablet_handle) override;
  virtual bool is_replay_update_mds_table_() const override
  {
    return true;
  }
private:
  mds::BufferCtx *user_ctx_;
  ObTruncateTabletArg &truncate_arg_;
  share::SCN scn_;
};

int ObTruncateInfoMdsHelper::on_register(
  const char* buf,
  const int64_t len,
  BufferCtx &ctx)
{
  MDS_TG(1_s);
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_allocator;
  ObTruncateTabletArg arg;
  int64_t pos = 0;
  ObLS *tenant_ls = nullptr;
  ObTabletHandle tablet_handle;
  mds::MdsCtx &user_ctx = static_cast<mds::MdsCtx &>(ctx);

  if (OB_UNLIKELY(nullptr == buf || len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(len));
  } else if (CLICK_FAIL(arg.deserialize(tmp_allocator, buf, len, pos))) {
    LOG_WARN("failed to deserialize", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("arg is invalid", K(ret), K(arg));
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(tenant_ls))) {
  } else if (OB_FAIL(tenant_ls->get_tablet(arg.index_tablet_id_, tablet_handle))) {
  } else if (OB_FAIL(tablet_handle.get_obj()->set_truncate_info(
      arg.truncate_info_.key_,
      arg.truncate_info_,
      user_ctx,
      0/*lock_timeout_us*/))) {
  } else {
    LOG_INFO("[TRUNCATE INFO] on_register for ObTruncateTabletArg", K(ret), K(arg), K(user_ctx.get_writer()));
  }
  return ret;
}

int ObTruncateInfoMdsHelper::on_replay(
    const char* buf,
    const int64_t len,
    const share::SCN &scn,
    BufferCtx &ctx)
{
  MDS_TG(1_s);
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_allocator;
  ObTruncateTabletArg arg;
  int64_t pos = 0;

  if (OB_ISNULL(buf) || OB_UNLIKELY(len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(buf), K(len));
  } else if (CLICK_FAIL(arg.deserialize(tmp_allocator, buf, len, pos))) {
    LOG_WARN("failed to deserialize", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("arg is invalid", K(ret), K(arg));
  } else {
    ObTruncateInfoClogReplayExecutor executor(arg);
    if (OB_FAIL(executor.init(ctx, scn))) {
    } else if (OB_FAIL(executor.execute(scn, arg.index_tablet_id_))) {
    } else {
      LOG_INFO("[TRUNCATE INFO] on_replay for ObTruncateTabletArg", K(ret), K(arg));
    }
  }
  return ret;
}

ObTruncateInfoClogReplayExecutor::ObTruncateInfoClogReplayExecutor(
    ObTruncateTabletArg &truncate_arg)
    : user_ctx_(nullptr),
      truncate_arg_(truncate_arg),
      scn_()
{
}

int ObTruncateInfoClogReplayExecutor::init(
  mds::BufferCtx &user_ctx, 
  const share::SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!truncate_arg_.is_valid() || !scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(truncate_arg_), K(scn));
  } else {
    user_ctx_ = &user_ctx;
    scn_ = scn;
    is_inited_ = true;
  }
  return ret;
}

int ObTruncateInfoClogReplayExecutor::do_replay_(ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  mds::MdsCtx &user_ctx = static_cast<mds::MdsCtx&>(*user_ctx_);
  if (OB_FAIL(tablet_handle.get_obj()->replay_set_truncate_info(
      scn_,
      truncate_arg_.truncate_info_.key_,
      truncate_arg_.truncate_info_,
      user_ctx))) {
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
