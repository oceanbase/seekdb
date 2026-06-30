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

/* moved from share/ob_rpc_struct.h: holds transaction::ObTxExecResult by value,
 * share must not depend upward on storage/tx, so the whole type lives in storage/tx (ns obrpc is unchanged, serialization compatible) */
#ifndef OCEANBASE_STORAGE_TX_OB_TX_RESULT_STRUCT_H_
#define OCEANBASE_STORAGE_TX_OB_TX_RESULT_STRUCT_H_

#include "storage/tx/ob_trans_define_v4.h"

namespace oceanbase
{
namespace obcall
{
struct ObCreateTabletBatchInTransRes
{
  OB_UNIS_VERSION(1);

public:
  ObCreateTabletBatchInTransRes()
    : ret_(common::OB_SUCCESS), tx_result_() {}
  ~ObCreateTabletBatchInTransRes() {}

  DECLARE_TO_STRING;
  int ret_;
  transaction::ObTxExecResult tx_result_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateTabletBatchInTransRes);
};

using ObRemoveTabletsInTransRes = ObCreateTabletBatchInTransRes;

struct ObRegisterTxDataResult
{
  OB_UNIS_VERSION(1);
public:
  ObRegisterTxDataResult() : result_(common::OB_SUCCESS), tx_result_() {}
  ~ObRegisterTxDataResult() {}
  void reset();
  TO_STRING_KV(K_(result), K_(tx_result));
public:
  int64_t result_;
  transaction::ObTxExecResult tx_result_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRegisterTxDataResult);
};

}  // namespace obcall
}  // namespace oceanbase
#endif
