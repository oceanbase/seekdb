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

#define USING_LOG_PREFIX COMMON

#include <gtest/gtest.h>
#include "share/ob_debug_sync.h"
#include "share/ob_i_debug_sync_broadcaster.h"
#include "lib/string/ob_sql_string.h"
#include "lib/container/ob_array.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{
namespace common
{
class RecordingDebugSyncBroadcaster final
    : public ObIDebugSyncBroadcaster
{
public:
  RecordingDebugSyncBroadcaster()
      : call_count_(0), reset_(false), clear_(false), action_()
  {}

  int broadcast_debug_sync_action(
      const bool reset,
      const bool clear,
      const ObDebugSyncAction &action) override
  {
    ++call_count_;
    reset_ = reset;
    clear_ = clear;
    action_ = action;
    return OB_SUCCESS;
  }

  int64_t call_count_;
  bool reset_;
  bool clear_;
  ObDebugSyncAction action_;
};

TEST(common, ObDebugSyncAction)
{
  ObDebugSyncAction a;
  ASSERT_FALSE(a.is_valid());

  a.sync_point_ = NOW;
  a.execute_ = 1;
  ASSERT_FALSE(a.is_valid());
  a.signal_ = "a";
  ASSERT_TRUE(a.is_valid());
  a.wait_ = "b";
  ASSERT_TRUE(a.is_valid());
  a.execute_ = 0;
  ASSERT_FALSE(a.is_valid());
  a.execute_ = 1;
  ASSERT_TRUE(a.is_valid());
  a.signal_ = "";
  ASSERT_TRUE(a.is_valid());

  a.timeout_ = 1024;
  a.no_clear_ = true;
  a.signal_ = "signal";

  int64_t len = a.get_serialize_size();
  char buf[len];
  LOG_INFO("action serialize size", K(len));
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, a.serialize(buf, len, pos));
  ASSERT_EQ(pos, len);
  pos = 0;

  ObDebugSyncAction b;
  ASSERT_EQ(OB_SUCCESS, b.deserialize(buf, len, pos));

  ASSERT_EQ(a.sync_point_, b.sync_point_);
  ASSERT_EQ(a.timeout_, b.timeout_);
  ASSERT_EQ(a.signal_, b.signal_);
  ASSERT_EQ(a.wait_, b.wait_);
  ASSERT_EQ(a.no_clear_, b.no_clear_);
}

TEST(common, ObDebugSyncBroadcasterSeam)
{
  ObMalloc allocator;
  ObDSSessionActions session_actions;
  RecordingDebugSyncBroadcaster broadcaster;
  ASSERT_EQ(OB_SUCCESS, session_actions.init(1024, allocator));

  ASSERT_EQ(OB_SUCCESS, GDS.add_debug_sync(
      ObString::make_string("NOW CLEAR"),
      false,
      session_actions,
      nullptr));
  ASSERT_EQ(0, broadcaster.call_count_);

  ASSERT_EQ(OB_SUCCESS, GDS.add_debug_sync(
      ObString::make_string("NOW SIGNAL ready"),
      true,
      session_actions,
      &broadcaster));
  ASSERT_EQ(1, broadcaster.call_count_);
  ASSERT_FALSE(broadcaster.reset_);
  ASSERT_FALSE(broadcaster.clear_);
  ASSERT_EQ(NOW, broadcaster.action_.sync_point_);
  ASSERT_EQ(0, broadcaster.action_.signal_.str().compare("ready"));

  ASSERT_EQ(OB_NOT_INIT, GDS.add_debug_sync(
      ObString::make_string("NOW SIGNAL not-composed"),
      true,
      session_actions,
      nullptr));
}

