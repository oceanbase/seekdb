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
#include <cstdint>
#include <memory>
#include <string>

namespace seekdb::gis {
struct ProjectionInputError {};

// Plugin-private C++ interface; neither this class nor its state crosses SPI.
class Projection {
public:
  // SQL catalog calls require an explicit/known datum. Raw proj4 callers may
  // deliberately use unknown-datum local projections, retaining Boost policy.
  Projection(const std::string &source, const std::string &target, bool require_datum = false);
  ~Projection();
  bool forward(double &x, double &y, double &z, uint32_t dimensions) const;
private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace seekdb::gis
