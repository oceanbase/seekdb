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
#ifndef OCEANBASE_LOGSERVICE_LOGRPC_OB_LOG_RPC_ARG_H_
#define OCEANBASE_LOGSERVICE_LOGRPC_OB_LOG_RPC_ARG_H_
// moved from share/ob_rpc_struct.h:palf member-list/access-mode RPC arguments
// (owner=logservice;ns obrpc unchanged,no semantic change for consumers;only a lightweight header,does not pull in the palf implementation)
#include "lib/utility/ob_unify_serialize.h"
#include "share/ob_ls_id.h"
#include "common/ob_member_list.h"
#include "logservice/palf/palf_options.h"
#include "logservice/palf/log_meta_info.h"
namespace oceanbase
{
namespace obcall
{
struct ObLSAccessModeInfo
{
  OB_UNIS_VERSION(1);
public:
  ObLSAccessModeInfo(): ls_id_(),
                        mode_version_(palf::INVALID_PROPOSAL_ID),
                        access_mode_(palf::AccessMode::INVALID_ACCESS_MODE),
                        ref_scn_(),
                        addr_(),
                        sys_ls_end_scn_() {}
  ~ObLSAccessModeInfo() {}
  bool is_valid() const;
  int init(const share::ObLSID &ls_idd,
           const int64_t mode_version,
           const palf::AccessMode &access_mode,
           const share::SCN &ref_scn,
           const share::SCN &sys_ls_end_scn);
  int assign(const ObLSAccessModeInfo &other);
  TO_STRING_KV(K_(ls_id), K_(mode_version),
               K_(access_mode), K_(ref_scn), K_(sys_ls_end_scn));
  share::ObLSID get_ls_id() const
  {
    return ls_id_;
  }
  palf::AccessMode get_access_mode() const
  {
    return access_mode_;
  }
  int64_t get_mode_version() const
  {
    return mode_version_;
  }
  const share::SCN &get_ref_scn() const
  {
    return ref_scn_;
  }
  const share::SCN &get_sys_ls_end_scn() const
  {
    return sys_ls_end_scn_;
  }
private:
  DISALLOW_COPY_AND_ASSIGN(ObLSAccessModeInfo);
private:
  share::ObLSID ls_id_;
  int64_t mode_version_;
  palf::AccessMode access_mode_;
  share::SCN ref_scn_;
  ObAddr addr_;//no used, add in 4200 RC1
  share::SCN sys_ls_end_scn_; // new arg in V4.2.0
};

struct ObFetchStableMemberListArg final
{
  OB_UNIS_VERSION(1);
public:
  ObFetchStableMemberListArg(): ls_id_() {}
  ~ObFetchStableMemberListArg() {}
  bool is_valid() const { return ls_id_.is_valid() && 1UL != OB_INVALID_TENANT_ID; }
  void reset() { ls_id_.reset();  }
  const share::ObLSID &get_ls_id() const { return ls_id_; }
  TO_STRING_KV(K_(ls_id));
private:
  share::ObLSID ls_id_;
};

struct ObFetchStableMemberListInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObFetchStableMemberListInfo() : member_list_(), config_version_() {}
  ~ObFetchStableMemberListInfo() {}
  bool is_valid() const { return member_list_.is_valid() && config_version_.is_valid(); }
  void reset() { member_list_.reset(); }
  int init(const common::ObMemberList &member_list, const palf::LogConfigVersion &config_version);
  const common::ObMemberList &get_member_list() const { return member_list_; }
  const palf::LogConfigVersion &get_config_version() const { return config_version_; }
  TO_STRING_KV(K_(member_list));
private:
  common::ObMemberList member_list_;
  palf::LogConfigVersion config_version_;
};
}  // namespace obcall
}  // namespace oceanbase
#endif
