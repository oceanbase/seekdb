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

#include "standby/ob_standby_grpc_stream_util.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"
#include "standby/ob_standby_grpc.h"
#include "standby/restore/ob_standby_restore_reader.h"

namespace oceanbase
{
namespace standby
{

using storage::ObCopySSTableInfoObProducer;
using storage::ObCopyMacroRangeInfo;
using storage::ObCopySSTableMacroRangeObProducer;
using storage::ObICopySSTableMacroRangeObProducer;

int ObStandbyGrpcStreamUtil::build_tablet_sstable_info(
    grpc::ServerContext *context,
    const obcall::ObCopyTabletSSTableInfoArg &tablet_arg,
    storage::ObLS *ls,
    grpc::ServerWriter<standbyservice::FetchTabletSSTableInfoRes> *writer)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ls) || OB_ISNULL(writer)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls), KP(writer));
  } else {
    ObCopySSTableInfoObProducer producer;
    obcall::ObCopyTabletSSTableHeader header;
    standbyservice::FetchTabletSSTableInfoRes header_response;
    if (OB_FAIL(producer.init(tablet_arg, ls))) {
      LOG_WARN("failed to init copy sstable info ob producer", K(ret), K(tablet_arg));
    } else if (OB_FAIL(producer.get_copy_tablet_sstable_header(header))) {
      LOG_WARN("failed to get copy tablet sstable header", K(ret), K(tablet_arg));
    } else if (OB_FAIL(obgrpc::serialize_ob_to_proto(header, &header_response))) {
      LOG_WARN("failed to serialize ObCopyTabletSSTableHeader", K(ret));
    } else if (!writer->Write(header_response)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to write tablet sstable header to stream", K(ret));
    } else {
      int64_t sstable_count = header.sstable_count_;
      for (int64_t i = 0; OB_SUCC(ret) && i < sstable_count; ++i) {
        if (context->IsCancelled()) {
          ret = OB_CANCELED;
          LOG_WARN("client cancelled the request", K(ret));
          break;
        }

        obcall::ObCopyTabletSSTableInfo sstable_info;
        if (OB_FAIL(producer.get_next_sstable_info(sstable_info))) {
          if (OB_ITER_END == ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected end of sstable info stream", K(ret), K(i), K(sstable_count), K(tablet_arg));
          } else {
            LOG_WARN("failed to get next sstable info", K(ret), K(tablet_arg));
          }
        } else {
          standbyservice::FetchTabletSSTableInfoRes sstable_response;
          if (OB_FAIL(obgrpc::serialize_ob_to_proto(sstable_info, &sstable_response))) {
            LOG_WARN("failed to serialize ObCopyTabletSSTableInfo", K(ret));
          } else if (!writer->Write(sstable_response)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to write sstable info to stream", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObStandbyGrpcStreamUtil::build_sstable_macro_info(
    grpc::ServerContext *context,
    const obcall::ObCopySSTableMacroRangeInfoHeader &header,
    const obcall::ObCopySSTableMacroRangeInfoArg &arg,
    grpc::ServerWriter<standbyservice::FetchSSTableMacroInfoRes> *writer)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(writer)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(writer));
  } else if (!header.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid header", K(ret), K(header));
  } else {
    standbyservice::FetchSSTableMacroInfoRes header_response;
    if (OB_FAIL(obgrpc::serialize_ob_to_proto(header, &header_response))) {
      LOG_WARN("failed to serialize ObCopySSTableMacroRangeInfoHeader", K(ret));
    } else if (!writer->Write(header_response)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to write sstable macro range header to stream", K(ret));
    } else if (header.macro_range_count_ > 0) {
      ObCopySSTableMacroRangeObProducer producer;
      if (OB_FAIL(producer.init(arg.ls_id_, arg.tablet_id_,
              header, arg.macro_range_max_marco_count_))) {
        LOG_WARN("failed to init sstable macro range producer", K(ret), K(arg), K(header));
      }
      if (OB_SUCC(ret)) {
        SMART_VAR(ObCopyMacroRangeInfo, macro_range_info) {
          for (int64_t i = 0; OB_SUCC(ret) && i < header.macro_range_count_; ++i) {
            if (context->IsCancelled()) {
              ret = OB_CANCELED;
              LOG_WARN("client cancelled the request", K(ret));
              break;
            }

            macro_range_info.reuse();
            if (OB_FAIL(producer.get_next_macro_range_info(macro_range_info))) {
              if (OB_ITER_END == ret) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("unexpected end of macro range info stream",
                    K(ret), K(i), K(header.macro_range_count_), K(header.copy_table_key_));
              } else {
                LOG_WARN("failed to get next macro range info", K(ret), K(header.copy_table_key_));
              }
            } else {
              standbyservice::FetchSSTableMacroInfoRes macro_response;
              if (OB_FAIL(obgrpc::serialize_ob_to_proto(macro_range_info, &macro_response))) {
                LOG_WARN("failed to serialize ObCopyMacroRangeInfo", K(ret));
              } else if (!writer->Write(macro_response)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("failed to write macro range info to stream", K(ret));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
