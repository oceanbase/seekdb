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

#ifndef OCEANBASE_STANDBY_GRPC_H_
#define OCEANBASE_STANDBY_GRPC_H_

#include "lib/ob_define.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/net/ob_addr.h"
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#ifndef CONST
#define CONST const
#define _OB_UNDEF_CONST
#endif
#ifndef OPTIONAL
#define OPTIONAL
#define _OB_UNDEF_OPTIONAL
#endif
#include <mswsock.h>
#ifdef _OB_UNDEF_CONST
#undef CONST
#undef _OB_UNDEF_CONST
#endif
#ifdef _OB_UNDEF_OPTIONAL
#undef OPTIONAL
#undef _OB_UNDEF_OPTIONAL
#endif
#undef ERROR
#undef DELETE
#endif
#include "grpc/standbyservice.grpc.pb.h"
#include "grpc/ob_grpc_context.h"
#include "lib/container/ob_se_array.h"
#include "standby/ob_standby_palf_base_info.h"
#include "standby/restore/ob_standby_restore_rpc.h"
#include "share/log/palf/lsn.h"
#include "storage/ls/ob_ls_meta.h"
#include "lib/oblog/ob_log_module.h"
#include <climits>
#include <string>

namespace oceanbase
{
namespace obgrpc
{

template <typename ObType, typename ProtoType>
int serialize_ob_to_proto(const ObType &obj, ProtoType *proto)
{
  int ret = common::OB_SUCCESS;
  common::ObArenaAllocator allocator("StandbyGrpcSer");
  const int64_t buf_len = serialization::encoded_length(obj);
  char *buf = nullptr;
  int64_t pos = 0;

  if (OB_ISNULL(proto)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("proto is null", K(ret));
  } else if (buf_len < 0) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("invalid encoded length", K(ret), K(buf_len));
  } else if (0 < buf_len && OB_ISNULL(buf = static_cast<char *>(allocator.alloc(buf_len)))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate serialize buffer", K(ret), K(buf_len));
  } else if (OB_FAIL(serialization::encode(buf, buf_len, pos, obj))) {
    LOG_WARN("failed to encode object for grpc", K(ret), K(buf_len), K(pos));
  } else {
    proto->set_buf(buf, pos);
    proto->set_size(pos);
  }
  return ret;
}

template <typename ProtoType, typename ObType>
int deserialize_proto_to_ob(const ProtoType &proto, ObType &obj)
{
  int ret = common::OB_SUCCESS;
  const std::string &buf = proto.buf();
  const uint64_t size = proto.size();
  int64_t pos = 0;

  if (size > static_cast<uint64_t>(buf.size())
      || size > static_cast<uint64_t>(INT64_MAX)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("invalid grpc payload", K(ret), K(size), "buf_size", buf.size());
  } else if (OB_FAIL(serialization::decode(buf.data(), static_cast<int64_t>(size), pos, obj))) {
    LOG_WARN("failed to decode object from grpc", K(ret), K(size), K(pos));
  }
  return ret;
}

} // namespace obgrpc

namespace restore
{
struct ObRestoreHelperLSViewCtx;
struct ObRestoreHelperSSTableInfoCtx;
struct ObRestoreHelperSSTableMacroRangeCtx;
struct ObRestoreHelperMacroBlockCtx;
struct ObRestoreHelperTabletInfoCtx;
}
namespace standby
{

static constexpr int64_t MAX_PROMOTION_BOUNDARY_HOPS = 16;

struct ObStandbyLSViewTabletCountResult final
{
  OB_UNIS_VERSION(1);
public:
  bool is_valid() const { return tablet_count_ > 0; }
  ObStandbyLSViewTabletCountResult();
  ~ObStandbyLSViewTabletCountResult() {}

  TO_STRING_KV(K_(tablet_count));
  int64_t tablet_count_;
};

struct ObStandbyLSViewMeta final
{
  OB_UNIS_VERSION(1);
public:
  ObStandbyLSViewMeta();
  ~ObStandbyLSViewMeta() {}
  bool is_valid() const { return ls_meta_.is_valid() && physical_checkpoint_scn_.is_valid(); }

  TO_STRING_KV(K_(ls_meta), K_(physical_checkpoint_scn));
  storage::ObLSMeta ls_meta_;
  share::SCN physical_checkpoint_scn_;
};

struct StandbyPromotionBoundary final
{
  OB_UNIS_VERSION(1);
public:
  StandbyPromotionBoundary();
  ~StandbyPromotionBoundary() = default;
  bool is_valid() const;
  bool is_same_as(const StandbyPromotionBoundary &other) const;
  int add_source_hop(
      const common::ObAddr &relay,
      const common::ObAddr &source,
      int64_t source_version);

