/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_SHARE_OB_TELEMETRY_H_
#define OCEANBASE_SHARE_OB_TELEMETRY_H_

#include <stdint.h>

namespace oceanbase
{
namespace share
{

static const int64_t TELEMETRY_UUID_STRING_LENGTH = 36;

// Generate an RFC 9562 UUID v8 in the seekdb telemetry application namespace.
// The machine ID accepts a 32-hex identifier or its canonical UUID form. The
// base directory must be an absolute, canonical path. The instance ID accepts
// the same text forms and represents one database deployment. The resulting
// UUID is stable for the same (machine ID, base directory, instance ID) tuple.
// The derivation is
// HMAC-SHA256(machine-id bytes,
//             app-id[16]
//             || uint64_be(base-dir byte length) || base-dir bytes
//             || uint64_be(16) || instance-id bytes[16]),
// truncated to 16 bytes before applying the UUID v8 and RFC variant bits.
int generate_telemetry_uuid(const char *machine_id,
                            const int64_t machine_id_len,
                            const char *base_dir,
                            const int64_t base_dir_len,
                            const char *instance_id,
                            const int64_t instance_id_len,
                            char *uuid,
                            const int64_t uuid_len);

int report_telemetry(const char *reporter, const char *event_name);

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_TELEMETRY_H_
