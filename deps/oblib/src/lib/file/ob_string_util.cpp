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

#include "lib/file/ob_string_util.h"

namespace obsys {
    
    bool ObStringUtil::is_int(const char *p) {
        if (NULL == p || (*p) == '\0') {
            return false;
        }
        if ((*p) == '-') p++;
        while((*p)) {
            if ((*p) < '0' || (*p) > '9') return false;
            p++;
        }
        return true;
    }




}
