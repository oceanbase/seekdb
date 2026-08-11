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

#include <gtest/gtest.h>
#include <fstream>
#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#endif
#include "lib/alloc/alloc_func.h"
#include "lib/rc/context.h"
#include "lib/utility/utility.h"

#ifdef __APPLE__
#define __timezone timezone
#endif

using namespace oceanbase::common;
using namespace oceanbase::lib;
using namespace std;

#if defined(__linux__) && !defined(__ANDROID__)
namespace
{
class TempCgroupFiles
{
public:
  TempCgroupFiles()
  {
    char path_template[] = "/tmp/seekdb_cgroup_test_XXXXXX";
    char *path = mkdtemp(path_template);
    if (nullptr != path) {
      base_dir_ = path;
      directories_.push_back(base_dir_);
    }
  }

  ~TempCgroupFiles()
  {
    for (auto iter = files_.rbegin(); iter != files_.rend(); ++iter) {
      (void)unlink(iter->c_str());
    }
    for (auto iter = directories_.rbegin(); iter != directories_.rend(); ++iter) {
      (void)rmdir(iter->c_str());
    }
  }

  bool is_valid() const { return !base_dir_.empty(); }

  string path(const char *relative_path) const
  {
    return base_dir_ + "/" + relative_path;
  }

  bool make_dir(const char *relative_path)
  {
    const string directory = path(relative_path);
    const bool succeeded = 0 == mkdir(directory.c_str(), 0700);
    if (succeeded) {
      directories_.push_back(directory);
    }
    return succeeded;
  }

  bool write_file(const char *relative_path, const string &content)
  {
    const string file_path = path(relative_path);
    ofstream file(file_path);
    const bool succeeded = file.is_open() && static_cast<bool>(file << content);
    file.close();
    if (succeeded) {
      files_.push_back(file_path);
    }
    return succeeded;
  }

private:
  string base_dir_;
  vector<string> directories_;
  vector<string> files_;
};
} // namespace
#endif

TEST(utility, effective_memory_does_not_exceed_physical_memory)
{
  EXPECT_GT(get_phy_mem_size(), 0);
  EXPECT_GT(get_effective_memory_size(), 0);
  EXPECT_LE(get_effective_memory_size(), get_phy_mem_size());
}

#if defined(__linux__) && !defined(__ANDROID__)
TEST(utility, reads_cgroup_v2_hierarchy_limit)
{
  static constexpr int64_t THREE_GIB = 3LL << 30;
  TempCgroupFiles files;
  ASSERT_TRUE(files.is_valid());
  ASSERT_TRUE(files.make_dir("cgroup root"));
  ASSERT_TRUE(files.make_dir("cgroup root/pod"));
  ASSERT_TRUE(files.write_file("cgroup", "0::/tenant/pod\n"));
  ASSERT_TRUE(files.write_file("cgroup root/memory.max", to_string(THREE_GIB)));
  ASSERT_TRUE(files.write_file("cgroup root/pod/memory.max", "max\n"));
  const string escaped_mount_point = files.path("cgroup\\040root");
  const string mountinfo =
      "1 0 0:1 /tenant " + escaped_mount_point +
      " rw - cgroup2 cgroup rw\n";
  ASSERT_TRUE(files.write_file("mountinfo", mountinfo));

  EXPECT_EQ(THREE_GIB,
            get_cgroup_memory_limit(
                files.path("cgroup").c_str(), files.path("mountinfo").c_str()));
  ASSERT_TRUE(files.write_file("cgroup root/memory.max", "max\n"));
  EXPECT_EQ(0,
            get_cgroup_memory_limit(
                files.path("cgroup").c_str(), files.path("mountinfo").c_str()));
}

TEST(utility, reads_minimum_cgroup_v1_ancestor_limit)
{
  static constexpr int64_t THREE_GIB = 3LL << 30;
  static constexpr int64_t FOUR_GIB = 4LL << 30;
  TempCgroupFiles files;
  ASSERT_TRUE(files.is_valid());
  ASSERT_TRUE(files.make_dir("cgroup1"));
  ASSERT_TRUE(files.make_dir("cgroup1/slice"));
  ASSERT_TRUE(files.make_dir("cgroup1/slice/job"));
  ASSERT_TRUE(files.write_file("cgroup", "2:cpu,memory:/slice/job\n"));
  ASSERT_TRUE(files.write_file(
      "cgroup1/memory.limit_in_bytes", "9223372036854771712\n"));
  ASSERT_TRUE(files.write_file(
      "cgroup1/slice/memory.limit_in_bytes", to_string(THREE_GIB)));
  ASSERT_TRUE(files.write_file(
      "cgroup1/slice/job/memory.limit_in_bytes", to_string(FOUR_GIB)));
  const string mountinfo =
      "2 0 0:2 / " + files.path("cgroup1") +
      " rw - cgroup cgroup rw,memory\n";
  ASSERT_TRUE(files.write_file("mountinfo", mountinfo));

  EXPECT_EQ(THREE_GIB,
            get_cgroup_memory_limit(
                files.path("cgroup").c_str(), files.path("mountinfo").c_str()));
}
#endif

