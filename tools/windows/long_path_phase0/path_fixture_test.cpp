/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "path_fixture.h"
#include <iostream>

static void require(bool value)
{
  if (!value) throw std::runtime_error("invalid path fixture");
}

int main()
{
  try {
    const std::u16string root = u"C:\\phase0\\exclusive-run";
    for (bool unicode : {false, true}) {
      // Include every component-join boundary as well as the product limits.
      for (size_t units = root.size() + 2; units <= 4097; ++units) {
        const auto path = seekdb_phase0::directory_at_length(root, units, unicode);
        require(path.size() == units && path.compare(0, root.size(), root) == 0);
        size_t component = 0;
        for (size_t i = root.size() + 1; i < path.size(); ++i) {
          const char16_t ch = path[i];
          if (ch == u'\\') {
            require(component > 0 && component <= 100);
            component = 0;
          } else {
            ++component;
            if (ch >= 0xd800 && ch <= 0xdbff) {
              require(i + 1 < path.size() && path[i + 1] >= 0xdc00 && path[i + 1] <= 0xdfff);
            } else if (ch >= 0xdc00 && ch <= 0xdfff) {
              require(i > 0 && path[i - 1] >= 0xd800 && path[i - 1] <= 0xdbff);
            }
          }
        }
        require(component > 0 && component <= 100);
      }
    }
    bool rejected = false;
    try { seekdb_phase0::directory_at_length(root, root.size() + 1, false); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected);
    std::cout << "FIXTURE_PASS (length generation only; no Windows/VFS evidence)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
