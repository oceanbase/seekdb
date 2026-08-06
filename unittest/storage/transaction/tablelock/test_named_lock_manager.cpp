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

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "lib/time/ob_time_utility.h"
#include "share/ob_errno.h"
#include "storage/tablelock/ob_named_lock_manager.h"

namespace oceanbase
{
namespace unittest
{
using namespace common;
using namespace transaction::tablelock;

class NamedLockManagerTest : public ::testing::Test
{
public:
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, manager_.init());
    ASSERT_EQ(OB_SUCCESS,
              owner1_.convert_from_value(ObLockOwnerType::SESS_ID_OWNER_TYPE, 1001));
    ASSERT_EQ(OB_SUCCESS,
              owner2_.convert_from_value(ObLockOwnerType::SESS_ID_OWNER_TYPE, 1002));
  }

  void TearDown() override { manager_.destroy(); }

protected:
  bool wait_for_waiter_count(const int64_t expected_count)
  {
    const int64_t max_retry = 1000;
    for (int64_t i = 0; i < max_retry; ++i) {
      int64_t lock_count = 0;
      int64_t waiter_count = 0;
      if (OB_SUCCESS == manager_.get_counts(lock_count, waiter_count)
          && expected_count == waiter_count) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

  NamedLockManager manager_;
  ObTableLockOwnerID owner1_;
  ObTableLockOwnerID owner2_;
};

TEST_F(NamedLockManagerTest, recursive_acquire_and_release)
{
  const ObString name = ObString::make_string("lock-a");
  ObTableLockOwnerID owner;
  bool is_free = true;
  int64_t release_result = 0;

  ASSERT_EQ(OB_SUCCESS, manager_.acquire(name, owner1_, 0));
  ASSERT_EQ(OB_SUCCESS, manager_.acquire(name, owner1_, 0));
  ASSERT_EQ(OB_SUCCESS, manager_.is_free(name, is_free));
  ASSERT_FALSE(is_free);
  ASSERT_EQ(OB_SUCCESS, manager_.get_owner(name, owner));
  ASSERT_EQ(owner1_, owner);

  ASSERT_EQ(OB_SUCCESS, manager_.release(name, owner1_, release_result));
  ASSERT_EQ(NamedLockManager::LOCK_RELEASED_RESULT, release_result);
  ASSERT_EQ(OB_SUCCESS, manager_.is_free(name, is_free));
  ASSERT_FALSE(is_free);

  ASSERT_EQ(OB_SUCCESS, manager_.release(name, owner1_, release_result));
  ASSERT_EQ(NamedLockManager::LOCK_RELEASED_RESULT, release_result);
  ASSERT_EQ(OB_SUCCESS, manager_.is_free(name, is_free));
  ASSERT_TRUE(is_free);
}

TEST_F(NamedLockManagerTest, release_semantics)
{
  const ObString name = ObString::make_string("lock-a");
  int64_t release_result = 0;

  ASSERT_EQ(OB_SUCCESS, manager_.release(name, owner1_, release_result));
  ASSERT_EQ(NamedLockManager::LOCK_NOT_EXIST_RELEASE_RESULT, release_result);
  ASSERT_EQ(OB_SUCCESS, manager_.acquire(name, owner1_, 0));
  ASSERT_EQ(OB_SUCCESS, manager_.release(name, owner2_, release_result));
  ASSERT_EQ(NamedLockManager::LOCK_NOT_OWN_RELEASE_RESULT, release_result);
  ASSERT_EQ(OB_ERR_EXCLUSIVE_LOCK_CONFLICT, manager_.acquire(name, owner2_, 0));
}

TEST_F(NamedLockManagerTest, legacy_name_length_boundary)
{
  std::string max_name(NamedLockManager::MAX_LOCK_NAME_LENGTH, 'a');
  std::string overlong_name(NamedLockManager::MAX_LOCK_NAME_LENGTH + 1, 'b');
  const ObString max_name_str(max_name.length(), max_name.data());
  const ObString overlong_name_str(overlong_name.length(), overlong_name.data());

  ASSERT_EQ(OB_SUCCESS, manager_.acquire(max_name_str, owner1_, 0));
  ASSERT_EQ(OB_ERR_DATA_TOO_LONG, manager_.acquire(overlong_name_str, owner1_, 0));
}

TEST_F(NamedLockManagerTest, legacy_name_collation)
{
  int64_t release_result = 0;
  bool is_free = true;

  ASSERT_EQ(OB_SUCCESS,
            manager_.acquire(ObString::make_string("NamedLock"), owner1_, 0));
  ASSERT_EQ(OB_ERR_EXCLUSIVE_LOCK_CONFLICT,
            manager_.acquire(ObString::make_string("namedlock"), owner2_, 0));
  ASSERT_EQ(OB_SUCCESS,
            manager_.is_free(ObString::make_string("NamedLock "), is_free));
  ASSERT_FALSE(is_free);
  ASSERT_EQ(OB_SUCCESS,
            manager_.release(ObString::make_string("namedlock "), owner1_, release_result));
  ASSERT_EQ(NamedLockManager::LOCK_RELEASED_RESULT, release_result);
}

TEST_F(NamedLockManagerTest, cross_session_conflict_and_timeout)
{
  const ObString name = ObString::make_string("lock-a");
  const int64_t timeout_us = 20 * 1000L;
  ASSERT_EQ(OB_SUCCESS, manager_.acquire(name, owner1_, 0));

  const int64_t start_ts = ObTimeUtility::current_time();
  ASSERT_EQ(OB_ERR_EXCLUSIVE_LOCK_CONFLICT,
            manager_.acquire(name, owner2_, timeout_us));
  ASSERT_GE(ObTimeUtility::current_time() - start_ts, timeout_us);
}

TEST_F(NamedLockManagerTest, release_all_counts_recursive_locks)
{
  int64_t release_count = 0;
  bool has_lock = false;

  ASSERT_EQ(OB_SUCCESS, manager_.acquire(ObString::make_string("lock-a"), owner1_, 0));
  ASSERT_EQ(OB_SUCCESS, manager_.acquire(ObString::make_string("lock-a"), owner1_, 0));
  ASSERT_EQ(OB_SUCCESS, manager_.acquire(ObString::make_string("lock-b"), owner1_, 0));
  ASSERT_EQ(OB_SUCCESS, manager_.release_all(owner1_, release_count));
  ASSERT_EQ(3, release_count);
  ASSERT_EQ(OB_SUCCESS, manager_.has_lock(owner1_, has_lock));
  ASSERT_FALSE(has_lock);
}

TEST_F(NamedLockManagerTest, waiter_is_woken_after_release)
{
  const ObString name = ObString::make_string("lock-a");
  int waiter_ret = OB_SUCCESS;
  int64_t release_result = 0;

  ASSERT_EQ(OB_SUCCESS, manager_.acquire(name, owner1_, 0));
  std::thread waiter([&]() { waiter_ret = manager_.acquire(name, owner2_, 2 * 1000 * 1000L); });
  EXPECT_TRUE(wait_for_waiter_count(1));
  EXPECT_EQ(OB_SUCCESS, manager_.release(name, owner1_, release_result));
  waiter.join();

  ASSERT_EQ(OB_SUCCESS, waiter_ret);
  ASSERT_EQ(OB_SUCCESS, manager_.release(name, owner2_, release_result));
}

TEST_F(NamedLockManagerTest, deadlock_detection)
{
  const ObString name_a = ObString::make_string("lock-a");
  const ObString name_b = ObString::make_string("lock-b");
  int owner1_wait_ret = OB_SUCCESS;
  int64_t release_result = 0;

  ASSERT_EQ(OB_SUCCESS, manager_.acquire(name_a, owner1_, 0));
  ASSERT_EQ(OB_SUCCESS, manager_.acquire(name_b, owner2_, 0));

  std::thread owner1_waiter([&]() {
    owner1_wait_ret = manager_.acquire(name_b, owner1_, 2 * 1000 * 1000L);
  });
  EXPECT_TRUE(wait_for_waiter_count(1));
  EXPECT_EQ(OB_DEAD_LOCK, manager_.acquire(name_a, owner2_, 2 * 1000 * 1000L));
  EXPECT_EQ(OB_SUCCESS, manager_.release(name_b, owner2_, release_result));
  owner1_waiter.join();

  ASSERT_EQ(OB_SUCCESS, owner1_wait_ret);
}

TEST_F(NamedLockManagerTest, owner_cleanup_releases_all_locks)
{
  int64_t release_count = 0;
  bool is_free = false;
  const ObString name = ObString::make_string("disconnect-lock");

  ASSERT_EQ(OB_SUCCESS, manager_.acquire(name, owner1_, 0));
  ASSERT_EQ(OB_SUCCESS, manager_.release_all(owner1_, release_count));
  ASSERT_EQ(1, release_count);
  ASSERT_EQ(OB_SUCCESS, manager_.is_free(name, is_free));
  ASSERT_TRUE(is_free);
  ASSERT_EQ(OB_SUCCESS, manager_.acquire(name, owner2_, 0));
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