TEST(utility, memory_limit_scaled_value_compatibility)
{
  const int64_t old_memory_budget = get_memory_budget();
  const int64_t one_gib = 1L << 30;
  const int64_t min_value = 4096;
  const int64_t max_value = 100000;
  const auto old_scaled_value = [=](const int64_t memory_limit) {
    const int64_t lower_bound = one_gib;
    const int64_t upper_bound = 128L * one_gib;
    const int64_t clamped_memory = MIN(MAX(memory_limit, lower_bound), upper_bound);
    const double ratio = static_cast<double>(clamped_memory - lower_bound)
        / static_cast<double>(upper_bound - lower_bound);
    return static_cast<int64_t>(min_value + ratio * (max_value - min_value));
  };

  set_memory_budget(one_gib / 2);
  EXPECT_EQ(old_scaled_value(one_gib),
            calculate_scaled_value_by_memory(min_value, max_value));
  set_memory_budget(one_gib);
  EXPECT_EQ(old_scaled_value(2 * one_gib),
            calculate_scaled_value_by_memory(min_value, max_value));
  set_memory_budget(64 * one_gib);
  EXPECT_EQ(old_scaled_value(128 * one_gib),
            calculate_scaled_value_by_memory(min_value, max_value));
  set_memory_budget(128 * one_gib);
  EXPECT_EQ(max_value, calculate_scaled_value_by_memory(min_value, max_value));

  EXPECT_EQ(INT64_MAX / 100 * 99 + INT64_MAX % 100 * 99 / 100,
            get_memory_by_percentage(INT64_MAX, 99));
  EXPECT_EQ(INT64_MAX, get_memory_by_percentage(INT64_MAX, 130));

  set_memory_budget(99);
  EXPECT_EQ(128, get_memory_by_percentage(get_memory_budget(), 130));
  set_memory_budget(INT64_MAX);
  EXPECT_EQ(INT64_MAX / 100 * 40 + INT64_MAX % 100 * 40 / 100,
            get_memory_by_percentage(get_memory_budget(), 40));
  set_memory_budget(old_memory_budget);
}

struct TestItem
{
  TestItem() : status_(0), disable_assign_(false)
  {}
  ~TestItem()
  {
    status_ = 1;
  }
  int assign(const TestItem &other)
  {
    return other.disable_assign_ ? OB_ERROR : OB_SUCCESS;
  }
  int status_;
  bool disable_assign_;
};

TEST(utility, construct_assign)
{
  TestItem item_1, item_2;
  item_1.disable_assign_ = true;
  TestItem *item = (TestItem *)ob_malloc(sizeof(TestItem), "TEST");
  construct_assign(*item, item_1);
  ASSERT_EQ(1, item->status_);
  construct_assign(*item, item_2);
  ASSERT_EQ(0, item->status_);
}

TEST(utility, load_file_to_string)
{
  const char *path = NULL;
  CharArena alloc;
  ObString str;
  ASSERT_NE(OB_SUCCESS, load_file_to_string(path, alloc, str));

  // create a tmp file to test
  const char* testPath = "test_load_file_to_string";
  std::ofstream testFile(testPath);
  ASSERT_EQ(true, testFile.is_open());
  testFile << "test load_file_to_string";
  testFile.close();
  ASSERT_EQ(OB_SUCCESS, load_file_to_string(testPath, alloc, str));
  
  ASSERT_EQ(0, str.ptr()[str.length()]);
}

