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

#include "storage/blocksstable/ob_data_file_prepare.h"
#include "storage/slog/simple_ob_storage_redo_module.h"

#define private public
#undef private

namespace oceanbase
{
using namespace common;

namespace storage
{

class TestStorageLogReplay : public blocksstable::TestDataFilePrepare
{
public:
  TestStorageLogReplay()
    : blocksstable::TestDataFilePrepare("TestStorageLogReplay")
  {
  }
  virtual ~TestStorageLogReplay() = default;

  virtual void SetUp() override;
  virtual void TearDown();
  static void SetUpTestCase();
  static void TearDownTestCase();
  void build_storage(int64_t cnt);

public:

public:
  ObStorageLogReplayer replayer_;
  char dir_[128];
  ObLogCursor replay_start_cursor_;
  ObLogCursor replay_finish_cursor_;
  blocksstable::ObLogFileSpec log_file_spec_;
  SimpleObStorageModule runtime_storage_;
};

void TestStorageLogReplay::SetUp()
{
  replay_start_cursor_.file_id_ = 1;
  replay_start_cursor_.log_id_ = 1;
  replay_start_cursor_.offset_ = 0;
  log_file_spec_.retry_write_policy_ = "normal";
  log_file_spec_.log_create_policy_ = "normal";
  log_file_spec_.log_write_policy_ = "truncate";
 
  blocksstable::TestDataFilePrepare::SetUp();
}

void TestStorageLogReplay::TearDown()
{
  blocksstable::TestDataFilePrepare::TearDown();
}

void TestStorageLogReplay::SetUpTestCase()
{
  ASSERT_EQ(OB_SUCCESS, ObTimerService::get_instance().start());
}

void TestStorageLogReplay::TearDownTestCase()
{
  ObTimerService::get_instance().stop();
  ObTimerService::get_instance().wait();
  ObTimerService::get_instance().destroy();
}

void TestStorageLogReplay::build_storage(int64_t cnt)
{
  runtime_storage_.slog_cnt_ = cnt;
  for (int i = 0; i < cnt; i++) {
    runtime_storage_.slogs_[i].block_cnt_ = ObRandom::rand(1, 1024);
    for (int j = 0; j < runtime_storage_.slogs_[i].block_cnt_; j++) {
      runtime_storage_.slogs_[i].blocks_[j] = ObRandom::rand(0, 10<<20);
    }
  }
}

TEST_F(TestStorageLogReplay, test_basic)
{
  int ret = OB_SUCCESS;
  SimpleObStorageModule redo_module;

  // test invalid initialization
  ret = replayer_.init(nullptr, log_file_spec_);
  ASSERT_NE(OB_SUCCESS, ret);
  // test invalid unregister
  ret = replayer_.unregister_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE);
  ASSERT_NE(OB_SUCCESS, ret);
  // test invalid register
  ret = replayer_.register_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE, &redo_module);

