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
#include "lib/oblog/ob_base_log_writer.h"

//using namespace ::oblib;

namespace oceanbase
{
namespace common
{
class ObTLogItem : public ObIBaseLogItem
{
public:
  ObTLogItem() {}
  virtual ~ObTLogItem() {}
  virtual char *get_buf() { return a_; }
  virtual const char *get_buf() const { return NULL; }
  virtual int64_t get_buf_size() const { return 0; }
  virtual int64_t get_data_len() const { return 0; }
  char a_[16];
};

class ObTLogWriter : public ObBaseLogWriter
{
public:
  ObTLogWriter() : process_cnt_(0), notify_cnt_(0) {}
  virtual ~ObTLogWriter() {}
  void start_external() { has_stopped_ = false; }
  int flush_external(
      const int64_t max_batch_count,
      int64_t &processed_count,
      bool &has_more)
  {
    return flush_log_one_quantum(
        max_batch_count, processed_count, has_more);
  }
  int64_t process_cnt_;
  int64_t notify_cnt_;
protected:
  virtual void on_log_item_appended() override { ++notify_cnt_; }
  virtual void process_log_items(ObIBaseLogItem **items, const int64_t item_cnt, int64_t &finish_cnt);
};

void ObTLogWriter::process_log_items(ObIBaseLogItem **items, const int64_t item_cnt, int64_t &finish_cnt)
{
  if (NULL != items) {
    finish_cnt = item_cnt;
    process_cnt_ += item_cnt;
    ObTLogItem* a = (ObTLogItem*)(items[finish_cnt - 1]);
    (a->get_buf())[15] = '@';
  }
}

TEST(ObBaseLogWriter, normal)
{
  int ret = OB_SUCCESS;
  ObTLogWriter writer;
  ObBaseLogWriterCfg cfg;
  ObTLogItem log_item;
  int64_t process_cnt = 0;

  //invoke when not init
  ret = writer.append_log(log_item);
  ASSERT_NE(OB_SUCCESS, ret);

  //normal init
  cfg = ObBaseLogWriterCfg(512 << 10, 500000, 1, 4);
  ret = writer.init(cfg);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = writer.start();

  //repeat init
  ret = writer.init(cfg);
  ASSERT_NE(OB_SUCCESS, ret);

  //normal append
  ret = writer.append_log(log_item, 10000000);
  //ASSERT_EQ(OB_SUCCESS, ret);
  ++process_cnt;

  //multi append
  //constexpr int cnt = 64;
  //std::thread threads[cnt];
  //for (auto j = 0; j < cnt; ++j) {
  //  threads[j] = std::thread([&]() {
  //    for (int64_t i = 0; i < 100000; ++i) {
  //      ret = writer.append_log(log_item, 10000000);
  //      //ASSERT_EQ(OB_SUCCESS, ret);
  //      process_cnt++;
  //    }
  //  });
  //}

  //run multi append again
  for (int64_t i = 0; i < 100000; ++i) {
    ret = writer.append_log(log_item, 10000000);
    //ASSERT_EQ(OB_SUCCESS, ret);
    process_cnt++;
  }
  //for (auto j = 0; j < cnt; ++j) {
  //  threads[j].join();
  //}

  //destroy and init
  writer.stop();
  writer.wait();
  writer.destroy();
  ret = writer.init(cfg);
  ASSERT_EQ(OB_SUCCESS, ret);

  //repeat destroy
  writer.destroy();
  writer.destroy();
}

TEST(ObBaseLogWriter, external_driver)
{
  ObTLogWriter writer;
  ObBaseLogWriterCfg cfg(8, 500000, 1, 2);
  ObTLogItem log_items[3];
  int64_t processed_count = 0;
  bool has_more = false;

  ASSERT_EQ(OB_SUCCESS, writer.init(cfg));
  writer.start_external();
  for (int64_t i = 0; i < ARRAYSIZEOF(log_items); ++i) {
    ASSERT_EQ(OB_SUCCESS, writer.append_log(log_items[i]));
  }
  ASSERT_EQ(ARRAYSIZEOF(log_items), writer.notify_cnt_);
  ASSERT_EQ(ARRAYSIZEOF(log_items), writer.get_queued_item_cnt());

  ASSERT_EQ(OB_SUCCESS,
      writer.flush_external(1, processed_count, has_more));
  ASSERT_EQ(2, processed_count);
  ASSERT_TRUE(has_more);
  ASSERT_EQ(1, writer.get_queued_item_cnt());

  ASSERT_EQ(OB_SUCCESS,
      writer.flush_external(1, processed_count, has_more));
  ASSERT_EQ(1, processed_count);
  ASSERT_FALSE(has_more);
  ASSERT_EQ(0, writer.get_queued_item_cnt());

  writer.stop();
  writer.destroy();
}


}
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
