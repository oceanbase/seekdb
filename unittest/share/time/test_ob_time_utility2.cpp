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
#include <stdint.h>
#include "lib/ob_define.h"
#include "share/ob_time_utility2.h"

using namespace oceanbase;
using namespace oceanbase::common;
using oceanbase::share::ObTimeUtility2;

class ObTimeUtilityTest: public ::testing::Test
{
public:
  ObTimeUtilityTest();
  virtual ~ObTimeUtilityTest();
  virtual void SetUp();
  virtual void TearDown();
private:
  // disallow copy
  ObTimeUtilityTest(const ObTimeUtilityTest &other);
  ObTimeUtilityTest& operator=(const ObTimeUtilityTest &other);
protected:
  // data members
};

ObTimeUtilityTest::ObTimeUtilityTest()
{
}

ObTimeUtilityTest::~ObTimeUtilityTest()
{
}

void ObTimeUtilityTest::SetUp()
{
}

void ObTimeUtilityTest::TearDown()
{
}

TEST(ObTimeUtilityTest, str_to_timestamp_test)
{
  struct tm t;
  int64_t usec = 0;
  const char *date_ptr = "1970-02-03 07:08:09.12";
  ASSERT_EQ(OB_SUCCESS, ObTimeUtility2::str_to_timestamp(ObString::make_string(date_ptr), t, usec));
  printf("date: %04d-%02d-%02d %02d:%02d:%02d.%06ld\n",
    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, usec);
  date_ptr = "1970^^02***03&&&07:08:09->12";
  ASSERT_EQ(OB_SUCCESS, ObTimeUtility2::str_to_timestamp(ObString::make_string(date_ptr), t, usec));
  printf("date: %04d-%02d-%02d %02d:%02d:%02d.%06ld\n",
    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, usec);
}

#define T_WEEK(today, wday, flag_mask, result) \
  { \
    struct tm t; \
    t.tm_yday = today - 1; \
    t.tm_wday = wday; \
    ASSERT_EQ(result, ObTimeUtility2::get_weeks_of_year(t, flag_mask)); \
  }

#define T_WEEK_CHECK_YEAR(today, wday, year, flag_mask, week_count, r_year) \
  { \
    struct tm t; \
    t.tm_year = year - 1900; \
    t.tm_yday = today - 1; \
    t.tm_wday = wday; \
    ASSERT_EQ(week_count, ObTimeUtility2::get_weeks_of_year(t, flag_mask)); \
    ASSERT_EQ(r_year, t.tm_year + 1900); \
  }

TEST(ObTimeUtilityTest, get_weeks_of_year_test)
{
  //'%U'
  T_WEEK(1, 0, START_WITH_SUNDAY | WEEK_FIRST_WEEKDAY, 1);
  T_WEEK(1, 3, START_WITH_SUNDAY | WEEK_FIRST_WEEKDAY, 0);
  T_WEEK(8, 4, START_WITH_SUNDAY | WEEK_FIRST_WEEKDAY, 1);
  T_WEEK(32, 6, START_WITH_SUNDAY | WEEK_FIRST_WEEKDAY, 4);
  //'%u'
  T_WEEK(1, 0, 0, 0);
  T_WEEK(1, 1, 0, 1);
  T_WEEK(8, 4, 0, 2);
  T_WEEK(32, 6, 0, 5);
  T_WEEK(1, 3, 0, 1);
  //'%V'
  T_WEEK_CHECK_YEAR(1, 0, 2014, START_WITH_SUNDAY | WEEK_FIRST_WEEKDAY | INCLUDE_CRITICAL_WEEK, 1, 2014);
  T_WEEK_CHECK_YEAR(1, 3, 2014, START_WITH_SUNDAY | WEEK_FIRST_WEEKDAY | INCLUDE_CRITICAL_WEEK, 52, 2013);
  T_WEEK_CHECK_YEAR(32, 6, 2014, START_WITH_SUNDAY | WEEK_FIRST_WEEKDAY | INCLUDE_CRITICAL_WEEK, 4, 2014);
  T_WEEK_CHECK_YEAR(365, 2, 2013, START_WITH_SUNDAY | WEEK_FIRST_WEEKDAY | INCLUDE_CRITICAL_WEEK, 52, 2013);
  //'%v'
  T_WEEK_CHECK_YEAR(1, 3, 2014, INCLUDE_CRITICAL_WEEK, 1, 2014);
  T_WEEK_CHECK_YEAR(365, 2, 2013, INCLUDE_CRITICAL_WEEK, 1, 2014);
  T_WEEK_CHECK_YEAR(1, 0, 2012, INCLUDE_CRITICAL_WEEK, 52, 2011);
  T_WEEK_CHECK_YEAR(32, 6, 2014, INCLUDE_CRITICAL_WEEK, 5, 2014);
  T_WEEK_CHECK_YEAR(365, 0, 2023, INCLUDE_CRITICAL_WEEK, 52, 2023);
}

TEST(ObTimeUtilityTest, extract_usec_test2)
{
  int64_t pos = 0;
  int64_t usec = 0;
  ASSERT_EQ(OB_SUCCESS,
            ObTimeUtility2::extract_usec(
                ObString::make_string("123"), pos, usec, ObTimeUtility2::DIGTS_INSENSITIVE));
  ASSERT_EQ(123000, usec);
}

TEST(ObTimeUtilityTest, extract_date_test)
{
  int64_t pos = 0;
  int64_t date = 0;
  ASSERT_EQ(OB_SUCCESS, ObTimeUtility2::extract_date(ObString::make_string("123"), 0, pos, date));
  ASSERT_EQ(123, date);
}

TEST(ObTimeUtilityTest, is_valid_date_test)
{
  ASSERT_TRUE(ObTimeUtility2::is_valid_date(2013, 12, 23));
  ASSERT_FALSE(ObTimeUtility2::is_valid_date(2013, 2, 29));
  ASSERT_TRUE(ObTimeUtility2::is_valid_date(2012, 2, 29));
  ASSERT_FALSE(ObTimeUtility2::is_valid_date(1900, 2, 29));
  ASSERT_FALSE(ObTimeUtility2::is_valid_date(2013, 1, 32));
  ASSERT_TRUE(ObTimeUtility2::is_valid_date(2013, 3, 31));
  ASSERT_TRUE(ObTimeUtility2::is_valid_date(2013, 5, 31));
  ASSERT_TRUE(ObTimeUtility2::is_valid_date(2013, 7, 31));
  ASSERT_TRUE(ObTimeUtility2::is_valid_date(2013, 8, 31));
  ASSERT_TRUE(ObTimeUtility2::is_valid_date(2013, 10, 31));
  ASSERT_TRUE(ObTimeUtility2::is_valid_date(2013, 12, 31));
  ASSERT_FALSE(ObTimeUtility2::is_valid_date(2013, 4, 31));
  ASSERT_FALSE(ObTimeUtility2::is_valid_date(2013, 6, 31));
  ASSERT_FALSE(ObTimeUtility2::is_valid_date(2013, 9, 31));
  ASSERT_FALSE(ObTimeUtility2::is_valid_date(2013, 11, 31));
}
