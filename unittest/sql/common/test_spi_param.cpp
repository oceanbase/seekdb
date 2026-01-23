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

#define USING_LOG_PREFIX SQL

#include <gtest/gtest.h>
#include <cstring>
#include "sql/ob_spi_param.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
using namespace common;
using namespace sql;

namespace test
{

class ObSPIParamTest : public ::testing::Test
{
public:
  ObSPIParamTest() : allocator_(ObModIds::TEST) {}
  virtual ~ObSPIParamTest() {}

protected:
  ObArenaAllocator allocator_;
};

TEST_F(ObSPIParamTest, FactoryAndGetters)
{
  ObSPIParam null_param = ObSPIParam::null();
  EXPECT_TRUE(null_param.is_null());
  EXPECT_EQ(ObNullType, null_param.get_type());
  EXPECT_EQ(ObSPIParam::SPI_PARAM_IN, null_param.get_mode());
  null_param.set_mode(ObSPIParam::SPI_PARAM_OUT);
  EXPECT_EQ(ObSPIParam::SPI_PARAM_OUT, null_param.get_mode());

  ObSPIParam int_param = ObSPIParam::from_int(123);
  EXPECT_TRUE(int_param.get_obj_param().is_integer_type());
  EXPECT_EQ(123, int_param.get_obj_param().get_int());

  ObSPIParam uint_param = ObSPIParam::from_uint(42);
  EXPECT_TRUE(uint_param.get_obj_param().is_uint64());
  EXPECT_EQ(42U, uint_param.get_obj_param().get_uint64());

  ObSPIParam float_param = ObSPIParam::from_float(1.25f);
  EXPECT_TRUE(float_param.get_obj_param().is_float());
  EXPECT_FLOAT_EQ(1.25f, float_param.get_obj_param().get_float());

  ObSPIParam double_param = ObSPIParam::from_double(3.5);
  EXPECT_TRUE(double_param.get_obj_param().is_double());
  EXPECT_DOUBLE_EQ(3.5, double_param.get_obj_param().get_double());

  ObSPIParam dt_param = ObSPIParam::from_datetime(1000);
  EXPECT_TRUE(dt_param.get_obj_param().is_datetime());
  EXPECT_EQ(1000, dt_param.get_obj_param().get_datetime());

  ObSPIParam ts_param = ObSPIParam::from_timestamp(2000);
  EXPECT_TRUE(ts_param.get_obj_param().is_timestamp());
  EXPECT_EQ(2000, ts_param.get_obj_param().get_timestamp());

  ObSPIParam date_param = ObSPIParam::from_date(10);
  EXPECT_TRUE(date_param.get_obj_param().is_date());
  EXPECT_EQ(10, date_param.get_obj_param().get_date());

  ObSPIParam time_param = ObSPIParam::from_time(20);
  EXPECT_TRUE(time_param.get_obj_param().is_time());
  EXPECT_EQ(20, time_param.get_obj_param().get_time());

  ObSPIParam str_param = ObSPIParam::from_string("abc");
  EXPECT_FALSE(str_param.is_null());
  EXPECT_TRUE(str_param.get_obj_param().is_string_type());
  EXPECT_EQ(ObString::make_string("abc"), str_param.get_obj_param().get_string());
  EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, str_param.get_obj_param().get_collation_type());

  ObSPIParam null_str = ObSPIParam::from_string(static_cast<const char*>(NULL));
  EXPECT_TRUE(null_str.is_null());
}

TEST_F(ObSPIParamTest, VarcharAndBlobEdgeCases)
{
  ObSPIParam empty_varchar = ObSPIParam::from_varchar("abc", 0);
  EXPECT_FALSE(empty_varchar.is_null());
  EXPECT_EQ(0, empty_varchar.get_obj_param().get_string().length());
  EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, empty_varchar.get_obj_param().get_collation_type());

  ObSPIParam null_varchar = ObSPIParam::from_varchar(NULL, 5);
  EXPECT_TRUE(null_varchar.is_null());

  ObSPIParam neg_varchar = ObSPIParam::from_varchar("abc", -1);
  EXPECT_TRUE(neg_varchar.is_null());

  const char blob_data[] = {1, 2, 3};
  ObSPIParam blob_param = ObSPIParam::from_blob(blob_data, sizeof(blob_data));
  EXPECT_TRUE(blob_param.get_obj_param().is_lob());
  EXPECT_EQ(ObLongTextType, blob_param.get_type());
  ObString blob_str = blob_param.get_obj_param().get_string();
  EXPECT_EQ(sizeof(blob_data), blob_str.length());
  EXPECT_EQ(0, memcmp(blob_str.ptr(), blob_data, sizeof(blob_data)));
  EXPECT_EQ(CS_TYPE_BINARY, blob_param.get_obj_param().get_collation_type());

  ObSPIParam empty_blob = ObSPIParam::from_blob("", 0);
  EXPECT_FALSE(empty_blob.is_null());
  EXPECT_TRUE(empty_blob.get_obj_param().is_lob());
  EXPECT_EQ(0, empty_blob.get_obj_param().get_string().length());

  ObSPIParam null_blob = ObSPIParam::from_blob(NULL, 0);
  EXPECT_TRUE(null_blob.is_null());
  ObSPIParam neg_blob = ObSPIParam::from_blob(blob_data, -1);
  EXPECT_TRUE(neg_blob.is_null());
}

