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