TEST(common, ObDSActionArray)
{
  ObDSActionArray aa;
  ASSERT_TRUE(aa.is_empty());
  ASSERT_FALSE(aa.is_active(NOW));

  ObDebugSyncAction a;
  a.sync_point_ = NOW;
  a.execute_ = 2;
  a.wait_ = "abc";
  a.timeout_ = 1024000;

  ASSERT_EQ(OB_SUCCESS, aa.add_action(a));
  ASSERT_TRUE(aa.is_active(NOW));

  ObDebugSyncAction b;
  aa.copy_action(NOW, b);
  ASSERT_EQ(a.get_serialize_size(), b.get_serialize_size());

  // fetch to empty
  ASSERT_EQ(OB_SUCCESS, aa.fetch_action(NOW, b));
  ASSERT_FALSE(aa.is_empty());
  ASSERT_EQ(OB_SUCCESS, aa.fetch_action(NOW, b));
  ASSERT_TRUE(aa.is_empty());
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, aa.fetch_action(NOW, b));

  ASSERT_EQ(OB_SUCCESS, aa.add_action(a));
  ASSERT_EQ(OB_SUCCESS, aa.fetch_action(NOW, b));
  // over write
  ASSERT_EQ(OB_SUCCESS, aa.add_action(a));
  ASSERT_EQ(OB_SUCCESS, aa.fetch_action(NOW, b));
  ASSERT_EQ(OB_SUCCESS, aa.fetch_action(NOW, b));
  ASSERT_TRUE(aa.is_empty());

  ASSERT_EQ(OB_SUCCESS, aa.add_action(a));
  ASSERT_FALSE(aa.is_empty());

  aa.clear(NOW);
  ASSERT_TRUE(aa.is_empty());

  ASSERT_EQ(OB_SUCCESS, aa.add_action(a));
  aa.clear_all();
  ASSERT_TRUE(aa.is_empty());

  ObDSActionArray ba;
  ASSERT_EQ(OB_SUCCESS, ba.add_action(a));

  const static int64_t BUF_SIZE = 1024;
  char buf[BUF_SIZE];
  int64_t len = aa.get_serialize_size();
  LOG_INFO("empty debug sync array actions overhead", K(len));
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, aa.serialize(buf, len, pos));
  ASSERT_EQ(pos, len);

  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ba.deserialize(buf, len, pos));
  ASSERT_TRUE(ba.is_empty());

  ASSERT_EQ(OB_SUCCESS, aa.add_action(a));
  a.sync_point_ = MAJOR_FREEZE_BEFORE_SYS_COORDINATE_COMMIT;

  ASSERT_EQ(OB_SUCCESS, aa.add_action(a));
  len = aa.get_serialize_size();

  pos = 0;
  ASSERT_EQ(OB_SUCCESS, aa.serialize(buf, len, pos));
  ASSERT_EQ(pos, len);

  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ba.deserialize(buf, len, pos));
  ASSERT_EQ(pos, len);

  ASSERT_TRUE(ba.is_active(NOW));
  ASSERT_TRUE(ba.is_active(MAJOR_FREEZE_BEFORE_SYS_COORDINATE_COMMIT));

  // const action array will always be empty
  const bool is_const = true;
  ObDSActionArray ca(is_const);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ca.deserialize(buf, len, pos));
  ASSERT_EQ(pos, len);

  ASSERT_FALSE(ca.is_active(NOW));
  ASSERT_FALSE(ca.is_active(MAJOR_FREEZE_BEFORE_SYS_COORDINATE_COMMIT));

  ASSERT_TRUE(ca.is_empty());
}

TEST(common, ObDSSessionActions)
{
  ObMalloc allocator;
  ObDSSessionActions sa;

  ObDebugSyncAction a;
  a.sync_point_ = NOW;
  a.signal_ = "abc";
  a.execute_ = 1024;

  ASSERT_NE(OB_SUCCESS, sa.add_action(a));
  ASSERT_EQ(OB_SUCCESS, sa.init(1024, allocator));

  ASSERT_TRUE(sa.is_inited());

  ASSERT_EQ(OB_SUCCESS, sa.add_action(a));
  ObDSActionArray aa;
  sa.to_thread_local(aa);
  ASSERT_FALSE(aa.is_empty());
  ASSERT_TRUE(aa.is_active(NOW));

  aa.clear_all();
  sa.get_thread_local_result(aa);
  sa.to_thread_local(aa);
  ASSERT_TRUE(aa.is_empty());

  ASSERT_EQ(OB_SUCCESS, sa.add_action(a));
  a.sync_point_ = MAJOR_FREEZE_BEFORE_SYS_COORDINATE_COMMIT;
  ASSERT_EQ(OB_SUCCESS, aa.add_action(a));
  sa.to_thread_local(aa);
  ASSERT_TRUE(aa.is_active(NOW));
  ASSERT_FALSE(aa.is_active(MAJOR_FREEZE_BEFORE_SYS_COORDINATE_COMMIT));

  ASSERT_EQ(OB_SUCCESS, sa.add_action(a));
  sa.clear(NOW);
  sa.to_thread_local(aa);
  ASSERT_FALSE(aa.is_active(NOW));
  ASSERT_TRUE(aa.is_active(MAJOR_FREEZE_BEFORE_SYS_COORDINATE_COMMIT));

  sa.clear_all();
  sa.to_thread_local(aa);
  ASSERT_TRUE(aa.is_empty());
}

class Timer
{
public:
  Timer() { begin_ = ::oceanbase::common::ObTimeUtility::current_time(); }
  int64_t used() const { return ::oceanbase::common::ObTimeUtility::current_time() - begin_; }
public:
  int64_t begin_;
};

