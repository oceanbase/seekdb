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

#include <gtest/gtest.h>
#include <cstdio>
#include "storage/tx/ob_tx_ctx.h"
#include "storage/tx/ob_trans_define_v4.h"

namespace oceanbase
{
namespace transaction
{
using namespace common;

// The dedicated per-transaction trace recorder that used to be embedded in
// every transaction descriptor and every transaction context reserved 189
// event slots (189 * 16 bytes) plus a 1200 byte information buffer and the
// lock and bookkeeping overhead, that is roughly 4.4 KiB per structure.
//
// A structure that no longer embeds the recorder has to stay below that
// budget. The assertion below therefore fails as soon as the recorder, or
// any comparable per-transaction recorder, is embedded again.
static const int64_t REMOVED_TRACE_RECORDER_BYTES = 4608;

// The descriptor is 1920 bytes without the recorder and 6464 bytes with it.
static_assert(sizeof(ObTxDesc) < REMOVED_TRACE_RECORDER_BYTES,
              "ObTxDesc embeds a per-transaction recorder again");

// The context is 5824 bytes without the recorder and 10368 bytes with it.
// The budget below still leaves room for unrelated growth.
static const int64_t MAX_TX_CTX_WITHOUT_TRACE_RECORDER = 8192;
static_assert(sizeof(ObTxCtx) < MAX_TX_CTX_WITHOUT_TRACE_RECORDER,
              "ObTxCtx embeds a per-transaction recorder again");

TEST(TestTxDiagnoseInfo, transaction_structures_stay_small)
{
  // Report the sizes so that a regression of the per-transaction footprint
  // stays visible in the test log.
  fprintf(stdout, "sizeof(ObTxDesc)=%zu sizeof(ObTxCtx)=%zu\n",
          sizeof(ObTxDesc), sizeof(ObTxCtx));
  EXPECT_LT(sizeof(ObTxDesc), REMOVED_TRACE_RECORDER_BYTES);
  EXPECT_LT(sizeof(ObTxCtx), MAX_TX_CTX_WITHOUT_TRACE_RECORDER);
}

TEST(TestTxDiagnoseInfo, tx_desc_dump_works_without_trace_recorder)
{
  ObTxDesc tx;

  // These dumps used to print the recorded event trace of the transaction.
  // They must keep working and must dump the transaction state instead.
  tx.print_trace();
  tx.dump_and_print_trace();

  SUCCEED();
}

TEST(TestTxDiagnoseInfo, tx_ctx_dump_works_without_trace_recorder)
{
  ObTxCtx ctx;

  // The context dump replaced the force printed trace log of the removed
  // recorder. It has to stay callable on a freshly built context.
  ctx.dump_trans_diagnose_info();

  SUCCEED();
}

} // namespace transaction
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