  // test normal initialization
  ret = replayer_.init(OB_FILE_SYSTEM_ROUTER.get_slog_dir(), log_file_spec_);
  ASSERT_EQ(OB_SUCCESS, ret);
  // test no redo log
  ret = replayer_.replay(replay_start_cursor_, replay_finish_cursor_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(1, replay_finish_cursor_.file_id_);
  ASSERT_EQ(1, replay_finish_cursor_.log_id_);
  ASSERT_EQ(0, replay_finish_cursor_.offset_);

  // test normal replay (single write)
  build_storage(ObRandom::rand(1, 127));

  ObStorageLogger *slogger = OB_NEW(ObStorageLogger, ObModIds::TEST);
  ASSERT_EQ(OB_SUCCESS, slogger->init(
      OB_FILE_SYSTEM_ROUTER.get_slog_dir(),
      ObLogConstants::MAX_LOG_FILE_SIZE,
      OB_FILE_SYSTEM_ROUTER.get_slog_file_spec(),
      true));
  ASSERT_EQ(OB_SUCCESS, slogger->start());

  slogger->is_start_ = false;
  slogger->start_log(replay_start_cursor_);

  ObStorageLogParam log_param;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE,
      ObRedoLogSubType::OB_REDO_LOG_UPDATE_TABLET);
  for (int i = 0; i < runtime_storage_.slog_cnt_; i++) {
    log_param.data_ = &runtime_storage_.slogs_[i];
    ret = slogger->write_log(log_param);
    ASSERT_EQ(OB_SUCCESS, ret);
  }
  replayer_.destroy();
  ret = replayer_.init(slogger->get_dir(), log_file_spec_);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = replayer_.register_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE, &redo_module);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = replayer_.replay(replay_start_cursor_, replay_finish_cursor_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_TRUE(runtime_storage_ == redo_module);
  ret = replayer_.unregister_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE);
  ASSERT_EQ(OB_SUCCESS, ret);
  replayer_.destroy();
  redo_module.reset();


  // test normal replay (batch write)
  build_storage(ObRandom::rand(1, 127));

  // mock module removal
  slogger->~ObStorageLogger();
  OB_DELETE(ObStorageLogger, ObModIds::TEST, slogger);

  slogger = OB_NEW(ObStorageLogger, ObModIds::TEST);
  ASSERT_EQ(OB_SUCCESS, slogger->init(
      OB_FILE_SYSTEM_ROUTER.get_slog_dir(),
      ObLogConstants::MAX_LOG_FILE_SIZE,
      OB_FILE_SYSTEM_ROUTER.get_slog_file_spec(),
      true));
  ASSERT_EQ(OB_SUCCESS, slogger->start());

  slogger->is_start_ = false;
  slogger->start_log(replay_finish_cursor_);

  ObSEArray<ObStorageLogParam, 10> param_arr;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE,
      ObRedoLogSubType::OB_REDO_LOG_UPDATE_TABLET);
  for (int i = 0; i < runtime_storage_.slog_cnt_; i++) {
    log_param.data_ = &(runtime_storage_.slogs_[i]);
    param_arr.push_back(log_param);
  }
  ret = slogger->get_active_cursor(replay_start_cursor_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = slogger->write_log(param_arr);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = replayer_.init(slogger->get_dir(), log_file_spec_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = replayer_.register_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE, &redo_module);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = replayer_.replay(replay_start_cursor_, replay_finish_cursor_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_TRUE(runtime_storage_ == redo_module);
  ret = replayer_.unregister_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE);
  ASSERT_EQ(OB_SUCCESS, ret);
  replayer_.destroy();
  redo_module.reset();


  // test different sub_type and checkpoint
  build_storage(ObRandom::rand(1, 127));
  slogger->get_active_cursor(replay_start_cursor_);

  for (int i = 0; i < runtime_storage_.slog_cnt_; i++) {
    log_param.data_ = &runtime_storage_.slogs_[i];
    ret = slogger->write_log(log_param);
    ASSERT_EQ(OB_SUCCESS, ret);
  }

  runtime_storage_.slog_cnt_++;
  int tmp_cnt = runtime_storage_.slog_cnt_ - 1;
  runtime_storage_.slogs_[tmp_cnt].blocks_[0] = 3214;
  runtime_storage_.slogs_[tmp_cnt].block_cnt_ = 1;
  log_param.data_ = &(runtime_storage_.slogs_[tmp_cnt]);
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE,
      ObRedoLogSubType::OB_REDO_LOG_DELETE_TABLET);
  ret = slogger->write_log(log_param);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = replayer_.init(slogger->get_dir(), log_file_spec_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = replayer_.register_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE, &redo_module);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = replayer_.replay(replay_start_cursor_, replay_finish_cursor_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_FALSE(runtime_storage_ == redo_module);

  for (int i = 0; i < tmp_cnt; i++) {
    ASSERT_TRUE(runtime_storage_.slogs_[i] == redo_module.slogs_[i]);
  }
  ASSERT_TRUE(runtime_storage_.slogs_[tmp_cnt] != redo_module.slogs_[tmp_cnt]);
  OB_DELETE(ObStorageLogger, ObModIds::TEST, slogger);
}

TEST_F(TestStorageLogReplay, test_switch_file_replay)
{
  // replay start cursor is the end of the first file and slogs are dumped to the second file
  int ret = OB_SUCCESS;
  ObLogCursor write_start_cursor;
  write_start_cursor.file_id_ = 1;
  write_start_cursor.log_id_ = 1;
  SimpleObStorageModule redo_module;

  ObStorageLogger *slogger = OB_NEW(ObStorageLogger, ObModIds::TEST);
  ASSERT_EQ(OB_SUCCESS, slogger->init(
      OB_FILE_SYSTEM_ROUTER.get_slog_dir(),
      ObLogConstants::MAX_LOG_FILE_SIZE,
      OB_FILE_SYSTEM_ROUTER.get_slog_file_spec(),
      true));
  ASSERT_EQ(OB_SUCCESS, slogger->start());

  slogger->is_start_ = false;
  slogger->start_log(write_start_cursor);

  ObStorageLogParam log_param;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE,
      ObRedoLogSubType::OB_REDO_LOG_UPDATE_TABLET);

  build_storage(ObRandom::rand(1, 127));
  for (int i = 0; i < runtime_storage_.slog_cnt_; i++) {
    log_param.data_ = &runtime_storage_.slogs_[i];
    ret = slogger->write_log(log_param);
    ASSERT_EQ(OB_SUCCESS, ret);
  }
  slogger->get_active_cursor(replay_start_cursor_);

  build_storage(ObRandom::rand(1, 127));
  write_start_cursor.file_id_ = 2;
  write_start_cursor.log_id_ = replay_start_cursor_.log_id_;
  write_start_cursor.offset_ = 0;

  // mock module removal
  slogger->~ObStorageLogger();
  OB_DELETE(ObStorageLogger, ObModIds::TEST, slogger);

  slogger = OB_NEW(ObStorageLogger, ObModIds::TEST);
  ASSERT_EQ(OB_SUCCESS, slogger->init(
      OB_FILE_SYSTEM_ROUTER.get_slog_dir(),
      ObLogConstants::MAX_LOG_FILE_SIZE,
      OB_FILE_SYSTEM_ROUTER.get_slog_file_spec(),
      true));
  ASSERT_EQ(OB_SUCCESS, slogger->start());

  slogger->is_start_ = false;
  slogger->start_log(write_start_cursor);

  for (int i = 0; i < runtime_storage_.slog_cnt_; i++) {
    log_param.data_ = &runtime_storage_.slogs_[i];
    ret = slogger->write_log(log_param);
    ASSERT_EQ(OB_SUCCESS, ret);
  }
  ret = replayer_.init(slogger->get_dir(), log_file_spec_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = replayer_.register_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE, &redo_module);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = replayer_.replay(replay_start_cursor_, replay_finish_cursor_);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_TRUE(runtime_storage_ == redo_module);
  OB_DELETE(ObStorageLogger, ObModIds::TEST, slogger);
}

