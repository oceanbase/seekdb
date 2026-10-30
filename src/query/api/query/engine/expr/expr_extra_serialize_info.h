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

#pragma once

#include "query/engine/statement_time.h"
#include "lib/profile/ob_trace_id.h"

namespace oceanbase
{
namespace sql
{

struct ObExprExtraSerializeInfo
{
  OB_UNIS_VERSION(1);
public:
  ObExprExtraSerializeInfo() :
    current_time_(nullptr),
    last_trace_id_(nullptr)
    { }
  StatementTime *current_time_;
  common::ObCurTraceId::TraceId *last_trace_id_;
};

} // namespace sql
} // namespace oceanbase
