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

#include "share/client_feedback/ob_client_feedback_basic.h"

namespace oceanbase
{
namespace share
{
using namespace common;
using namespace obmysql;

const char *get_feedback_element_type_str(const ObFeedbackElementType type)
{
  switch (type) {
    case MIN_FB_ELE:
      return "MIN_FB_ELE";
#define OB_FB_TYPE_DEF(name) \
    case name: \
      return #name;
#include "share/client_feedback/ob_feedback_type_define.h"
#undef  OB_FB_TYPE_DEF
    case MAX_FB_ELE:
      return "MAX_FB_ELE";
    default:
      return "UNKNOWN_FB_ELE";
  };
}

bool is_valid_fb_element_type(const int64_t type)
{
  return (type > static_cast<int64_t>(MIN_FB_ELE)) && (type < static_cast<int64_t>(MAX_FB_ELE));
}

} // end namespace share
} // end namespace oceanbase