TEST_F(ObSPIParamTest, ParamListAddAndDeepCopy)
{
  ObSPIParamList params(allocator_);
  params.add_int(10).add_double(2.5).add_null();
  EXPECT_EQ(3, params.count());
  EXPECT_EQ(10, params.at(0).get_obj_param().get_int());
  EXPECT_DOUBLE_EQ(2.5, params.at(1).get_obj_param().get_double());
  EXPECT_TRUE(params.at(2).is_null());

  char buf[] = "abc";
  params.add_string(buf);
  buf[0] = 'x';
  ObString stored_str = params.at(3).get_obj_param().get_string();
  EXPECT_EQ(ObString::make_string("abc"), stored_str);
  EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, params.at(3).get_obj_param().get_collation_type());

  ObString src = ObString::make_string("hello");
  params.add_string(src);
  ObString stored_src = params.at(4).get_obj_param().get_string();
  EXPECT_EQ(ObString::make_string("hello"), stored_src);

  char blob_buf[] = {1, 2, 3};
  params.add_blob(blob_buf, sizeof(blob_buf));
  blob_buf[0] = 9;
  ObString stored_blob = params.at(5).get_obj_param().get_string();
  EXPECT_EQ(sizeof(blob_buf), stored_blob.length());
  EXPECT_EQ(0, memcmp(stored_blob.ptr(), "\x01\x02\x03", sizeof(blob_buf)));
  EXPECT_EQ(CS_TYPE_BINARY, params.at(5).get_obj_param().get_collation_type());

  char raw_buf[] = "xyz";
  ObSPIParam raw_param = ObSPIParam::from_string(raw_buf);
  params.add_param(raw_param);
  raw_buf[0] = 'a';
  ObString stored_raw = params.at(6).get_obj_param().get_string();
  EXPECT_EQ(ObString::make_string("xyz"), stored_raw);

  ObArenaAllocator other_allocator(ObModIds::TEST);
  number::ObNumber num;
  ASSERT_EQ(OB_SUCCESS, num.from(static_cast<int64_t>(12345), other_allocator));
  params.add_number(num);
  EXPECT_EQ(OB_SUCCESS, params.get_last_error());
  number::ObNumber stored_num = params.at(7).get_obj_param().get_number();
  EXPECT_EQ(0, stored_num.compare(num));
}

TEST_F(ObSPIParamTest, BatchAndParamStore)
{
  ObSPIParamList batch_list(allocator_);
  ObSEArray<ObSPIParam, 4> batch_params;
  ASSERT_EQ(OB_SUCCESS, batch_params.push_back(ObSPIParam::from_int(1)));
  ASSERT_EQ(OB_SUCCESS, batch_params.push_back(ObSPIParam::from_string("batch")));
  ASSERT_EQ(OB_SUCCESS, batch_list.add_batch(batch_params));
  EXPECT_EQ(2, batch_list.count());

  ObSPIParamList params(allocator_);
  params.add_int(7).add_string("abc").add_null();
  ParamStore param_store{ObWrapperAllocator(&allocator_)};
  ASSERT_EQ(OB_SUCCESS, params.to_param_store(param_store));
  ASSERT_EQ(3, param_store.count());
  EXPECT_EQ(7, param_store.at(0).get_int());
  EXPECT_EQ(ObString::make_string("abc"), param_store.at(1).get_string());
  EXPECT_TRUE(param_store.at(2).is_null());

  int64_t int_val = 0;
  ObSPIParam int_param = ObSPIParam::from_int(9);
  EXPECT_EQ(OB_SUCCESS, int_param.get_result_int(int_val));
  EXPECT_EQ(9, int_val);

  ObSPIParam null_param = ObSPIParam::null();
  EXPECT_EQ(OB_ERR_NULL_VALUE, null_param.get_result_int(int_val));

  ObSPIParam str_param = ObSPIParam::from_string("hello");
  ObString out_str;
  EXPECT_EQ(OB_SUCCESS, str_param.get_result_string(out_str));
  EXPECT_EQ(ObString::make_string("hello"), out_str);
  EXPECT_EQ(OB_ERR_UNEXPECTED, str_param.get_result_int(int_val));
}

} // namespace test
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