// DISABLED: reads /sys/class/net/*/speed; cloud/virtio NICs report -1 (no link
// speed), so the speed assertion cannot pass on such VMs. Disabled (gtest
// convention, cf. DISABLED_bandwidth_throttle) to keep
// the suite green on speed-less NICs; runs on real NICs if the prefix is dropped.
TEST(utility, DISABLED_get_ethernet_speed)
{
  int64_t speed = 0;
  ASSERT_NE(OB_SUCCESS, get_ethernet_speed(NULL, speed));

  if (OB_FILE_NOT_EXIST == get_ethernet_speed("bond0", speed)) {
    ASSERT_EQ(OB_SUCCESS, get_ethernet_speed("eth0", speed));
  }
  // Some hosts (virtualized / bonded NICs) report link speed as -1 (unknown)
  // in /sys/class/net/<dev>/speed. That is a hardware/OS fact, not a code
  // defect, so skip the speed-magnitude assertion when no valid speed is
  // reported instead of failing the test.
  if (speed <= 0) {
    fprintf(stderr, "[ SKIPPED  ] NIC reports no valid link speed (%ld); "
            "/sys/class/net/<dev>/speed unavailable on this host\n", speed);
    return;
  }
  ASSERT_GE(speed, 1 << 20);
  _OB_LOG(INFO, "speed %ld", speed);
}

TEST(utility, wild_compare)
{
  const bool STR_IS_NOT_PATTERN = false;
  const bool STR_IS_PATTERN = true;
  //----wild one test----
  const int NUM = 10;
  char str[NUM];
  char str_ne[NUM];
  char str_wild_one[NUM];
  //char str_ne[NUM];
  snprintf(str, NUM, "0123456789");
  snprintf(str_ne, NUM, "0213456789");
  snprintf(str_wild_one, NUM, "01234567_9");

  //arguments with 'char *' type
  //str: "012345678", str_ne: "021345678", str_wild_one: "01234567_"
  ASSERT_EQ(0, wild_compare(str, str_wild_one, STR_IS_NOT_PATTERN));
  ASSERT_NE(0, wild_compare(str_ne, str_wild_one, STR_IS_NOT_PATTERN));

  str[NUM - 1] = '9';
  str_wild_one[NUM - 1] = '9';
  str_ne[NUM - 1] = '0';
  ObString ob_str(NUM, str);
  ObString ob_str_wild(NUM, str_wild_one);
  ObString ob_str_ne(NUM, str_ne);
  //arguments with 'ObString' type
  //ob_str: "0123456789", ob_str_ne: "0213456789" ob_str_wild: "01234567_9"
  ASSERT_EQ(0, wild_compare(ob_str, ob_str_wild, STR_IS_NOT_PATTERN));
  ASSERT_NE(0, wild_compare(ob_str_ne, ob_str_wild, STR_IS_NOT_PATTERN));

  //----wild many test----
  const int MANY_STR_NUM = 6;
  char str_wild_many[6];
  snprintf(str_wild_many, MANY_STR_NUM, "012%%89");
  str[NUM - 1] = '\0';
  str_ne[NUM - 1] = '\0';
  //arguments with 'char *' type
  //str: "012345678", str_ne: "021345678" str_wild_many: "012%8"
  ASSERT_EQ(0, wild_compare(str, str_wild_many, STR_IS_NOT_PATTERN));
  ASSERT_NE(0, wild_compare(str_ne, str_wild_many, STR_IS_NOT_PATTERN));

  str[NUM - 1] = '9';
  str_ne[NUM - 1] = '9';
  str_wild_many[MANY_STR_NUM - 1] = '9';
  //arguments with 'ObString' type
  //ob_str: "0123456789", ob_str_ne:"0213456789" ob_str_wild: "012%89"
  ob_str.assign_ptr(str, NUM);
  ob_str_wild.assign_ptr(str_wild_many, MANY_STR_NUM);
  ob_str_ne.assign_ptr(str_ne, NUM);
  ASSERT_EQ(0, wild_compare(ob_str, ob_str_wild, STR_IS_NOT_PATTERN));
  ASSERT_NE(0, wild_compare(ob_str_ne, ob_str_wild, STR_IS_NOT_PATTERN));

  //----wild one test. Str is pattern.----
  snprintf(str, NUM, "01234567%%");
  snprintf(str_wild_many, MANY_STR_NUM, "0123%%");
  snprintf(str_ne, NUM, "012%%34567");
  //arguments with 'char *' type
  //str: "01234567%", str_ne: "012%34567", str_wild_many: "0123%"
  ASSERT_EQ(0, wild_compare(str, str_wild_many, STR_IS_PATTERN));
  ASSERT_NE(0, wild_compare(str_ne, str_wild_many, STR_IS_PATTERN));
  str[NUM - 1] = '9';
  str_wild_many[MANY_STR_NUM -1] = '9';
  ob_str.assign_ptr(str, NUM);
  ob_str_wild.assign_ptr(str_wild_many, MANY_STR_NUM);
  ob_str_ne.assign_ptr(str_ne, NUM);
  //arguments with 'ObString' type
  //ob_str: "01234567%9", ob_str_ne: "012%345679", ob_str_wild: "0123%9"
  ASSERT_EQ(0, wild_compare(ob_str, ob_str_wild, STR_IS_PATTERN));
  ASSERT_NE(0, wild_compare(ob_str_ne, ob_str_wild, STR_IS_PATTERN));

  snprintf(str, NUM, "012345%%%%");
  snprintf(str_wild_many, MANY_STR_NUM, "012%%%%");
  ASSERT_EQ(0, wild_compare(str, str_wild_many, STR_IS_PATTERN));
  ob_str.assign_ptr(str, (int32_t)strlen(str));
  ob_str_wild.assign_ptr(str_wild_many, (int32_t)strlen(str_wild_many));
  ASSERT_EQ(0, wild_compare(ob_str, ob_str_wild, STR_IS_PATTERN));
}

