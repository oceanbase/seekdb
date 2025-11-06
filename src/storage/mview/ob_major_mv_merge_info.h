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

#ifndef OCEABASE_MAJOR_MV_MERGE_INFO_
#define OCEABASE_MAJOR_MV_MERGE_INFO_

#include "share/scn.h"
#include "storage/multi_data_source/buffer_ctx.h"

namespace oceanbase
{
namespace storage
{

struct ObMajorMVMergeInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObMajorMVMergeInfo();
  ~ObMajorMVMergeInfo() = default;
  bool is_valid() const;
  void reset();
  bool need_update_major_mv_merge_scn()
  { return major_mv_merge_scn_ < major_mv_merge_scn_safe_calc_; }
  void operator=(const ObMajorMVMergeInfo &other);

  TO_STRING_KV(K_(major_mv_merge_scn), K_(major_mv_merge_scn_safe_calc), K_(major_mv_merge_scn_publish));

  share::SCN major_mv_merge_scn_;
  share::SCN major_mv_merge_scn_safe_calc_;
  share::SCN major_mv_merge_scn_publish_;
};

struct ObUpdateMergeScnArg final
{
  OB_UNIS_VERSION(1);
public:
  ObUpdateMergeScnArg();
  ~ObUpdateMergeScnArg() = default;
  bool is_valid() const;
  int init(const share::ObLSID &ls_id, const share::SCN &merge_scn);
  TO_STRING_KV(K_(ls_id), K_(merge_scn));

  share::ObLSID ls_id_;
  share::SCN merge_scn_;
};

struct ObMVPublishSCNHelper 
{
  static int on_register(
      const char *buf,
      const int64_t len,
      mds::BufferCtx &ctx);

  static int on_replay(
      const char *buf,
      const int64_t len,
      const share::SCN scn,
      mds::BufferCtx &ctx);
};

struct ObMVNoticeSafeHelper
{
  static int on_register(
      const char *buf,
      const int64_t len,
      mds::BufferCtx &ctx);

  static int on_replay(
      const char *buf,
      const int64_t len,
      const share::SCN scn,
      mds::BufferCtx &ctx);
};

struct ObMVUpdateSCNHelper 
{
  static int on_register(
      const char *buf,
      const int64_t len,
      mds::BufferCtx &ctx)
  {
    return OB_SUCCESS;
  }

  static int on_replay(
      const char *buf,
      const int64_t len,
      const share::SCN scn,
      mds::BufferCtx &ctx)
  {
    return OB_SUCCESS;
  }
};

struct ObMVMergeSCNHelper 
{
  static int on_register(
      const char *buf,
      const int64_t len,
      mds::BufferCtx &ctx);

  static int on_replay(
      const char *buf,
      const int64_t len,
      const share::SCN scn,
      mds::BufferCtx &ctx);
};

struct ObMVCheckReplicaHelper
{
  static int get_and_update_merge_info(
      const share::ObLSID &ls_id, 
      ObMajorMVMergeInfo &info);
  static int check_can_add_member(
      const common::ObAddr &server,
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const uint64_t rpc_timeout);
  static int get_merge_info(
      const share::ObLSID &ls_id, 
      ObMajorMVMergeInfo &info);
};

struct ObUnUseCtx : public mds::BufferCtx
{
  TO_STRING_EMPTY();
  int serialize(char *buf, const int64_t len, int64_t &pos) const { return OB_SUCCESS; }
  int deserialize(const char *buf, const int64_t len, int64_t &pos) { return OB_SUCCESS; }
  int64_t get_serialize_size() const { return 0; }
  const mds::MdsWriter get_writer() const { return mds::MdsWriter(mds::WriterType::TRANSACTION, 0); }
};

}
}

#endif
