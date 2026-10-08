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
#include "lib/allocator/ob_allocator.h"
#include "lib/string/ob_string.h"

namespace oceanbase::common {
// Host-side serialization of owned numeric metadata using the original dtoa.
// No geometry/projection engine or plugin pointers are used.
int build_srs_geographic_proj4(ObIAllocator &allocator, double semi_major, double inverse_flattening,
                               bool wgs84, const double *towgs84, ObString &out);
}

