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

#ifndef OCEANBASE_STORAGE_OB_TABLET_BINDING_HELPER
#define OCEANBASE_STORAGE_OB_TABLET_BINDING_HELPER

#include "storage/tablet/ob_batch_create_tablet_arg.h"
#include "common/ob_tablet_id.h"
#include "storage/ob_storage_rpc_arg.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_array_serialization.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "lib/ob_define.h"
#include "storage/tablet/ob_tablet_binding_mds_user_data.h"

namespace oceanbase
{
namespace obcall
{
struct ObBatchCreateTabletArg;
struct ObBatchRemoveTabletArg;
struct ObCreateTabletInfo;
struct ObBatchGetTabletBindingArg;
struct ObBatchGetTabletBindingRes;
}

namespace share
{
class SCN;
}

namespace rootserver
{
class ObDDLSQLTransaction;
}

namespace transaction
{
struct ObMulSourceDataNotifyArg;
class ObTransID;
}

namespace storage
{
namespace mds
{
struct BufferCtx;
class MdsCtx;
}

class ObLS;
class ObTabletHandle;
class ObTabletTxMultiSourceDataUnit;
class ObTabletMapKey;

// deprecated
class ObBatchUnbindTabletArg final
{
public:
  ObBatchUnbindTabletArg();
  ~ObBatchUnbindTabletArg() {}
  int assign(const ObBatchUnbindTabletArg &other);
  inline bool is_redefined() const { return schema_version_ != OB_INVALID_VERSION; }
  TO_STRING_KV(K_(schema_version), K_(orig_tablet_ids), K_(hidden_tablet_ids), K_(is_write_defensive));
  bool is_valid() { return true; }
  OB_UNIS_VERSION_V(2);

public:
  int64_t schema_version_;
  ObSArray<ObTabletID> orig_tablet_ids_;
  ObSArray<ObTabletID> hidden_tablet_ids_;
  bool is_write_defensive_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBatchUnbindTabletArg);
};

class ObBatchUnbindLobTabletArg final
{
public:
  ObBatchUnbindLobTabletArg();
  ~ObBatchUnbindLobTabletArg() {}
  int assign(const ObBatchUnbindLobTabletArg &other);
  TO_STRING_KV(K_(data_tablet_ids));
  bool is_valid() { return true; }
  OB_UNIS_VERSION_V(2);

public:
  
  ObSArray<ObTabletID> data_tablet_ids_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBatchUnbindLobTabletArg);
};

// deprecated
class ObTabletBindingHelper final
{
public:
  ObTabletBindingHelper(const ObLS &ls, const transaction::ObMulSourceDataNotifyArg &trans_flags)
    : ls_(ls), trans_flags_(trans_flags) {}
  ~ObTabletBindingHelper() {}

  // create tablet by new mds
  static int modify_tablet_binding_for_new_mds_create(const obcall::ObBatchCreateTabletArg &arg, const share::SCN &replay_scn, mds::BufferCtx &ctx);
  static int bind_hidden_tablet_to_orig_tablet(ObLS &ls, const obcall::ObCreateTabletInfo &info, const share::SCN &replay_scn, mds::BufferCtx &ctx);
  static int bind_lob_tablet_to_data_tablet(ObLS &ls, const obcall::ObBatchCreateTabletArg &arg, const obcall::ObCreateTabletInfo &info, const share::SCN &replay_scn, mds::BufferCtx &ctx);
  // TODO (lihongqin.lhq) delete get_tablet_for_new_mds
  static int get_tablet_for_new_mds(const ObLS &ls, const ObTabletID &tablet_id, const share::SCN &replay_scn, ObTabletHandle &handle);

  // common
  template<typename F>
  static int modify_tablet_binding_new_mds(ObLS &ls, const ObTabletID &tablet_id, const share::SCN &replay_scn, mds::BufferCtx &ctx, F &&op);
  static int has_lob_tablets(const obcall::ObBatchCreateTabletArg &arg, const obcall::ObCreateTabletInfo &info, bool &has_lob);
  static int get_ls(ObLS *&tenant_ls);
  static int build_single_table_write_defensive(const share::schema::ObTableSchema &table_schema,
                                                const int64_t schema_version,
                                                rootserver::ObDDLSQLTransaction &trans);
private:
  const ObLS &ls_;
  const transaction::ObMulSourceDataNotifyArg &trans_flags_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTabletBindingHelper);
};

