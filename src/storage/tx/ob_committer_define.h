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

#ifndef OCEANBASE_STORAGE_TX_OB_TX_COMMITTER
#define OCEANBASE_STORAGE_TX_OB_TX_COMMITTER

#include "share/ob_errno.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace transaction
{
// =================== NB: REMOVE DURING JOINT DEBUGGING ===================
class ObICommitCallback
{
};
// =================== NB: REMOVE DURING JOINT DEBUGGING ===================

enum class ObTxState : uint8_t
{
  UNKNOWN = 0,
  INIT = 10,
  REDO_COMPLETE = 20,
  PREPARE = 30,
  PRE_COMMIT = 40,
  COMMIT = 50,
  ABORT = 60,
  CLEAR = 70,
  MAX = 100
};

#define TRX_ENUM_CASE_TO_STR(class_name, src) \
  case class_name::src:                       \
    str = #src;                               \
    break;

static const char *to_str_tx_state(ObTxState state)
{
  const char *str = "INVALID";
  switch (state) {
    TRX_ENUM_CASE_TO_STR(ObTxState, UNKNOWN)
    TRX_ENUM_CASE_TO_STR(ObTxState, INIT)
    TRX_ENUM_CASE_TO_STR(ObTxState, REDO_COMPLETE)
    TRX_ENUM_CASE_TO_STR(ObTxState, PREPARE)
    TRX_ENUM_CASE_TO_STR(ObTxState, PRE_COMMIT)
    TRX_ENUM_CASE_TO_STR(ObTxState, COMMIT)
    TRX_ENUM_CASE_TO_STR(ObTxState, ABORT)
    TRX_ENUM_CASE_TO_STR(ObTxState, CLEAR)
    TRX_ENUM_CASE_TO_STR(ObTxState, MAX)
  };
  return str;
}

/* // ObITxCommitter provides method to commit the transaction with user provided callbacks. */
/* // The interface need guarantee the atomicity of the transaction. */
/* class ObITxCommitter */
/* { */
/*   public: */
/*   // transaction commit interface, the callback will be invoked under different situation. */
/*   virtual int commit(ObICommitCallback &cb) = 0; */
/* }; */
} // transaction
} // oceanbase

#endif // OCEANBASE_STORAGE_TX_OB_TX_COMMITTER
