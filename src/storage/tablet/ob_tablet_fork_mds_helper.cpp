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

#define USING_LOG_PREFIX STORAGE

#include "ob_tablet_fork_mds_helper.h"
#include "storage/ob_tablet_autoinc_seq_rpc_handler.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "observer/ob_inner_sql_connection.h"
#include "lib/mysqlclient/ob_isql_connection.h"
#include "storage/tx/ob_multi_data_source.h"

using namespace oceanbase::obrpc;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::transaction;
using namespace oceanbase::observer;
using oceanbase::sqlclient::ObISQLConnection;

namespace oceanbase
{
namespace storage
{

ObTabletForkMdsArg::ObTabletForkMdsArg()
  : tenant_id_(OB_INVALID_TENANT_ID),
    ls_id_(),
    autoinc_seq_arg_(),
    truncate_arg_(),
    allocator_("ForkMdsArg")
{
}

ObTabletForkMdsArg::~ObTabletForkMdsArg()
{
  reset();
}

int ObTabletForkMdsArg::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, tenant_id_, ls_id_, autoinc_seq_arg_);
  if (OB_SUCC(ret)) {
    bool has_truncate_arg = truncate_arg_.is_valid();
    if (OB_FAIL(serialization::encode_bool(buf, buf_len, pos, has_truncate_arg))) {
      LOG_WARN("failed to encode has_truncate_arg", K(ret));
    } else if (has_truncate_arg) {
      if (OB_FAIL(truncate_arg_.serialize(buf, buf_len, pos))) {
        LOG_WARN("failed to serialize truncate arg", K(ret));
      }
    }
  }
  return ret;
}

int ObTabletForkMdsArg::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, tenant_id_, ls_id_, autoinc_seq_arg_);
  if (OB_SUCC(ret)) {
    bool has_truncate_arg = false;
    if (OB_FAIL(serialization::decode_bool(buf, data_len, pos, &has_truncate_arg))) {
      LOG_WARN("failed to decode has_truncate_arg", K(ret));
    } else if (has_truncate_arg) {
      truncate_arg_.destroy();
      allocator_.reset();
      if (OB_FAIL(truncate_arg_.deserialize(allocator_, buf, data_len, pos))) {
        LOG_WARN("failed to deserialize truncate arg", K(ret));
      }
    } else {
      truncate_arg_.destroy();
      allocator_.reset();
    }
  }
  return ret;
}

int64_t ObTabletForkMdsArg::get_serialize_size() const
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, tenant_id_, ls_id_, autoinc_seq_arg_);
  len += serialization::encoded_length_bool(true); // has_truncate_arg flag
  if (truncate_arg_.is_valid()) {
    len += truncate_arg_.get_serialize_size();
  }
  return len;
}

bool ObTabletForkMdsArg::is_valid() const
{
  return OB_INVALID_TENANT_ID != tenant_id_
    && ls_id_.is_valid()
    && (autoinc_seq_arg_.is_valid() || truncate_arg_.is_valid());
}

void ObTabletForkMdsArg::reset()
{
  tenant_id_ = OB_INVALID_TENANT_ID;
  ls_id_.reset();
  autoinc_seq_arg_.reset();
  truncate_arg_.destroy();
  allocator_.reset();
}

int ObTabletForkMdsArg::set_autoinc_seq_arg(const obrpc::ObBatchSetTabletAutoincSeqArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid autoinc seq arg", K(ret), K(arg));
  } else {
    autoinc_seq_arg_ = arg;
  }
  return ret;
}

int ObTabletForkMdsArg::set_truncate_arg(const rootserver::ObTruncateTabletArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid truncate arg", K(ret), K(arg));
  } else {
    ObArenaAllocator tmp_allocator("ForkMdsSetArg");
    const int64_t buf_len = arg.get_serialize_size();
    int64_t pos = 0;
    char *buf = nullptr;

    if (OB_ISNULL(buf = static_cast<char *>(tmp_allocator.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail alloc memory", K(ret), K(buf_len));
    } else if (OB_FAIL(arg.serialize(buf, buf_len, pos))) {
      LOG_WARN("fail to serialize", K(ret), K(arg));
    } else {
      truncate_arg_.destroy();
      allocator_.reset();
      pos = 0;
      if (OB_FAIL(truncate_arg_.deserialize(allocator_, buf, buf_len, pos))) {
        LOG_WARN("fail to deserialize", K(ret), K(arg));
      }
    }
  }
  return ret;
}

int ObTabletForkMdsHelper::register_mds(
    const ObTabletForkMdsArg &arg,
    const bool need_flush_redo,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  sqlclient::ObISQLConnection *isql_conn = nullptr;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet fork mds arg", KR(ret), K(arg));
  } else if (OB_ISNULL(isql_conn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid connection when register tablet fork mds", KR(ret), K(arg.tenant_id_), K(arg.ls_id_));
  } else {
    const int64_t size = arg.get_serialize_size();
    ObArenaAllocator allocator;
    char *buf = nullptr;
    int64_t pos = 0;
    ObRegisterMdsFlag flag;
    flag.need_flush_redo_instantly_ = need_flush_redo;
    flag.mds_base_scn_.reset();
    if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate buffer for tablet fork mds", KR(ret), K(arg.tenant_id_), K(arg.ls_id_), K(size));
    } else if (OB_FAIL(arg.serialize(buf, size, pos))) {
      LOG_WARN("failed to serialize tablet fork mds arg", KR(ret), K(arg.tenant_id_), K(arg.ls_id_), K(size), K(pos));
    } else if (OB_FAIL(static_cast<ObInnerSQLConnection *>(isql_conn)->register_multi_data_source(
        arg.tenant_id_, arg.ls_id_, ObTxDataSourceType::TABLET_FORK, buf, pos, flag))) {
      LOG_WARN("failed to register tablet fork mds", KR(ret), K(arg.tenant_id_), K(arg.ls_id_), K(need_flush_redo), K(pos));
    }
  }
  return ret;
}

