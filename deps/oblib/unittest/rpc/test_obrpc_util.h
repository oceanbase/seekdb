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

#ifndef OCEANBASE_TEST_OBRPC_UTIL_H_
#define OCEANBASE_TEST_OBRPC_UTIL_H_
struct GconfDummpy {
  struct Dummpy {
    static oceanbase::ObString get_value_string() {
      return oceanbase::ObString();
    }
  };
  Dummpy _ob_ssl_invited_nodes;
  static oceanbase::ObAddr self_addr() {
    return oceanbase::ObAddr();
  }
};
GconfDummpy GCONF;
GconfDummpy GCTX;

#endif
