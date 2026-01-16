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

#include "ob_multi_data_source_printer.h"
#include "storage/tx/ob_multi_data_source.h"
#include "storage/tx/ob_committer_define.h"

namespace oceanbase
{
namespace transaction
{
const char *ObMultiDataSourcePrinter::to_str_mds_type(const ObTxDataSourceType &mds_type)
{
  const char *str = "INVALID";
  switch(mds_type)
  {
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, UNKNOWN);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, MEM_TABLE);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TABLE_LOCK);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, LS_TABLE);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, DDL_BARRIER);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, DDL_TRANS);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, STANDBY_UPGRADE);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, STANDBY_UPGRADE_DATA_VERSION);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, BEFORE_VERSION_4_1);

    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TEST1);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TEST2);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TEST3);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, CREATE_TABLET_NEW_MDS);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, DELETE_TABLET_NEW_MDS);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, UNBIND_TABLET_NEW_MDS);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, START_TRANSFER_OUT);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, START_TRANSFER_IN);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, FINISH_TRANSFER_OUT);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, FINISH_TRANSFER_IN);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TRANSFER_TASK);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, START_TRANSFER_OUT_PREPARE);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, START_TRANSFER_OUT_V2);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TRANSFER_MOVE_TX_CTX);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TRANSFER_DEST_PREPARE);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, UNBIND_LOB_TABLET);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, CHANGE_TABLET_TO_TABLE_MDS);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TRANSFER_IN_ABORTED);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TABLET_SPLIT);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TABLET_BINDING);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, MV_PUBLISH_SCN);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, MV_NOTICE_SAFE);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, MV_UPDATE_SCN);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, SYNC_TRUNCATE_INFO);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, MV_MERGE_SCN);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, MVIEW_MDS_OP);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, DDL_COMPLETE_MDS);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, TABLET_FORK);
    TRX_ENUM_CASE_TO_STR(ObTxDataSourceType, MAX_TYPE);
  }
  return str;
}

const char *ObMultiDataSourcePrinter::to_str_notify_type(const NotifyType &notify_type)
{
  const char *str = "INVALID";
  switch(notify_type)
  {
    TRX_ENUM_CASE_TO_STR(NotifyType, UNKNOWN);
    TRX_ENUM_CASE_TO_STR(NotifyType, REGISTER_SUCC);
    TRX_ENUM_CASE_TO_STR(NotifyType, ON_REDO);
    TRX_ENUM_CASE_TO_STR(NotifyType, TX_END);
    TRX_ENUM_CASE_TO_STR(NotifyType, ON_PREPARE);
    TRX_ENUM_CASE_TO_STR(NotifyType, ON_COMMIT);
    TRX_ENUM_CASE_TO_STR(NotifyType, ON_ABORT);
  }
  return str;
}
}
}
