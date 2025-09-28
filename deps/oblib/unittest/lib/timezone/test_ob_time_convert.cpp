/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include <gtest/gtest.h>
#include "lib/timezone/ob_timezone_info.h"

using namespace oceanbase;
using namespace oceanbase::common;

class ObTimeConvertTest : public ::testing::Test
{
public:
  ObTimeConvertTest();
  virtual ~ObTimeConvertTest();
  virtual void SetUp();
  virtual void TearDown();
private:
  // disallow copy
  ObTimeConvertTest(const ObTimeConvertTest &other);
  ObTimeConvertTest& operator=(const ObTimeConvertTest &other);
};



static bool ob_time_eq(const ObTime& ans, int64_t year, int64_t month, int64_t day,
                       int64_t hour, int64_t minute, int64_t second, int64_t usecond)
{
  bool ret = (ans.parts_[DT_YEAR] == year || -1 == year)
              && (ans.parts_[DT_MON] == month || -1 == month)
              && (ans.parts_[DT_MDAY] == day || -1 == day)
              && (ans.parts_[DT_HOUR] == hour || -1 == hour)
              && (ans.parts_[DT_MIN] == minute || -1 == minute)
              && (ans.parts_[DT_SEC] == second || -1 == second)
              && (ans.parts_[DT_USEC] == usecond || -1 == usecond);
  if (!ret) {
    printf("%04u-%02u-%02u %02u:%02u:%02u.%06u\n",
           ans.parts_[DT_YEAR], ans.parts_[DT_MON], ans.parts_[DT_MDAY],
           ans.parts_[DT_HOUR], ans.parts_[DT_MIN], ans.parts_[DT_SEC], ans.parts_[DT_USEC]);
  }
  return ret;
}

static bool ob_interval_eq(const ObInterval& ans, int64_t year, int64_t month, int64_t day,
                           int64_t hour, int64_t minute, int64_t second, int64_t usecond)
{
  bool ret = (ans.parts_[DT_YEAR] == year || -1 == year)
              && (ans.parts_[DT_MON] == month || -1 == month)
              && (ans.parts_[DT_MDAY] == day || -1 == day)
              && (ans.parts_[DT_HOUR] == hour || -1 == hour)
              && (ans.parts_[DT_MIN] == minute || -1 == minute)
              && (ans.parts_[DT_SEC] == second || -1 == second)
              && (ans.parts_[DT_USEC] == usecond || -1 == usecond);
  if (!ret) {
    printf("%04u-%02u-%02u %02u:%02u:%02u.%06u\n",
           ans.parts_[DT_YEAR], ans.parts_[DT_MON], ans.parts_[DT_MIN],
           ans.parts_[DT_HOUR], ans.parts_[DT_MIN], ans.parts_[DT_SEC], ans.parts_[DT_USEC]);
  }
  return ret;
}

int64_t interval_value(uint32_t day, uint32_t hour, uint32_t minute, uint32_t second, uint32_t usecond)
{
  int64_t value = day;
  value *= HOURS_PER_DAY;
  value += hour;
  value *= MINS_PER_HOUR;
  value += minute;
  value *= SECS_PER_MIN;
  value += second;
  value *= USECS_PER_SEC;
  value += usecond;
  return value;
}

TEST(ObTimeConvertTest, str_to_datetime)
{
//  ObTimezoneUtils zone;
  ObTimeZoneInfo tz_info;
  char buf[50] = {0};
  ObString str;
  int64_t value;

//  zone.init("/usr/share/zoneinfo/America/Chicago");
//  strcpy(buf, "1970-01-01 00:26:30");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 23190 * USECS_PER_SEC);
//  strcpy(buf, "2015-4-15 4:22:7");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 1429089727 * USECS_PER_SEC);
//
//  zone.parse_timezone_file("/usr/share/zoneinfo/America/Cordoba");
//  strcpy(buf, "2015-4-15 6.33.40.0123");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 1429090420 * USECS_PER_SEC + 12300);
//  strcpy(buf, "1971.3.28 12.46.30.123");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 39023190 * USECS_PER_SEC + 123000);
//
//  zone.parse_timezone_file("/usr/share/zoneinfo/Asia/Tehran");
//  strcpy(buf, "2015/4/15 14.5.41.04560");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 1429090541 * USECS_PER_SEC + 45600);
//  strcpy(buf, "1976/12/1 1.41.32.45600");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 218239892 * USECS_PER_SEC + 456000);
//
//  zone.parse_timezone_file("/usr/share/zoneinfo/Singapore");
//  strcpy(buf, "2015/4/15 17:37:26");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 1429090646 * USECS_PER_SEC);
//  strcpy(buf, "2005/6/29 13:45:38");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 1120023938 * USECS_PER_SEC);
//
//  // dst test
//  zone.parse_timezone_file("/usr/share/zoneinfo/America/New_York");
//  strcpy(buf, "2015/4/20 8:4:49");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 1429531489 * USECS_PER_SEC);
//  strcpy(buf, "2015/1/15 12:00:00");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, 1421341200 * USECS_PER_SEC);
//
//  // usec < 0
//  zone.parse_timezone_file("/usr/share/zoneinfo/UTC");
//  strcpy(buf, "1969/12/31 23:59:59");
//  str.assign(buf, static_cast<int32_t>(strlen(buf)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, zone.get_tz_ptr(), value));
//  EXPECT_EQ(value, -1 * USECS_PER_SEC);

  // timezone with offset only.
  ObTimeConvertCtx cvrt_ctx(&tz_info, true);
  strcpy(buf, "+8:00");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  tz_info.set_timezone(str);
  strcpy(buf, "1000-1-1 0:0:0");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, cvrt_ctx, value, nullptr, 0));
  EXPECT_EQ(value / USECS_PER_SEC, -30610252800);
  strcpy(buf, "-08:00");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  tz_info.set_timezone(str);
  strcpy(buf, "2015-7-3 11:12:0");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, cvrt_ctx, value, nullptr, 0));
  EXPECT_EQ(value / static_cast<int64_t>(USECS_PER_SEC), 1435950720);

  // NULL timezone
  cvrt_ctx.is_timestamp_ = false;
  strcpy(buf, "1000-1-1 0:0:0");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, cvrt_ctx, value, nullptr, 0));
  EXPECT_EQ(value / USECS_PER_SEC, -30610224000);
  strcpy(buf, "2015-7-3 11:12:0");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, cvrt_ctx, value, nullptr, 0));
  EXPECT_EQ(value / static_cast<int64_t>(USECS_PER_SEC), 1435921920);
  strcpy(buf, "9999-12-31 23:59:59");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, cvrt_ctx, value, nullptr, 0));
  EXPECT_EQ(value / static_cast<int64_t>(USECS_PER_SEC), 253402300799);
}



TEST(ObTimeConvertTest, str_to_otimestamp)
{
//  ObTimezoneUtils zone;
  ObTimeZoneInfo tz_info;
  char buf[50] = {0};
  ObString str;
  int64_t value = 0;
  ObOTimestampData ot_data;
  int16_t scale = 0;

  ObTime ob_time(DT_TYPE_ORACLE_TIMESTAMP);
  ObTimeConvertCtx cvrt_ctx(&tz_info, false);
  strcpy(buf, "-8:00");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  tz_info.set_timezone(str);

  // NULL timezone
  cvrt_ctx.is_timestamp_ = false;
  cvrt_ctx.oracle_nls_format_ = ObTimeConverter::COMPAT_OLD_NLS_DATE_FORMAT;
  strcpy(buf, "1000-1-1 0:0:0");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, cvrt_ctx, value, nullptr, 0));
  EXPECT_EQ(value, -30610224000 * USECS_PER_SEC);
  strcpy(buf, "9999-12-31 23:59:59");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_datetime(str, cvrt_ctx, value, nullptr, 0));
  EXPECT_EQ(value, 253402300799 * static_cast<int64_t>(USECS_PER_SEC));
}

#define STR_TO_DATE_SUCC(str, day) \
  do { \
    buf.assign_ptr(str, static_cast<int32_t>(strlen(str))); \
    EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_date(str, value, 0)); \
    EXPECT_EQ(value, 0); \
  } while (0)

TEST(ObTimeConvertTest, str_to_date)
{
  char buf[50] = {0};
  ObString str;
  int32_t value;


  strcpy(buf, "1970-1-1");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_date(str, value, 0));
  EXPECT_EQ(value, 0);
  strcpy(buf, "2015-4-15");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_date(str, value, 0));
  EXPECT_EQ(value, 16540);
  strcpy(buf, "1969-12-31");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_date(str, value, 0));
  EXPECT_EQ(value, -1);
  strcpy(buf, "1000-1-1");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_date(str, value, 0));
  EXPECT_EQ(value, -354285);
  strcpy(buf, "9999-12-31");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_date(str, value, 0));
  EXPECT_EQ(value, 2932896);
}

#define STR_TO_INTERVAL_SUCC(str, unit, interval) \
  do { \
    buf.assign_ptr(str, static_cast<int32_t>(strlen(str))); \
    EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_interval(buf, unit, value)); \
    EXPECT_EQ(value, interval); \
  } while (0)

#define STR_TO_INTERVAL_FAIL(str, unit) \
  do { \
    buf.assign_ptr(str, static_cast<int32_t>(strlen(str))); \
    EXPECT_NE(OB_SUCCESS, ObTimeConverter::str_to_interval(buf, unit, value)); \
  } while (0)