int ObTabletForkMdsHelper::on_register(const char* buf, const int64_t len, mds::BufferCtx &ctx)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  ObTabletForkMdsArg arg;
  if (OB_FAIL(arg.deserialize(buf, len, pos))) {
    LOG_WARN("failed to deserialize tablet fork mds arg", KR(ret), K(len), K(pos));
  } else if (OB_FAIL(modify(arg, SCN::invalid_scn(), ctx))) {
    LOG_WARN("failed to apply tablet fork mds on_register", KR(ret), K(arg));
  }
  return ret;
}

int ObTabletForkMdsHelper::on_replay(const char* buf, const int64_t len,
    const share::SCN &scn, mds::BufferCtx &ctx)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  ObTabletForkMdsArg arg;
  if (OB_FAIL(arg.deserialize(buf, len, pos))) {
    LOG_WARN("failed to deserialize tablet fork mds arg", KR(ret), K(len), K(pos), K(scn));
  } else if (OB_FAIL(modify(arg, scn, ctx))) {
    LOG_WARN("failed to apply tablet fork mds on_replay", KR(ret), K(scn), K(arg));
  }
  return ret;
}

int ObTabletForkMdsHelper::modify(
    const ObTabletForkMdsArg &arg,
    const share::SCN &scn,
    mds::BufferCtx &ctx)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = arg.ls_id_;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;

  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(MTL(ObLSService*)->get_ls(ls_id, ls_handle, ObLSGetMod::STORAGE_MOD))) {
    LOG_WARN("fail to get ls", KR(ret), K(arg));
  } else if (OB_UNLIKELY(nullptr == (ls = ls_handle.get_ls()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be NULL", KR(ret), K(arg), KP(ls));
  }

  if (OB_SUCC(ret) && arg.autoinc_seq_arg_.is_valid()) {
    if (OB_FAIL(ObTabletAutoincSeqRpcHandler::get_instance().batch_set_tablet_autoinc_seq_in_trans(*ls, arg.autoinc_seq_arg_, scn, ctx))) {
      LOG_WARN("failed to batch set tablet autoinc seq", KR(ret), K(arg.tenant_id_), K(ls_id), K(scn),
          "param_cnt", arg.autoinc_seq_arg_.autoinc_params_.count());
    } else {
      LOG_INFO("fork table: successfully set autoinc seq", K(arg.tenant_id_), K(ls_id), K(scn),
          "param_cnt", arg.autoinc_seq_arg_.autoinc_params_.count());
    }
  }

  if (OB_SUCC(ret) && arg.truncate_arg_.is_valid()) {
    const rootserver::ObTruncateTabletArg &truncate_arg = arg.truncate_arg_;
    ObTabletHandle tablet_handle;
    const ObTabletMapKey key(ls_id, truncate_arg.index_tablet_id_);

    if (OB_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, tablet_handle))) {
      LOG_WARN("failed to get tablet for truncate info", K(ret), K(key));
    } else if (OB_FAIL(tablet_handle.get_obj()->set_truncate_info(
        truncate_arg.truncate_info_.key_,
        truncate_arg.truncate_info_,
        static_cast<mds::MdsCtx &>(ctx),
        0/*lock_timeout_us*/))) {
      LOG_WARN("failed to set truncate info", KR(ret), K(arg.tenant_id_), K(ls_id), K(scn), K(truncate_arg));
    } else {
      LOG_INFO("fork table: successfully set truncate info", K(arg.tenant_id_), K(ls_id), K(scn),
          K(truncate_arg.index_tablet_id_), K(truncate_arg.truncate_info_.key_));
    }
  }

  if (OB_FAIL(ret)) {
    LOG_WARN("failed to apply tablet fork mds", KR(ret), K(scn), K(arg.tenant_id_), K(arg.ls_id_), K(arg));
  } else {
    LOG_DEBUG("tablet fork mds applied", K(scn), K(arg.tenant_id_), K(arg.ls_id_),
        "has_autoinc", arg.autoinc_seq_arg_.is_valid(),
        "autoinc_param_cnt", arg.autoinc_seq_arg_.is_valid() ? arg.autoinc_seq_arg_.autoinc_params_.count() : 0,
        "has_truncate", arg.truncate_arg_.is_valid());
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