static ObDSEventControl global_event_control; // avoid large stack object
#define WAIT_TIME 200000
#define WAIT_TIME_SAFE (WAIT_TIME * 9 / 10)

#define TO_STRING_(x) #x
#define TO_STRING(x) "" TO_STRING_(x)

void *run_wait(void *arg)
{
  ObDSEventControl *ec = static_cast<ObDSEventControl *>(arg);
  ec->wait("multi-thread-event", WAIT_TIME, true);
  return NULL;
}

TEST(common, ObDSEventControl)
{
  ObDSEventControl &ec = global_event_control;
  ASSERT_NE(OB_SUCCESS, ec.signal(""));

  ObSqlString event;
  for (int64_t i = 0; i < ec.MAX_EVENT_CNT; ++i) {
    ASSERT_EQ(OB_SUCCESS, event.assign_fmt("%ld", i));
    ASSERT_EQ(OB_SUCCESS, ec.signal(event.ptr())) << "event: " << event.ptr() << std::endl;
  }
  ASSERT_NE(OB_SUCCESS, ec.signal("e"));
  ec.clear_event();
  ASSERT_EQ(OB_SUCCESS, ec.signal("e"));
  const bool DO_CLEAR = true;
  const bool NO_CLEAR = false;

  {
    Timer t;
    ASSERT_EQ(OB_SUCCESS, ec.wait("e", WAIT_TIME, NO_CLEAR));
    ASSERT_LT(t.used(), WAIT_TIME_SAFE);
  }

  ASSERT_EQ(OB_SUCCESS, ec.signal("e"));
  {
    Timer t;
    ASSERT_EQ(OB_SUCCESS, ec.wait("e", WAIT_TIME, DO_CLEAR));
    ASSERT_EQ(OB_SUCCESS, ec.wait("e", WAIT_TIME, DO_CLEAR));
    ASSERT_LT(t.used(), WAIT_TIME_SAFE);
  }

  {
    Timer t;
    ASSERT_EQ(OB_SUCCESS, ec.wait("e", WAIT_TIME, DO_CLEAR));
    ASSERT_GT(t.used(), WAIT_TIME_SAFE);
  }

  {
    Timer t;
    ObArray<pthread_t> tids;
    const int64_t MAX_THREAD_CNT = 10;
    int64_t n = 0;
    for (; n < MAX_THREAD_CNT / 2; ++n) {
      ASSERT_EQ(OB_SUCCESS, ec.signal("multi-thread-event"));
    }
    for (int64_t i = 0; i < MAX_THREAD_CNT; ++i) {
      pthread_t tid;
      ASSERT_EQ(0, pthread_create(&tid, NULL, run_wait, &ec));
      ASSERT_EQ(OB_SUCCESS, tids.push_back(tid));
    }
    for (; n < MAX_THREAD_CNT; ++n) {
      ASSERT_EQ(OB_SUCCESS, ec.signal("multi-thread-event"));
    }
    for (int64_t i = 0; i < MAX_THREAD_CNT; ++i) {
      pthread_join(tids.at(i), NULL);
    }
    ASSERT_LT(t.used(), WAIT_TIME_SAFE);
  }

  {
    Timer t;
    pthread_t tid;
    ASSERT_EQ(0, pthread_create(&tid, NULL, run_wait, &ec));
    ec.stop();
    pthread_join(tid, NULL);
    ASSERT_LT(t.used(), WAIT_TIME_SAFE);
  }
}


TEST(debug_sync, debug_sync_action_overflow)
{
  ObDSActionArray dsa;
  ObDebugSyncAction action;
  action.sync_point_ = NOW;
  action.signal_ = "a";
  action.wait_ = "b";
  action.execute_ = 1;
  ASSERT_TRUE(action.is_valid());
  for (int i = 0; i < ObDSActionArray::MAX_DEBUG_SYNC_CACHED_POINT; i++) {
    action.sync_point_ = (oceanbase::common::ObDebugSyncPoint)(i + 1);
    ASSERT_EQ(OB_SUCCESS, dsa.add_action(action));
    ASSERT_TRUE(dsa.is_active((oceanbase::common::ObDebugSyncPoint)(i + 1 )));
  }
  action.sync_point_ = (oceanbase::common::ObDebugSyncPoint)(ObDSActionArray::MAX_DEBUG_SYNC_CACHED_POINT + 1);
  ASSERT_EQ(OB_SIZE_OVERFLOW, dsa.add_action(action));
  ASSERT_FALSE(dsa.is_active((oceanbase::common::ObDebugSyncPoint)(ObDSActionArray::MAX_DEBUG_SYNC_CACHED_POINT + 1)));
}

}
}
