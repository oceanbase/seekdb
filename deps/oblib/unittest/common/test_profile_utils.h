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

#ifndef OCEANBASE_COMMON_TEST_PROFILE_UTILS_H
#define OCEANBASE_COMMON_TEST_PROFILE_UTILS_H
#include "lib/utility/ob_macro_utils.h"
namespace oceanbase
{
namespace common
{
const char* const FMT_STR = "%s%ld";
const char* const MODEL_STR = "Copyright 2014 Alibaba Inc. All Rights Reserved. ";

class TestProfileUtils
{
public:
  TestProfileUtils() {}
  ~TestProfileUtils() {}
  static int build_string(char *str_buf,
                          int64_t str_buf_length,
                          int64_t str_length)
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(str_buf)
        || OB_UNLIKELY(str_buf_length < str_length)) {
      ret = OB_INVALID_ARGUMENT;
      OB_LOG(WARN, "invalid argument", K(ret), K(str_buf), K(str_buf_length), K(str_length));
    } else {
      memset(str_buf, '\0', str_buf_length);
      int64_t model_size = static_cast<int64_t>(strlen(MODEL_STR));
      if (str_length < model_size) {
        strncat(str_buf, MODEL_STR, model_size);
      } else {
        int64_t pos = 0;
        for (int64_t i = 0; i < str_length / model_size; i++) {
          strcat(str_buf + pos, MODEL_STR);
          pos = pos + model_size;
        }
        int64_t left_size = str_buf_length - strlen(str_buf) - 1;
        strncat(str_buf + pos, MODEL_STR, left_size);
      }
    }
    return ret;

  }
private:
  DISALLOW_COPY_AND_ASSIGN(TestProfileUtils);
};
}
}
#endif

