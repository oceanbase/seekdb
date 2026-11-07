// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstddef>
#include <cstdint>
#include "rust/cxx.h"

namespace oceanbase {
namespace expression {


// The synchronous execution scope keeps this callback alive. A nested demand
// uses a distinct workspace; callbacks must not retain any supplied slices.
class ILogicalHost
{
public:
  virtual ~ILogicalHost() = default;
  virtual int32_t demand(size_t node, rust::Slice<const uint64_t> demand,
      rust::Slice<uint64_t> values, rust::Slice<uint64_t> flags) = 0;
  virtual int32_t check_cancel() = 0;
};

inline int32_t demand_logical_input(ILogicalHost *host, size_t node,
    rust::Slice<const uint64_t> demand, rust::Slice<uint64_t> values,
    rust::Slice<uint64_t> flags)
{
  return host->demand(node, demand, values, flags);
}

inline int32_t check_logical_cancel(ILogicalHost *host)
{
  return host->check_cancel();
}

// The synchronous caller owns this host until it destroys Prepared and Runner.
// Rust exclusively owns the region between allocate and release. Neither side
// retains a borrow across execute, input binding, or runner destruction.
class IExecutionHost
{
public:
  virtual ~IExecutionHost() = default;
  virtual uint64_t *allocate(size_t words) = 0;
  virtual void release(uint64_t *data, size_t words) = 0;
  virtual int32_t check_cancel() = 0;
};

inline uint64_t *allocate_workspace(IExecutionHost *host, size_t words)
{
  return host->allocate(words);
}

inline void release_workspace(IExecutionHost *host, uint64_t *data, size_t words)
{
  host->release(data, words);
}

inline int32_t check_cancel(IExecutionHost *host)
{
  return host->check_cancel();
}

} // namespace expression
} // namespace oceanbase