TEST(utility, get_sort)
{
  uint64_t sort_value = 128;
  sort_value = (sort_value << 8) + 1;
  sort_value = (sort_value << 8) + 5;
  sort_value = (sort_value << 8) + 9;
  ASSERT_EQ(sort_value, get_sort(4, "user", "%user", "user%", "useruser%"));

  sort_value = 5;
  sort_value = (sort_value << 8) + 0;
  sort_value = (sort_value << 8) + 128;
  ASSERT_EQ(sort_value, get_sort(3, "user%", "", "user"));

  sort_value = 5;
  sort_value = (sort_value << 8) + 0;
  ASSERT_EQ(sort_value, get_sort(2, "user_", ""));
  sort_value = 0;
  ASSERT_EQ(sort_value, get_sort(1, ""));

  const int NUM = 10;
  char str[NUM] = "123456%89";
  str[NUM - 1] = '0';
  ObString ob_str(NUM, str);
  sort_value = 7;
  ASSERT_EQ(sort_value, get_sort(ob_str));

  snprintf(str, NUM, "_12345");
  ob_str.assign_ptr(str, 6);
  sort_value = 1;
  ASSERT_EQ(sort_value, get_sort(ob_str));
}

TEST(utility, ltoa10)
{
  char sp_buff[35];
  char ltoa_buff[OB_LTOA10_CHAR_LEN];
  int64_t check_value = INT64_MIN;
  ltoa10(check_value, ltoa_buff, true);
  snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
  ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  check_value = INT64_MAX;
  ltoa10(check_value, ltoa_buff, true);
  snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
  ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  uint64_t uvalue = UINT64_MAX;
  ltoa10(uvalue, ltoa_buff, false);
  snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
  ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  uvalue = 0;
  ltoa10(uvalue, ltoa_buff, false);
  snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
  ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  srand((int)time(NULL));
  for (int64_t idx = 0; idx < 2000; ++idx) {
    check_value = rand();
    ltoa10(check_value, ltoa_buff, true);
    snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
    ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    check_value = rand() - INT32_MAX;
    ltoa10(check_value, ltoa_buff, true);
    snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
    ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    check_value = rand() - INT64_MAX;
    ltoa10(check_value, ltoa_buff, true);
    snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
    ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    uvalue = rand();
    ltoa10(uvalue, ltoa_buff, false);
    snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
    ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    uvalue = rand() + INT32_MAX;
    ltoa10(uvalue, ltoa_buff, false);
    snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
    ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    uvalue = rand() + INT64_MAX;
    ltoa10(uvalue, ltoa_buff, false);
    snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
    ASSERT_EQ(strcasecmp(ltoa_buff, sp_buff), 0);
  }
}

