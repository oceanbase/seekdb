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
#ifndef SEEKDB_WINDOWS_PHASE0_PATH_FIXTURE_H_
#define SEEKDB_WINDOWS_PHASE0_PATH_FIXTURE_H_

#include <algorithm>
#include <stdexcept>
#include <string>

namespace seekdb_phase0 {
// Lengths are UTF-16 code units, excluding NUL. Never split a surrogate pair.
inline std::u16string directory_at_length(const std::u16string &prefix,
                                        size_t units, bool unicode)
{
  if (prefix.empty() || prefix.back() == u'\\' || units < prefix.size() + 2) {
    throw std::invalid_argument("directory length cannot accommodate a component");
  }
  std::u16string result(prefix);
  const std::u16string token = unicode ? u"\u4e2d\U0001f680" : u"a";
  while (result.size() < units) {
    const size_t remaining = units - result.size();
    size_t component = std::min<size_t>(100, remaining - 1);
    if (remaining - component - 1 == 1) {
      --component; // Reserve a separator AND a character for the next component.
    }
    result += u'\\';
    while (component >= token.size()) {
      result += token;
      component -= token.size();
    }
    result.append(component, u'a');
  }
  return result;
}
} // namespace seekdb_phase0
#endif
