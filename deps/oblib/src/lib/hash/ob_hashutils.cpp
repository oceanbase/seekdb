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

#include "lib/hash/ob_hashutils.h"
namespace oceanbase {
namespace common {
namespace hash {
const int64_t PRIME_LIST[PRIME_NUM] =
  {
    53l, 97l, 193l, 389l, 769l,
    1543l, 3079l, 6151l, 12289l, 24593l,
    49157l, 98317l, 196613l, 393241l, 786433l,
    1572869l, 3145739l, 6291469l, 12582917l, 25165843l,
    50331653l, 100663319l, 201326611l, 402653189l, 805306457l,
    1610612741l, 3221225473l, 4294967291l
  };
}
}
}