  struct SourceHop final
  {
    OB_UNIS_VERSION(1);
  public:
    SourceHop() : relay_(), source_(), source_version_(0) {}
    SourceHop(
        const common::ObAddr &relay,
        const common::ObAddr &source,
        const int64_t source_version)
      : relay_(relay), source_(source), source_version_(source_version) {}
    bool is_valid() const
    {
      return relay_.is_valid()
          && source_.is_valid()
          && relay_ != source_
          && source_version_ >= 0;
    }
    bool is_same_as(const SourceHop &other) const
    {
      return relay_ == other.relay_
          && source_ == other.source_
          && source_version_ == other.source_version_;
    }

    TO_STRING_KV(K_(relay), K_(source), K_(source_version));
    common::ObAddr relay_;
    common::ObAddr source_;
    int64_t source_version_;
  };

  TO_STRING_KV(K_(origin), K_(cutover_scn), K_(source_chain));
  common::ObAddr origin_;
  share::SCN cutover_scn_;
  common::ObSEArray<SourceHop, 4> source_chain_;
};

struct StandbyPromotionBoundaryRequest final
{
  OB_UNIS_VERSION(1);
public:
  StandbyPromotionBoundaryRequest() : visited_() {}
  ~StandbyPromotionBoundaryRequest() = default;

  bool is_valid() const;
  bool contains(const common::ObAddr &addr) const;
  int add_visited(const common::ObAddr &addr);

  TO_STRING_KV(K_(visited));
  common::ObSEArray<common::ObAddr, 4> visited_;
};

class ObStandbyGrpcClient;

class ObStandbyGrpcClient
{
public:
  ObStandbyGrpcClient();
  ~ObStandbyGrpcClient();

  int init(const common::ObAddr& addr, int64_t timeout, bool rpc_tls_enabled);
  int get_ls_view_tablet_count(ObStandbyLSViewTabletCountResult& result);
  int check_restore_precondition(obcall::ObCheckRestorePreconditionResult& result);
  int fetch_standby_palf_base_info(const standby::ObFetchStandbyPalfBaseInfoArg &arg,
                                   standby::ObFetchStandbyPalfBaseInfoResult &result);
  int fetch_log(
      const palf::LSN &start_lsn,
      const int64_t max_bytes,
      const std::function<int(const char *, int64_t, const palf::LSN &,
                              const share::SCN &)> &consume_log);
  int get_promotion_boundary(
      const StandbyPromotionBoundaryRequest &request,
      StandbyPromotionBoundary &boundary);
  int fetch_tablet_info(const obcall::ObCopyTabletInfoArg& arg,
                        std::function<int(const obcall::ObCopyTabletInfo&)> callback);
  int create_tablet_info_stream(
      const obcall::ObCopyTabletInfoArg &arg,
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<standbyservice::FetchTabletInfoRes>> &reader);
  int create_ls_view_stream(
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<standbyservice::FetchLSViewRes>> &reader);
  int create_tablet_sstable_info_stream(
      const obcall::ObCopyTabletsSSTableInfoArg &arg,
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<standbyservice::FetchTabletSSTableInfoRes>> &reader);
  int create_sstable_macro_info_stream(
      const obcall::ObCopySSTableMacroRangeInfoArg &arg,
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<standbyservice::FetchSSTableMacroInfoRes>> &reader);
  int create_macro_block_stream(
      const obcall::ObCopyMacroBlockRangeArg &arg,
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<standbyservice::FetchMacroBlockRes>> &reader);
  int translate_error(const grpc::Status &status);
  static int init_ls_view_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      bool rpc_tls_enabled,
      common::ObIAllocator &allocator,
      ObLSMeta &ls_meta,
      share::SCN &physical_checkpoint_scn,
      restore::ObRestoreHelperLSViewCtx &ls_view_ctx);
  static int init_tablet_sstable_info_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      bool rpc_tls_enabled,
      const obcall::ObCopyTabletsSSTableInfoArg &arg,
      common::ObIAllocator &allocator,
      restore::ObRestoreHelperSSTableInfoCtx &sstable_info_ctx);
  static int init_sstable_macro_info_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      bool rpc_tls_enabled,
      const obcall::ObCopySSTableMacroRangeInfoArg &arg,
      common::ObIAllocator &allocator,
      restore::ObRestoreHelperSSTableMacroRangeCtx &macro_range_ctx);
  static int init_macro_block_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      bool rpc_tls_enabled,
      const obcall::ObCopyMacroBlockRangeArg &arg,
      common::ObIAllocator &allocator,
      restore::ObRestoreHelperMacroBlockCtx &macro_block_ctx);
  static int init_tablet_info_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      bool rpc_tls_enabled,
      const obcall::ObCopyTabletInfoArg &arg,
      common::ObIAllocator &allocator,
      restore::ObRestoreHelperTabletInfoCtx &tablet_info_ctx);

private:
  bool is_inited_;
  obgrpc::ObGrpcClient<standbyservice::StandbyService> grpc_client_;
};

} // namespace standby
} // oceanbase

#endif // OCEANBASE_STANDBY_GRPC_H_