TEST(ObTimeConvertTest, str_to_interval)
{
  ObString buf;
  int64_t value;
  // normal case.
  STR_TO_INTERVAL_SUCC("1", DATE_UNIT_MICROSECOND, interval_value(0, 0, 0, 0, 1));
  STR_TO_INTERVAL_SUCC("1", DATE_UNIT_SECOND,      interval_value(0, 0, 0, 1, 0));
  STR_TO_INTERVAL_SUCC("1", DATE_UNIT_MINUTE,      interval_value(0, 0, 1, 0, 0));
  STR_TO_INTERVAL_SUCC("1", DATE_UNIT_HOUR,        interval_value(0, 1, 0, 0, 0));
  STR_TO_INTERVAL_SUCC("1", DATE_UNIT_DAY,         interval_value(1, 0, 0, 0, 0));
  STR_TO_INTERVAL_SUCC("1", DATE_UNIT_WEEK,        interval_value(7, 0, 0, 0, 0));
  STR_TO_INTERVAL_FAIL("1", DATE_UNIT_MONTH);
  STR_TO_INTERVAL_FAIL("1", DATE_UNIT_QUARTER);
  STR_TO_INTERVAL_FAIL("1", DATE_UNIT_YEAR);
  STR_TO_INTERVAL_SUCC("1:2", DATE_UNIT_SECOND_MICROSECOND, interval_value(0, 0, 0, 1, 200000));
  STR_TO_INTERVAL_SUCC("1-2+3", DATE_UNIT_MINUTE_MICROSECOND, interval_value(0, 0, 1, 2, 300000));
  STR_TO_INTERVAL_SUCC("1_2", DATE_UNIT_MINUTE_SECOND, interval_value(0, 0, 1, 2, 0));
  STR_TO_INTERVAL_SUCC("1=2(3)4", DATE_UNIT_HOUR_MICROSECOND, interval_value(0, 1, 2, 3, 400000));
  STR_TO_INTERVAL_SUCC("1&2*3", DATE_UNIT_HOUR_SECOND, interval_value(0, 1, 2, 3, 0));
  STR_TO_INTERVAL_SUCC("1^2", DATE_UNIT_HOUR_MINUTE, interval_value(0, 1, 2, 0, 0));
  STR_TO_INTERVAL_SUCC("1@2#3$4%5", DATE_UNIT_DAY_MICROSECOND, interval_value(1, 2, 3, 4, 500000));
  STR_TO_INTERVAL_SUCC("1|2~3!4", DATE_UNIT_DAY_SECOND, interval_value(1, 2, 3, 4, 0));
  STR_TO_INTERVAL_SUCC("1`2;3", DATE_UNIT_DAY_MINUTE, interval_value(1, 2, 3, 0, 0));
  STR_TO_INTERVAL_SUCC("1?2", DATE_UNIT_DAY_HOUR, interval_value(1, 2, 0, 0, 0));
  STR_TO_INTERVAL_FAIL("1/2", DATE_UNIT_YEAR_MONTH);
  // mutli-delimiters or letters or spaces.
  STR_TO_INTERVAL_SUCC("1he ,.2<llo>3{} wo4[rld]5", DATE_UNIT_DAY_MICROSECOND, interval_value(1, 2, 3, 4, 500000));
}

TEST(ObTimeConvertTest, str_to_time)
{
  char buf[50] = {0};
  ObString str;
  int64_t value;

  strcpy(buf, "12:5:9.01234");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_time(str, value));
  EXPECT_EQ(value, 43509 * static_cast<int64_t>(USECS_PER_SEC) + 12340);
  strcpy(buf, "123:0:59");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_time(str, value));
  EXPECT_EQ(value, 442859 * static_cast<int64_t>(USECS_PER_SEC));
  strcpy(buf, "-12:5:9.01234");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_time(str, value));
  EXPECT_EQ(value, -43509 * static_cast<int64_t>(USECS_PER_SEC) - 12340);
  strcpy(buf, "-123:0:59");
  str.assign(buf, static_cast<int32_t>(strlen(buf)));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_time(str, value));
  EXPECT_EQ(value, -442859 * static_cast<int64_t>(USECS_PER_SEC));
}

TEST(ObTimeConvertTest, datetime_to_str)
{
//  ObTimezoneUtils zone;
  ObTimeZoneInfo tz_info;
  char buf[50] = {0};
  ObString str;
  int64_t value;
  str.assign_buffer(buf, 50);
  int64_t pos = 0;

//  zone.init("/usr/share/zoneinfo/America/Chicago");
//  value = 23190 * USECS_PER_SEC;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "1970-01-01 00:26:30"));
//  value = 1429089727 * USECS_PER_SEC;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "2015-04-15 04:22:07"));
//
//  zone.parse_timezone_file("/usr/share/zoneinfo/America/Cordoba");
//  value = 1429090420 * USECS_PER_SEC + 12300;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "2015-04-15 06:33:40.012300"));
//  value = 39023190 * USECS_PER_SEC + 123000;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "1971-03-28 12:46:30.123000"));
//
//  zone.parse_timezone_file("/usr/share/zoneinfo/Asia/Tehran");
//  value = 1429090541 * USECS_PER_SEC + 45600;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "2015-04-15 14:05:41.045600"));
//  value = 218239892 * USECS_PER_SEC + 456000;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "1976-12-01 01:41:32.456000"));
//
//  zone.parse_timezone_file("/usr/share/zoneinfo/Singapore");
//  value = 1429090646 * USECS_PER_SEC;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "2015-04-15 17:37:26"));
//  value = 1120023938 * USECS_PER_SEC;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "2005-06-29 13:45:38"));
//
//  // dst test
//  zone.parse_timezone_file("/usr/share/zoneinfo/America/New_York");
//  value = 1429531489 * USECS_PER_SEC;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "2015-04-20 08:04:49"));
//  value = 1421341200 * USECS_PER_SEC;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "2015-01-15 12:00:00"));
//
//  // usec < 0
//  zone.parse_timezone_file("/usr/share/zoneinfo/UTC");
//  value = -1 * USECS_PER_SEC;
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, zone.get_tz_ptr(), str));
//  EXPECT_TRUE(0 == strcmp(str.ptr(), "1969-12-31 23:59:59"));

  // timezone with offset only.
  strcpy(buf, "+8:00");
  str.set_length(static_cast<int32_t>(strlen(buf)));
  tz_info.set_timezone(str);
  value = -30610252800 * USECS_PER_SEC;
  const ObDataTypeCastParams dtc_params(&tz_info);
  const ObString nls_format;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, &tz_info, nls_format, 0, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "1000-01-01 00:00:00"));
  pos = 0;
  strcpy(buf, "-8:00");
  str.set_length(static_cast<int32_t>(strlen(buf)));
  tz_info.set_timezone(str);
  value = 1435950720 * static_cast<int64_t>(USECS_PER_SEC);
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, &tz_info, nls_format, 0, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "2015-07-03 11:12:00"));
  pos = 0;

  const ObDataTypeCastParams dtc_params2(NULL);
  // NULL timezone
  value = -30610224000 * USECS_PER_SEC;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, NULL, nls_format, 0, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "1000-01-01 00:00:00"));
  pos = 0;
  value = 253402300799 * static_cast<int64_t>(USECS_PER_SEC);
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_str(value, NULL, nls_format, 0, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "9999-12-31 23:59:59"));
  pos = 0;
}

TEST(ObTimeConvertTest, date_to_str)
{
  char buf[50] = {0};
  ObString str;
  int32_t value;
  str.assign_buffer(buf, 50);
  int64_t pos = 0;

  value = 0;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::date_to_str(value, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "1970-01-01"));
  pos = 0;
  value = 16540;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::date_to_str(value, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "2015-04-15"));
  pos = 0;
  value = -1;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::date_to_str(value, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(str.ptr(), "1969-12-31"));
  pos = 0;
  value = -354285;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::date_to_str(value, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "1000-01-01"));
  pos = 0;
  value = 2932896;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::date_to_str(value, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "9999-12-31"));
  pos = 0;
}

TEST(ObTimeConvertTest, time_to_str)
{
  char buf[50] = {0};
  ObString str;
  int64_t value;
  str.assign_buffer(buf, 50);
  int64_t pos = 0;

  value = 43509 * static_cast<int64_t>(USECS_PER_SEC) + 12340;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::time_to_str(value, 6, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "12:05:09.012340"));
  pos = 0;
  memset(buf, 0, sizeof(buf));
  value = 442859 * static_cast<int64_t>(USECS_PER_SEC);
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::time_to_str(value, 0, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "123:00:59"));
  pos = 0;
  memset(buf, 0, sizeof(buf));
  value = -43509 * static_cast<int64_t>(USECS_PER_SEC) - 12340;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::time_to_str(value, 6, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "-12:05:09.012340"));
  pos = 0;
  memset(buf, 0, sizeof(buf));
  value = -442859 * static_cast<int64_t>(USECS_PER_SEC);
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::time_to_str(value, 0, buf, sizeof(buf), pos));
  EXPECT_TRUE(0 == strcmp(buf, "-123:00:59"));
  pos = 0;
}

void int_to_week_succ(int64_t int64, int64_t mode, int32_t res_week, int64_t line)
{
  int ret = OB_SUCCESS;
  int32_t value = 0;
  if (OB_FAIL(ObTimeConverter::int_to_week(int64, mode, value))) {
    EXPECT_EQ(0, line);
  } else if (res_week != value) {
    EXPECT_EQ(0, line);
  }
}

