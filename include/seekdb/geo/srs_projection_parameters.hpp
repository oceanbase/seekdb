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
#include <array>
#include <cstddef>
#include <cstdint>

namespace seekdb::geo::srs {
struct ProjectionParameters {
  int method;
  std::array<int, 19> codes;
  size_t count;
};

// Extracted from the original factory-to-class mapping and registration lists.
// Keys are factory EPSG codes, not projection names or an SRID allowlist.
inline constexpr ProjectionParameters projection_parameters[] = {
  {1024, {8801, 8802, 8806, 8807}, 4}, // POPULAR_VISUAL_PSEUDO_MERCATOR
  {1027, {8801, 8802, 8806, 8807}, 4}, // LAMBERT_AZIMUTHAL_EQUAL_AREA_SPHERICAL
  {1028, {8823, 8802, 8806, 8807}, 4}, // EQUIDISTANT_CYLINDRICAL
  {1029, {8823, 8802, 8806, 8807}, 4}, // EQUIDISTANT_CYLINDRICAL_SPHERICAL
  {1041, {8811, 8833, 1036, 8818, 8819, 8806, 8807}, 7}, // KROVAK_NORTH_ORIENTATED
  {1042, {8811, 8833, 1036, 8818, 8819, 8806, 8807, 8617, 8618, 1026, 1027, 1028, 1029, 1030, 1031, 1032, 1033, 1034, 1035}, 19}, // KROVAK_MODIFIED
  {1043, {8811, 8833, 1036, 8818, 8819, 8806, 8807, 8617, 8618, 1026, 1027, 1028, 1029, 1030, 1031, 1032, 1033, 1034, 1035}, 19}, // KROVAK_MODIFIED_NORTH_ORIENTATED
  {1051, {8821, 8822, 8823, 8824, 8826, 8827, 1038}, 7}, // LAMBERT_CONIC_CONFORMAL_2SP_MICHIGAN
  {1052, {8801, 8802, 8806, 8807, 1039}, 5}, // COLOMBIA_URBAN
  {9801, {8801, 8802, 8805, 8806, 8807}, 5}, // LAMBERT_CONIC_CONFORMAL_1SP
  {9802, {8821, 8822, 8823, 8824, 8826, 8827}, 6}, // LAMBERT_CONIC_CONFORMAL_2SP
  {9803, {8821, 8822, 8823, 8824, 8826, 8827}, 6}, // LAMBERT_CONIC_CONFORMAL_2SP_BELGIUM
  {9804, {8801, 8802, 8805, 8806, 8807}, 5}, // MERCATOR_VARIANT_A
  {9805, {8823, 8802, 8806, 8807}, 4}, // MERCATOR_VARIANT_B
  {9806, {8801, 8802, 8806, 8807}, 4}, // CASSINI_SOLDNER
  {9807, {8801, 8802, 8805, 8806, 8807}, 5}, // TRANSVERSE_MERCATOR
  {9808, {8801, 8802, 8805, 8806, 8807}, 5}, // TRANSVERSE_MERCATOR_SOUTH_ORIENTATED
  {9809, {8801, 8802, 8805, 8806, 8807}, 5}, // OBLIQUE_STEREOGRAPHIC
  {9810, {8801, 8802, 8805, 8806, 8807}, 5}, // POLAR_STEREOGRAPHIC_VARIANT_A
  {9811, {8801, 8802, 8806, 8807}, 4}, // NEW_ZEALAND_MAP_GRID
  {9812, {8811, 8812, 8813, 8814, 8815, 8806, 8807}, 7}, // HOTINE_OBLIQUE_MERCATOR_VARIANT_A
  {9813, {8811, 8812, 8813, 8815, 8806, 8807}, 6}, // LABORDE_OBLIQUE_MERCATOR
  {9815, {8811, 8812, 8813, 8814, 8815, 8816, 8817}, 7}, // HOTINE_OBLIQUE_MERCATOR_VARIANT_B
  {9816, {8821, 8822, 8826, 8827}, 4}, // TUNISIA_MINING_GRID
  {9817, {8801, 8802, 8805, 8806, 8807}, 5}, // LAMBERT_CONIC_NEAR_CONFORMAL
  {9818, {8801, 8802, 8806, 8807}, 4}, // AMERICAN_POLYCONIC
  {9819, {8811, 8833, 1036, 8818, 8819, 8806, 8807}, 7}, // KROVAK
  {9820, {8801, 8802, 8806, 8807}, 4}, // LAMBERT_AZIMUTHAL_EQUAL_AREA
  {9822, {8821, 8822, 8823, 8824, 8826, 8827}, 6}, // ALBERS_EQUAL_AREA
  {9824, {8801, 8830, 8831, 8805, 8806, 8807}, 6}, // TRANSVERSE_MERCATOR_ZONED_GRID_SYSTEM
  {9826, {8801, 8802, 8805, 8806, 8807}, 5}, // LAMBERT_CONIC_CONFORMAL_WEST_ORIENTATED
  {9828, {8801, 8802, 8806, 8807}, 4}, // BONNE_SOUTH_ORIENTATED
  {9829, {8832, 8833, 8806, 8807}, 4}, // POLAR_STEREOGRAPHIC_VARIANT_B
  {9830, {8832, 8833, 8826, 8827}, 4}, // POLAR_STEREOGRAPHIC_VARIANT_C
  {9831, {8801, 8802, 8806, 8807}, 4}, // GUAM_PROJECTION
  {9832, {8801, 8802, 8806, 8807}, 4}, // MODIFIED_AZIMUTHAL_EQUIDISTANT
  {9833, {8801, 8802, 8806, 8807}, 4}, // HYPERBOLIC_CASSINI_SOLDNER
  {9834, {8823, 8802, 8806, 8807}, 4}, // LAMBERT_CYLINDRICAL_EQUAL_AREA_SPHERICAL
  {9835, {8823, 8802, 8806, 8807}, 4}, // LAMBERT_CYLINDRICAL_EQUAL_AREA
};

inline const ProjectionParameters *find_projection(int method)
{
  for (const auto &entry : projection_parameters) if (entry.method == method) return &entry;
  return nullptr; // The original unknown-projection fallback has no required parameters.
}
} // namespace seekdb::geo::srs

