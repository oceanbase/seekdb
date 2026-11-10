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
#include "common/datum/datum_value.h"
#include "common/object/ob_value_print_params.h"
namespace oceanbase { namespace common {
// Generate SQL syntax directly from a typed native value. Buffers/position and
// errors follow the existing SQL literal printer contract. No legacy value is
// constructed; raw value scale and LOB state come from value metadata.
int print_datum_sql_literal(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params = ObValuePrintParams());
// Plain session/schema text and SHOW CREATE DEFAULT text have distinct quoting
// and flag contracts; neither is SQL literal generation with quotes stripped.
int print_datum_plain_str_literal(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params = ObValuePrintParams());
int print_datum_varchar_literal(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params = ObValuePrintParams());
// Format the typed debug representation used by row/SSTable printers. This is
// intentionally distinct from SQL literal rendering and consumes only Datum
// bytes plus their metadata.
int print_datum_debug_json(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params = ObValuePrintParams());
int print_datum_debug_smart(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params = ObValuePrintParams());
// Format an already unwrapped native collection payload. Collection metadata
// stays opaque here so native callers do not acquire the array implementation's
// legacy value header dependency.
int print_collection_plain_payload(const ObString &payload,
    const ObSqlCollectionInfo &meta, bool use_memcpy,
    char *buffer, int64_t length, int64_t &pos);

} }
