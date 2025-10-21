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

#include "lib/profile/ob_trace_id.h"
using namespace oceanbase;
using namespace oceanbase::common;

namespace oceanbase
{
namespace common
{
#ifdef COMPILE_DLL_MODE
TLOCAL(ObCurTraceId::TraceId, ObCurTraceId::trace_id_);
#endif

uint64_t ObCurTraceId::SeqGenerator::seq_generator_ = 0;

OB_SERIALIZE_MEMBER(ObCurTraceId::TraceId, uval_[0], uval_[1], uval_[2], uval_[3]);

} // end namespace common
} // end namespace oceanbase

extern "C" {
} /* extern "C" */
