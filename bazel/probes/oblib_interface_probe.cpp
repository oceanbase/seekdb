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

#if defined(SEEKDB_PROBE_OBLIB_FOUNDATION)
#include "lib/ob_errno.h"
#elif defined(SEEKDB_PROBE_OBLIB_COMPRESSION)
#include "lib/compress/ob_compressor_pool.h"
#elif defined(SEEKDB_PROBE_OBLIB_VECTOR)
#include "lib/vector/ob_vector_util.h"
#elif defined(SEEKDB_PROBE_OBLIB_RESTORE_ADVANCED)
#include "lib/restore/ob_io_device.h"
#elif defined(SEEKDB_PROBE_OBLIB_COMMON)
#include "common/ob_range.h"
#elif defined(SEEKDB_PROBE_OBLIB_RPC)
#include "rpc/frame/ob_req_processor.h"
#else
#error "select one OBLib interface probe"
#endif

void seekdb_oblib_interface_probe()
{
}
