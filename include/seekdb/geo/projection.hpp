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
#include <boost/geometry/srs/transformation.hpp>

namespace seekdb::geo::projection {
// Original ObGeoFuncTransform backend. Keep the same prepared Boost version,
// proj4 interpretation and empty-grid forward policy in both build profiles.
// Callers own geometry normalization, output publication and error translation.
using Transformer = boost::geometry::srs::transformation<>;
using Definition = boost::geometry::srs::proj4;
} // namespace seekdb::geo::projection