TEST_F(TestStorageLogReplay, test_mock_restart)
{
  int ret = OB_SUCCESS;
  ObLogCursor write_start_cursor;
  write_start_cursor.file_id_ = 1;
  write_start_cursor.log_id_ = 1;
  SimpleObStorageModule redo_module;

  ObStorageLogger *slogger = OB_NEW(ObStorageLogger, ObModIds::TEST);
  ASSERT_EQ(OB_SUCCESS, slogger->init(
      OB_FILE_SYSTEM_ROUTER.get_slog_dir(),
      ObLogConstants::MAX_LOG_FILE_SIZE,
      OB_FILE_SYSTEM_ROUTER.get_slog_file_spec(),
      true));
  ASSERT_EQ(OB_SUCCESS, slogger->start());

  slogger->is_start_ = false;
  slogger->start_log(write_start_cursor);

  ObStorageLogParam log_param;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE,
      ObRedoLogSubType::OB_REDO_LOG_UPDATE_TABLET);

  build_storage(40);
  // first time to write slog
  for (int i = 0; i < runtime_storage_.slog_cnt_; i++) {
    log_param.data_ = &runtime_storage_.slogs_[i];
    ASSERT_EQ(OB_SUCCESS, slogger->write_log(log_param));
  }
  // replay first slog file
  ASSERT_EQ(OB_SUCCESS, replayer_.init(slogger->get_dir(), log_file_spec_));
  ASSERT_EQ(OB_SUCCESS, replayer_.register_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE, &redo_module));
  ASSERT_EQ(OB_SUCCESS, replayer_.replay(replay_start_cursor_, replay_finish_cursor_));
  replayer_.destroy();
  redo_module.reset();

  // reset slogger and set its start cursor as replay_finish_cursor
  slogger->destroy();
  ASSERT_EQ(OB_SUCCESS, slogger->init(
      OB_FILE_SYSTEM_ROUTER.get_slog_dir(),
      ObLogConstants::MAX_LOG_FILE_SIZE,
      OB_FILE_SYSTEM_ROUTER.get_slog_file_spec(),
      true));
  ASSERT_EQ(OB_SUCCESS, slogger->start());
  slogger->is_start_ = false;
  ASSERT_EQ(OB_SUCCESS, slogger->start_log(replay_finish_cursor_));

  build_storage(30);
  // second time to write slog
  for (int i = 0; i < runtime_storage_.slog_cnt_; i++) {
    log_param.data_ = &runtime_storage_.slogs_[i];
    ASSERT_EQ(OB_SUCCESS, slogger->write_log(log_param));
  }
  // replay first and second slog files
  ASSERT_EQ(OB_SUCCESS, replayer_.init(slogger->get_dir(), log_file_spec_));
  ASSERT_EQ(OB_SUCCESS, replayer_.register_redo_module(ObRedoLogMainType::OB_REDO_LOG_LOCAL_STORAGE, &redo_module));
  ASSERT_EQ(OB_SUCCESS, replayer_.replay(replay_start_cursor_, replay_finish_cursor_));
  OB_DELETE(ObStorageLogger, ObModIds::TEST, slogger);
}

}
}

int main(int argc, char **argv)
{
  system("rm -f test_storage_log_replay.log*");
  OB_LOGGER.set_file_name("test_storage_log_replay.log", true);
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
