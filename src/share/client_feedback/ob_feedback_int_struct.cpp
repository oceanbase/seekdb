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

#define USING_LOG_PREFIX SHARE

#include "share/client_feedback/ob_feedback_int_struct.h"

namespace oceanbase
{
namespace share
{
using namespace common;

int ObFeedbackIntStruct::serialize_struct_content(char *buf, const int64_t len, int64_t &pos) const
{
  OB_FB_SER_START;
  OB_FB_ENCODE_INT(int_value_);
  OB_FB_SER_END;
}

int ObFeedbackIntStruct::deserialize_struct_content(char *buf, const int64_t len, int64_t &pos)
{
  OB_FB_DESER_START;
  OB_FB_DECODE_INT(int_value_, int64_t);
  OB_FB_DESER_END;
}

} // end namespace share
} // end namespace oceanbase
