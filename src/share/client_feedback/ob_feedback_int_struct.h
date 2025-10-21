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

#ifndef OCEANBASE_SHARE_OB_INT_STRUCT_H_
#define OCEANBASE_SHARE_OB_INT_STRUCT_H_

#include "share/client_feedback/ob_client_feedback_basic.h"

namespace oceanbase
{
namespace share
{

class ObFeedbackIntStruct : public ObAbstractFeedbackObject<ObFeedbackIntStruct>
{
public:
  ObFeedbackIntStruct(ObFeedbackElementType type)
    : ObAbstractFeedbackObject<ObFeedbackIntStruct>(type), int_value_(0) {}
  virtual ~ObFeedbackIntStruct() {}

  void set_value(const int64_t value) { int_value_ = value; }
  int64_t get_value() const { return int_value_; }

  bool operator==(const ObFeedbackIntStruct &other) const
  {
    return ((type_ == other.type_) && (int_value_ == other.int_value_));
  }

  bool operator!=(const ObFeedbackIntStruct &other) const
  {
    return !(*this == other);
  }

  void reset() { int_value_ = 0; }

  FB_OBJ_DEFINE_METHOD;

  TO_STRING_KV("type", get_feedback_element_type_str(type_), K_(int_value));

protected:
  int64_t int_value_;
};

inline bool ObFeedbackIntStruct::is_valid_obj() const
{
  return true;
}

#define INT_FB_STRUCT(name, type) \
class name : public ObFeedbackIntStruct \
{ \
public: \
  name() : ObFeedbackIntStruct(type) {} \
  virtual ~name() {} \
};

enum ObFollowerFirstFeedbackType
{
  FFF_HIT_MIN = 0,
  FFF_HIT_LEADER = 1,   // all related partitions are leaders
  // add others if needed
  FF_HIT_MAX,
};
INT_FB_STRUCT(ObFollowerFirstFeedback, FOLLOWER_FIRST_FB_ELE);

} // end namespace share
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_OB_INT_STRUCT_H_