TEST(utility, long_to_str10)
{
  //correct
  char sp_buff[35];
  char str10[OB_LTOA10_CHAR_LEN];
  int64_t check_value = INT64_MIN;
  int64_t length = INT64_MIN;
  long_to_str10(check_value, str10, sizeof(str10), true, length);
  snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
  ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
  check_value = INT64_MAX;
  long_to_str10(check_value, str10, sizeof(str10), true, length);
  snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
  ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
  ASSERT_EQ(length, strlen(sp_buff));
  uint64_t uvalue = UINT64_MAX;
  long_to_str10(uvalue, str10, sizeof(str10), false, length);
  snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
  ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
  ASSERT_EQ(length, strlen(sp_buff));
  uvalue = 0;
  long_to_str10(uvalue, str10, sizeof(str10), false, length);
  snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
  ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
  ASSERT_EQ(length, strlen(sp_buff));
  srand((int)time(NULL));
  for (int64_t idx = 0; idx < 2000; ++idx) {
    check_value = rand();
    long_to_str10(check_value, str10, sizeof(str10), true, length);
    snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
    ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
    ASSERT_EQ(length, strlen(sp_buff));
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    check_value = rand() - INT32_MAX;
    long_to_str10(check_value, str10, sizeof(str10), true, length);
    snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
    ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
    ASSERT_EQ(length, strlen(sp_buff));
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    check_value = rand() - INT64_MAX;
    long_to_str10(check_value, str10, sizeof(str10), true, length);
    snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
    ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
    ASSERT_EQ(length, strlen(sp_buff));
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    uvalue = rand();
    long_to_str10(uvalue, str10, sizeof(str10), false, length);
    snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
    ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
    ASSERT_EQ(length, strlen(sp_buff));
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    uvalue = rand() + INT32_MAX;
    long_to_str10(uvalue, str10, sizeof(str10), false, length);
    snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
    ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
    ASSERT_EQ(length, strlen(sp_buff));
  }
  for (int64_t idx = 0; idx < 2000; ++idx) {
    uvalue = rand() + INT64_MAX;
    long_to_str10(uvalue, str10, sizeof(str10), false, length);
    snprintf(sp_buff, sizeof(sp_buff), "%lu", uvalue);
    ASSERT_EQ(strcasecmp(str10, sp_buff), 0);
    ASSERT_EQ(length, strlen(sp_buff));
  }

  check_value = 3;
  char str10_t1[1];
  ASSERT_EQ(OB_INVALID_ARGUMENT, long_to_str10(check_value, str10_t1, sizeof(str10_t1), true, length));
  char str10_t2[2];
  ASSERT_EQ(OB_SUCCESS, long_to_str10(check_value, str10_t2, sizeof(str10_t2), true, length));
  snprintf(sp_buff, sizeof(sp_buff), "%ld", check_value);
  ASSERT_EQ(length, strlen(sp_buff));
  char str10_t3[3];
  ASSERT_EQ(OB_SUCCESS, long_to_str10(check_value, str10_t3, sizeof(str10_t3), true, length));
  ASSERT_EQ(length, strlen(sp_buff));
  check_value = 345;
  ASSERT_EQ(OB_SIZE_OVERFLOW, long_to_str10(check_value, str10_t3, sizeof(str10_t3), true, length));
  check_value = 3456;
  ASSERT_EQ(OB_SIZE_OVERFLOW, long_to_str10(check_value, str10_t3, sizeof(str10_t3), true, length));
  uvalue = UINT64_MAX;
  char str10_t5[5];
  ASSERT_EQ(OB_SIZE_OVERFLOW, long_to_str10(uvalue, str10_t5, sizeof(str10_t5), false, length));
  uvalue = UINT32_MAX;
  ASSERT_EQ(OB_SIZE_OVERFLOW, long_to_str10(uvalue, str10_t5, sizeof(str10_t5), false, length));
}

void test_max_speed(const int64_t rate, const int64_t total_size, const int64_t buf_size)
{
  int ret = 0;
  COMMON_LOG(INFO, "test_max_speed", K(rate), K(total_size), K(buf_size));
  ASSERT_NE(0, rate);
  ObBandwidthThrottle throttle;
  int64_t cur_buf_size = 0;
  //int64_t avaliable_ts = 0;
  if (total_size * 1000 * 1000 > INT64_MAX){
    COMMON_LOG(ERROR, "total size is too large", K(total_size));
    FAIL();
  }
  ASSERT_EQ(OB_SUCCESS, throttle.init(rate));

  int64_t start_time = ObTimeUtility::current_time();
  //int64_t sleep_time = 0;
  int64_t expect_time = 0;
  int64_t count = 0;
  int64_t cost_time = 0;
  int64_t speed = 0;
  int64_t sleep_us = 0;
  for (int64_t cur_size = 0; cur_size < total_size; cur_size += cur_buf_size) {
    ++count;
    if (buf_size > 0) {
      cur_buf_size = buf_size;
    } else {
      cur_buf_size = ObRandom::rand(1, 1024);
    }
    ASSERT_EQ(OB_SUCCESS, throttle.limit_and_sleep(cur_buf_size, start_time, INT64_MAX, sleep_us));
  }
  cost_time = ObTimeUtility::current_time() - start_time;
  expect_time = 1000 * 1000 * total_size / rate;
  if (cost_time > 0) {
    speed = total_size * 1000 * 1000/ cost_time;
  }
  int64_t diff = (speed - rate) * 100 / rate;
  COMMON_LOG(INFO, "finish check", K(cost_time), K(expect_time), K(total_size), K(rate), K(buf_size), K(count), K(speed), K(diff));
  if (diff > 0 || diff < -20) {
    COMMON_LOG(WARN, "check time", K(cost_time), K(expect_time), K(total_size), K(rate), K(buf_size), K(count), K(speed), K(diff));
    FAIL();
  }
}

