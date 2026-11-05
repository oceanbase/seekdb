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
 
#ifndef UNITTEST_STORAGE_MULTI_DATA_SOURCE_COMMON_DEFINE_H
#define UNITTEST_STORAGE_MULTI_DATA_SOURCE_COMMON_DEFINE_H
#include "src/share/scn.h"

namespace oceanbase {
namespace unittest {

inline share::SCN mock_scn(int64_t val) { share::SCN scn; scn.convert_for_gts(val); return scn; }

}
}
#endif