// deprecated
class ObTabletUnbindMdsHelper
{
public:
  static int on_register(const char* buf, const int64_t len, mds::BufferCtx &ctx);
  static int register_process(ObBatchUnbindTabletArg &arg, mds::BufferCtx &ctx);
  static int replay_process(ObBatchUnbindTabletArg &arg, const share::SCN &scn, mds::BufferCtx &ctx);
  static int on_replay(const char* buf, const int64_t len, const share::SCN &scn, mds::BufferCtx &ctx);
private:
  static int unbind_hidden_tablets_from_orig_tablets(ObLS &ls, const ObBatchUnbindTabletArg &arg, const share::SCN &replay_scn, mds::BufferCtx &ctx);
  static int set_redefined_versions_for_hidden_tablets(ObLS &ls, const ObBatchUnbindTabletArg &arg, const share::SCN &replay_scn, mds::BufferCtx &ctx);
  static int modify_tablet_binding_for_unbind(const ObBatchUnbindTabletArg &arg, const share::SCN &replay_scn, mds::BufferCtx &ctx);
};

class ObTabletBindingMdsArg final
{
public:
  OB_UNIS_VERSION(2);
public:
  // arg with such tablet cnt cannot be more than mds buffer limit (1.5M)
  const static int64_t BATCH_TABLET_CNT = 8192;
  ObTabletBindingMdsArg();
  ~ObTabletBindingMdsArg() {}
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(tablet_ids), K_(binding_datas));
public:
  ObSArray<ObTabletID> tablet_ids_;
  ObSArray<ObTabletBindingMdsUserData> binding_datas_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTabletBindingMdsArg);
};

class ObBindHiddenTabletToOrigTabletOp final
{
public:
  ObBindHiddenTabletToOrigTabletOp() : info_(nullptr) {}
  ObBindHiddenTabletToOrigTabletOp(const obcall::ObCreateTabletInfo &info) : info_(&info) {}
  ~ObBindHiddenTabletToOrigTabletOp() = default;
  int assign(const ObBindHiddenTabletToOrigTabletOp &other);
  int operator()(ObTabletBindingMdsUserData &data);
  TO_STRING_KV(KPC_(info));
private:
  const obcall::ObCreateTabletInfo *info_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBindHiddenTabletToOrigTabletOp);
};

class ObBindLobTabletToDataTabletOp final
{
public:
  ObBindLobTabletToDataTabletOp() : arg_(nullptr), info_(nullptr) {}
  ObBindLobTabletToDataTabletOp(const obcall::ObBatchCreateTabletArg &arg, const obcall::ObCreateTabletInfo &info)
    : arg_(&arg), info_(&info) {}
  int assign(const ObBindLobTabletToDataTabletOp &other);
  ~ObBindLobTabletToDataTabletOp() = default;
  int operator()(ObTabletBindingMdsUserData &data);
  TO_STRING_KV(KPC_(arg), KPC_(info));
private:
  const obcall::ObBatchCreateTabletArg *arg_;
  const obcall::ObCreateTabletInfo *info_;
};

class ObUnbindHiddenTabletFromOrigTabletOp final
{
public:
  ObUnbindHiddenTabletFromOrigTabletOp(const int64_t schema_version)
    : schema_version_(schema_version) {}
  ~ObUnbindHiddenTabletFromOrigTabletOp() = default;
  int operator()(ObTabletBindingMdsUserData &data);
private:
  int64_t schema_version_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObUnbindHiddenTabletFromOrigTabletOp);
};

class ObSetRwDefensiveOp final
{
public:
  ObSetRwDefensiveOp(const int64_t schema_version)
    : schema_version_(schema_version) {}
  ~ObSetRwDefensiveOp() = default;
  int operator()(ObTabletBindingMdsUserData &data);
private:
  int64_t schema_version_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSetRwDefensiveOp);
};

class ObSetWriteDefensiveOp final
{
public:
  ObSetWriteDefensiveOp(const int64_t schema_version)
    : schema_version_(schema_version) {}
  ~ObSetWriteDefensiveOp() = default;
  int operator()(ObTabletBindingMdsUserData &data);
private:
  int64_t schema_version_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSetWriteDefensiveOp);
};

template<typename F>
struct ModifyBindingByOp final
{
  ModifyBindingByOp(F &&op) : op_(op) {}
  ~ModifyBindingByOp() = default;
  int operator()(const int64_t i, ObTabletBindingMdsUserData &data) { return op_(data); }
  F &&op_;
};