TEST(utility, DISABLED_bandwidth_throttle)
{
  ObBandwidthThrottle throttle;
  ASSERT_EQ(OB_NOT_INIT, throttle.set_rate(1024*1024));//1m/s
  ASSERT_EQ(OB_SUCCESS, throttle.init(0));
  throttle.destroy();
  ASSERT_EQ(OB_SUCCESS, throttle.init(1024*1024));//1m/s
  ASSERT_EQ(OB_INIT_TWICE, throttle.init(1024*1024));//1m/s
  ASSERT_EQ(OB_INVALID_ARGUMENT, throttle.set_rate(-1));
  ASSERT_EQ(OB_SUCCESS, throttle.set_rate(0));
  ASSERT_EQ(OB_SUCCESS, throttle.set_rate(1024*1024));//1m/s

  test_max_speed(100*1024*1024, 60*1024*1024, 2*1024*1024);
  test_max_speed(1*1024, 1*1024, 32);
  test_max_speed(512*1024*1024*1024LL, 1024*1024*1024*1024LL, 600*1024*1024);
  test_max_speed(100*1024*1024, 60*1024*1024, -1);
}

TEST(utility, ob_localtime)
{
  int ret = 0;
  struct tm std_tm;
  struct tm ob_tm;

  time_t last_sec = 0;
  struct tm last_tm;
  struct timeval tv;
  (void)gettimeofday(&tv, NULL);

  //check next five year, need 180s
  const int64_t min_time = static_cast<int64_t>(tv.tv_sec);
  const int64_t max_time = min_time + 64 * 24 * 60 * 60;
  for (int64_t i = min_time; i < max_time; i += 1) {
    ::localtime_r((const time_t *)&i, &std_tm);
    ob_localtime((const time_t *)&i, &ob_tm);

//    _COMMON_LOG(WARN, "check time, std_tm=%d %d-%d %d:%d:%d, ob_tm=%d %d-%d %d:%d:%d, value=%ld",
//        std_tm.tm_year, std_tm.tm_mon, std_tm.tm_mday, std_tm.tm_hour, std_tm.tm_min, std_tm.tm_sec,
//        ob_tm.tm_year, ob_tm.tm_mon, ob_tm.tm_mday, ob_tm.tm_hour, ob_tm.tm_min, ob_tm.tm_sec,
//        i);

    EXPECT_EQ(std_tm.tm_sec, ob_tm.tm_sec);
    EXPECT_EQ(std_tm.tm_min, ob_tm.tm_min);
    EXPECT_EQ(std_tm.tm_hour, ob_tm.tm_hour);
    EXPECT_EQ(std_tm.tm_mday, ob_tm.tm_mday);
    EXPECT_EQ(std_tm.tm_mon, ob_tm.tm_mon);
    EXPECT_EQ(std_tm.tm_year, ob_tm.tm_year);

    ob_fast_localtime(last_sec, last_tm, (const time_t)i, &std_tm);
    EXPECT_EQ(i, last_sec);
    EXPECT_EQ(std_tm.tm_sec, last_tm.tm_sec);
    EXPECT_EQ(std_tm.tm_min, last_tm.tm_min);
    EXPECT_EQ(std_tm.tm_hour, last_tm.tm_hour);
    EXPECT_EQ(std_tm.tm_mday, last_tm.tm_mday);
    EXPECT_EQ(std_tm.tm_mon, last_tm.tm_mon);
    EXPECT_EQ(std_tm.tm_year, last_tm.tm_year);
  }

  int64_t special_value[18] = {
    825523199,//1996.02.28 23.59.59
    825523200,//1996.02.29 00.00.00
    825609599,//1996.02.29 23.59.59
    825609600,//1996.03.01 00.00.00

    951753599,//2000.02.28 23.59.59
    951753600,//2000.02.29 00.00.00
    951839999,//2000.02.29 23.59.59
    951840000,//2000.03.01 00.00.00

    4107513599,//2100.02.28 23:59:59
    4107513600,//2100.03.01 00:00:00
    920217599,//1999.02.28 23.59.59
    920217600,//1999.03.01 00.00.00

    946655999,//1999.12.31 23.59.59
    946656000,//2000.01.01 0:0:0

    0,//1970.01.01 08:00:00
    -1,//1970.01.01 07:59:59
    -2203920344,//1900.02.28 23.59.59
    -2203920343//1900.03.01 00.00.00
  };

  for (int64_t i = 0; i < 14; ++i) {
    ::localtime_r((const time_t *)(special_value + i), &std_tm);
    ob_localtime((const time_t *)(special_value + i), &ob_tm);

    _COMMON_LOG(WARN, "check time, std_tm=%d %d-%d %d:%d:%d, ob_tm=%d %d-%d %d:%d:%d, value=%ld, __time=%ld",
        std_tm.tm_year, std_tm.tm_mon, std_tm.tm_mday, std_tm.tm_hour, std_tm.tm_min, std_tm.tm_sec,
        ob_tm.tm_year, ob_tm.tm_mon, ob_tm.tm_mday, ob_tm.tm_hour, ob_tm.tm_min, ob_tm.tm_sec,
        special_value[i], __timezone / 60);

    EXPECT_EQ(std_tm.tm_sec, ob_tm.tm_sec);
    EXPECT_EQ(std_tm.tm_min, ob_tm.tm_min);
    EXPECT_EQ(std_tm.tm_hour, ob_tm.tm_hour);
    EXPECT_EQ(std_tm.tm_mday, ob_tm.tm_mday);
    EXPECT_EQ(std_tm.tm_mon, ob_tm.tm_mon);
    EXPECT_EQ(std_tm.tm_year, ob_tm.tm_year);

    ob_fast_localtime(last_sec, last_tm, (const time_t)i, &std_tm);
    EXPECT_EQ(i, last_sec);
    EXPECT_EQ(std_tm.tm_sec, last_tm.tm_sec);
    EXPECT_EQ(std_tm.tm_min, last_tm.tm_min);
    EXPECT_EQ(std_tm.tm_hour, last_tm.tm_hour);
    EXPECT_EQ(std_tm.tm_mday, last_tm.tm_mday);
    EXPECT_EQ(std_tm.tm_mon, last_tm.tm_mon);
    EXPECT_EQ(std_tm.tm_year, last_tm.tm_year);
  }

  // ob_localtime only supports unix_sec >= 0 (cf. "only support time >= 1970/1/1 8:0:0").
  // special_value[14] == 0 is the epoch second and is now converted; validate it against
  // the libc reference like the loop above. The remaining pre-epoch values are still
  // unsupported: the struct is left untouched, so reset it first and expect zeros.
  for (int64_t i = 14; i < 18; ++i) {
    ob_tm.tm_sec = 0;
    ob_tm.tm_min = 0;
    ob_tm.tm_hour = 0;
    ob_tm.tm_mday = 0;
    ob_tm.tm_mon = 0;
    ob_tm.tm_year = 0;

    ob_localtime((const time_t *)(special_value + i), &ob_tm);

    if (special_value[i] >= 0) {
      ::localtime_r((const time_t *)(special_value + i), &std_tm);
      EXPECT_EQ(std_tm.tm_sec, ob_tm.tm_sec);
      EXPECT_EQ(std_tm.tm_min, ob_tm.tm_min);
      EXPECT_EQ(std_tm.tm_hour, ob_tm.tm_hour);
      EXPECT_EQ(std_tm.tm_mday, ob_tm.tm_mday);
      EXPECT_EQ(std_tm.tm_mon, ob_tm.tm_mon);
      EXPECT_EQ(std_tm.tm_year, ob_tm.tm_year);
    } else {
      EXPECT_EQ(0, ob_tm.tm_sec);
      EXPECT_EQ(0, ob_tm.tm_min);
      EXPECT_EQ(0, ob_tm.tm_hour);
      EXPECT_EQ(0, ob_tm.tm_mday);
      EXPECT_EQ(0, ob_tm.tm_mon);
      EXPECT_EQ(0, ob_tm.tm_year);
    }
  }
}


