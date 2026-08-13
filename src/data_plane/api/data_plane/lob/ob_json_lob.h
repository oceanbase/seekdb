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

#ifndef OCEANBASE_DATA_PLANE_API_LOB_OB_JSON_LOB_H_
#define OCEANBASE_DATA_PLANE_API_LOB_OB_JSON_LOB_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObJsonBinUpdateCtx;
class ObLobLocatorV2;
class ObString;
}
namespace data_plane
{

// An allocator-backed, opaque owner for the storage state needed while a JSON
// LOB is edited.  Its layout and concrete cursor/partial-data types are private
// to the data plane.  Binding transfers cursor ownership to the JSON update
// context; destroying an unbound handle releases the cursor immediately.
class ObJsonLobHandle;

int open_json_lob(common::ObIAllocator &allocator,
                  common::ObLobLocatorV2 &locator,
                  int64_t query_timeout_ts,
                  ObJsonLobHandle *&handle);

int restore_json_lob_delta(common::ObIAllocator &allocator,
                           const common::ObLobLocatorV2 &delta_locator,
                           int64_t query_timeout_ts,
                           common::ObJsonBinUpdateCtx &update_context,
                           ObJsonLobHandle *&handle);

void destroy_json_lob_handle(ObJsonLobHandle *&handle);

int read_json_lob_root_type(ObJsonLobHandle &handle, uint8_t &root_type);
int try_get_single_chunk_json_lob(ObJsonLobHandle &handle,
                                  bool &is_single_chunk,
                                  common::ObString &data);

// The JSON update context is the neutral cursor consumer.  This operation
// transfers the handle's cursor ownership to that context without exposing the
// storage cursor to the caller.
int bind_json_lob(ObJsonLobHandle &handle,
                  common::ObJsonBinUpdateCtx &update_context);

int validate_json_lob_delta(const common::ObJsonBinUpdateCtx &update_context);
int64_t get_json_lob_delta_serialize_size(
    const common::ObJsonBinUpdateCtx &update_context);
int serialize_json_lob_delta(const common::ObJsonBinUpdateCtx &update_context,
                             char *buf,
                             int64_t buf_len,
                             int64_t &pos);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_LOB_OB_JSON_LOB_H_
