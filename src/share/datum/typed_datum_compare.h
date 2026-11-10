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

namespace oceanbase { namespace common {
// Constants and parameters share native payload operations. Views borrow the
// producer's bytes; cloning into a plan arena makes their lifetime explicit.
bool can_compare_datum_values(const DatumValue &left, const DatumValue &right);
int compare_datum_values(const DatumValue &left, const DatumValue &right, int &cmp);
// Metadata-only counterpart of the legacy no-cast comparison preflight.
// It answers whether a direct comparator exists; it does not compare values.
bool can_compare_types_without_cast(const SqlTypeMeta &left, const SqlTypeMeta &right);
bool is_datetime_timestamp_type_pair(SqlValueType left, SqlValueType right);
int datum_values_equal(const DatumValue &left, const DatumValue &right, bool &equal);
bool binary_equal_datum_values(const DatumValue &left, const DatumValue &right);
bool strict_equal_datum_values(const DatumValue &left, const DatumValue &right);
int hash_datum_value(const DatumValue &value, uint64_t seed, uint64_t &hash);
// Own constant payload/descriptor bytes; extended pointees keep the producer's
// lifetime, matching constant-copy semantics. PL execution snapshots must use
// the separate recursive PL clone API.
int clone_typed_value(const DatumValue &source, DatumValue &target, ObIAllocator &allocator);
} }