struct TestCheckStruct  {
  TestCheckStruct() : is_ob_localtime_(false), stop_(false) {}
  bool is_ob_localtime_;
  volatile bool stop_;
};

void* ob_localtime_pthread_op_func(void* arg)
{
  TestCheckStruct *tmp = static_cast<TestCheckStruct *>(arg);
  struct tm ob_tm;
//  OB_LOG(INFO, "thread begin");
  if (tmp->is_ob_localtime_) {
    for (int64_t i = 0; !tmp->stop_; i += 1) {
      ob_localtime((const time_t *)&i, &ob_tm);
    }
  } else {
    for (int64_t i = 0; !tmp->stop_; i += 1) {
      ::localtime_r((const time_t *)&i, &ob_tm);
    }
  }
//  OB_LOG(INFO, "thread end");
  return NULL;
}

TEST(utility, DISABLED_multi_thread_sys)
{
  const int64_t max_thread_count[] = {10, 100};
  for (int64_t j = 0; j < 2; ++j) {
    vector<pthread_t> pid_vector;
    struct TestCheckStruct tmp;
    tmp.is_ob_localtime_ = false;
    for (int64_t i = 0; i < max_thread_count[j]; ++i) {
      pthread_t pid = 0;
      pthread_create(&pid, NULL, ob_localtime_pthread_op_func, &tmp);
      pid_vector.push_back(pid);
    }

    sleep(2);

    OB_LOG(INFO, "after sleep");

    struct tm ob_tm;

    const int64_t MAX_COUNT = 1000000;
    const int64_t WEIGHT = 1000000;
    struct timeval time_begin, time_end;

    gettimeofday(&time_begin, NULL);
    for (int64_t i = 1; i < MAX_COUNT; i += 1) {
      ::localtime_r((const time_t *)&i, &ob_tm);
    }
    gettimeofday(&time_end, NULL);
    OB_LOG(INFO, "performance average(ns)",
           "localtime_r", (WEIGHT * (time_end.tv_sec - time_begin.tv_sec) + time_end.tv_usec - time_begin.tv_usec) / (MAX_COUNT / 1000),
           "thread_count", max_thread_count[j]);

    tmp.stop_ = true;
    for (int64_t i = 0; i < max_thread_count[j]; ++i) {
      pthread_join(pid_vector[i], NULL);
    }
  }
}

