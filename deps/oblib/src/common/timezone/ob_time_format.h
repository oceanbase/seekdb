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

#ifndef OCEANBASE_LIB_FILE_TIME_FORMAT
#define OCEANBASE_LIB_FILE_TIME_FORMAT

#include <stdint.h>

namespace oceanbase
{
namespace common
{

//enum enum_ob_timestamp_type
//{
//  OB_TIMESTAMP_NONE= -2, OB_TIMESTAMP_ERROR= -1,
//  OB_TIMESTAMP_DATE= 0, OB_TIMESTAMP_DATETIME= 1, OB_TIMESTAMP_TIME= 2
//};

//typedef struct
//{
//  /*
//    this format is different since in the struct tm,
//    the range of month is [0, 11], here, the month range
//    is [1, 12], that's different
//   */
//  uint32_t year, month, day, hour, minute, second, usecond;  // usecond is microsecond.
//  bool     neg;
//} OB_TIME;

}
}

#endif