template<typename F>
struct ModifyBindingByOps final
{
  ModifyBindingByOps(ObIArray<F> &ops) : ops_(ops) {}
  ~ModifyBindingByOps() = default;
  int operator()(const int64_t i, ObTabletBindingMdsUserData &data) { return ops_.at(i)(data); }
  ObIArray<F> &ops_;
};

struct TabletIDCmp final
{
  bool operator()(const ObTabletID &lhs, const ObTabletID &rhs) {
    return lhs < rhs;
  }
};

class ObTabletBindingMdsHelper
{
public:
  static int on_register(const char* buf, const int64_t len, mds::BufferCtx &ctx);
  static int on_replay(const char* buf, const int64_t len, const share::SCN &scn, mds::BufferCtx &ctx);

public:
  static int get_sorted_tablets(const ObIArray<ObTabletID> &tablet_ids,
    ObArray<ObTabletID> &sorted_tablet_ids,
    ObMySQLTransaction &trans);
  static int batch_get_tablet_binding(
    const int64_t abs_timeout_us,
    const obcall::ObBatchGetTabletBindingArg &arg,
    obcall::ObBatchGetTabletBindingRes &res);
  static int get_tablet_binding_mds_by_rpc(const ObIArray<ObTabletID> &tablet_ids,
    const int64_t abs_timeout_us,
    ObIArray<ObTabletBindingMdsUserData> &datas);
  static int modify_tablet_binding_for_create(const obcall::ObBatchCreateTabletArg &arg,
    const int64_t abs_timeout_us,
    ObMySQLTransaction &trans);
  static int modify_tablet_binding_for_unbind(
    const ObIArray<ObTabletID> &orig_tablet_ids,
    const ObIArray<ObTabletID> &hidden_tablet_ids,
    const int64_t redefined_schema_version,
    const int64_t abs_timeout_us,
    ObMySQLTransaction &trans);
  static int modify_tablet_binding_for_rw_defensive(const ObIArray<ObTabletID> &tablet_ids,
    const int64_t schema_version,
    const int64_t abs_timeout_us,
    ObMySQLTransaction &trans);
  static int modify_tablet_binding_for_write_defensive(const ObIArray<ObTabletID> &tablet_ids,
    const int64_t schema_version,
    const int64_t abs_timeout_us,
    ObMySQLTransaction &trans);

private:
  template<typename F>
  static int modify_tablet_binding_batch_(const ObIArray<ObTabletID> &tablet_ids,
    const int64_t abs_timeout_us,
    F &&op,
    ObMySQLTransaction &trans);
  template<typename F>
  static int modify_tablet_binding_(const ObIArray<ObTabletID> &tablet_ids,
    const int64_t abs_timeout_us,
    F &&op,
    ObMySQLTransaction &trans);
  static int register_mds_(
    const ObTabletBindingMdsArg &arg,
    ObMySQLTransaction &trans);

  static int modify_(const ObTabletBindingMdsArg &arg, const share::SCN &scn, mds::BufferCtx &ctx);
  static int set_tablet_binding_mds_(
    ObLS &ls,
    const ObTabletID &tablet_id,
    const share::SCN &replay_scn,
    const ObTabletBindingMdsUserData &data,
    mds::BufferCtx &ctx);
};

class ObTabletUnbindLobMdsHelper
{
public:
  static int on_register(const char* buf, const int64_t len, mds::BufferCtx &ctx);
  static int register_process(ObBatchUnbindTabletArg &arg, mds::BufferCtx &ctx);
  static int on_replay(const char* buf, const int64_t len, const share::SCN &scn, mds::BufferCtx &ctx);
private:
  static int modify_tablet_binding_for_unbind_lob_(const ObBatchUnbindLobTabletArg &arg, const share::SCN &replay_scn, mds::BufferCtx &ctx);
};

struct ClearLobTabletId
{
public:
  ClearLobTabletId() {}
  ClearLobTabletId& operator=(const ClearLobTabletId&) = delete;
  int operator()(ObTabletBindingMdsUserData &data) const
  {
    // lob_meta_tablet and lob_piece_tablet_id need to be cleaned up at the same time
    data.lob_meta_tablet_id_.reset();
    data.lob_piece_tablet_id_.reset();
    return OB_SUCCESS;
  }
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_BINDING_HELPER