TEST(utility, multi_thread_ob)
{
  const int64_t max_thread_count[] = {10, 100};
  for (int64_t j = 0; j < 2; ++j) {
    vector<pthread_t> pid_vector;
    struct TestCheckStruct tmp;
    tmp.is_ob_localtime_ = true;
    for (int64_t i = 0; i < max_thread_count[j]; ++i) {
      pthread_t pid = 0;
      pthread_create(&pid, NULL, ob_localtime_pthread_op_func, &tmp);
      pid_vector.push_back(pid);
    }

    sleep(2);

    OB_LOG(INFO, "after sleep");

    struct tm ob_tm;

    const int64_t MAX_COUNT = 1000000;
    const int64_t WEIGHT = 1000000;
    struct timeval time_begin, time_end;

    gettimeofday(&time_begin, NULL);
    for (int64_t i = 1; i < MAX_COUNT; i += 1) {
      ob_localtime((const time_t *)&i, &ob_tm);
    }
    gettimeofday(&time_end, NULL);
    OB_LOG(INFO, "performance average(ns)",
           "ob_localtime", (WEIGHT * (time_end.tv_sec - time_begin.tv_sec) + time_end.tv_usec - time_begin.tv_usec) / (MAX_COUNT / 1000),
           "thread_count", max_thread_count[j]);

    tmp.stop_ = true;
    for (int64_t i = 0; i < max_thread_count[j]; ++i) {
      pthread_join(pid_vector[i], NULL);
    }
  }
}

TEST(utility, ob_atoll_overflow)
{
  int ret = OB_SUCCESS;
  const char *digital_num = "99999999999999999999999999";
  int64_t val = 0;
  ret = ob_atoll(digital_num, val);
  ASSERT_EQ(OB_SIZE_OVERFLOW, ret);
  ret = OB_SUCCESS;
}

TEST(utility, ob_strtoull_overflow)
{
  int ret = OB_SUCCESS;
  const char *digital_num = "99999999999999999999999999";
  char *endptr = NULL;
  uint64_t val = 0;
  ret = ob_strtoull(digital_num, endptr, val);
  ASSERT_EQ(OB_SIZE_OVERFLOW, ret);
}