TEST(ObTimeConvertTest, int_to_week)
{
  int_to_week_succ(10000101, 0, 0, __LINE__);
  int_to_week_succ(10000101, 1, 1, __LINE__);
  int_to_week_succ(10000101, 2, 52, __LINE__);
  int_to_week_succ(10000101, 3, 1, __LINE__);
  int_to_week_succ(10000101, 4, 1, __LINE__);
  int_to_week_succ(10000101, 5, 0, __LINE__);
  int_to_week_succ(10000101, 6, 1, __LINE__);
  int_to_week_succ(10000101, 7, 52, __LINE__);
  int_to_week_succ(10000103, 0, 0, __LINE__);
  int_to_week_succ(10000103, 1, 1, __LINE__);
  int_to_week_succ(10000103, 2, 52, __LINE__);
  int_to_week_succ(10000103, 3, 1, __LINE__);
  int_to_week_succ(10000103, 4, 1, __LINE__);
  int_to_week_succ(10000103, 5, 0, __LINE__);
  int_to_week_succ(10000103, 6, 1, __LINE__);
  int_to_week_succ(10000103, 7, 52, __LINE__);
  int_to_week_succ(10000105, 0, 1, __LINE__);
  int_to_week_succ(10000105, 1, 1, __LINE__);
  int_to_week_succ(10000105, 2, 1, __LINE__);
  int_to_week_succ(10000105, 3, 1, __LINE__);
  int_to_week_succ(10000105, 4, 2, __LINE__);
  int_to_week_succ(10000105, 5, 0, __LINE__);
  int_to_week_succ(10000105, 6, 2, __LINE__);
  int_to_week_succ(10000105, 7, 52, __LINE__);
  int_to_week_succ(10000107, 0, 1, __LINE__);
  int_to_week_succ(10000107, 1, 2, __LINE__);
  int_to_week_succ(10000107, 2, 1, __LINE__);
  int_to_week_succ(10000107, 3, 2, __LINE__);
  int_to_week_succ(10000107, 4, 2, __LINE__);
  int_to_week_succ(10000107, 5, 1, __LINE__);
  int_to_week_succ(10000107, 6, 2, __LINE__);
  int_to_week_succ(10000107, 7, 1, __LINE__);
  int_to_week_succ(10001231, 0, 52, __LINE__);
  int_to_week_succ(10001231, 1, 53, __LINE__);
  int_to_week_succ(10001231, 2, 52, __LINE__);
  int_to_week_succ(10001231, 3, 1, __LINE__);
  int_to_week_succ(10001231, 4, 53, __LINE__);
  int_to_week_succ(10001231, 5, 52, __LINE__);
  int_to_week_succ(10001231, 6, 53, __LINE__);
  int_to_week_succ(10001231, 7, 52, __LINE__);
  int_to_week_succ(15000101, 0, 0, __LINE__);
  int_to_week_succ(15000101, 1, 1, __LINE__);
  int_to_week_succ(15000101, 2, 53, __LINE__);
  int_to_week_succ(15000101, 3, 1, __LINE__);
  int_to_week_succ(15000101, 4, 1, __LINE__);
  int_to_week_succ(15000101, 5, 1, __LINE__);
  int_to_week_succ(15000101, 6, 1, __LINE__);
  int_to_week_succ(15000101, 7, 1, __LINE__);
  int_to_week_succ(15000103, 0, 0, __LINE__);
  int_to_week_succ(15000103, 1, 1, __LINE__);
  int_to_week_succ(15000103, 2, 53, __LINE__);
  int_to_week_succ(15000103, 3, 1, __LINE__);
  int_to_week_succ(15000103, 4, 1, __LINE__);
  int_to_week_succ(15000103, 5, 1, __LINE__);
  int_to_week_succ(15000103, 6, 1, __LINE__);
  int_to_week_succ(15000103, 7, 1, __LINE__);
  int_to_week_succ(15000105, 0, 0, __LINE__);
  int_to_week_succ(15000105, 1, 1, __LINE__);
  int_to_week_succ(15000105, 2, 53, __LINE__);
  int_to_week_succ(15000105, 3, 1, __LINE__);
  int_to_week_succ(15000105, 4, 1, __LINE__);
  int_to_week_succ(15000105, 5, 1, __LINE__);
  int_to_week_succ(15000105, 6, 1, __LINE__);
  int_to_week_succ(15000105, 7, 1, __LINE__);
  int_to_week_succ(15000107, 0, 1, __LINE__);
  int_to_week_succ(15000107, 1, 1, __LINE__);
  int_to_week_succ(15000107, 2, 1, __LINE__);
  int_to_week_succ(15000107, 3, 1, __LINE__);
  int_to_week_succ(15000107, 4, 2, __LINE__);
  int_to_week_succ(15000107, 5, 1, __LINE__);
  int_to_week_succ(15000107, 6, 2, __LINE__);
  int_to_week_succ(15000107, 7, 1, __LINE__);
  int_to_week_succ(15001231, 0, 52, __LINE__);
  int_to_week_succ(15001231, 1, 53, __LINE__);
  int_to_week_succ(15001231, 2, 52, __LINE__);
  int_to_week_succ(15001231, 3, 1, __LINE__);
  int_to_week_succ(15001231, 4, 53, __LINE__);
  int_to_week_succ(15001231, 5, 53, __LINE__);
  int_to_week_succ(15001231, 6, 1, __LINE__);
  int_to_week_succ(15001231, 7, 53, __LINE__);
  int_to_week_succ(20000101, 0, 0, __LINE__);
  int_to_week_succ(20000101, 1, 0, __LINE__);
  int_to_week_succ(20000101, 2, 52, __LINE__);
  int_to_week_succ(20000101, 3, 52, __LINE__);
  int_to_week_succ(20000101, 4, 0, __LINE__);
  int_to_week_succ(20000101, 5, 0, __LINE__);
  int_to_week_succ(20000101, 6, 52, __LINE__);
  int_to_week_succ(20000101, 7, 52, __LINE__);
  int_to_week_succ(20000103, 0, 1, __LINE__);
  int_to_week_succ(20000103, 1, 1, __LINE__);
  int_to_week_succ(20000103, 2, 1, __LINE__);
  int_to_week_succ(20000103, 3, 1, __LINE__);
  int_to_week_succ(20000103, 4, 1, __LINE__);
  int_to_week_succ(20000103, 5, 1, __LINE__);
  int_to_week_succ(20000103, 6, 1, __LINE__);
  int_to_week_succ(20000103, 7, 1, __LINE__);
  int_to_week_succ(20000105, 0, 1, __LINE__);
  int_to_week_succ(20000105, 1, 1, __LINE__);
  int_to_week_succ(20000105, 2, 1, __LINE__);
  int_to_week_succ(20000105, 3, 1, __LINE__);
  int_to_week_succ(20000105, 4, 1, __LINE__);
  int_to_week_succ(20000105, 5, 1, __LINE__);
  int_to_week_succ(20000105, 6, 1, __LINE__);
  int_to_week_succ(20000105, 7, 1, __LINE__);
  int_to_week_succ(20000107, 0, 1, __LINE__);
  int_to_week_succ(20000107, 1, 1, __LINE__);
  int_to_week_succ(20000107, 2, 1, __LINE__);
  int_to_week_succ(20000107, 3, 1, __LINE__);
  int_to_week_succ(20000107, 4, 1, __LINE__);
  int_to_week_succ(20000107, 5, 1, __LINE__);
  int_to_week_succ(20000107, 6, 1, __LINE__);
  int_to_week_succ(20000107, 7, 1, __LINE__);
  int_to_week_succ(20001231, 0, 53, __LINE__);
  int_to_week_succ(20001231, 1, 52, __LINE__);
  int_to_week_succ(20001231, 2, 53, __LINE__);
  int_to_week_succ(20001231, 3, 52, __LINE__);
  int_to_week_succ(20001231, 4, 53, __LINE__);
  int_to_week_succ(20001231, 5, 52, __LINE__);
  int_to_week_succ(20001231, 6, 1, __LINE__);
  int_to_week_succ(20001231, 7, 52, __LINE__);
}

#define INT_TO_OB_TIME_SUCC(int_val, mode, year, month, day, hour, minute, second, usecond) \
  do { \
    memset(&ob_time, 0, sizeof(ob_time)); \
    ob_time.mode_ = mode; \
    if (DT_TYPE_DATE & mode) { \
      EXPECT_EQ(OB_SUCCESS, ObTimeConverter::int_to_ob_time_with_date(int_val, ob_time, 0)); \
    } else { \
      EXPECT_EQ(OB_SUCCESS, ObTimeConverter::int_to_ob_time_without_date(int_val, ob_time)); \
    } \
    EXPECT_TRUE(ob_time_eq(ob_time, year, month, day, hour, minute, second, usecond)); \
  } while (0)

#define INT_TO_OB_TIME_FAIL(int_val, mode) \
  do { \
    memset(&ob_time, 0, sizeof(ob_time)); \
    ob_time.mode_ = mode; \
    if (DT_TYPE_DATE & mode) { \
      EXPECT_NE(OB_SUCCESS, ObTimeConverter::int_to_ob_time_with_date(int_val, ob_time, 0)); \
    } else { \
      EXPECT_NE(OB_SUCCESS, ObTimeConverter::int_to_ob_time_without_date(int_val, ob_time)); \
    } \
  } while (0)

