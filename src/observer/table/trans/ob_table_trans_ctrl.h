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

#ifndef _OB_TABLE_TRANS_CTRL_H
#define _OB_TABLE_TRANS_CTRL_H

#include "observer/table/utils/ob_table_trans_utils.h"


namespace oceanbase
{
namespace table
{
class ObTableCreateCbFunctor;

class ObTableTransCtrl final
{
public:
  static const uint32_t OB_KV_DEFAULT_SESSION_ID = 0;
public:
private:
  static int setup_tx_snapshot(ObTableTransParam &trans_param);
  static int async_commit_trans(ObTableTransParam &trans_param);
  static int init_read_trans(ObTableTransParam &trans_param);
  static void release_read_trans(transaction::ObTxDesc *&trans_desc);
  static int sync_end_trans(ObTableTransParam &trans_param);
};

} // end namespace observer
} // end namespace oceanbase

#endif /* _OB_TABLE_TRANS_CTRL_H */
