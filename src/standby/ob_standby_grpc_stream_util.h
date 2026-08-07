/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may not use this file except in compliance with the License.
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

#ifndef OCEANBASE_STANDBY_GRPC_STREAM_UTIL_H_
#define OCEANBASE_STANDBY_GRPC_STREAM_UTIL_H_

#include "grpc/standbyservice.grpc.pb.h"
#include "standby/restore/ob_standby_restore_rpc.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
}
namespace standby
{

class ObStandbyGrpcStreamUtil final
{
public:
  static int build_tablet_sstable_info(
      grpc::ServerContext *context,
      const obcall::ObCopyTabletSSTableInfoArg &tablet_arg,
      storage::ObLS *ls,
      grpc::ServerWriter<standbyservice::FetchTabletSSTableInfoRes> *writer);
  static int build_sstable_macro_info(
      grpc::ServerContext *context,
      const obcall::ObCopySSTableMacroRangeInfoHeader &header,
      const obcall::ObCopySSTableMacroRangeInfoArg &arg,
      grpc::ServerWriter<standbyservice::FetchSSTableMacroInfoRes> *writer);
};

} // namespace standby
} // namespace oceanbase

#endif // OCEANBASE_STANDBY_GRPC_STREAM_UTIL_H_
