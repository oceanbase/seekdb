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

#pragma once

#include <optional>
#include "rust/sql_parser.h"

namespace oceanbase::sql
{
using ParameterBinding = peg::ParameterBinding;
using NumericLowering = peg::ParameterLowering;

class ParameterizationResult
{
public:
  void install(rust::Box<peg::ParameterEffects> effects)
  {
    effects_ = std::move(effects);
  }

  const ParameterBinding *find(const peg::Node *node) const
  {
    if (!effects_ || node == nullptr) return nullptr;
    const auto binding = (*effects_)->parameter_binding(*node);
    return binding.empty() ? nullptr : binding.data();
  }

  const NumericLowering *numeric_lowering(const peg::Node *node) const
  {
    if (!effects_ || node == nullptr) return nullptr;
    const auto lowering = (*effects_)->numeric_lowering(*node);
    return lowering.empty() ? nullptr : lowering.data();
  }

  const peg::ParameterEffects *effects() const
  {
    return effects_ ? &**effects_ : nullptr;
  }

  void reset() { effects_.reset(); }

private:
  std::optional<rust::Box<peg::ParameterEffects>> effects_;
};
}
