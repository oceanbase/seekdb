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

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <emscripten.h>

extern "C" int _emscripten_thread_supports_atomics_wait(void);
extern "C" int __real_emscripten_futex_wait(volatile void *address, uint32_t expected,
                                         double timeout_ms);

EM_JS(void, seekdb_wasm_wait_checkpoint, (), {});

extern "C" int __wrap_emscripten_futex_wait(volatile void *address, uint32_t expected,
                                         double timeout_ms)
{
  if (!_emscripten_thread_supports_atomics_wait()) {
    return __real_emscripten_futex_wait(address, expected, timeout_ms);
  }
  constexpr double slice_ms = 1000.0;
  const bool finite = std::isfinite(timeout_ms);
  const double started_ms = finite && timeout_ms > slice_ms ? emscripten_get_now() : 0.0;
  double remaining_ms = timeout_ms;
  for (;;) {
    const double wait_ms = remaining_ms > slice_ms ? slice_ms : remaining_ms;
    const int result = __real_emscripten_futex_wait(address, expected, wait_ms);
    if (result != -ETIMEDOUT) {
      return result;
    }
    seekdb_wasm_wait_checkpoint();
    if (!(remaining_ms > slice_ms)) {
      return result;
    }
    if (finite) {
      remaining_ms = timeout_ms - (emscripten_get_now() - started_ms);
      if (remaining_ms <= 0.0) {
        return result;
      }
    }
  }
}
