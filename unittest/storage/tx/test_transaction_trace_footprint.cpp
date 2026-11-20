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

#include "lib/trace/ob_trace_event.h"
#include "storage/tx/ob_trans_ctx.h"
#include "storage/tx/ob_trans_define_v4.h"

namespace oceanbase
{
namespace transaction
{
// Transaction state must not retain an embedded event buffer, even when disabled.
static_assert(sizeof(ObTransCtx) < sizeof(common::ObTraceEventRecorder),
              "transaction contexts must not embed an event recorder");
static_assert(sizeof(ObTxDesc) < sizeof(common::ObTraceEventRecorder),
              "transaction descriptors must not embed an event recorder");
} // namespace transaction
} // namespace oceanbase
