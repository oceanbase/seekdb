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

#include "lib/statistic_event/ob_stat_event.h"

namespace oceanbase
{
namespace common
{

#define STAT_DEF_true(def, name, stat_class, stat_id, summary_in_session, can_visible)\
{name, stat_class, stat_id, summary_in_session, can_visible},

#define STAT_DEF_false(def, name, stat_class, stat_id, summary_in_session, can_visible)

const ObStatEvent OB_STAT_EVENTS[] = {
#define STAT_EVENT_ADD_DEF(def, name, stat_class, stat_id, summary_in_session, can_visible, enable) \
  STAT_DEF_##enable(def, name, stat_class, stat_id, summary_in_session, can_visible)
#include "lib/statistic_event/ob_stat_event.h"
#undef STAT_EVENT_ADD_DEF
#define STAT_EVENT_SET_DEF(def, name, stat_class, stat_id, summary_in_session, can_visible, enable) \
  STAT_DEF_##enable(def, name, stat_class, stat_id, summary_in_session, can_visible)
#include "lib/statistic_event/ob_stat_event.h"
#undef STAT_EVENT_SET_DEF
};

#undef STAT_DEF_true
#undef STAT_DEF_false

ObStatEventAddStat::ObStatEventAddStat()
  : stat_value_(0)
{
}

int ObStatEventAddStat::add(const ObStatEventAddStat &other)
{
  stat_value_ += other.stat_value_;
  return OB_SUCCESS;
}

int ObStatEventAddStat::add(int64_t value)
{
  int ret = OB_SUCCESS;
  stat_value_ += value;
  return ret;
}

int ObStatEventAddStat::atomic_add(int64_t value)
{
  IGNORE_RETURN ATOMIC_AAF(&stat_value_, value);
  return OB_SUCCESS;
}


ObStatEventSetStat::ObStatEventSetStat()
  : stat_value_(0)
{
}

int ObStatEventSetStat::add(const ObStatEventSetStat &other)
{
  int ret = OB_SUCCESS;
  if (other.is_valid()) {
    *this = other;
  }
  return ret;
}



}
}
