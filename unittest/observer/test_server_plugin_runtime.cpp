/*
 * Copyright (c) 2026 OceanBase.
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

#include "observer/ob_server_plugin_runtime.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>

#include "lib/ob_errno.h"
#include "share/plugin/ob_plugin_catalog.h"
#include "share/storage/ob_sqlite_connection_pool.h"

namespace oceanbase
{
namespace observer
{

using namespace common;
using namespace share;
using namespace share::plugin;

namespace
{

const char *const TEST_PLUGIN_ID = "org.seekdb.server-gate-test";

std::string make_temporary_database_path()
{
  static std::atomic<uint64_t> sequence(0);
  const uint64_t tick = static_cast<uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  std::ostringstream path;
  path << ::testing::internal::TempDir() << "seekdb_server_plugin_runtime_"
       << tick << "_" << sequence.fetch_add(1) << ".db";
  return path.str();
}

void remove_sqlite_files(const std::string &path)
{
  std::remove(path.c_str());
  std::remove((path + "-wal").c_str());
  std::remove((path + "-shm").c_str());
  std::remove((path + "-journal").c_str());
}

int bind_string(ObSQLiteBinder &binder, const std::string &value)
{
  return binder.bind_text(value.data(), static_cast<int>(value.size()));
}

ObPluginPackageInstallSpec make_install_spec()
{
  ObPluginPackageInstallSpec spec;
  spec.relative_path_ = "server-gate-test/plugin.so";
  spec.artifact_.plugin_id_ = TEST_PLUGIN_ID;
  spec.artifact_.build_id_ = "server-gate-test-build-v1";
  spec.artifact_.package_digest_ =
      "sha256:0123456789abcdef0123456789abcdef"
      "0123456789abcdef0123456789abcdef";
  spec.artifact_.package_version_ = {1, 0, 0};
  spec.artifact_.catalog_version_ = 1;
  spec.artifact_.data_format_version_ = 1;
  spec.verification_level_ = ObPluginVerificationLevel::IDENTITY_PINNED;
  spec.operator_id_ = "operator.server-gate-test";
  spec.audit_id_ = "audit.server-gate-test";
  return spec;
}

struct DurableEvidence
{
  ObPluginDesiredState desired_state_;
  ObPluginState actual_state_;
  uint64_t generation_;
  std::string runtime_incarnation_;
  std::string operation_id_;
};

DurableEvidence durable_evidence(const ObPluginCatalogRecord &record)
{
  DurableEvidence evidence;
  evidence.desired_state_ = record.desired_state_;
  evidence.actual_state_ = record.actual_state_;
  evidence.generation_ = record.generation_;
  evidence.runtime_incarnation_ = record.runtime_incarnation_;
  evidence.operation_id_ = record.operation_id_;
  return evidence;
}

void expect_same_evidence(const DurableEvidence &expected,
                          const DurableEvidence &actual)
{
  EXPECT_EQ(expected.desired_state_, actual.desired_state_);
  EXPECT_EQ(expected.actual_state_, actual.actual_state_);
  EXPECT_EQ(expected.generation_, actual.generation_);
  EXPECT_EQ(expected.runtime_incarnation_, actual.runtime_incarnation_);
  EXPECT_EQ(expected.operation_id_, actual.operation_id_);
}

class TestServerPluginRuntime : public ::testing::Test
{
protected:
  void SetUp() override
  {
    database_path_ = make_temporary_database_path();
    ASSERT_EQ(OB_SUCCESS, pool_.init(database_path_.c_str()));
    catalog_.reset(new ObPluginCatalog());
    ASSERT_NE(nullptr, catalog_.get());
    ASSERT_EQ(OB_SUCCESS, catalog_->init(&pool_));
    ASSERT_EQ(OB_SUCCESS, runtime_.init(&pool_));
  }

  void TearDown() override
  {
    runtime_.destroy();
    catalog_.reset();
    pool_.destroy();
    remove_sqlite_files(database_path_);
  }

  void install_test_package()
  {
    std::string error;
    ASSERT_EQ(OB_SUCCESS, catalog_->install_package(make_install_spec(), error))
        << error;
  }

  ObPluginCatalogRecord get_test_record()
  {
    ObPluginCatalogRecord record;
    EXPECT_EQ(OB_SUCCESS, catalog_->get_record(TEST_PLUGIN_ID, record));
    return record;
  }

  ObSQLiteConnectionPool pool_;
  std::unique_ptr<ObPluginCatalog> catalog_;
  ObServerPluginRuntime runtime_;
  std::string database_path_;
};

TEST_F(TestServerPluginRuntime, empty_catalog_allows_server_ready)
{
  std::string error = "stale error";
  EXPECT_EQ(OB_SUCCESS, runtime_.recover_before_server_ready(error)) << error;
  EXPECT_TRUE(error.empty());
}

TEST_F(TestServerPluginRuntime,
       desired_active_fails_closed_without_mutating_package_evidence)
{
  install_test_package();
  const DurableEvidence before = durable_evidence(get_test_record());
  ASSERT_EQ(ObPluginDesiredState::ACTIVE, before.desired_state_);

  std::string error;
  EXPECT_EQ(OB_NOT_SUPPORTED, runtime_.recover_before_server_ready(error));
  EXPECT_FALSE(error.empty());

  const DurableEvidence after = durable_evidence(get_test_record());
  expect_same_evidence(before, after);
}

TEST_F(TestServerPluginRuntime,
       blocked_state_takes_precedence_without_mutating_package_evidence)
{
  install_test_package();
  const uint64_t injected_generation = 37;
  const std::string injected_incarnation = "plugin-runtime-injected";
  const std::string injected_operation = "plugin-op-injected";
  {
    ObSQLiteConnectionGuard injection(&pool_);
    ASSERT_NE(nullptr, injection.get_connection());
    int64_t affected_rows = 0;
    ASSERT_EQ(
        OB_SUCCESS,
        injection->execute(
            "UPDATE __all_plugin_package SET actual_state=?,generation=?,"
            "runtime_incarnation=?,operation_id=? WHERE plugin_id=? AND "
            "desired_state=? AND actual_state=? AND generation=0 AND "
            "runtime_incarnation='' AND operation_id=''",
            [&](ObSQLiteBinder &binder) {
              int ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginState::BLOCKED));
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(
                    static_cast<int64_t>(injected_generation));
              }
              if (OB_SUCCESS == ret) {
                ret = bind_string(binder, injected_incarnation);
              }
              if (OB_SUCCESS == ret) {
                ret = bind_string(binder, injected_operation);
              }
              if (OB_SUCCESS == ret) {
                ret = bind_string(binder, TEST_PLUGIN_ID);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginDesiredState::ACTIVE));
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int(
                    static_cast<int32_t>(ObPluginState::DISCOVERED));
              }
              return ret;
            },
            &affected_rows));
    ASSERT_EQ(1, affected_rows);
  }

  const DurableEvidence before = durable_evidence(get_test_record());
  ASSERT_EQ(ObPluginDesiredState::ACTIVE, before.desired_state_);
  ASSERT_EQ(ObPluginState::BLOCKED, before.actual_state_);
  ASSERT_EQ(injected_generation, before.generation_);
  ASSERT_EQ(injected_incarnation, before.runtime_incarnation_);
  ASSERT_EQ(injected_operation, before.operation_id_);

  std::string error;
  EXPECT_EQ(OB_STATE_NOT_MATCH,
            runtime_.recover_before_server_ready(error));
  EXPECT_FALSE(error.empty());

  const DurableEvidence after = durable_evidence(get_test_record());
  expect_same_evidence(before, after);
}

} // namespace
} // namespace observer
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