TEST(ObTimeConvertTest, int_to_ob_time)
{
  ObTime ob_time;
  // datetime.
  INT_TO_OB_TIME_FAIL(23, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_SUCC(213, DT_TYPE_DATETIME, 2000, 2, 13, 0, 0, 0, 0);
  INT_TO_OB_TIME_FAIL(2113, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_SUCC(21113, DT_TYPE_DATETIME, 2002, 11, 13, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(211113, DT_TYPE_DATETIME, 2021, 11, 13, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(2111113, DT_TYPE_DATETIME, 211, 11, 13, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(21111113, DT_TYPE_DATETIME, 2111, 11, 13, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(211111113, DT_TYPE_DATETIME, 2000, 2, 11, 11, 11, 13, 0);
  INT_TO_OB_TIME_FAIL(2111111113, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_SUCC(21111111113, DT_TYPE_DATETIME, 2002, 11, 11, 11, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(211111111113, DT_TYPE_DATETIME, 2021, 11, 11, 11, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(2111111111113, DT_TYPE_DATETIME, 211, 11, 11, 11, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(21111111111113, DT_TYPE_DATETIME, 2111, 11, 11, 11, 11, 13, 0);
  INT_TO_OB_TIME_FAIL(211111111111113, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_FAIL(2111111111111113, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_FAIL(21111111111111113, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_FAIL(211111111111111113, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_FAIL(9223372036854775807, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_FAIL(10, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_SUCC(101, DT_TYPE_DATETIME, 2000, 1, 1, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(1011, DT_TYPE_DATETIME, 2000, 10, 11, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(10111, DT_TYPE_DATETIME, 2001, 1, 11, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(101112, DT_TYPE_DATETIME, 2010, 11, 12, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(1011121, DT_TYPE_DATETIME, 101, 11, 21, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(10111213, DT_TYPE_DATETIME, 1011, 12, 13, 0, 0, 0, 0);
  INT_TO_OB_TIME_SUCC(101112131, DT_TYPE_DATETIME, 2000, 1, 1, 11, 21, 31, 0);
  INT_TO_OB_TIME_SUCC(1011121314, DT_TYPE_DATETIME, 2000, 10, 11, 12, 13, 14, 0);
  INT_TO_OB_TIME_SUCC(10111213141, DT_TYPE_DATETIME, 2001, 1, 11, 21, 31, 41, 0);
  INT_TO_OB_TIME_SUCC(101112131415, DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  INT_TO_OB_TIME_FAIL(1011121314151, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_SUCC(10111213141516, DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  INT_TO_OB_TIME_FAIL(101112131415161, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_FAIL(1011121314151617, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_FAIL(10111213141516171, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_FAIL(101112131415161718, DT_TYPE_DATETIME);
  INT_TO_OB_TIME_FAIL(9223372036854775807, DT_TYPE_DATETIME);
  // date.
  INT_TO_OB_TIME_FAIL(23, DT_TYPE_DATE);
  INT_TO_OB_TIME_SUCC(213, DT_TYPE_DATE, 2000, 2, 13, -1, -1, -1, -1);
  INT_TO_OB_TIME_FAIL(2113, DT_TYPE_DATE);
  INT_TO_OB_TIME_SUCC(21113, DT_TYPE_DATE, 2002, 11, 13, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(211113, DT_TYPE_DATE, 2021, 11, 13, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(2111113, DT_TYPE_DATE, 211, 11, 13, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(21111113, DT_TYPE_DATE, 2111, 11, 13, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(211111113, DT_TYPE_DATE, 2000, 2, 11, -1, -1, -1, -1);
  INT_TO_OB_TIME_FAIL(2111111113, DT_TYPE_DATE);
  INT_TO_OB_TIME_SUCC(21111111113, DT_TYPE_DATE, 2002, 11, 11, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(211111111113, DT_TYPE_DATE, 2021, 11, 11, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(2111111111113, DT_TYPE_DATE, 211, 11, 11, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(21111111111113, DT_TYPE_DATE, 2111, 11, 11, -1, -1, -1, -1);
  INT_TO_OB_TIME_FAIL(211111111111113, DT_TYPE_DATE);
  INT_TO_OB_TIME_FAIL(2111111111111113, DT_TYPE_DATE);
  INT_TO_OB_TIME_FAIL(21111111111111113, DT_TYPE_DATE);
  INT_TO_OB_TIME_FAIL(211111111111111113, DT_TYPE_DATE);
  INT_TO_OB_TIME_FAIL(9223372036854775807, DT_TYPE_DATE);
  INT_TO_OB_TIME_FAIL(10, DT_TYPE_DATE);
  INT_TO_OB_TIME_SUCC(101, DT_TYPE_DATE, 2000, 1, 1, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(1011, DT_TYPE_DATE, 2000, 10, 11, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(10111, DT_TYPE_DATE, 2001, 1, 11, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(101112, DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(1011121, DT_TYPE_DATE, 101, 11, 21, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(10111213, DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(101112131, DT_TYPE_DATE, 2000, 1, 1, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(1011121314, DT_TYPE_DATE, 2000, 10, 11, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(10111213141, DT_TYPE_DATE, 2001, 1, 11, -1, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(101112131415, DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  INT_TO_OB_TIME_FAIL(1011121314151, DT_TYPE_DATE);
  INT_TO_OB_TIME_SUCC(10111213141516, DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  INT_TO_OB_TIME_FAIL(101112131415161, DT_TYPE_DATE);
  INT_TO_OB_TIME_FAIL(1011121314151617, DT_TYPE_DATE);
  INT_TO_OB_TIME_FAIL(10111213141516171, DT_TYPE_DATE);
  INT_TO_OB_TIME_FAIL(101112131415161718, DT_TYPE_DATE);
  INT_TO_OB_TIME_FAIL(9223372036854775807, DT_TYPE_DATE);
  // time.
  INT_TO_OB_TIME_SUCC(23, DT_TYPE_TIME, -1, -1, -1, 0, 0, 23, 0);
  INT_TO_OB_TIME_SUCC(213, DT_TYPE_TIME, -1, -1, -1, 0, 2, 13, 0);
  INT_TO_OB_TIME_SUCC(2113, DT_TYPE_TIME, -1, -1, -1, 0, 21, 13, 0);
  INT_TO_OB_TIME_SUCC(21113, DT_TYPE_TIME, -1, -1, -1, 2, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(211113, DT_TYPE_TIME, -1, -1, -1, 21, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(2111113, DT_TYPE_TIME, -1, -1, -1, 211, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(21111113, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(211111113, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(2111111113, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(21111111113, DT_TYPE_TIME, -1, -1, -1, 11, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(211111111113, DT_TYPE_TIME, -1, -1, -1, 11, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(2111111111113, DT_TYPE_TIME, -1, -1, -1, 11, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(21111111111113, DT_TYPE_TIME, -1, -1, -1, 11, 11, 13, 0);
  INT_TO_OB_TIME_SUCC(211111111111113, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(2111111111111113, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(21111111111111113, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(211111111111111113, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(9223372036854775807, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(10, DT_TYPE_TIME, -1, -1, -1, 0, 0, 10, 0);
  INT_TO_OB_TIME_SUCC(101, DT_TYPE_TIME, -1, -1, -1, 0, 1, 1, 0);
  INT_TO_OB_TIME_SUCC(1011, DT_TYPE_TIME, -1, -1, -1, 0, 10, 11, 0);
  INT_TO_OB_TIME_SUCC(10111, DT_TYPE_TIME, -1, -1, -1, 1, 1, 11, 0);
  INT_TO_OB_TIME_SUCC(101112, DT_TYPE_TIME, -1, -1, -1, 10, 11, 12, 0);
  INT_TO_OB_TIME_SUCC(1011121, DT_TYPE_TIME, -1, -1, -1, 101, 11, 21, 0);
  INT_TO_OB_TIME_SUCC(10111213, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(101112131, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(1011121314, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(10111213141, DT_TYPE_TIME, -1, -1, -1, 21, 31, 41, 0);
  INT_TO_OB_TIME_SUCC(101112131415, DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  INT_TO_OB_TIME_FAIL(1011121314151, DT_TYPE_TIME);
  INT_TO_OB_TIME_SUCC(10111213141516, DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  INT_TO_OB_TIME_SUCC(101112131415161, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(1011121314151617, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(10111213141516171, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(101112131415161718, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  INT_TO_OB_TIME_SUCC(9223372036854775807, DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
}

#define STR_TO_OB_TIME_SUCC(str, mode, year, month, day, hour, minute, second, usecond) \
  do { \
    buf.assign_ptr(str, static_cast<int32_t>(strlen(str))); \
    memset(&ob_time, 0, sizeof(ob_time)); \
    ob_time.mode_ = mode; \
    if (DT_TYPE_TIME != mode) { \
      EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_ob_time_with_date(buf, ob_time, NULL, false, 0)); \
    } else { \
      EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_ob_time_without_date(buf, ob_time)); \
    } \
    EXPECT_TRUE(ob_time_eq(ob_time, year, month, day, hour, minute, second, usecond)); \
  } while (0)

#define STR_TO_OB_TIME_FAIL(str, mode) \
  do { \
    buf.assign_ptr(str, static_cast<int32_t>(strlen(str))); \
    memset(&ob_time, 0, sizeof(ob_time)); \
    ob_time.mode_ = mode; \
    if (DT_TYPE_TIME != mode) { \
      EXPECT_NE(OB_SUCCESS, ObTimeConverter::str_to_ob_time_with_date(buf, ob_time, NULL, false, 0)); \
    } else { \
      EXPECT_NE(OB_SUCCESS, ObTimeConverter::str_to_ob_time_without_date(buf, ob_time)); \
    } \
  } while (0)

TEST(ObTimeConvertTest, str_to_ob_time)
{
  ObString buf;
  ObTime ob_time;
  // datetime.
  STR_TO_OB_TIME_SUCC("2015-05-30 11:12:13.1415", DT_TYPE_DATETIME, 2015, 5, 30, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015.05.30 11.12.13.1415", DT_TYPE_DATETIME, 2015, 5, 30, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015/05/30 11/12/13.1415", DT_TYPE_DATETIME, 2015, 5, 30, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015+05=30 11*12&13.1415", DT_TYPE_DATETIME, 2015, 5, 30, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015^05%30 11$12#13.1415", DT_TYPE_DATETIME, 2015, 5, 30, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015@05!30 11~12`13.1415", DT_TYPE_DATETIME, 2015, 5, 30, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("0000-05-30 11:12:13.1415", DT_TYPE_DATETIME, 0, 5, 30, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("00002015-00000005-00000030 11:12:13.1415", DT_TYPE_DATETIME, 2015, 5, 30, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("00000015-00000005-00000030 11:12:13.1415", DT_TYPE_DATETIME, 15, 5, 30, 11, 12, 13, 141500);
  STR_TO_OB_TIME_FAIL("00000015.00000005.00000030 11:12:13.1415", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("10000-05-30 11:12:13.1415", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("10000.05.30 11:12:13.1415", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("2015-13-30 11:12:13.1415", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("2015-05-32 11:12:13.1415", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("2015-05-30 24:12:13.1415", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("2015-05-30 11:60:13.1415", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("2015-05-30 11:12:60.1415", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("2015-12-31 23:59:59.9999999", DT_TYPE_DATETIME, 2016, 1, 1, 00, 00, 00, 000000);
  STR_TO_OB_TIME_FAIL("10", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("101", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1011", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("10111", DT_TYPE_DATETIME, 2010, 11, 1, 0, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("101112", DT_TYPE_DATETIME, 2010, 11, 12, 0, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("1011121", DT_TYPE_DATETIME, 2010, 11, 12, 1, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("10111213", DT_TYPE_DATETIME, 1011, 12, 13, 0, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("101112131", DT_TYPE_DATETIME, 2010, 11, 12, 13, 1, 0, 0);
  STR_TO_OB_TIME_SUCC("1011121314", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 0, 0);
  STR_TO_OB_TIME_SUCC("10111213141", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 1, 0);
  STR_TO_OB_TIME_SUCC("101112131415", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("1011121314151", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("10111213141516", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1011121314151617", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("10111213141516171", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_FAIL("10", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("10.1", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("10.11", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1011.1", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1011.12", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("101112.1", DT_TYPE_DATETIME, 2010, 11, 12, 1, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("101112.13", DT_TYPE_DATETIME, 2010, 11, 12, 13, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("10111213.1", DT_TYPE_DATETIME, 1011, 12, 13, 1, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("10111213.14", DT_TYPE_DATETIME, 1011, 12, 13, 14, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("1011121314.1", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 1, 0);
  STR_TO_OB_TIME_SUCC("1011121314.15", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415.1", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 100000);
  STR_TO_OB_TIME_SUCC("101112131415.16", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 160000);
  STR_TO_OB_TIME_SUCC("101112131415.16.17", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 160000);
  STR_TO_OB_TIME_SUCC("101112131415.16-17", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 160000);
  STR_TO_OB_TIME_SUCC("101112131415..16", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415..16.17", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415..16-17", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415.-.16", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415.-.16.17", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415.-.16-17", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_FAIL("101112131415-16", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("101112131415-16.17", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("101112131415-16-17", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("101112131415.161111.17", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 161111);
  STR_TO_OB_TIME_SUCC("101112131415.1611112.17", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 161111);
  STR_TO_OB_TIME_SUCC("10111213141516.1", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 100000);
  STR_TO_OB_TIME_SUCC("10111213141516.17", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 170000);
  STR_TO_OB_TIME_SUCC("1011121314151617.1", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1011121314151617.18", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161.718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("10111213141516.1718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 171800);
  STR_TO_OB_TIME_SUCC("1011121314151.61718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415.161718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 161718);
  STR_TO_OB_TIME_SUCC("10111213141.5161718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 1, 516172);
  STR_TO_OB_TIME_SUCC("1011121314.15161718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131.415161718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 1, 41, 0);
  STR_TO_OB_TIME_SUCC("10111213.1415161718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1011121.31415161718", DT_TYPE_DATETIME, 2010, 11, 12, 1, 31, 41, 0);
  STR_TO_OB_TIME_SUCC("101112.131415161718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("10111.2131415161718", DT_TYPE_DATETIME, 2010, 11, 1, 21, 31, 41, 0);
  STR_TO_OB_TIME_SUCC("1011.12131415161718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101.112131415161718", DT_TYPE_DATETIME, 2010, 1, 11, 21, 31, 41, 0);
  STR_TO_OB_TIME_SUCC("10.1112131415161718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("10.11121314151617.18", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101.112131415161.718", DT_TYPE_DATETIME, 2010, 1, 11, 21, 31, 41, 0);
  STR_TO_OB_TIME_SUCC("1011.1213141516.1718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 171800);
  STR_TO_OB_TIME_SUCC("10111.21314151.61718", DT_TYPE_DATETIME, 2010, 11, 1, 21, 31, 41, 0);
  STR_TO_OB_TIME_SUCC("101112.131415.161718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 161718);
  STR_TO_OB_TIME_SUCC("1011121.3141.5161718", DT_TYPE_DATETIME, 2010, 11, 12, 1, 31, 41, 516172);
  STR_TO_OB_TIME_SUCC("10111213.14.15161718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415.1.61718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 100000);
  STR_TO_OB_TIME_SUCC("101112131415.16.1718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 160000);
  STR_TO_OB_TIME_SUCC("101112131415.161.718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 161000);
  STR_TO_OB_TIME_SUCC("10111213141516.1.718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 100000);
  STR_TO_OB_TIME_SUCC("10111213141516.17.18", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 170000);
  STR_TO_OB_TIME_SUCC("10111213141516.171.8", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 171000);
  STR_TO_OB_TIME_SUCC("10..1112131415161718", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101..112131415161718", DT_TYPE_DATETIME, 2010, 1, 11, 21, 31, 41, 0);
  STR_TO_OB_TIME_SUCC("1011..12131415161718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("10111213141516..1718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161..718", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1011121314151617..18", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("2012.010112345", DT_TYPE_DATETIME, 2012, 1, 1, 12, 34, 5, 0);
  STR_TO_OB_TIME_FAIL("2012+010112345", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("2012.01=0112345", DT_TYPE_DATETIME, 2012, 1, 1, 12, 34, 5, 0);
  STR_TO_OB_TIME_FAIL("2012*01=0112345", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("2012.01=011*2345", DT_TYPE_DATETIME, 2012, 1, 1, 1, 23, 45, 0);
  STR_TO_OB_TIME_FAIL("2012!01=011*2345", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("2012.*01^011&2345", DT_TYPE_DATETIME, 2012, 1, 1, 1, 23, 45, 0);
  STR_TO_OB_TIME_FAIL("2012*.01^011&2345", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("2012**01^011&2345", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("201201.0112345", DT_TYPE_DATETIME, 2020, 12, 1, 1, 12, 34, 0);
  STR_TO_OB_TIME_FAIL("201201%0112345", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("201201.01#12345", DT_TYPE_DATETIME, 2020, 12, 1, 1, 12, 34, 0);
  STR_TO_OB_TIME_FAIL("201201%01#12345", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("201201.01@12!345", DT_TYPE_DATETIME, 2020, 12, 1, 1, 12, 34, 0);
  STR_TO_OB_TIME_FAIL("201201%01@12!345", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("2012.01%01@12!34:5", DT_TYPE_DATETIME, 2012, 1, 1, 12, 34, 5, 0);
  STR_TO_OB_TIME_SUCC("2012;01%01@12!34:5", DT_TYPE_DATETIME, 2012, 1, 1, 12, 34, 5, 0);
  STR_TO_OB_TIME_SUCC("201-2-1 1-12-34", DT_TYPE_DATETIME, 201, 2, 1, 1, 12, 34, 0);
  STR_TO_OB_TIME_FAIL("201-0201 1-12-34", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("201. 0201 1-12-34", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("201.2 1 1-12 34", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("123:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1234:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12345:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("123456:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1234567:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12345678:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("123456789:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1234567890:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1011131314151617", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1011123214151617", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("10.55.55", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("101.55.55", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1011.55.55", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("10111.55.55", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("101112.55.55", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("1011121.55.55", DT_TYPE_DATETIME, 2010, 11, 12, 1, 55, 55, 0);
  STR_TO_OB_TIME_FAIL("10111213.55.55", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("101112131.55.55", DT_TYPE_DATETIME, 2010, 11, 12, 13, 1, 55, 550000);
  STR_TO_OB_TIME_SUCC("1011121314.55.55", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 55, 550000);
  STR_TO_OB_TIME_SUCC("10111213141.55.55", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 1, 550000);
  STR_TO_OB_TIME_SUCC("101112131415.22.22", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 220000);
  STR_TO_OB_TIME_SUCC("101112131415.55.55", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 550000);
  STR_TO_OB_TIME_SUCC("1011121314151.55.55", DT_TYPE_DATETIME, 2010, 11, 12, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("10111213141516.55.55", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 550000);
  STR_TO_OB_TIME_SUCC("101112131415161.55.55", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1011121314151617.55.55", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("10111213141516171.55.55", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161718.55.55", DT_TYPE_DATETIME, 1011, 12, 13, 14, 15, 16, 0);
  STR_TO_OB_TIME_FAIL("1", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("123", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1234", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12345", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("123456", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1123456", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12+3456", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:3456", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1234=56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("1234:56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12!34@56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34@56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12!34:56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34:56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34::56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34:=56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34+:56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34: 56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34 :56", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34:56..789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34:56:.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34:56.:789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34:56 .789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34:56. 789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("12:34:61.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 12:34:56.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("200 : 59 : 59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("200 :59 :59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("200: 59: 59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11: 12:34:56.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11. 12:34:56.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 :12:34:56.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 .12:34:56.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 12:3456.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 12+3456.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 1234:56.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 1234=56.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 1234.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 12:34.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 12.789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 12 .789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("11 12. 789", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_SUCC("1000-01-01 00:00:00", DT_TYPE_DATETIME, 1000, 1, 1, 0, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("1969-12-31 23:59:59", DT_TYPE_DATETIME, 1969, 12, 31, 23, 59, 59, 0);
  STR_TO_OB_TIME_SUCC("1970-01-01 00:00:00", DT_TYPE_DATETIME, 1970, 1, 1, 0, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("9999-12-31 23:59:59", DT_TYPE_DATETIME, 9999, 12, 31, 23, 59, 59, 0);
  STR_TO_OB_TIME_FAIL("838:59:59", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("838:59:59.000001", DT_TYPE_DATETIME);
  STR_TO_OB_TIME_FAIL("840:00:00", DT_TYPE_DATETIME);
  // date.
  STR_TO_OB_TIME_SUCC("2015-05-30 11:12:13.1415", DT_TYPE_DATE, 2015, 5, 30, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("2015.05.30 11.12.13.1415", DT_TYPE_DATE, 2015, 5, 30, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("2015/05/30 11/12/13.1415", DT_TYPE_DATE, 2015, 5, 30, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("2015+05=30 11*12&13.1415", DT_TYPE_DATE, 2015, 5, 30, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("2015^05%30 11$12#13.1415", DT_TYPE_DATE, 2015, 5, 30, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("2015@05!30 11~12`13.1415", DT_TYPE_DATE, 2015, 5, 30, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("0000-05-30 11:12:13.1415", DT_TYPE_DATE, 0, 5, 30, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("00002015-00000005-00000030 11:12:13.1415", DT_TYPE_DATE, 2015, 5, 30, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("00000015-00000005-00000030 11:12:13.1415", DT_TYPE_DATE, 15, 5, 30, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("00000015.00000005.00000030 11:12:13.1415", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("10000-05-30 11:12:13.1415", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("10000.05.30 11:12:13.1415", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("2015-13-30 11:12:13.1415", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("2015-05-32 11:12:13.1415", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("2015-05-30 24:12:13.1415", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("2015-05-30 11:60:13.1415", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("2015-05-30 11:12:60.1415", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("2015-12-31 23:59:59.9999999", DT_TYPE_DATE, 2016, 1, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("10", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("101", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1011", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("10111", DT_TYPE_DATE, 2010, 11, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314151", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415161", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314151617", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516171", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415161718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("10", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("10.1", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("10.11", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1011.1", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1011.12", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("101112.1", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112.13", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213.1", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213.14", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314.1", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314.15", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.1", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.16", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.16.17", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.16-17", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415..16", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415..16.17", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415..16-17", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.-.16", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.-.16.17", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.-.16-17", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("101112131415-16", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("101112131415-16.17", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("101112131415-16-17", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("101112131415.161111.17", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.1611112.17", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516.1", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516.17", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314151617.1", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314151617.18", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415161.718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516.1718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314151.61718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141.5161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314.15161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131.415161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213.1415161718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121.31415161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112.131415161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111.2131415161718", DT_TYPE_DATE, 2010, 11, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011.12131415161718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101.112131415161718", DT_TYPE_DATE, 2010, 1, 11, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10.1112131415161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10.11121314151617.18", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101.112131415161.718", DT_TYPE_DATE, 2010, 1, 11, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011.1213141516.1718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111.21314151.61718", DT_TYPE_DATE, 2010, 11, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112.131415.161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121.3141.5161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213.14.15161718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.1.61718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.16.1718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.161.718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516.1.718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516.17.18", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516.171.8", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10..1112131415161718", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101..112131415161718", DT_TYPE_DATE, 2010, 1, 11, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011..12131415161718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516..1718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415161..718", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314151617..18", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("2012.010112345", DT_TYPE_DATE, 2012, 1, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("2012+010112345", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("2012.01=0112345", DT_TYPE_DATE, 2012, 1, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("2012*01=0112345", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("2012.01=011*2345", DT_TYPE_DATE, 2012, 1, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("2012!01=011*2345", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("2012.*01^011&2345", DT_TYPE_DATE, 2012, 1, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("2012*.01^011&2345", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("2012**01^011&2345", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("201201.0112345", DT_TYPE_DATE, 2020, 12, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("201201%0112345", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("201201.01#12345", DT_TYPE_DATE, 2020, 12, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("201201%01#12345", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("201201.01@12!345", DT_TYPE_DATE, 2020, 12, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("201201%01@12!345", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("2012.01%01@12!34:5", DT_TYPE_DATE, 2012, 1, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("2012;01%01@12!34:5", DT_TYPE_DATE, 2012, 1, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("201-2-1 1-12-34", DT_TYPE_DATE, 201, 2, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("201-0201 1-12-34", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("201. 0201 1-12-34", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("201.2 1 1-12 34", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("123:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1234:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12345:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("123456:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1234567:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12345678:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("123456789:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1234567890:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1011131314151617", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1011123214151617", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("10.55.55", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("101.55.55", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1011.55.55", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("10111.55.55", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("101112.55.55", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("1011121.55.55", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("10111213.55.55", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("101112131.55.55", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314.55.55", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141.55.55", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.22.22", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.55.55", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314151.55.55", DT_TYPE_DATE, 2010, 11, 12, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516.55.55", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415161.55.55", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314151617.55.55", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213141516171.55.55", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415161718.55.55", DT_TYPE_DATE, 1011, 12, 13, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("1", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("123", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1234", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12345", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("123456", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1123456", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12+3456", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:3456", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1234=56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("1234:56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12!34@56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34@56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12!34:56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34:56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34::56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34:=56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34+:56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34: 56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34 :56", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34:56..789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34:56:.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34:56.:789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34:56 .789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34:56. 789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("12:34:61.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 12:34:56.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("200 : 59 : 59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("200 :59 :59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("200: 59: 59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11: 12:34:56.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11. 12:34:56.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 :12:34:56.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 .12:34:56.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 12:3456.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 12+3456.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 1234:56.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 1234=56.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 1234.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 12:34.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 12.789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 12 .789", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("11 12. 789", DT_TYPE_DATE);
  STR_TO_OB_TIME_SUCC("1000-01-01 00:00:00", DT_TYPE_DATE, 1000, 1, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1969-12-31 23:59:59", DT_TYPE_DATE, 1969, 12, 31, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1970-01-01 00:00:00", DT_TYPE_DATE, 1970, 1, 1, -1, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("9999-12-31 23:59:59", DT_TYPE_DATE, 9999, 12, 31, -1, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("838:59:59", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("838:59:59.000001", DT_TYPE_DATE);
  STR_TO_OB_TIME_FAIL("840:00:00", DT_TYPE_DATE);
  // time.
  STR_TO_OB_TIME_SUCC("2015-05-30 11:12:13.1415", DT_TYPE_TIME, -1, -1, -1, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015.05.30 11.12.13.1415", DT_TYPE_TIME, -1, -1, -1, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015/05/30 11/12/13.1415", DT_TYPE_TIME, -1, -1, -1, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015+05=30 11*12&13.1415", DT_TYPE_TIME, -1, -1, -1, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015^05%30 11$12#13.1415", DT_TYPE_TIME, -1, -1, -1, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("2015@05!30 11~12`13.1415", DT_TYPE_TIME, -1, -1, -1, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("0000-05-30 11:12:13.1415", DT_TYPE_TIME, -1, -1, -1, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("00002015-00000005-00000030 11:12:13.1415", DT_TYPE_TIME, -1, -1, -1, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("00000015-00000005-00000030 11:12:13.1415", DT_TYPE_TIME, -1, -1, -1, 11, 12, 13, 141500);
  STR_TO_OB_TIME_SUCC("00000015.00000005.00000030 11:12:13.1415", DT_TYPE_TIME, -1, -1, -1, 0, 0, 15, 0);
  STR_TO_OB_TIME_FAIL("10000-05-30 11:12:13.1415", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("10000.05.30 11:12:13.1415", DT_TYPE_TIME, -1, -1, -1, 1, 0, 0, 50000);
  STR_TO_OB_TIME_FAIL("2015-13-30 11:12:13.1415", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("2015-05-32 11:12:13.1415", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("2015-05-30 24:12:13.1415", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("2015-05-30 11:60:13.1415", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("2015-05-30 11:12:60.1415", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("2015-12-31 23:59:59.9999999", DT_TYPE_TIME, -1, -1, -1, 0, 0, 0, 000000);
  STR_TO_OB_TIME_SUCC("10", DT_TYPE_TIME, -1, -1, -1, 0, 0, 10, 0);
  STR_TO_OB_TIME_SUCC("101", DT_TYPE_TIME, -1, -1, -1, 0, 1, 1, 0);
  STR_TO_OB_TIME_SUCC("1011", DT_TYPE_TIME, -1, -1, -1, 0, 10, 11, 0);
  STR_TO_OB_TIME_SUCC("10111", DT_TYPE_TIME, -1, -1, -1, 1, 1, 11, 0);
  STR_TO_OB_TIME_SUCC("101112", DT_TYPE_TIME, -1, -1, -1, 10, 11, 12, 0);
  STR_TO_OB_TIME_SUCC("1011121", DT_TYPE_TIME, -1, -1, -1, 101, 11, 21, 0);
  STR_TO_OB_TIME_SUCC("10111213", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("10111213141", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("101112131415", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("1011121314151", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("10111213141516", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1011121314151617", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("10111213141516171", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161718", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("10", DT_TYPE_TIME, -1, -1, -1, 0, 0, 10, 0);
  STR_TO_OB_TIME_SUCC("10.1", DT_TYPE_TIME, -1, -1, -1, 0, 0, 10, 100000);
  STR_TO_OB_TIME_SUCC("10.11", DT_TYPE_TIME, -1, -1, -1, 0, 0, 10, 110000);
  STR_TO_OB_TIME_SUCC("1011.1", DT_TYPE_TIME, -1, -1, -1, 0, 10, 11, 100000);
  STR_TO_OB_TIME_SUCC("1011.12", DT_TYPE_TIME, -1, -1, -1, 0, 10, 11, 120000);
  STR_TO_OB_TIME_SUCC("101112.1", DT_TYPE_TIME, -1, -1, -1, 10, 11, 12, 100000);
  STR_TO_OB_TIME_SUCC("101112.13", DT_TYPE_TIME, -1, -1, -1, 10, 11, 12, 130000);
  STR_TO_OB_TIME_SUCC("10111213.1", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213.14", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314.1", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314.15", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131415.1", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 100000);
  STR_TO_OB_TIME_SUCC("101112131415.16", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 160000);
  STR_TO_OB_TIME_FAIL("101112131415.16.17", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("101112131415.16-17", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("101112131415..16", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415..16.17", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415..16-17", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415.-.16", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415.-.16.17", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415.-.16-17", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_FAIL("101112131415-16", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("101112131415-16.17", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("101112131415-16-17", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("101112131415.161111.17", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("101112131415.1611112.17", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 161111);
  STR_TO_OB_TIME_SUCC("10111213141516.1", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 100000);
  STR_TO_OB_TIME_SUCC("10111213141516.17", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 170000);
  STR_TO_OB_TIME_SUCC("1011121314151617.1", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1011121314151617.18", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161.718", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("10111213141516.1718", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 171800);
  STR_TO_OB_TIME_SUCC("1011121314151.61718", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_SUCC("101112131415.161718", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 161718);
  STR_TO_OB_TIME_SUCC("10111213141.5161718", DT_TYPE_TIME, -1, -1, -1, 13, 14, 1, 516172);
  STR_TO_OB_TIME_SUCC("1011121314.15161718", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131.415161718", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("10111213.1415161718", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121.31415161718", DT_TYPE_TIME, -1, -1, -1, 101, 11, 21, 314152);
  STR_TO_OB_TIME_SUCC("101112.131415161718", DT_TYPE_TIME, -1, -1, -1, 10, 11, 12, 131415);
  STR_TO_OB_TIME_SUCC("10111.2131415161718", DT_TYPE_TIME, -1, -1, -1, 1, 1, 11, 213142);
  STR_TO_OB_TIME_SUCC("1011.12131415161718", DT_TYPE_TIME, -1, -1, -1, 0, 10, 11, 121314);
  STR_TO_OB_TIME_SUCC("101.112131415161718", DT_TYPE_TIME, -1, -1, -1, 0, 1, 1, 112131);
  STR_TO_OB_TIME_SUCC("10.1112131415161718", DT_TYPE_TIME, -1, -1, -1, 0, 0, 10, 111213);
  STR_TO_OB_TIME_SUCC("10.11121314151617.18", DT_TYPE_TIME, -1, -1, -1, 0, 0, 10, 111213);
  STR_TO_OB_TIME_SUCC("101.112131415161.718", DT_TYPE_TIME, -1, -1, -1, 0, 1, 1, 112131);
  STR_TO_OB_TIME_SUCC("1011.1213141516.1718", DT_TYPE_TIME, -1, -1, -1, 0, 10, 11, 121314);
  STR_TO_OB_TIME_SUCC("10111.21314151.61718", DT_TYPE_TIME, -1, -1, -1, 1, 1, 11, 213142);
  STR_TO_OB_TIME_SUCC("101112.131415.161718", DT_TYPE_TIME, -1, -1, -1, 10, 11, 12, 131415);
  STR_TO_OB_TIME_SUCC("1011121.3141.5161718", DT_TYPE_TIME, -1, -1, -1, 101, 11, 21, 314100);
  STR_TO_OB_TIME_SUCC("10111213.14.15161718", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("101112131415.1.61718", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("101112131415.16.1718", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("101112131415.161.718", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("10111213141516.1.718", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("10111213141516.17.18", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("10111213141516.171.8", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("10..1112131415161718", DT_TYPE_TIME, -1, -1, -1, 0, 0, 10, 0);
  STR_TO_OB_TIME_SUCC("101..112131415161718", DT_TYPE_TIME, -1, -1, -1, 0, 1, 1, 0);
  STR_TO_OB_TIME_SUCC("1011..12131415161718", DT_TYPE_TIME, -1, -1, -1, 0, 10, 11, 0);
  STR_TO_OB_TIME_SUCC("10111213141516..1718", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161..718", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1011121314151617..18", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("2012.010112345", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 10112);
  // STR_TO_OB_TIME_SUCC("2012+010112345", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 0);
  STR_TO_OB_TIME_SUCC("2012.01=0112345", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 10000);
  STR_TO_OB_TIME_SUCC("2012*01=0112345", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 0);
  STR_TO_OB_TIME_SUCC("2012.01=011*2345", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 10000);
  STR_TO_OB_TIME_SUCC("2012!01=011*2345", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 0);
  STR_TO_OB_TIME_SUCC("2012.*01^011&2345", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 0);
  STR_TO_OB_TIME_SUCC("2012*.01^011&2345", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 0);
  STR_TO_OB_TIME_SUCC("2012**01^011&2345", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 0);
  STR_TO_OB_TIME_SUCC("201201.0112345", DT_TYPE_TIME, -1, -1, -1, 20, 12, 1, 11235);
  //STR_TO_OB_TIME_SUCC("201201%0112345", DT_TYPE_TIME, -1, -1, -1, 20, 12, 1, 0);
  STR_TO_OB_TIME_SUCC("201201.01#12345", DT_TYPE_TIME, -1, -1, -1, 20, 12, 1, 10000);
  STR_TO_OB_TIME_SUCC("201201%01#12345", DT_TYPE_TIME, -1, -1, -1, 20, 12, 1, 0);
  STR_TO_OB_TIME_SUCC("201201.01@12!345", DT_TYPE_TIME, -1, -1, -1, 20, 12, 1, 10000);
  STR_TO_OB_TIME_SUCC("201201%01@12!345", DT_TYPE_TIME, -1, -1, -1, 20, 12, 1, 0);
  STR_TO_OB_TIME_SUCC("2012.01%01@12!34:5", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 10000);
  STR_TO_OB_TIME_SUCC("2012;01%01@12!34:5", DT_TYPE_TIME, -1, -1, -1, 0, 20, 12, 0);
  STR_TO_OB_TIME_SUCC("201-2-1 1-12-34", DT_TYPE_TIME, -1, -1, -1, 1, 12, 34, 0);
  STR_TO_OB_TIME_SUCC("201-0201 1-12-34", DT_TYPE_TIME, -1, -1, -1, 0, 2, 1, 0);
  STR_TO_OB_TIME_SUCC("201. 0201 1-12-34", DT_TYPE_TIME, -1, -1, -1, 0, 2, 1, 0);
  STR_TO_OB_TIME_SUCC("201.2 1 1-12 34", DT_TYPE_TIME, -1, -1, -1, 0, 2, 1, 200000);
  STR_TO_OB_TIME_SUCC("12:59:59", DT_TYPE_TIME, -1, -1, -1, 12, 59, 59, 0);
  STR_TO_OB_TIME_SUCC("123:59:59", DT_TYPE_TIME, -1, -1, -1, 123, 59, 59, 0);
  STR_TO_OB_TIME_SUCC("1234:59:59", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("12345:59:59", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("123456:59:59", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1234567:59:59", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("12345678:59:59", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("123456789:59:59", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1234567890:59:59", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("1011131314151617", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("1011123214151617", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("10.55.55", DT_TYPE_TIME, -1, -1, -1, 0, 0, 10, 550000);
  STR_TO_OB_TIME_SUCC("101.55.55", DT_TYPE_TIME, -1, -1, -1, 0, 1, 1, 550000);
  STR_TO_OB_TIME_SUCC("1011.55.55", DT_TYPE_TIME, -1, -1, -1, 0, 10, 11, 550000);
  STR_TO_OB_TIME_SUCC("10111.55.55", DT_TYPE_TIME, -1, -1, -1, 1, 1, 11, 550000);
  STR_TO_OB_TIME_SUCC("101112.55.55", DT_TYPE_TIME, -1, -1, -1, 10, 11, 12, 550000);
  STR_TO_OB_TIME_SUCC("1011121.55.55", DT_TYPE_TIME, -1, -1, -1, 101, 11, 21, 550000);
  STR_TO_OB_TIME_SUCC("10111213.55.55", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("101112131.55.55", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("1011121314.55.55", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_FAIL("10111213141.55.55", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("101112131415.22.22", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("101112131415.55.55", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("1011121314151.55.55", DT_TYPE_TIME, -1, -1, -1, 13, 14, 15, 0);
  STR_TO_OB_TIME_FAIL("10111213141516.55.55", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("101112131415161.55.55", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1011121314151617.55.55", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("10111213141516171.55.55", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("101112131415161718.55.55", DT_TYPE_TIME, -1, -1, -1, 14, 15, 16, 0);
  STR_TO_OB_TIME_SUCC("1", DT_TYPE_TIME, -1, -1, -1, 0, 0, 1, 0);
  STR_TO_OB_TIME_SUCC("12", DT_TYPE_TIME, -1, -1, -1, 0, 0, 12, 0);
  STR_TO_OB_TIME_SUCC("123", DT_TYPE_TIME, -1, -1, -1, 0, 1, 23, 0);
  STR_TO_OB_TIME_SUCC("1234", DT_TYPE_TIME, -1, -1, -1, 0, 12, 34, 0);
  STR_TO_OB_TIME_SUCC("12345", DT_TYPE_TIME, -1, -1, -1, 1, 23, 45, 0);
  STR_TO_OB_TIME_SUCC("123456", DT_TYPE_TIME, -1, -1, -1, 12, 34, 56, 0);
  STR_TO_OB_TIME_SUCC("1123456", DT_TYPE_TIME, -1, -1, -1, 112, 34, 56, 0);
  STR_TO_OB_TIME_SUCC("12+3456", DT_TYPE_TIME, -1, -1, -1, 0, 0, 12, 0);
  STR_TO_OB_TIME_FAIL("12:3456", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("1234=56", DT_TYPE_TIME, -1, -1, -1, 0, 12, 34, 0);
  STR_TO_OB_TIME_SUCC("1234:56", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("12!34@56", DT_TYPE_TIME, -1, -1, -1, 0, 0, 12, 0);
  STR_TO_OB_TIME_SUCC("12:34@56", DT_TYPE_TIME, -1, -1, -1, 12, 34, 0, 0);
  STR_TO_OB_TIME_SUCC("12!34:56", DT_TYPE_TIME, -1, -1, -1, 0, 0, 12, 0);
  STR_TO_OB_TIME_SUCC("12:34:56", DT_TYPE_TIME, -1, -1, -1, 12, 34, 56, 0);
  STR_TO_OB_TIME_SUCC("12:34::56", DT_TYPE_TIME, -1, -1, -1, 12, 34, 0, 0);
  STR_TO_OB_TIME_SUCC("12:34:=56", DT_TYPE_TIME, -1, -1, -1, 12, 34, 0, 0);
  STR_TO_OB_TIME_SUCC("12:34+:56", DT_TYPE_TIME, -1, -1, -1, 12, 34, 0, 0);
  STR_TO_OB_TIME_SUCC("12:34: 56", DT_TYPE_TIME, -1, -1, -1, 12, 34, 0, 0);
  STR_TO_OB_TIME_SUCC("12:34 :56", DT_TYPE_TIME, -1, -1, -1, 12, 34, 0, 0);
  STR_TO_OB_TIME_SUCC("12:34:56..789", DT_TYPE_TIME, -1, -1, -1, 12, 34, 56, 0);
  STR_TO_OB_TIME_SUCC("12:34:56:.789", DT_TYPE_TIME, -1, -1, -1, 12, 34, 56, 0);
  STR_TO_OB_TIME_SUCC("12:34:56.:789", DT_TYPE_TIME, -1, -1, -1, 12, 34, 56, 0);
  STR_TO_OB_TIME_FAIL("12:34:56 .789", DT_TYPE_TIME);
  STR_TO_OB_TIME_FAIL("12:34:56. 789", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("12:34.789", DT_TYPE_TIME, -1, -1, -1, 12, 34, 0, 789000);
  STR_TO_OB_TIME_FAIL("12:34:61.789", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("11 12:34:56.789", DT_TYPE_TIME, -1, -1, -1, 276, 34, 56, 789000);
  STR_TO_OB_TIME_SUCC("200 : 59 : 59", DT_TYPE_TIME, -1, -1, -1, 0, 2, 0, 0);
  STR_TO_OB_TIME_SUCC("200 :59 :59", DT_TYPE_TIME, -1, -1, -1, 200, 59, 0, 0);
  STR_TO_OB_TIME_SUCC("200: 59: 59", DT_TYPE_TIME, -1, -1, -1, 0, 2, 0, 0);
  STR_TO_OB_TIME_SUCC("11: 12:34:56.789", DT_TYPE_TIME, -1, -1, -1, 0, 0, 11, 0);
  STR_TO_OB_TIME_SUCC("11. 12:34:56.789", DT_TYPE_TIME, -1, -1, -1, 0, 0, 11, 0);
  STR_TO_OB_TIME_SUCC("11 :12:34:56.789", DT_TYPE_TIME, -1, -1, -1, 11, 12, 34, 0);
//  STR_TO_OB_TIME_SUCC("11 .12:34:56.789", DT_TYPE_TIME, -1, -1, -1, 0, 0, 11, 120000);
  STR_TO_OB_TIME_FAIL("11 12:3456.789", DT_TYPE_TIME);
  STR_TO_OB_TIME_SUCC("11 12+3456.789", DT_TYPE_TIME, -1, -1, -1, 276, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("11 1234:56.789", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("11 1234=56.789", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("11 1234.789", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
  STR_TO_OB_TIME_SUCC("11 12:34.789", DT_TYPE_TIME, -1, -1, -1, 276, 34, 0, 789000);
  STR_TO_OB_TIME_SUCC("11 12.789", DT_TYPE_TIME, -1, -1, -1, 276, 0, 0, 789000);
  STR_TO_OB_TIME_SUCC("11 12 .789", DT_TYPE_TIME, -1, -1, -1, 276, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("11 12. 789", DT_TYPE_TIME, -1, -1, -1, 276, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("1000-01-01 00:00:00", DT_TYPE_TIME, -1, -1, -1, 0, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("1969-12-31 23:59:59", DT_TYPE_TIME, -1, -1, -1, 23, 59, 59, 0);
  STR_TO_OB_TIME_SUCC("1970-01-01 00:00:00", DT_TYPE_TIME, -1, -1, -1, 0, 0, 0, 0);
  STR_TO_OB_TIME_SUCC("9999-12-31 23:59:59", DT_TYPE_TIME, -1, -1, -1, 23, 59, 59, 0);
  STR_TO_OB_TIME_SUCC("838:59:59", DT_TYPE_TIME, -1, -1, -1, 838, 59, 59, 0);
  STR_TO_OB_TIME_SUCC("838:59:59.000001", DT_TYPE_TIME, -1, -1, -1, 838, 59, 59, 1);
  STR_TO_OB_TIME_SUCC("840:00:00", DT_TYPE_TIME, -1, -1, -1, 839, -1, -1, -1);
}

//void STR_TO_OB_INTERVAL_SUCC(const char *str, ObDateUnitType unit,
//                             uint32_t year, uint32_t month, uint32_t day,
//                             uint32_t hour, uint32_t minute, uint32_t second, uint32_t usecond)
//{
//  ObString buf;
//  ObInterval ob_interval;
//  buf.assign_ptr(str, static_cast<int32_t>(strlen(str)));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_ob_interval(buf, unit, ob_interval));
//  EXPECT_TRUE(ob_interval_eq(ob_interval, year, month, day, hour, minute, second, usecond));
//}

#define STR_TO_OB_INTERVAL_SUCC(str, unit, year, month, day, hour, minute, second, usecond) \
  do { \
    buf.assign_ptr(str, static_cast<int32_t>(strlen(str))); \
    memset(&ob_interval, 0, sizeof(ob_interval)); \
    EXPECT_EQ(OB_SUCCESS, ObTimeConverter::str_to_ob_interval(buf, unit, ob_interval)); \
    EXPECT_TRUE(ob_interval_eq(ob_interval, year, month, day, hour, minute, second, usecond)); \
  } while (0)

TEST(ObTimeConvertTest, str_to_ob_interval)
{
  ObString buf;
  ObInterval ob_interval;
  // normal case.
  STR_TO_OB_INTERVAL_SUCC("1", DATE_UNIT_MICROSECOND, 0, 0, 0, 0, 0, 0, 1);
  STR_TO_OB_INTERVAL_SUCC("1", DATE_UNIT_SECOND,      0, 0, 0, 0, 0, 1, 0);
  STR_TO_OB_INTERVAL_SUCC("1", DATE_UNIT_MINUTE,      0, 0, 0, 0, 1, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1", DATE_UNIT_HOUR,        0, 0, 0, 1, 0, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1", DATE_UNIT_DAY,         0, 0, 1, 0, 0, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1", DATE_UNIT_WEEK,        0, 0, 7, 0, 0, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1", DATE_UNIT_MONTH,       0, 1, 0, 0, 0, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1", DATE_UNIT_QUARTER,     0, 3, 0, 0, 0, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1", DATE_UNIT_YEAR,        1, 0, 0, 0, 0, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1:2", DATE_UNIT_SECOND_MICROSECOND, 0, 0, 0, 0, 0, 1, 200000);
  STR_TO_OB_INTERVAL_SUCC("1-2+3", DATE_UNIT_MINUTE_MICROSECOND, 0, 0, 0, 0, 1, 2, 300000);
  STR_TO_OB_INTERVAL_SUCC("1_2", DATE_UNIT_MINUTE_SECOND, 0, 0, 0, 0, 1, 2, 0);
  STR_TO_OB_INTERVAL_SUCC("1=2(3)4", DATE_UNIT_HOUR_MICROSECOND, 0, 0, 0, 1, 2, 3, 400000);
  STR_TO_OB_INTERVAL_SUCC("1&2*3", DATE_UNIT_HOUR_SECOND, 0, 0, 0, 1, 2, 3, 0);
  STR_TO_OB_INTERVAL_SUCC("1^2", DATE_UNIT_HOUR_MINUTE, 0, 0, 0, 1, 2, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1@2#3$4%5", DATE_UNIT_DAY_MICROSECOND, 0, 0, 1, 2, 3, 4, 500000);
  STR_TO_OB_INTERVAL_SUCC("1|2~3!4", DATE_UNIT_DAY_SECOND, 0, 0, 1, 2, 3, 4, 0);
  STR_TO_OB_INTERVAL_SUCC("1`2;3", DATE_UNIT_DAY_MINUTE, 0, 0, 1, 2, 3, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1?2", DATE_UNIT_DAY_HOUR, 0, 0, 1, 2, 0, 0, 0);
  STR_TO_OB_INTERVAL_SUCC("1/2", DATE_UNIT_YEAR_MONTH, 1, 2, 0, 0, 0, 0, 0);
  // mutli-delimiters or letters or spaces.
  STR_TO_OB_INTERVAL_SUCC("1he ,.2<llo>3{} wo4[rld]5", DATE_UNIT_DAY_MICROSECOND, 0, 0, 1, 2, 3, 4, 500000);
}

//TEST(ObTimeConvertTest, datetime_to_ob_time)
//{
//  ObTimezoneUtils zone;
//  ObTime ob_time;
//
//  zone.init("/usr/share/zoneinfo/America/Chicago");
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(1429089727 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 2015, 4, 15, 4, 22, 7, 0));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(23190 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 1970, 1, 1, 0, 26, 30, 0));
//
//  zone.parse_timezone_file("/usr/share/zoneinfo/America/Cordoba");
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(1429090420 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 2015, 4, 15, 6, 33, 40, 0));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(39023190 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 1971, 3, 28, 12, 46, 30, 0));
//
//  zone.parse_timezone_file("/usr/share/zoneinfo/Asia/Tehran");
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(1429090541 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 2015, 4, 15, 14, 5, 41, 0));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(218239892 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 1976, 12, 1, 1, 41, 32, 0));
//
//  zone.parse_timezone_file("/usr/share/zoneinfo/Singapore");
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(1429090646 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 2015, 4, 15, 17, 37, 26, 0));
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(1120023938 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 2005, 6, 29, 13, 45, 38, 0));
//
//  // test dst time New York
//  zone.parse_timezone_file("/usr/share/zoneinfo/America/New_York");
//  //in dst
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(1429531489 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 2015, 4, 20, 8, 4, 49, 0));
//  // not in dst
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(1421341200 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 2015, 1, 15, 12, 0, 0, 0));
//
//  // usec < 0
//  zone.parse_timezone_file("/usr/share/zoneinfo/UTC");
//  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(-1 * USECS_PER_SEC, zone.get_tz_ptr(), ob_time));
//  EXPECT_TRUE(ob_time_eq(ob_time, 1969, 12, 31, 23, 59, 59, 0));
//}

TEST(ObTimeConvertTest, date_to_ob_time)
{
  ObTime ob_time;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::date_to_ob_time(0, ob_time));
  EXPECT_TRUE(ob_time_eq(ob_time, 1970, 1, 1, 0, 0, 0, 0));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::date_to_ob_time(16540, ob_time));
  EXPECT_TRUE(ob_time_eq(ob_time, 2015, 4, 15, 0, 0, 0, 0));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::date_to_ob_time(-1, ob_time));
  EXPECT_TRUE(ob_time_eq(ob_time, 1969, 12, 31, 0, 0, 0, 0));
}

TEST(ObTimeConvertTest, time_to_ob_time)
{
  ObTime ob_time;
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::time_to_ob_time(43509 * static_cast<int64_t>(USECS_PER_SEC) + 1234, ob_time));
  EXPECT_TRUE(ob_time_eq(ob_time, 0, 0, 0, 12, 5, 9, 1234));
  EXPECT_EQ(OB_SUCCESS, ObTimeConverter::time_to_ob_time(442859 * static_cast<int64_t>(USECS_PER_SEC), ob_time));
  EXPECT_TRUE(ob_time_eq(ob_time, 0, 0, 0, 123, 0, 59, 0));
  // TODO: -123:34:45
}

TEST(ObTimeConvertTest, scn_to_str)
{
  // timezone with offset only.
  ObString tz_str;
  ObTimeZoneInfo tz_info;
  char tz_buf[50] = {0};
  tz_str.assign_buffer(tz_buf, 50);
  strcpy(tz_buf, "+8:00");
  tz_str.set_length(static_cast<int32_t>(strlen(tz_buf)));
  tz_info.set_timezone(tz_str);

  const int64_t BUF_LEN = 100;
  char buf[BUF_LEN] = {0};
  int64_t pos = 0;
  uint64_t scn_val = 9223372036854775808UL;

  ASSERT_EQ(OB_INVALID_ARGUMENT, ObTimeConverter::scn_to_str(scn_val, NULL, buf, BUF_LEN, pos));
  ASSERT_EQ(OB_INVALID_ARGUMENT, ObTimeConverter::scn_to_str(scn_val, &tz_info, NULL, BUF_LEN, pos));
  ASSERT_EQ(OB_INVALID_ARGUMENT, ObTimeConverter::scn_to_str(scn_val, &tz_info, buf, 0, pos));

  ASSERT_EQ(OB_SUCCESS, ObTimeConverter::scn_to_str(scn_val, &tz_info, buf, BUF_LEN, pos));
  OB_LOG(INFO, "YYY +8:00", K(scn_val), K(tz_buf), K(buf));

  pos= 0;
  scn_val = 1687780338123456789;
  ASSERT_EQ(OB_SUCCESS, ObTimeConverter::scn_to_str(scn_val, &tz_info, buf, BUF_LEN, pos));
  OB_LOG(INFO, "YYY +8:00", K(scn_val), K(tz_buf), K(buf));
  printf("buf is \'%s\'\n", buf);
  ASSERT_TRUE(0 == strcmp(buf, "2023-06-26 19:52:18.123456789"));


  strcpy(tz_buf, "-08:00");
  tz_str.assign(tz_buf, static_cast<int32_t>(strlen(tz_buf)));
  tz_info.set_timezone(tz_str);

  pos= 0;
  ASSERT_EQ(OB_SUCCESS, ObTimeConverter::scn_to_str(scn_val, &tz_info, buf, BUF_LEN, pos));
  OB_LOG(INFO, "YYY -08:00", K(scn_val), K(tz_buf), K(buf));
  ASSERT_TRUE(0 == strcmp(buf, "2023-06-26 03:52:18.123456789"));

}

int main(int argc, char **argv)
{
  system("rm -f test_time_convert.log");
  OB_LOGGER.set_file_name("test_time_convert.log", true);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
