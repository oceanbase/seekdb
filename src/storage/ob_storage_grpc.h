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

#ifndef OCEABASE_STORAGE_GRPC
#define OCEABASE_STORAGE_GRPC

#include "lib/ob_define.h"
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
#include "grpc/storageservice.grpc.pb.h"
#include "grpc/ob_grpc_context.h"
#include "storage/ob_storage_rpc.h"
#include "storage/ls/ob_ls_meta_package.h"
#include "lib/oblog/ob_log_module.h"
#include <string>

namespace oceanbase
{
namespace restore
{
struct ObRestoreHelperLSViewCtx;
struct ObRestoreHelperSSTableInfoCtx;
struct ObRestoreHelperSSTableMacroRangeCtx;
struct ObRestoreHelperMacroBlockCtx;
struct ObRestoreHelperTabletInfoCtx;
}
namespace storage
{

struct ObGetLSViewTabletCountResult final
{
  OB_UNIS_VERSION(1);
public:
  bool is_valid() const { return tablet_count_ > 0; }
  ObGetLSViewTabletCountResult();
  ~ObGetLSViewTabletCountResult() {}

  TO_STRING_KV(K_(tablet_count));
  int64_t tablet_count_;
};

class ObStorageGrpcClient;

class ObStorageGrpcServiceImpl final : public storageservice::StorageService::Service
{
public:
  ObStorageGrpcServiceImpl() {}
  virtual ~ObStorageGrpcServiceImpl() {}

  grpc::Status fetch_ls_view(grpc::ServerContext* context,
                             const storageservice::FetchLSViewReq* request,
                             grpc::ServerWriter<storageservice::FetchLSViewRes>* writer) override;

  grpc::Status fetch_tablet_info(grpc::ServerContext* context,
                                    const storageservice::FetchTabletInfoReq* request,
                                    grpc::ServerWriter<storageservice::FetchTabletInfoRes>* writer) override;

  grpc::Status fetch_tablet_sstable_info(grpc::ServerContext* context,
                                            const storageservice::FetchTabletSSTableInfoReq* request,
                                            grpc::ServerWriter<storageservice::FetchTabletSSTableInfoRes>* writer) override;

  grpc::Status fetch_sstable_macro_info(grpc::ServerContext* context,
                                            const storageservice::FetchSSTableMacroInfoReq* request,
                                            grpc::ServerWriter<storageservice::FetchSSTableMacroInfoRes>* writer) override;

  grpc::Status fetch_macro_block(grpc::ServerContext* context,
                                    const storageservice::FetchMacroBlockReq* request,
                                    grpc::ServerWriter<storageservice::FetchMacroBlockRes>* writer) override;

  grpc::Status get_ls_view_tablet_count(grpc::ServerContext* context,
                                            const storageservice::GetLSViewTabletCountReq* request,
                                            storageservice::GetLSViewTabletCountRes* response) override;

  grpc::Status check_restore_precondition(grpc::ServerContext* context,
                                            const storageservice::CheckRestorePreconditionReq* request,
                                            storageservice::CheckRestorePreconditionRes* response) override;

private:
  int build_tablet_sstable_info_(
      grpc::ServerContext* context,
      const obrpc::ObCopyTabletSSTableInfoArg &tablet_arg,
      ObLS *ls,
      grpc::ServerWriter<storageservice::FetchTabletSSTableInfoRes>* writer);
  int build_sstable_macro_info_(
      grpc::ServerContext* context,
      const obrpc::ObCopySSTableMacroRangeInfoHeader &header,
      const obrpc::ObCopySSTableMacroRangeInfoArg &arg,
      grpc::ServerWriter<storageservice::FetchSSTableMacroInfoRes>* writer);
};

class ObStorageGrpcClient
{
public:
  ObStorageGrpcClient();
  ~ObStorageGrpcClient();

  int init(const common::ObAddr& addr, int64_t timeout);
  int get_ls_view_tablet_count(ObGetLSViewTabletCountResult& result);
  int check_restore_precondition(obrpc::ObCheckRestorePreconditionResult& result);
  int fetch_tablet_info(const obrpc::ObCopyTabletInfoArg& arg,
                        std::function<int(const obrpc::ObCopyTabletInfo&)> callback);
  int create_tablet_info_stream(
      const obrpc::ObCopyTabletInfoArg &arg,
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<storageservice::FetchTabletInfoRes>> &reader);
  int create_ls_view_stream(
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<storageservice::FetchLSViewRes>> &reader);
  int create_tablet_sstable_info_stream(
      const obrpc::ObCopyTabletsSSTableInfoArg &arg,
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<storageservice::FetchTabletSSTableInfoRes>> &reader);
  int create_sstable_macro_info_stream(
      const obrpc::ObCopySSTableMacroRangeInfoArg &arg,
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<storageservice::FetchSSTableMacroInfoRes>> &reader);
  int create_macro_block_stream(
      const obrpc::ObCopyMacroBlockRangeArg &arg,
      grpc::ClientContext &context,
      std::unique_ptr<grpc::ClientReader<storageservice::FetchMacroBlockRes>> &reader);
  int translate_error(const grpc::Status &status);
  static int init_ls_view_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      common::ObIAllocator &allocator,
      ObLSMetaPackage &ls_meta,
      restore::ObRestoreHelperLSViewCtx &ls_view_ctx);
  static int init_tablet_sstable_info_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      const obrpc::ObCopyTabletsSSTableInfoArg &arg,
      common::ObIAllocator &allocator,
      restore::ObRestoreHelperSSTableInfoCtx &sstable_info_ctx);
  static int init_sstable_macro_info_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      const obrpc::ObCopySSTableMacroRangeInfoArg &arg,
      common::ObIAllocator &allocator,
      restore::ObRestoreHelperSSTableMacroRangeCtx &macro_range_ctx);
  static int init_macro_block_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      const obrpc::ObCopyMacroBlockRangeArg &arg,
      common::ObIAllocator &allocator,
      restore::ObRestoreHelperMacroBlockCtx &macro_block_ctx);
  static int init_tablet_info_stream(
      const common::ObAddr &src_addr,
      int64_t timeout,
      const obrpc::ObCopyTabletInfoArg &arg,
      common::ObIAllocator &allocator,
      restore::ObRestoreHelperTabletInfoCtx &tablet_info_ctx);

private:
  bool is_inited_;
  obgrpc::ObGrpcClient<storageservice::StorageService> grpc_client_;
};

} // storage
} // oceanbase

#endif
