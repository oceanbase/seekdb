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

#include "share/plugin/ob_plugin_catalog.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "lib/ob_errno.h"
#include "share/storage/ob_sqlite_connection_pool.h"

namespace oceanbase
{
namespace share
{
namespace plugin
{

using namespace oceanbase::common;

namespace
{

const char *const TEST_PLUGIN_ID = "org.seekdb.catalog-test";
const char *const TEST_BUILD_ID = "catalog-test-build-v1";
const char *const TEST_RELATIVE_PATH = "catalog-test/plugin.so";
const char *const TEST_PACKAGE_DIGEST =
    "sha256:0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef";

struct AttemptIdentity
{
  AttemptIdentity()
      : generation_(0), runtime_incarnation_(), operation_id_()
  {}

  uint64_t generation_;
  std::string runtime_incarnation_;
  std::string operation_id_;
};

std::string make_temporary_database_path()
{
  static std::atomic<uint64_t> sequence(0);
  const uint64_t tick = static_cast<uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  std::ostringstream path;
  path << ::testing::internal::TempDir() << "seekdb_plugin_catalog_" << tick << "_"
       << sequence.fetch_add(1) << ".db";
  return path.str();
}

void remove_sqlite_files(const std::string &path)
{
  std::remove(path.c_str());
  std::remove((path + "-wal").c_str());
  std::remove((path + "-shm").c_str());
  std::remove((path + "-journal").c_str());
}

std::string with_embedded_nul(const char *visible_prefix)
{
  std::string value(visible_prefix);
  value.push_back('\0');
  value.append("hidden");
  return value;
}

ObPluginPackageInstallSpec make_install_spec(
    const std::string &plugin_id = TEST_PLUGIN_ID)
{
  ObPluginPackageInstallSpec spec;
  spec.relative_path_ = TEST_RELATIVE_PATH;
  spec.artifact_.plugin_id_ = plugin_id;
  spec.artifact_.build_id_ = TEST_BUILD_ID;
  spec.artifact_.package_digest_ = TEST_PACKAGE_DIGEST;
  spec.artifact_.package_version_ = {1, 2, 3};
  spec.artifact_.catalog_version_ = 7;
  spec.artifact_.data_format_version_ = 11;
  spec.verification_level_ = ObPluginVerificationLevel::IDENTITY_PINNED;
  spec.operator_id_ = "operator.install";
  spec.audit_id_ = "audit.install";
  return spec;
}

ObPluginActivationRequest make_activation_request(
    const ObPluginPackageInstallSpec &spec)
{
  ObPluginActivationRequest request;
  request.mode_ = ObPluginActivationMode::ACTIVATE;
  request.relative_path_ = spec.relative_path_;
  request.plugin_id_ = spec.artifact_.plugin_id_;
  request.build_id_ = spec.artifact_.build_id_;
  request.package_digest_ = spec.artifact_.package_digest_;
  request.package_version_ = spec.artifact_.package_version_;
  request.catalog_version_ = spec.artifact_.catalog_version_;
  request.data_format_version_ = spec.artifact_.data_format_version_;
  return request;
}

AttemptIdentity identity_of(const ObPluginActivationPermit &permit)
{
  AttemptIdentity identity;
  identity.generation_ = permit.generation();
  identity.runtime_incarnation_ = permit.runtime_incarnation();
  identity.operation_id_ = permit.operation_id();
  return identity;
}

ObPluginRuntimeActivationResult make_candidate_result(
    const ObPluginActivationPermit &permit)
{
  ObPluginRuntimeActivationResult result;
  result.status_ = OB_SUCCESS;
  result.generation_ = permit.generation();
  result.runtime_incarnation_ = permit.runtime_incarnation();
  result.operation_id_ = permit.operation_id();
  result.actual_state_ = ObPluginState::INITIALIZING;
  result.phase_ = ObPluginActivationPhase::CATALOG_FINISH;
  result.start_entered_ = true;
  result.candidate_prepared_ = true;
  result.candidate_base_epoch_ = 0;
  return result;
}

ObPluginRuntimeActivationResult make_active_result(
    const ObPluginRuntimeActivationResult &candidate)
{
  ObPluginRuntimeActivationResult result(candidate);
  result.status_ = OB_SUCCESS;
  result.actual_state_ = ObPluginState::ACTIVE;
  result.phase_ = ObPluginActivationPhase::COMPLETE;
  result.error_.clear();
  return result;
}

ObPluginRuntimeActivationResult make_abort_result(
    const ObPluginActivationPermit &permit)
{
  ObPluginRuntimeActivationResult result;
  result.status_ = OB_ERR_UNEXPECTED;
  result.generation_ = permit.generation();
  result.runtime_incarnation_ = permit.runtime_incarnation();
  result.operation_id_ = permit.operation_id();
  result.actual_state_ = ObPluginState::FAILED;
  result.phase_ = ObPluginActivationPhase::LOADING;
  result.error_ = "injected activation failure";
  return result;
}

int install_package(ObPluginCatalog &catalog,
                    const ObPluginPackageInstallSpec &spec,
                    std::string &error)
{
  error.clear();
  return catalog.install_package(spec, error);
}

int begin_attempt(ObPluginCatalog &catalog,
                  const ObPluginPackageInstallSpec &spec,
                  std::unique_ptr<ObPluginActivationPermit> &permit,
                  std::string &error)
{
  error.clear();
  permit.reset();
  return catalog.begin_activation(
      make_activation_request(spec), permit, error);
}

int activate_and_complete(ObPluginCatalog &catalog,
                          const ObPluginPackageInstallSpec &spec,
                          AttemptIdentity &identity,
                          std::string &error)
{
  int ret = OB_SUCCESS;
  std::unique_ptr<ObPluginActivationPermit> permit;
  std::unique_ptr<ObPluginActivationCommit> commit;
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  ObPluginRuntimeActivationResult candidate;
  if (OB_FAIL(begin_attempt(catalog, spec, permit, error))) {
  } else if (OB_ISNULL(permit.get())) {
    ret = OB_ERR_UNEXPECTED;
    error = "catalog returned no activation permit";
  } else {
    identity = identity_of(*permit);
    candidate = make_candidate_result(*permit);
    if (OB_FAIL(permit->commit_candidate(candidate, decision, commit, error))) {
    } else if (OB_PLUGIN_ACTIVATION_PROMOTE != decision ||
               OB_ISNULL(commit.get())) {
      ret = OB_ERR_UNEXPECTED;
      error = "catalog returned a contradictory activation decision";
    } else {
      const ObPluginRuntimeActivationResult active =
          make_active_result(candidate);
      ret = commit->complete(active, error);
    }
  }
  return ret;
}

ObPluginServiceInfo make_service_info(
    const ObPluginPackageInstallSpec &spec,
    const ObPluginActivationPermit &permit,
    const std::string &service_id)
{
  ObPluginServiceInfo service{};
  service.name_ = service_id;
  service.abi_major_ = 1;
  service.abi_minor_ = 4;
  service.abi_patch_ = 2;
  service.capabilities_ = 5;
  service.owner_plugin_id_ = spec.artifact_.plugin_id_;
  service.owner_generation_ = permit.generation();
  return service;
}

ObPluginExtensionInfo make_function_extension(
    const ObPluginPackageInstallSpec &spec,
    const ObPluginActivationPermit &permit,
    const std::string &service_id,
    const std::string &extension_id)
{
  ObPluginExtensionInfo extension;
  extension.spec_.kind_ = SEEKDB_PLUGIN_EXTENSION_FUNCTION;
  extension.spec_.object_id_ = extension_id;
  extension.spec_.sql_name_ = extension_id;
  extension.spec_.definition_digest_ = TEST_PACKAGE_DIGEST;
  extension.spec_.minimum_arity_ = 1;
  extension.spec_.maximum_arity_ = 1;
  extension.spec_.cost_ = 1;
  extension.spec_.flags_ = SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC;
  extension.spec_.implementation_.service_id_ = service_id;
  extension.spec_.implementation_.version_range_.struct_size =
      sizeof(seekdb_plugin_version_range_t);
  extension.spec_.implementation_.version_range_.minimum_inclusive = {1, 0, 0};
  extension.spec_.implementation_.version_range_.maximum_exclusive = {2, 0, 0};
  extension.spec_.implementation_.required_capabilities_ = 1;
  extension.owner_plugin_id_ = spec.artifact_.plugin_id_;
  extension.owner_generation_ = permit.generation();
  return extension;
}

ObPluginExtensionInfo make_persistent_type_extension(
    const ObPluginPackageInstallSpec &spec,
    const ObPluginActivationPermit &permit,
    const std::string &service_id)
{
  ObPluginExtensionInfo extension;
  extension.spec_.kind_ = SEEKDB_PLUGIN_EXTENSION_TYPE;
  extension.spec_.object_id_ = "org.seekdb.catalog-test.geometry-type";
  extension.spec_.sql_name_ = "geometry";
  extension.spec_.physical_format_id_ =
      "org.seekdb.catalog-test.geometry-format";
  extension.spec_.physical_format_version_ = 1;
  extension.spec_.flags_ = SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT;
  extension.spec_.implementation_.service_id_ = service_id;
  extension.spec_.implementation_.version_range_.struct_size =
      sizeof(seekdb_plugin_version_range_t);
  extension.spec_.implementation_.version_range_.minimum_inclusive = {1, 0, 0};
  extension.spec_.implementation_.version_range_.maximum_exclusive = {2, 0, 0};
  extension.spec_.implementation_.required_capabilities_ = 1;
  extension.owner_plugin_id_ = spec.artifact_.plugin_id_;
  extension.owner_generation_ = permit.generation();
  return extension;
}

ObPluginRuntimeActivationResult make_persistent_format_candidate(
    const ObPluginPackageInstallSpec &spec,
    const ObPluginActivationPermit &permit)
{
  const std::string service_id = "org.seekdb.catalog-test.geometry-codec";
  ObPluginRuntimeActivationResult candidate = make_candidate_result(permit);
  candidate.services_.push_back(make_service_info(spec, permit, service_id));
  candidate.extensions_.push_back(
      make_persistent_type_extension(spec, permit, service_id));
  return candidate;
}

int activate_and_complete_with_persistent_format(
    ObPluginCatalog &catalog,
    const ObPluginPackageInstallSpec &spec,
    AttemptIdentity &identity,
    std::string &error)
{
  int ret = OB_SUCCESS;
  std::unique_ptr<ObPluginActivationPermit> permit;
  std::unique_ptr<ObPluginActivationCommit> commit;
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  if (OB_FAIL(begin_attempt(catalog, spec, permit, error))) {
  } else if (OB_ISNULL(permit.get())) {
    ret = OB_ERR_UNEXPECTED;
    error = "catalog returned no persistent-format activation permit";
  } else {
    identity = identity_of(*permit);
    const ObPluginRuntimeActivationResult candidate =
        make_persistent_format_candidate(spec, *permit);
    if (OB_FAIL(permit->commit_candidate(candidate, decision, commit, error))) {
    } else if (OB_PLUGIN_ACTIVATION_PROMOTE != decision ||
               OB_ISNULL(commit.get())) {
      ret = OB_ERR_UNEXPECTED;
      error = "catalog returned a contradictory activation decision";
    } else {
      ret = commit->complete(make_active_result(candidate), error);
    }
  }
  return ret;
}

ObPluginRuntimeServiceDependency make_runtime_service_dependency(
    const std::string &provider_plugin_id,
    const uint64_t provider_generation,
    const std::string &service_id)
{
  ObPluginRuntimeServiceDependency dependency;
  dependency.service_id_ = service_id;
  dependency.requested_version_.struct_size =
      sizeof(seekdb_plugin_version_range_t);
  dependency.requested_version_.minimum_inclusive = {1, 0, 0};
  dependency.requested_version_.maximum_exclusive = {2, 0, 0};
  dependency.required_capabilities_ = 1;
  dependency.optional_ = false;
  dependency.provider_plugin_id_ = provider_plugin_id;
  dependency.provider_generation_ = provider_generation;
  dependency.provider_version_ = {1, 4, 2};
  return dependency;
}

int activate_and_complete_with_metadata(
    ObPluginCatalog &catalog,
    const ObPluginPackageInstallSpec &spec,
    const std::string &service_id,
    const std::string &extension_id,
    const ObPluginRuntimeServiceDependency *dependency,
    AttemptIdentity &identity,
    std::string &error)
{
  int ret = OB_SUCCESS;
  std::unique_ptr<ObPluginActivationPermit> permit;
  std::unique_ptr<ObPluginActivationCommit> commit;
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  ObPluginRuntimeActivationResult candidate;
  if (OB_FAIL(begin_attempt(catalog, spec, permit, error))) {
  } else if (OB_ISNULL(permit.get())) {
    ret = OB_ERR_UNEXPECTED;
    error = "catalog returned no metadata activation permit";
  } else {
    identity = identity_of(*permit);
    candidate = make_candidate_result(*permit);
    candidate.services_.push_back(
        make_service_info(spec, *permit, service_id));
    candidate.extensions_.push_back(
        make_function_extension(spec, *permit, service_id, extension_id));
    if (nullptr != dependency) {
      candidate.dependencies_.push_back(*dependency);
    }
    if (OB_FAIL(permit->commit_candidate(candidate, decision, commit, error))) {
    } else if (OB_PLUGIN_ACTIVATION_PROMOTE != decision ||
               OB_ISNULL(commit.get())) {
      ret = OB_ERR_UNEXPECTED;
      error = "catalog returned a contradictory metadata activation decision";
    } else {
      ret = commit->complete(make_active_result(candidate), error);
    }
  }
  return ret;
}

int bind_test_string(ObSQLiteBinder &binder, const std::string &value)
{
  return binder.bind_text(value.c_str(), static_cast<int>(value.size()));
}

template <typename Binder>
int query_test_count(ObSQLiteConnection &connection,
                     const char *sql,
                     Binder binder,
                     int64_t &count)
{
  count = 0;
  return connection.query(
      sql, binder,
      [&](ObSQLiteRowReader &reader) {
        count = reader.get_int64(0);
        return OB_ITER_END;
      });
}

ObPluginDependencySpec make_persistent_dependency(
    const uint64_t provider_generation)
{
  ObPluginDependencySpec dependency;
  dependency.consumer_kind_ =
      ObPluginDependencyConsumerKind::PERSISTENT_DATA;
  dependency.consumer_id_ = "table_42.geometry_column";
  dependency.provider_plugin_id_ = TEST_PLUGIN_ID;
  dependency.provider_generation_ = provider_generation;
  dependency.dependency_kind_ =
      ObPluginDependencyKind::PERSISTENT_FORMAT;
  dependency.dependency_id_ = "org.seekdb.catalog-test.geometry-format";
  dependency.requested_version_.struct_size =
      sizeof(seekdb_plugin_version_range_t);
  dependency.requested_version_.minimum_inclusive = {1, 0, 0};
  dependency.requested_version_.maximum_exclusive = {2, 0, 0};
  dependency.required_capabilities_ = 0;
  return dependency;
}

const ObPluginStartupEntry *find_startup_entry(
    const std::vector<ObPluginStartupEntry> &entries,
    const std::string &plugin_id)
{
  const ObPluginStartupEntry *found = nullptr;
  for (const ObPluginStartupEntry &entry : entries) {
    if (entry.plugin_id_ == plugin_id) {
      found = &entry;
      break;
    }
  }
  return found;
}

class TestPluginCatalog : public ::testing::Test
{
protected:
  void SetUp() override
  {
    database_path_ = make_temporary_database_path();
    ASSERT_EQ(OB_SUCCESS, pool_.init(database_path_.c_str()));
    catalog_.reset(new ObPluginCatalog());
    ASSERT_NE(nullptr, catalog_.get());
    ASSERT_EQ(OB_SUCCESS, catalog_->init(&pool_));
  }

  void TearDown() override
  {
    catalog_.reset();
    pool_.destroy();
    remove_sqlite_files(database_path_);
  }

  int reopen_catalog()
  {
    catalog_.reset();
    catalog_.reset(new ObPluginCatalog());
    return catalog_ ? catalog_->init(&pool_) : OB_ALLOCATE_MEMORY_FAILED;
  }

  ObSQLiteConnectionPool pool_;
  std::unique_ptr<ObPluginCatalog> catalog_;
  std::string database_path_;
};

} // namespace

TEST_F(TestPluginCatalog, activation_requires_an_installed_exact_package)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::unique_ptr<ObPluginActivationPermit> permit;
  std::string error;

  const int ret = begin_attempt(*catalog_, spec, permit, error);
  EXPECT_NE(OB_SUCCESS, ret);
  EXPECT_EQ(nullptr, permit.get());
  EXPECT_FALSE(error.empty());

  ObPluginCatalogRecord record;
  EXPECT_NE(OB_SUCCESS, catalog_->get_record(TEST_PLUGIN_ID, record));
}

TEST_F(TestPluginCatalog, aborted_attempt_never_reuses_durable_identity)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;

  std::unique_ptr<ObPluginActivationPermit> first;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, first, error)) << error;
  ASSERT_NE(nullptr, first.get());
  const AttemptIdentity first_identity = identity_of(*first);
  EXPECT_GT(first_identity.generation_, 0U);
  EXPECT_FALSE(first_identity.runtime_incarnation_.empty());
  EXPECT_FALSE(first_identity.operation_id_.empty());
  ASSERT_EQ(OB_SUCCESS, first->abort(make_abort_result(*first), error)) << error;
  first.reset();

  std::unique_ptr<ObPluginActivationPermit> second;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, second, error)) << error;
  ASSERT_NE(nullptr, second.get());
  const AttemptIdentity second_identity = identity_of(*second);
  EXPECT_GT(second_identity.generation_, first_identity.generation_);
  EXPECT_NE(first_identity.runtime_incarnation_,
            second_identity.runtime_incarnation_);
  EXPECT_NE(first_identity.operation_id_, second_identity.operation_id_);
  ASSERT_EQ(OB_SUCCESS, second->abort(make_abort_result(*second), error))
      << error;
}

TEST_F(TestPluginCatalog, concurrent_catalogs_serialize_generation_assignment)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;

  // Two catalog instances model independent server-side callers.  Their
  // process-local mutexes do not overlap, so SQLite's write transaction is
  // the authority that must serialize the durable identity assignment.
  ObPluginCatalog other_catalog;
  ASSERT_EQ(OB_SUCCESS, other_catalog.init(&pool_));

  struct BeginResult
  {
    BeginResult() : ret_(OB_ERR_UNEXPECTED), permit_(), error_() {}
    int ret_;
    std::unique_ptr<ObPluginActivationPermit> permit_;
    std::string error_;
  };
  std::array<BeginResult, 2> results;
  std::atomic<int> waiting(0);
  std::atomic<bool> start(false);
  auto begin = [&](const size_t index, ObPluginCatalog *catalog) {
    waiting.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    results[index].ret_ = begin_attempt(
        *catalog, spec, results[index].permit_, results[index].error_);
  };

  std::thread first(begin, 0, catalog_.get());
  std::thread second(begin, 1, &other_catalog);
  while (waiting.load(std::memory_order_acquire) != 2) {
    std::this_thread::yield();
  }
  start.store(true, std::memory_order_release);
  first.join();
  second.join();

  const int success_count =
      (OB_SUCCESS == results[0].ret_ ? 1 : 0) +
      (OB_SUCCESS == results[1].ret_ ? 1 : 0);
  ASSERT_EQ(1, success_count);
  const size_t winner = OB_SUCCESS == results[0].ret_ ? 0 : 1;
  const size_t loser = 1 - winner;
  ASSERT_NE(nullptr, results[winner].permit_.get());
  EXPECT_EQ(OB_EAGAIN, results[loser].ret_);
  EXPECT_EQ(nullptr, results[loser].permit_.get());
  EXPECT_FALSE(results[loser].error_.empty());

  const AttemptIdentity durable = identity_of(*results[winner].permit_);
  ObPluginCatalogRecord record;
  ASSERT_EQ(OB_SUCCESS, catalog_->get_record(TEST_PLUGIN_ID, record));
  EXPECT_EQ(1U, durable.generation_);
  EXPECT_EQ(durable.generation_, record.generation_);
  EXPECT_EQ(durable.runtime_incarnation_, record.runtime_incarnation_);
  EXPECT_EQ(durable.operation_id_, record.operation_id_);
  ASSERT_EQ(OB_SUCCESS,
            results[winner].permit_->abort(
                make_abort_result(*results[winner].permit_), error))
      << error;
}

TEST_F(TestPluginCatalog,
       transaction_scoped_dependency_does_not_invert_catalog_writer_locks)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity active;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_persistent_format(
                *catalog_, spec, active, error))
      << error;
  const ObPluginDependencySpec dependency =
      make_persistent_dependency(active.generation_);

  ObSQLiteConnectionGuard writer(&pool_);
  ASSERT_NE(nullptr, writer.get_connection());
  ASSERT_EQ(OB_SUCCESS,
            writer->execute("BEGIN IMMEDIATE", nullptr));

  std::mutex state_mutex;
  std::condition_variable state_changed;
  bool disable_started = false;
  bool add_finished = false;
  int add_ret = OB_ERR_UNEXPECTED;
  int disable_ret = OB_ERR_UNEXPECTED;
  std::string add_error;
  std::string disable_error;
  std::unique_ptr<ObPluginDisablePermit> disable_permit;

  std::thread disable_thread([&] {
    {
      std::lock_guard<std::mutex> lock(state_mutex);
      disable_started = true;
    }
    state_changed.notify_all();
    disable_ret = catalog_->begin_restricted_disable(
        TEST_PLUGIN_ID, active.generation_, disable_permit, disable_error);
  });

  bool observed_disable_start = false;
  {
    std::unique_lock<std::mutex> lock(state_mutex);
    observed_disable_start = state_changed.wait_for(
        lock, std::chrono::seconds(1), [&] { return disable_started; });
  }
  if (!observed_disable_start) {
    (void)writer->rollback();
    disable_thread.join();
    FAIL() << "disable worker did not start within the bounded interval";
  }

  // Give begin_disable a scheduling window to take catalog mutex and wait on
  // this external SQLite writer.  The scoped add must still run because its
  // lock authority is the caller's transaction, not catalog mutex.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  std::thread add_thread([&] {
    add_ret = catalog_->add_dependency(
        *writer.get_connection(), dependency, add_error);
    {
      std::lock_guard<std::mutex> lock(state_mutex);
      add_finished = true;
    }
    state_changed.notify_all();
  });

  bool add_finished_in_time = false;
  {
    std::unique_lock<std::mutex> lock(state_mutex);
    add_finished_in_time = state_changed.wait_for(
        lock, std::chrono::seconds(1), [&] { return add_finished; });
  }
  // This commit is also the watchdog escape hatch: an incorrect writer ->
  // catalog-mutex implementation is released before either join can hang.
  const int commit_ret = writer->commit();
  add_thread.join();
  disable_thread.join();

  EXPECT_TRUE(add_finished_in_time)
      << "transaction-scoped add waited on catalog mutex";
  EXPECT_EQ(OB_SUCCESS, add_ret) << add_error;
  EXPECT_EQ(OB_SUCCESS, commit_ret);
  EXPECT_EQ(OB_OP_NOT_ALLOW, disable_ret) << disable_error;
  EXPECT_EQ(nullptr, disable_permit.get());
  std::vector<ObPluginRestrictBlocker> blockers;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->list_restrict_blockers(TEST_PLUGIN_ID, blockers));
  ASSERT_EQ(1U, blockers.size());
  EXPECT_EQ(dependency.consumer_id_, blockers[0].consumer_id_);
}

TEST_F(TestPluginCatalog, candidate_promote_and_complete_persist_active)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;

  AttemptIdentity identity;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete(*catalog_, spec, identity, error))
      << error;

  ObPluginCatalogRecord record;
  ASSERT_EQ(OB_SUCCESS, catalog_->get_record(TEST_PLUGIN_ID, record));
  EXPECT_EQ(ObPluginDesiredState::ACTIVE, record.desired_state_);
  EXPECT_EQ(ObPluginState::ACTIVE, record.actual_state_);
  EXPECT_EQ(identity.generation_, record.generation_);
  EXPECT_EQ(identity.runtime_incarnation_, record.runtime_incarnation_);
  EXPECT_EQ(identity.operation_id_, record.operation_id_);
  EXPECT_EQ(OB_SUCCESS, record.last_status_);
}

TEST_F(TestPluginCatalog,
       candidate_metadata_drives_restrict_startup_dag_and_generation_archival)
{
  const std::string provider_id = "org.seekdb.catalog-provider";
  const std::string consumer_id = "org.seekdb.catalog-consumer";
  const std::string provider_service =
      "org.seekdb.catalog-provider.geometry-service";
  const std::string consumer_service =
      "org.seekdb.catalog-consumer.query-service";
  const std::string provider_extension =
      "org.seekdb.catalog-provider.geometry-function";
  const std::string consumer_extension =
      "org.seekdb.catalog-consumer.query-function";

  ObPluginPackageInstallSpec provider_spec = make_install_spec(provider_id);
  provider_spec.relative_path_ = "catalog-provider/plugin.so";
  provider_spec.artifact_.build_id_ = "catalog-provider-build-v1";
  provider_spec.artifact_.package_digest_ =
      "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  ObPluginPackageInstallSpec consumer_spec = make_install_spec(consumer_id);
  consumer_spec.relative_path_ = "catalog-consumer/plugin.so";
  consumer_spec.artifact_.build_id_ = "catalog-consumer-build-v1";
  consumer_spec.artifact_.package_digest_ =
      "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

  std::string error;
  ASSERT_EQ(OB_SUCCESS,
            install_package(*catalog_, provider_spec, error))
      << error;
  ASSERT_EQ(OB_SUCCESS,
            install_package(*catalog_, consumer_spec, error))
      << error;

  AttemptIdentity provider_first;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_metadata(
                *catalog_, provider_spec, provider_service,
                provider_extension, nullptr, provider_first, error))
      << error;
  const ObPluginRuntimeServiceDependency dependency =
      make_runtime_service_dependency(
          provider_id, provider_first.generation_, provider_service);
  AttemptIdentity consumer_first;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_metadata(
                *catalog_, consumer_spec, consumer_service,
                consumer_extension, &dependency, consumer_first, error))
      << error;

  // Verify all three candidate collections reached their normalized SQLite
  // tables, including the exact generation fences and implementation binding.
  {
    ObSQLiteConnectionGuard metadata(&pool_);
    ASSERT_NE(nullptr, metadata.get_connection());
    int64_t count = 0;
    auto count_service = [&](const std::string &plugin_id,
                             const uint64_t generation,
                             const std::string &service_id) {
      return query_test_count(
          *metadata.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_service WHERE plugin_id=? "
          "AND generation=? AND service_id=? AND abi_major=1 "
          "AND abi_minor=4 AND abi_patch=2 AND capabilities=5",
          [&](ObSQLiteBinder &binder) {
            int ret = bind_test_string(binder, plugin_id);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int64(static_cast<int64_t>(generation));
            }
            if (OB_SUCCESS == ret) {
              ret = bind_test_string(binder, service_id);
            }
            return ret;
          },
          count);
    };
    ASSERT_EQ(OB_SUCCESS,
              count_service(
                  provider_id, provider_first.generation_, provider_service));
    EXPECT_EQ(1, count);
    ASSERT_EQ(OB_SUCCESS,
              count_service(
                  consumer_id, consumer_first.generation_, consumer_service));
    EXPECT_EQ(1, count);

    auto count_extension = [&](const std::string &plugin_id,
                               const uint64_t generation,
                               const std::string &extension_id,
                               const std::string &service_id) {
      return query_test_count(
          *metadata.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_extension WHERE plugin_id=? "
          "AND generation=? AND kind=? AND object_id=? "
          "AND implementation_service_id=?",
          [&](ObSQLiteBinder &binder) {
            int ret = bind_test_string(binder, plugin_id);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int64(static_cast<int64_t>(generation));
            }
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(SEEKDB_PLUGIN_EXTENSION_FUNCTION);
            }
            if (OB_SUCCESS == ret) {
              ret = bind_test_string(binder, extension_id);
            }
            if (OB_SUCCESS == ret) {
              ret = bind_test_string(binder, service_id);
            }
            return ret;
          },
          count);
    };
    ASSERT_EQ(OB_SUCCESS,
              count_extension(
                  provider_id, provider_first.generation_,
                  provider_extension, provider_service));
    EXPECT_EQ(1, count);
    ASSERT_EQ(OB_SUCCESS,
              count_extension(
                  consumer_id, consumer_first.generation_,
                  consumer_extension, consumer_service));
    EXPECT_EQ(1, count);

    ASSERT_EQ(
        OB_SUCCESS,
        query_test_count(
            *metadata.get_connection(),
            "SELECT COUNT(*) FROM __all_plugin_dependency "
            "WHERE consumer_kind=? AND consumer_plugin_id=? "
            "AND consumer_generation=? AND provider_plugin_id=? "
            "AND provider_generation=? AND dependency_kind=? "
            "AND dependency_id=?",
            [&](ObSQLiteBinder &binder) {
              int ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDependencyConsumerKind::PLUGIN));
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, consumer_id);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(
                    static_cast<int64_t>(consumer_first.generation_));
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, provider_id);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(
                    static_cast<int64_t>(provider_first.generation_));
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginDependencyKind::SERVICE));
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, provider_service);
              }
              return ret;
            },
            count));
    EXPECT_EQ(1, count);
  }

  std::vector<ObPluginRestrictBlocker> blockers;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->list_restrict_blockers(provider_id, blockers));
  ASSERT_EQ(1U, blockers.size());
  EXPECT_EQ(ObPluginDependencyConsumerKind::PLUGIN,
            blockers[0].consumer_kind_);
  EXPECT_EQ(consumer_id, blockers[0].consumer_id_);
  EXPECT_EQ(consumer_id, blockers[0].consumer_plugin_id_);
  EXPECT_EQ(consumer_first.generation_, blockers[0].consumer_generation_);
  EXPECT_EQ(ObPluginDependencyKind::SERVICE,
            blockers[0].dependency_kind_);
  EXPECT_EQ(provider_service, blockers[0].dependency_id_);
  std::unique_ptr<ObPluginDisablePermit> disable;
  EXPECT_EQ(OB_OP_NOT_ALLOW,
            catalog_->begin_restricted_disable(
                provider_id, provider_first.generation_, disable, error));
  EXPECT_EQ(nullptr, disable.get());

  ASSERT_EQ(OB_SUCCESS, reopen_catalog());
  std::vector<ObPluginStartupEntry> entries;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->prepare_startup_recovery(entries, error))
      << error;
  ASSERT_EQ(2U, entries.size());
  // consumer sorts before provider lexically, so this order can only come
  // from the persisted dependency edge.
  EXPECT_EQ(provider_id, entries[0].plugin_id_);
  EXPECT_EQ(consumer_id, entries[1].plugin_id_);
  EXPECT_FALSE(entries[0].exact_recovery_);
  EXPECT_FALSE(entries[1].exact_recovery_);

  // Start both packages in DAG order, but deliberately omit the dependency
  // from the consumer's new generation.  The previous row must remain as
  // immutable history without fencing the current generations.
  AttemptIdentity provider_second;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_metadata(
                *catalog_, provider_spec, provider_service,
                provider_extension, nullptr, provider_second, error))
      << error;
  AttemptIdentity consumer_second;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_metadata(
                *catalog_, consumer_spec, consumer_service,
                consumer_extension, nullptr, consumer_second, error))
      << error;
  EXPECT_EQ(provider_first.generation_ + 1, provider_second.generation_);
  EXPECT_EQ(consumer_first.generation_ + 1, consumer_second.generation_);

  {
    ObSQLiteConnectionGuard metadata(&pool_);
    ASSERT_NE(nullptr, metadata.get_connection());
    int64_t count = 0;
    ASSERT_EQ(
        OB_SUCCESS,
        query_test_count(
            *metadata.get_connection(),
            "SELECT COUNT(*) FROM __all_plugin_dependency "
            "WHERE consumer_kind=? AND consumer_plugin_id=? "
            "AND consumer_generation=? AND provider_plugin_id=? "
            "AND provider_generation=? AND dependency_id=?",
            [&](ObSQLiteBinder &binder) {
              int ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDependencyConsumerKind::PLUGIN));
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, consumer_id);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(
                    static_cast<int64_t>(consumer_first.generation_));
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, provider_id);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(
                    static_cast<int64_t>(provider_first.generation_));
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, provider_service);
              }
              return ret;
            },
            count));
    EXPECT_EQ(1, count);
    ASSERT_EQ(
        OB_SUCCESS,
        query_test_count(
            *metadata.get_connection(),
            "SELECT COUNT(*) FROM __all_plugin_dependency "
            "WHERE consumer_kind=? AND consumer_plugin_id=? "
            "AND consumer_generation=?",
            [&](ObSQLiteBinder &binder) {
              int ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDependencyConsumerKind::PLUGIN));
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, consumer_id);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(
                    static_cast<int64_t>(consumer_second.generation_));
              }
              return ret;
            },
            count));
    EXPECT_EQ(0, count);
  }

  blockers.clear();
  ASSERT_EQ(OB_SUCCESS,
            catalog_->list_restrict_blockers(provider_id, blockers));
  EXPECT_TRUE(blockers.empty());
  ASSERT_EQ(OB_SUCCESS,
            catalog_->begin_restricted_disable(
                provider_id, provider_second.generation_, disable, error))
      << error;
  ASSERT_NE(nullptr, disable.get());
  ObPluginRuntimeDisableResult stayed_active;
  stayed_active.status_ = OB_EAGAIN;
  stayed_active.generation_ = provider_second.generation_;
  stayed_active.actual_state_ = ObPluginState::ACTIVE;
  stayed_active.phase_ = ObPluginDisablePhase::QUIESCE;
  stayed_active.stop_entered_ = false;
  stayed_active.error_ = "injected quiesce cancellation";
  ASSERT_EQ(OB_SUCCESS, disable->finish(stayed_active, error)) << error;
  disable.reset();
  EXPECT_EQ(OB_SUCCESS, catalog_->check_server_ready(error)) << error;
}

TEST_F(TestPluginCatalog,
       same_service_id_with_distinct_abi_majors_persists_both_dependencies)
{
  const std::string provider_id = "org.seekdb.catalog-abi-provider";
  const std::string consumer_id = "org.seekdb.catalog-abi-consumer";
  const std::string service_id = "org.seekdb.catalog-abi-provider.service";
  ObPluginPackageInstallSpec provider_spec = make_install_spec(provider_id);
  provider_spec.relative_path_ = "catalog-abi-provider/plugin.so";
  provider_spec.artifact_.build_id_ = "catalog-abi-provider-build-v1";
  provider_spec.artifact_.package_digest_ =
      "sha256:cccccccccccccccccccccccccccccccc"
      "cccccccccccccccccccccccccccccccc";
  ObPluginPackageInstallSpec consumer_spec = make_install_spec(consumer_id);
  consumer_spec.relative_path_ = "catalog-abi-consumer/plugin.so";
  consumer_spec.artifact_.build_id_ = "catalog-abi-consumer-build-v1";
  consumer_spec.artifact_.package_digest_ =
      "sha256:dddddddddddddddddddddddddddddddd"
      "dddddddddddddddddddddddddddddddd";

  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, provider_spec, error))
      << error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, consumer_spec, error))
      << error;

  std::unique_ptr<ObPluginActivationPermit> provider_permit;
  ASSERT_EQ(OB_SUCCESS,
            begin_attempt(*catalog_, provider_spec, provider_permit, error))
      << error;
  ASSERT_NE(nullptr, provider_permit.get());
  const AttemptIdentity provider_identity = identity_of(*provider_permit);
  ObPluginRuntimeActivationResult provider_candidate =
      make_candidate_result(*provider_permit);
  ObPluginServiceInfo service_v1 =
      make_service_info(provider_spec, *provider_permit, service_id);
  ObPluginServiceInfo service_v2 = service_v1;
  service_v2.abi_major_ = 2;
  provider_candidate.services_.push_back(service_v1);
  provider_candidate.services_.push_back(service_v2);
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  std::unique_ptr<ObPluginActivationCommit> provider_commit;
  ASSERT_EQ(OB_SUCCESS,
            provider_permit->commit_candidate(
                provider_candidate, decision, provider_commit, error))
      << error;
  ASSERT_EQ(OB_PLUGIN_ACTIVATION_PROMOTE, decision);
  ASSERT_NE(nullptr, provider_commit.get());
  ASSERT_EQ(OB_SUCCESS,
            provider_commit->complete(
                make_active_result(provider_candidate), error))
      << error;

  std::unique_ptr<ObPluginActivationPermit> consumer_permit;
  ASSERT_EQ(OB_SUCCESS,
            begin_attempt(*catalog_, consumer_spec, consumer_permit, error))
      << error;
  ASSERT_NE(nullptr, consumer_permit.get());
  const AttemptIdentity consumer_identity = identity_of(*consumer_permit);
  ObPluginRuntimeServiceDependency dependency_v1 =
      make_runtime_service_dependency(
          provider_id, provider_identity.generation_, service_id);
  ObPluginRuntimeServiceDependency dependency_v2 = dependency_v1;
  dependency_v2.requested_version_.minimum_inclusive = {2, 0, 0};
  dependency_v2.requested_version_.maximum_exclusive = {3, 0, 0};
  dependency_v2.provider_version_ = {2, 4, 2};
  ObPluginRuntimeActivationResult consumer_candidate =
      make_candidate_result(*consumer_permit);
  consumer_candidate.dependencies_.push_back(dependency_v1);
  consumer_candidate.dependencies_.push_back(dependency_v2);
  decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  std::unique_ptr<ObPluginActivationCommit> consumer_commit;
  ASSERT_EQ(OB_SUCCESS,
            consumer_permit->commit_candidate(
                consumer_candidate, decision, consumer_commit, error))
      << error;
  ASSERT_EQ(OB_PLUGIN_ACTIVATION_PROMOTE, decision);
  ASSERT_NE(nullptr, consumer_commit.get());
  ASSERT_EQ(OB_SUCCESS,
            consumer_commit->complete(
                make_active_result(consumer_candidate), error))
      << error;

  ObSQLiteConnectionGuard metadata(&pool_);
  ASSERT_NE(nullptr, metadata.get_connection());
  int64_t count = 0;
  ASSERT_EQ(
      OB_SUCCESS,
      query_test_count(
          *metadata.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_dependency "
          "WHERE consumer_kind=? AND consumer_plugin_id=? "
          "AND consumer_generation=? AND provider_plugin_id=? "
          "AND provider_generation=? AND dependency_kind=? "
          "AND dependency_id=? AND "
          "((service_abi_major=1 AND requested_min_version_major=1 "
          "AND requested_max_version_major=2 AND provider_version_major=1) "
          "OR (service_abi_major=2 AND requested_min_version_major=2 "
          "AND requested_max_version_major=3 AND provider_version_major=2))",
          [&](ObSQLiteBinder &binder) {
            int ret = binder.bind_int(static_cast<int32_t>(
                ObPluginDependencyConsumerKind::PLUGIN));
            if (OB_SUCCESS == ret) ret = bind_test_string(binder, consumer_id);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int64(
                  static_cast<int64_t>(consumer_identity.generation_));
            }
            if (OB_SUCCESS == ret) ret = bind_test_string(binder, provider_id);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int64(
                  static_cast<int64_t>(provider_identity.generation_));
            }
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDependencyKind::SERVICE));
            }
            if (OB_SUCCESS == ret) ret = bind_test_string(binder, service_id);
            return ret;
          },
          count));
  EXPECT_EQ(2, count);
}

TEST_F(TestPluginCatalog,
       generic_service_dependency_preserves_abi_major_identity)
{
  const std::string service_id =
      "org.seekdb.catalog-test.generic-service";
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity provider;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_metadata(
                *catalog_, spec, service_id,
                "org.seekdb.catalog-test.generic-function", nullptr,
                provider, error))
      << error;

  ObPluginDependencySpec dependency;
  dependency.consumer_kind_ = ObPluginDependencyConsumerKind::USER_OBJECT;
  dependency.consumer_id_ = "table_42.generated_expression";
  dependency.provider_plugin_id_ = spec.artifact_.plugin_id_;
  dependency.provider_generation_ = provider.generation_;
  dependency.dependency_kind_ = ObPluginDependencyKind::SERVICE;
  dependency.dependency_id_ = service_id;
  dependency.service_abi_major_ = 1;
  dependency.requested_version_.minimum_inclusive = {1, 0, 0};
  dependency.requested_version_.maximum_exclusive = {2, 0, 0};
  dependency.required_capabilities_ = 1;
  ASSERT_EQ(OB_SUCCESS, catalog_->add_dependency(dependency, error)) << error;

  std::vector<ObPluginRestrictBlocker> blockers;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->list_restrict_blockers(
                spec.artifact_.plugin_id_, blockers));
  ASSERT_EQ(1U, blockers.size());
  EXPECT_EQ(ObPluginDependencyKind::SERVICE,
            blockers[0].dependency_kind_);
  EXPECT_EQ(1U, blockers[0].service_abi_major_);

  ASSERT_EQ(OB_SUCCESS, catalog_->remove_dependency(dependency, error))
      << error;
  blockers.clear();
  ASSERT_EQ(OB_SUCCESS,
            catalog_->list_restrict_blockers(
                spec.artifact_.plugin_id_, blockers));
  EXPECT_TRUE(blockers.empty());
}

TEST_F(TestPluginCatalog,
       persistent_dependency_blocks_restrict_until_removed_then_tombstones)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity identity;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_persistent_format(
                *catalog_, spec, identity, error))
      << error;

  const ObPluginDependencySpec dependency =
      make_persistent_dependency(identity.generation_);
  ASSERT_EQ(OB_SUCCESS, catalog_->add_dependency(dependency, error)) << error;

  std::vector<ObPluginRestrictBlocker> blockers;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->list_restrict_blockers(TEST_PLUGIN_ID, blockers));
  ASSERT_EQ(1U, blockers.size());
  EXPECT_EQ(ObPluginDependencyConsumerKind::PERSISTENT_DATA,
            blockers[0].consumer_kind_);
  EXPECT_EQ(dependency.consumer_id_, blockers[0].consumer_id_);

  std::unique_ptr<ObPluginDisablePermit> disable;
  int ret = catalog_->begin_restricted_disable(
      TEST_PLUGIN_ID, identity.generation_, disable, error);
  EXPECT_NE(OB_SUCCESS, ret);
  EXPECT_EQ(nullptr, disable.get());

  blockers.clear();
  ret = catalog_->uninstall_restrict(
      TEST_PLUGIN_ID, "operator.uninstall", "audit.blocked-uninstall",
      blockers, error);
  EXPECT_NE(OB_SUCCESS, ret);
  ASSERT_EQ(1U, blockers.size());
  EXPECT_EQ(dependency.consumer_id_, blockers[0].consumer_id_);

  ASSERT_EQ(OB_SUCCESS, catalog_->remove_dependency(dependency, error))
      << error;
  blockers.clear();
  ASSERT_EQ(OB_SUCCESS,
            catalog_->list_restrict_blockers(TEST_PLUGIN_ID, blockers));
  EXPECT_TRUE(blockers.empty());

  ASSERT_EQ(OB_SUCCESS,
            catalog_->begin_restricted_disable(
                TEST_PLUGIN_ID, identity.generation_, disable, error))
      << error;
  ASSERT_NE(nullptr, disable.get());
  ObPluginRuntimeDisableResult stopped;
  stopped.status_ = OB_SUCCESS;
  stopped.generation_ = identity.generation_;
  stopped.actual_state_ = ObPluginState::STOPPED;
  stopped.phase_ = ObPluginDisablePhase::COMPLETE;
  stopped.stop_entered_ = true;
  ASSERT_EQ(OB_SUCCESS, disable->record_stop_entered(error)) << error;
  ASSERT_EQ(OB_SUCCESS, disable->finish(stopped, error)) << error;
  EXPECT_EQ(OB_SUCCESS, disable->finish(stopped, error)) << error;
  disable.reset();

  ObPluginCatalogRecord stopped_record;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->get_record(TEST_PLUGIN_ID, stopped_record));
  EXPECT_EQ(ObPluginDesiredState::DISABLED, stopped_record.desired_state_);
  EXPECT_EQ(ObPluginState::STOPPED, stopped_record.actual_state_);

  blockers.clear();
  ASSERT_EQ(OB_SUCCESS,
            catalog_->uninstall_restrict(
                TEST_PLUGIN_ID, "operator.uninstall", "audit.uninstall",
                blockers, error))
      << error;
  EXPECT_TRUE(blockers.empty());

  ObPluginCatalogRecord tombstone;
  ASSERT_EQ(OB_SUCCESS, catalog_->get_record(TEST_PLUGIN_ID, tombstone));
  EXPECT_EQ(ObPluginDesiredState::UNINSTALLED, tombstone.desired_state_);
  EXPECT_EQ(ObPluginState::STOPPED, tombstone.actual_state_);
  EXPECT_EQ(identity.generation_, tombstone.generation_);

  // Reinstalling the same immutable R1 package may reuse its catalog row, but
  // it must not reuse any runtime fencing identity from the tombstoned life.
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  std::unique_ptr<ObPluginActivationPermit> reinstalled;
  ASSERT_EQ(OB_SUCCESS,
            begin_attempt(*catalog_, spec, reinstalled, error))
      << error;
  ASSERT_NE(nullptr, reinstalled.get());
  const AttemptIdentity reinstalled_identity = identity_of(*reinstalled);
  EXPECT_GT(reinstalled_identity.generation_, identity.generation_);
  EXPECT_NE(reinstalled_identity.runtime_incarnation_,
            identity.runtime_incarnation_);
  EXPECT_NE(reinstalled_identity.operation_id_, identity.operation_id_);
  ASSERT_EQ(OB_SUCCESS,
            reinstalled->abort(make_abort_result(*reinstalled), error))
      << error;
}

TEST_F(TestPluginCatalog,
       plugin_consumer_rejects_non_service_dependency_kinds)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity provider;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_persistent_format(
                *catalog_, spec, provider, error))
      << error;

  ObPluginDependencySpec dependency =
      make_persistent_dependency(provider.generation_);
  dependency.consumer_kind_ = ObPluginDependencyConsumerKind::PLUGIN;
  dependency.consumer_id_ = "org.seekdb.invalid-consumer";
  dependency.consumer_plugin_id_ = dependency.consumer_id_;
  dependency.consumer_generation_ = 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            catalog_->add_dependency(dependency, error));
  EXPECT_FALSE(error.empty());
}

TEST_F(TestPluginCatalog,
       unbounded_dependency_range_never_crosses_abi_major)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;

  std::unique_ptr<ObPluginActivationPermit> permit;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, permit, error)) << error;
  ASSERT_NE(nullptr, permit.get());
  ObPluginRuntimeActivationResult candidate =
      make_persistent_format_candidate(spec, *permit);
  ASSERT_EQ(1U, candidate.extensions_.size());
  candidate.extensions_[0].spec_.physical_format_version_ = 2;
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  std::unique_ptr<ObPluginActivationCommit> commit;
  ASSERT_EQ(OB_SUCCESS,
            permit->commit_candidate(candidate, decision, commit, error))
      << error;
  ASSERT_EQ(OB_PLUGIN_ACTIVATION_PROMOTE, decision);
  ASSERT_NE(nullptr, commit.get());
  ASSERT_EQ(OB_SUCCESS,
            commit->complete(make_active_result(candidate), error))
      << error;

  ObPluginDependencySpec dependency =
      make_persistent_dependency(permit->generation());
  dependency.requested_version_.maximum_exclusive = {0, 0, 0};
  EXPECT_EQ(OB_STATE_NOT_MATCH,
            catalog_->add_dependency(dependency, error));
  EXPECT_FALSE(error.empty());
}

TEST_F(TestPluginCatalog,
       disable_finish_rejects_contradictory_and_conflicting_observations)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity active;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete(*catalog_, spec, active, error))
      << error;

  std::unique_ptr<ObPluginDisablePermit> disable;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->begin_restricted_disable(
                TEST_PLUGIN_ID, active.generation_, disable, error))
      << error;
  ASSERT_NE(nullptr, disable.get());

  ObPluginRuntimeDisableResult contradictory;
  contradictory.status_ = OB_ERR_UNEXPECTED;
  contradictory.generation_ = active.generation_;
  contradictory.actual_state_ = ObPluginState::QUIESCING;
  contradictory.phase_ = ObPluginDisablePhase::STOP;
  contradictory.stop_entered_ = false;
  contradictory.error_ = "stop phase reported before stop entry";
  EXPECT_NE(OB_SUCCESS, disable->finish(contradictory, error));
  EXPECT_FALSE(error.empty());

  // The runtime records the irreversible STOP boundary before reporting any
  // STOP/COMPLETE observation to the catalog.
  ASSERT_EQ(OB_SUCCESS, disable->record_stop_entered(error)) << error;
  ObPluginRuntimeDisableResult stopped;
  stopped.status_ = OB_SUCCESS;
  stopped.generation_ = active.generation_;
  stopped.actual_state_ = ObPluginState::STOPPED;
  stopped.phase_ = ObPluginDisablePhase::COMPLETE;
  stopped.stop_entered_ = true;
  ASSERT_EQ(OB_SUCCESS, disable->finish(stopped, error)) << error;
  EXPECT_EQ(OB_SUCCESS, disable->finish(stopped, error)) << error;

  // Idempotence is exact-observation idempotence.  A different, individually
  // valid observation must not overwrite the completed result.
  ObPluginRuntimeDisableResult conflicting;
  conflicting.status_ = OB_ERR_UNEXPECTED;
  conflicting.generation_ = active.generation_;
  conflicting.actual_state_ = ObPluginState::BLOCKED;
  conflicting.phase_ = ObPluginDisablePhase::STOP;
  conflicting.stop_entered_ = true;
  conflicting.error_ = "conflicting replay after durable stop";
  EXPECT_NE(OB_SUCCESS, disable->finish(conflicting, error));
  EXPECT_FALSE(error.empty());

  ObPluginCatalogRecord record;
  ASSERT_EQ(OB_SUCCESS, catalog_->get_record(TEST_PLUGIN_ID, record));
  EXPECT_EQ(ObPluginDesiredState::DISABLED, record.desired_state_);
  EXPECT_EQ(ObPluginState::STOPPED, record.actual_state_);
  EXPECT_EQ(static_cast<int32_t>(ObPluginDisablePhase::COMPLETE),
            record.last_phase_);
  EXPECT_EQ(OB_SUCCESS, record.last_status_);
  EXPECT_TRUE(record.last_error_.empty());
}

TEST_F(TestPluginCatalog,
       stable_dependency_rebinds_to_fresh_provider_generation)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity first;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_persistent_format(
                *catalog_, spec, first, error))
      << error;

  const ObPluginDependencySpec dependency =
      make_persistent_dependency(first.generation_);
  ASSERT_EQ(OB_SUCCESS, catalog_->add_dependency(dependency, error)) << error;

  auto count_dependency_generation = [&](const uint64_t generation,
                                         int64_t &count) {
    ObSQLiteConnectionGuard observation(&pool_);
    if (nullptr == observation.get_connection()) {
      return OB_NOT_INIT;
    }
    return query_test_count(
        *observation.get_connection(),
        "SELECT COUNT(*) FROM __all_plugin_dependency "
        "WHERE consumer_kind=? AND consumer_id=? AND provider_plugin_id=? "
        "AND provider_generation=? AND dependency_kind=? AND dependency_id=?",
        [&](ObSQLiteBinder &binder) {
          int ret = binder.bind_int(static_cast<int32_t>(
              ObPluginDependencyConsumerKind::PERSISTENT_DATA));
          if (OB_SUCCESS == ret) {
            ret = bind_test_string(binder, dependency.consumer_id_);
          }
          if (OB_SUCCESS == ret) {
            ret = bind_test_string(binder, dependency.provider_plugin_id_);
          }
          if (OB_SUCCESS == ret) {
            ret = binder.bind_int64(static_cast<int64_t>(generation));
          }
          if (OB_SUCCESS == ret) {
            ret = binder.bind_int(static_cast<int32_t>(
                ObPluginDependencyKind::PERSISTENT_FORMAT));
          }
          if (OB_SUCCESS == ret) {
            ret = bind_test_string(binder, dependency.dependency_id_);
          }
          return ret;
        },
        count);
  };

  ASSERT_EQ(OB_SUCCESS, reopen_catalog());
  std::vector<ObPluginStartupEntry> entries;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->prepare_startup_recovery(entries, error))
      << error;
  ASSERT_EQ(1U, entries.size());
  ASSERT_EQ(TEST_PLUGIN_ID, entries[0].plugin_id_);
  ASSERT_FALSE(entries[0].exact_recovery_);

  std::unique_ptr<ObPluginActivationPermit> permit;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, permit, error)) << error;
  ASSERT_NE(nullptr, permit.get());
  const AttemptIdentity second = identity_of(*permit);
  ASSERT_EQ(first.generation_ + 1, second.generation_);

  int64_t count = 0;
  ASSERT_EQ(OB_SUCCESS,
            count_dependency_generation(first.generation_, count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(OB_SUCCESS,
            count_dependency_generation(second.generation_, count));
  EXPECT_EQ(0, count);

  const ObPluginRuntimeActivationResult candidate =
      make_persistent_format_candidate(spec, *permit);
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  std::unique_ptr<ObPluginActivationCommit> commit;
  ASSERT_EQ(OB_SUCCESS,
            permit->commit_candidate(candidate, decision, commit, error))
      << error;
  ASSERT_EQ(OB_PLUGIN_ACTIVATION_PROMOTE, decision);
  ASSERT_NE(nullptr, commit.get());

  // The stable edge changes fence in the same candidate transaction; begin
  // alone must never expose a dependency on an unvalidated runtime attempt.
  ASSERT_EQ(OB_SUCCESS,
            count_dependency_generation(first.generation_, count));
  EXPECT_EQ(0, count);
  ASSERT_EQ(OB_SUCCESS,
            count_dependency_generation(second.generation_, count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(OB_SUCCESS,
            commit->complete(make_active_result(candidate), error))
      << error;

  EXPECT_EQ(OB_SUCCESS, catalog_->check_server_ready(error)) << error;
  std::vector<ObPluginRestrictBlocker> blockers;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->list_restrict_blockers(TEST_PLUGIN_ID, blockers));
  ASSERT_EQ(1U, blockers.size());
  EXPECT_EQ(ObPluginDependencyConsumerKind::PERSISTENT_DATA,
            blockers[0].consumer_kind_);
  EXPECT_EQ(dependency.consumer_id_, blockers[0].consumer_id_);
  std::unique_ptr<ObPluginDisablePermit> disable;
  EXPECT_EQ(OB_OP_NOT_ALLOW,
            catalog_->begin_restricted_disable(
                TEST_PLUGIN_ID, second.generation_, disable, error));
  EXPECT_EQ(nullptr, disable.get());
}

TEST_F(TestPluginCatalog,
       stable_dependency_rejects_fresh_candidate_that_drops_format)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity first;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_persistent_format(
                *catalog_, spec, first, error))
      << error;

  const ObPluginDependencySpec dependency =
      make_persistent_dependency(first.generation_);
  ASSERT_EQ(OB_SUCCESS, catalog_->add_dependency(dependency, error)) << error;

  ASSERT_EQ(OB_SUCCESS, reopen_catalog());
  std::vector<ObPluginStartupEntry> entries;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->prepare_startup_recovery(entries, error))
      << error;

  std::unique_ptr<ObPluginActivationPermit> permit;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, permit, error)) << error;
  ASSERT_NE(nullptr, permit.get());
  const AttemptIdentity second = identity_of(*permit);
  ASSERT_EQ(first.generation_ + 1, second.generation_);

  // This is a structurally valid fresh candidate, but it no longer publishes
  // the TYPE extension which owns the stable physical format.
  const ObPluginRuntimeActivationResult candidate =
      make_candidate_result(*permit);
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  std::unique_ptr<ObPluginActivationCommit> commit;
  EXPECT_NE(OB_SUCCESS,
            permit->commit_candidate(candidate, decision, commit, error));
  EXPECT_EQ(OB_PLUGIN_ACTIVATION_NOT_COMMITTED, decision);
  EXPECT_EQ(nullptr, commit.get());
  EXPECT_FALSE(error.empty());

  auto count_dependency_generation = [&](const uint64_t generation,
                                         int64_t &count) {
    ObSQLiteConnectionGuard observation(&pool_);
    if (nullptr == observation.get_connection()) {
      return OB_NOT_INIT;
    }
    return query_test_count(
        *observation.get_connection(),
        "SELECT COUNT(*) FROM __all_plugin_dependency "
        "WHERE consumer_kind=? AND consumer_id=? AND provider_plugin_id=? "
        "AND provider_generation=? AND dependency_kind=? AND dependency_id=?",
        [&](ObSQLiteBinder &binder) {
          int ret = binder.bind_int(static_cast<int32_t>(
              ObPluginDependencyConsumerKind::PERSISTENT_DATA));
          if (OB_SUCCESS == ret) {
            ret = bind_test_string(binder, dependency.consumer_id_);
          }
          if (OB_SUCCESS == ret) {
            ret = bind_test_string(binder, dependency.provider_plugin_id_);
          }
          if (OB_SUCCESS == ret) {
            ret = binder.bind_int64(static_cast<int64_t>(generation));
          }
          if (OB_SUCCESS == ret) {
            ret = binder.bind_int(static_cast<int32_t>(
                ObPluginDependencyKind::PERSISTENT_FORMAT));
          }
          if (OB_SUCCESS == ret) {
            ret = bind_test_string(binder, dependency.dependency_id_);
          }
          return ret;
        },
        count);
  };

  // Stable-fence rebinding is in the rejected candidate transaction, so the
  // old production fence remains authoritative and no generation-2 edge is
  // exposed.
  int64_t count = 0;
  ASSERT_EQ(OB_SUCCESS,
            count_dependency_generation(first.generation_, count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(OB_SUCCESS,
            count_dependency_generation(second.generation_, count));
  EXPECT_EQ(0, count);

  ASSERT_EQ(OB_SUCCESS,
            permit->abort(make_abort_result(*permit), error))
      << error;
  permit.reset();

  ASSERT_EQ(OB_SUCCESS,
            count_dependency_generation(first.generation_, count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(OB_SUCCESS,
            count_dependency_generation(second.generation_, count));
  EXPECT_EQ(0, count);

  ObPluginCatalogRecord failed;
  ASSERT_EQ(OB_SUCCESS, catalog_->get_record(TEST_PLUGIN_ID, failed));
  EXPECT_EQ(second.generation_, failed.generation_);
  EXPECT_EQ(ObPluginState::FAILED, failed.actual_state_);
}

TEST_F(TestPluginCatalog,
       stable_dependency_rejects_corrupt_consumer_identity_without_rebind)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity first;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_persistent_format(
                *catalog_, spec, first, error))
      << error;

  const ObPluginDependencySpec dependency =
      make_persistent_dependency(first.generation_);
  ASSERT_EQ(OB_SUCCESS, catalog_->add_dependency(dependency, error)) << error;

  // Simulate a damaged durable stable edge.  Activation must reject this row
  // as catalog corruption; it must never normalize the identity while moving
  // the provider generation fence forward.
  const std::string corrupt_consumer_plugin_id = "corrupt.owner";
  const int64_t corrupt_consumer_generation = 17;
  {
    ObSQLiteConnectionGuard corruption(&pool_);
    ASSERT_NE(nullptr, corruption.get_connection());
    int64_t affected_rows = 0;
    ASSERT_EQ(
        OB_SUCCESS,
        corruption->execute(
            "UPDATE __all_plugin_dependency SET consumer_plugin_id=?,"
            "consumer_generation=?,optional=1 WHERE consumer_kind=? AND "
            "consumer_id=? AND consumer_plugin_id='' AND "
            "consumer_generation=0 AND provider_plugin_id=? AND "
            "provider_generation=? AND dependency_kind=? AND dependency_id=?",
            [&](ObSQLiteBinder &binder) {
              int ret = bind_test_string(
                  binder, corrupt_consumer_plugin_id);
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(corrupt_consumer_generation);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginDependencyConsumerKind::PERSISTENT_DATA));
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, dependency.consumer_id_);
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(
                    binder, dependency.provider_plugin_id_);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(
                    static_cast<int64_t>(first.generation_));
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginDependencyKind::PERSISTENT_FORMAT));
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, dependency.dependency_id_);
              }
              return ret;
            },
            &affected_rows));
    ASSERT_EQ(1, affected_rows);
  }

  ASSERT_EQ(OB_SUCCESS, reopen_catalog());
  std::vector<ObPluginStartupEntry> entries;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->prepare_startup_recovery(entries, error))
      << error;

  std::unique_ptr<ObPluginActivationPermit> permit;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, permit, error)) << error;
  ASSERT_NE(nullptr, permit.get());
  const AttemptIdentity second = identity_of(*permit);
  ASSERT_EQ(first.generation_ + 1, second.generation_);

  const ObPluginRuntimeActivationResult candidate =
      make_persistent_format_candidate(spec, *permit);
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  std::unique_ptr<ObPluginActivationCommit> commit;
  EXPECT_NE(OB_SUCCESS,
            permit->commit_candidate(candidate, decision, commit, error));
  EXPECT_EQ(OB_PLUGIN_ACTIVATION_NOT_COMMITTED, decision);
  EXPECT_EQ(nullptr, commit.get());
  EXPECT_NE(std::string::npos, error.find("consumer identity")) << error;

  auto count_corrupt_dependency_generation =
      [&](const uint64_t generation, int64_t &count) {
        ObSQLiteConnectionGuard observation(&pool_);
        if (nullptr == observation.get_connection()) {
          return OB_NOT_INIT;
        }
        return query_test_count(
            *observation.get_connection(),
            "SELECT COUNT(*) FROM __all_plugin_dependency WHERE "
            "consumer_kind=? AND consumer_id=? AND consumer_plugin_id=? AND "
            "consumer_generation=? AND provider_plugin_id=? AND "
            "provider_generation=? AND dependency_kind=? AND dependency_id=? "
            "AND optional=1",
            [&](ObSQLiteBinder &binder) {
              int ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDependencyConsumerKind::PERSISTENT_DATA));
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, dependency.consumer_id_);
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(
                    binder, corrupt_consumer_plugin_id);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(corrupt_consumer_generation);
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(
                    binder, dependency.provider_plugin_id_);
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int64(static_cast<int64_t>(generation));
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginDependencyKind::PERSISTENT_FORMAT));
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, dependency.dependency_id_);
              }
              return ret;
            },
            count);
      };

  // The candidate transaction is rejected before any repair/rebind.  The
  // damaged row remains visible only at its original production fence.
  int64_t count = 0;
  ASSERT_EQ(OB_SUCCESS,
            count_corrupt_dependency_generation(first.generation_, count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(OB_SUCCESS,
            count_corrupt_dependency_generation(second.generation_, count));
  EXPECT_EQ(0, count);

  ASSERT_EQ(OB_SUCCESS,
            permit->abort(make_abort_result(*permit), error))
      << error;
  permit.reset();

  ASSERT_EQ(OB_SUCCESS,
            count_corrupt_dependency_generation(first.generation_, count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(OB_SUCCESS,
            count_corrupt_dependency_generation(second.generation_, count));
  EXPECT_EQ(0, count);
}

TEST_F(TestPluginCatalog,
       startup_preserves_blocked_disable_recovery_evidence)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity active;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete(*catalog_, spec, active, error))
      << error;

  std::unique_ptr<ObPluginDisablePermit> disable;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->begin_restricted_disable(
                TEST_PLUGIN_ID, active.generation_, disable, error))
      << error;
  ASSERT_NE(nullptr, disable.get());
  ObPluginCatalogRecord disabling;
  ASSERT_EQ(OB_SUCCESS, catalog_->get_record(TEST_PLUGIN_ID, disabling));
  ASSERT_FALSE(disabling.operation_id_.empty());
  const std::string disable_operation_id = disabling.operation_id_;

  ObPluginRuntimeDisableResult blocked;
  blocked.status_ = OB_ERR_UNEXPECTED;
  blocked.generation_ = active.generation_;
  blocked.actual_state_ = ObPluginState::BLOCKED;
  blocked.phase_ = ObPluginDisablePhase::STOP;
  blocked.stop_entered_ = true;
  blocked.error_ = "injected stop callback failure";
  ASSERT_EQ(OB_SUCCESS, disable->record_stop_entered(error)) << error;
  ASSERT_EQ(OB_SUCCESS, disable->finish(blocked, error)) << error;
  // A repeated observation on the same completed token is idempotent.
  EXPECT_EQ(OB_SUCCESS, disable->finish(blocked, error)) << error;
  disable.reset();

  ObPluginCatalogRecord before_restart;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->get_record(TEST_PLUGIN_ID, before_restart));
  EXPECT_EQ(ObPluginDesiredState::DISABLED, before_restart.desired_state_);
  EXPECT_EQ(ObPluginState::BLOCKED, before_restart.actual_state_);
  EXPECT_EQ(disable_operation_id, before_restart.operation_id_);
  ASSERT_EQ(OB_SUCCESS, reopen_catalog());

  std::vector<ObPluginStartupEntry> entries;
  EXPECT_EQ(OB_STATE_NOT_MATCH,
            catalog_->prepare_startup_recovery(entries, error));
  EXPECT_TRUE(entries.empty());
  EXPECT_FALSE(error.empty());
  // Repeating startup planning must fail on the same evidence; the first
  // failure must not have normalized the operation to COMPLETED/STOPPED.
  error.clear();
  EXPECT_EQ(OB_STATE_NOT_MATCH,
            catalog_->prepare_startup_recovery(entries, error));
  EXPECT_TRUE(entries.empty());

  ObPluginCatalogRecord after_restart;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->get_record(TEST_PLUGIN_ID, after_restart));
  EXPECT_EQ(ObPluginDesiredState::DISABLED, after_restart.desired_state_);
  EXPECT_EQ(ObPluginState::BLOCKED, after_restart.actual_state_);
  EXPECT_EQ(active.generation_, after_restart.generation_);
  EXPECT_EQ(disable_operation_id, after_restart.operation_id_);

  ObSQLiteConnectionGuard observation(&pool_);
  ASSERT_NE(nullptr, observation.get_connection());
  int64_t count = 0;
  ASSERT_EQ(
      OB_SUCCESS,
      query_test_count(
          *observation.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_operation WHERE operation_id=? "
          "AND kind=? AND state=? AND phase=? AND status=? "
          "AND actual_state=? AND stop_entered=1",
          [&](ObSQLiteBinder &binder) {
            int ret = bind_test_string(binder, disable_operation_id);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationKind::DISABLE));
            }
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::RECOVERY_REQUIRED));
            }
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginDisablePhase::STOP));
            }
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(OB_ERR_UNEXPECTED);
            }
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginState::BLOCKED));
            }
            return ret;
          },
          count));
  EXPECT_EQ(1, count);
}

TEST_F(TestPluginCatalog, startup_replays_promote_pending_exact_identity)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;

  std::unique_ptr<ObPluginActivationPermit> permit;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, permit, error)) << error;
  ASSERT_NE(nullptr, permit.get());
  const AttemptIdentity identity = identity_of(*permit);
  const ObPluginRuntimeActivationResult candidate =
      make_candidate_result(*permit);
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  std::unique_ptr<ObPluginActivationCommit> commit;
  ASSERT_EQ(OB_SUCCESS,
            permit->commit_candidate(candidate, decision, commit, error))
      << error;
  ASSERT_EQ(OB_PLUGIN_ACTIVATION_PROMOTE, decision);
  ASSERT_NE(nullptr, commit.get());

  // Simulate a crash after the durable PROMOTE decision but before registry
  // publication/complete.  Neither token may turn this into an abort.
  commit.reset();
  permit.reset();
  ASSERT_EQ(OB_SUCCESS, reopen_catalog());

  std::vector<ObPluginStartupEntry> entries;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->prepare_startup_recovery(entries, error))
      << error;
  const ObPluginStartupEntry *entry =
      find_startup_entry(entries, TEST_PLUGIN_ID);
  ASSERT_NE(nullptr, entry);
  EXPECT_TRUE(entry->exact_recovery_);
  EXPECT_EQ(spec.relative_path_, entry->relative_path_);
  EXPECT_EQ(identity.generation_, entry->recovery_.generation_);
  EXPECT_EQ(identity.runtime_incarnation_,
            entry->recovery_.runtime_incarnation_);
  EXPECT_EQ(identity.operation_id_, entry->recovery_.operation_id_);
  EXPECT_EQ(spec.artifact_.package_digest_,
            entry->recovery_.package_digest_);
  EXPECT_NE(OB_SUCCESS, catalog_->check_server_ready(error));
}

TEST_F(TestPluginCatalog,
       promote_pending_replay_validation_failure_is_unknown_and_not_abortable)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  const std::string service_id = "org.seekdb.catalog-test.replay-service";
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;

  std::unique_ptr<ObPluginActivationPermit> permit;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, permit, error)) << error;
  ASSERT_NE(nullptr, permit.get());
  const AttemptIdentity identity = identity_of(*permit);
  ObPluginRuntimeActivationResult candidate = make_candidate_result(*permit);
  candidate.services_.push_back(make_service_info(spec, *permit, service_id));
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  std::unique_ptr<ObPluginActivationCommit> commit;
  ASSERT_EQ(OB_SUCCESS,
            permit->commit_candidate(candidate, decision, commit, error))
      << error;
  ASSERT_EQ(OB_PLUGIN_ACTIVATION_PROMOTE, decision);
  ASSERT_NE(nullptr, commit.get());

  // Crash after the candidate transaction committed.  A replay-time
  // validation error cannot prove that the durable candidate is absent, so it
  // must never be downgraded to the abortable NOT_COMMITTED outcome.
  commit.reset();
  permit.reset();
  ASSERT_EQ(OB_SUCCESS, reopen_catalog());

  ObPluginActivationRequest recovery = make_activation_request(spec);
  recovery.mode_ = ObPluginActivationMode::STARTUP_RECOVERY;
  recovery.expected_generation_ = identity.generation_;
  recovery.expected_runtime_incarnation_ = identity.runtime_incarnation_;
  recovery.expected_operation_id_ = identity.operation_id_;
  std::unique_ptr<ObPluginActivationPermit> replay;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->begin_activation(recovery, replay, error))
      << error;
  ASSERT_NE(nullptr, replay.get());

  ObPluginRuntimeActivationResult invalid = make_candidate_result(*replay);
  ++invalid.generation_;
  decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
  commit.reset();
  EXPECT_NE(OB_SUCCESS,
            replay->commit_candidate(invalid, decision, commit, error));
  EXPECT_EQ(OB_PLUGIN_ACTIVATION_UNKNOWN, decision);
  EXPECT_EQ(nullptr, commit.get());

  std::string abort_error;
  EXPECT_NE(OB_SUCCESS,
            replay->abort(make_abort_result(*replay), abort_error));
  replay.reset();

  ObSQLiteConnectionGuard observation(&pool_);
  ASSERT_NE(nullptr, observation.get_connection());
  int64_t count = 0;
  ASSERT_EQ(
      OB_SUCCESS,
      query_test_count(
          *observation.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_operation WHERE operation_id=? "
          "AND state=?",
          [&](ObSQLiteBinder &binder) {
            int ret = bind_test_string(binder, identity.operation_id_);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::PROMOTE_PENDING));
            }
            return ret;
          },
          count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(
      OB_SUCCESS,
      query_test_count(
          *observation.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_service WHERE plugin_id=? "
          "AND generation=? AND service_id=?",
          [&](ObSQLiteBinder &binder) {
            int ret = bind_test_string(binder, spec.artifact_.plugin_id_);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int64(
                  static_cast<int64_t>(identity.generation_));
            }
            if (OB_SUCCESS == ret) {
              ret = bind_test_string(binder, service_id);
            }
            return ret;
          },
          count));
  EXPECT_EQ(1, count);
}

TEST_F(TestPluginCatalog,
       promote_pending_exact_replay_cannot_rewrite_durable_service_set)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  const std::string original_service =
      "org.seekdb.catalog-test.replay-original";
  const std::string replacement_service =
      "org.seekdb.catalog-test.replay-replacement";
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;

  std::unique_ptr<ObPluginActivationPermit> permit;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, permit, error)) << error;
  ASSERT_NE(nullptr, permit.get());
  const AttemptIdentity identity = identity_of(*permit);
  ObPluginRuntimeActivationResult candidate = make_candidate_result(*permit);
  candidate.services_.push_back(
      make_service_info(spec, *permit, original_service));
  ObPluginActivationDecision decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  std::unique_ptr<ObPluginActivationCommit> commit;
  ASSERT_EQ(OB_SUCCESS,
            permit->commit_candidate(candidate, decision, commit, error))
      << error;
  ASSERT_EQ(OB_PLUGIN_ACTIVATION_PROMOTE, decision);
  ASSERT_NE(nullptr, commit.get());

  // Simulate a crash after the exact candidate contribution and PROMOTE
  // decision became durable, but before completion.
  commit.reset();
  permit.reset();
  ASSERT_EQ(OB_SUCCESS, reopen_catalog());

  ObPluginActivationRequest recovery = make_activation_request(spec);
  recovery.mode_ = ObPluginActivationMode::STARTUP_RECOVERY;
  recovery.expected_generation_ = identity.generation_;
  recovery.expected_runtime_incarnation_ = identity.runtime_incarnation_;
  recovery.expected_operation_id_ = identity.operation_id_;
  std::unique_ptr<ObPluginActivationPermit> replay;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->begin_activation(recovery, replay, error))
      << error;
  ASSERT_NE(nullptr, replay.get());

  // The replay identity is exact and the replacement service is itself valid;
  // only the immutable durable contribution differs.
  ObPluginRuntimeActivationResult replacement =
      make_candidate_result(*replay);
  replacement.services_.push_back(
      make_service_info(spec, *replay, replacement_service));
  decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
  commit.reset();
  EXPECT_NE(OB_SUCCESS,
            replay->commit_candidate(
                replacement, decision, commit, error));
  EXPECT_EQ(OB_PLUGIN_ACTIVATION_UNKNOWN, decision);
  EXPECT_EQ(nullptr, commit.get());
  EXPECT_FALSE(error.empty());

  std::string abort_error;
  EXPECT_NE(OB_SUCCESS,
            replay->abort(make_abort_result(*replay), abort_error));
  replay.reset();

  ObSQLiteConnectionGuard observation(&pool_);
  ASSERT_NE(nullptr, observation.get_connection());
  auto count_service = [&](const std::string &service_id, int64_t &count) {
    return query_test_count(
        *observation.get_connection(),
        "SELECT COUNT(*) FROM __all_plugin_service WHERE plugin_id=? "
        "AND generation=? AND service_id=?",
        [&](ObSQLiteBinder &binder) {
          int ret = bind_test_string(binder, spec.artifact_.plugin_id_);
          if (OB_SUCCESS == ret) {
            ret = binder.bind_int64(
                static_cast<int64_t>(identity.generation_));
          }
          if (OB_SUCCESS == ret) {
            ret = bind_test_string(binder, service_id);
          }
          return ret;
        },
        count);
  };

  int64_t count = 0;
  ASSERT_EQ(OB_SUCCESS, count_service(original_service, count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(OB_SUCCESS, count_service(replacement_service, count));
  EXPECT_EQ(0, count);
  ASSERT_EQ(
      OB_SUCCESS,
      query_test_count(
          *observation.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_service WHERE plugin_id=? "
          "AND generation=?",
          [&](ObSQLiteBinder &binder) {
            int ret = bind_test_string(binder, spec.artifact_.plugin_id_);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int64(
                  static_cast<int64_t>(identity.generation_));
            }
            return ret;
          },
          count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(
      OB_SUCCESS,
      query_test_count(
          *observation.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_operation WHERE operation_id=? "
          "AND state=? AND candidate_prepared=1",
          [&](ObSQLiteBinder &binder) {
            int ret = bind_test_string(binder, identity.operation_id_);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::PROMOTE_PENDING));
            }
            return ret;
          },
          count));
  EXPECT_EQ(1, count);
}

TEST_F(TestPluginCatalog, startup_turns_precommit_stale_intent_into_fresh_attempt)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;

  std::unique_ptr<ObPluginActivationPermit> permit;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, permit, error)) << error;
  ASSERT_NE(nullptr, permit.get());
  const AttemptIdentity stale = identity_of(*permit);

  // reset() normally marks an unresolved in-process permit RECOVERY_REQUIRED.
  // Rewrite that test-only row to the on-disk image left by a process crash
  // immediately after CATALOG_BEGIN, when no candidate metadata existed.
  permit.reset();
  {
    ObSQLiteConnectionGuard injection(&pool_);
    ASSERT_NE(nullptr, injection.get_connection());
    ASSERT_EQ(
        OB_SUCCESS,
        injection->execute(
            "UPDATE __all_plugin_operation SET state=?,status=0,error='' "
            "WHERE operation_id=?",
            [&](ObSQLiteBinder &binder) {
              int ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::CATALOG_BEGIN));
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, stale.operation_id_);
              }
              return ret;
            }));
  }
  ASSERT_EQ(OB_SUCCESS, reopen_catalog());

  std::vector<ObPluginStartupEntry> entries;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->prepare_startup_recovery(entries, error))
      << error;
  const ObPluginStartupEntry *entry =
      find_startup_entry(entries, TEST_PLUGIN_ID);
  ASSERT_NE(nullptr, entry);
  ASSERT_FALSE(entry->exact_recovery_);
  EXPECT_EQ(spec.relative_path_, entry->relative_path_);

  ObSQLiteConnectionGuard observation(&pool_);
  ASSERT_NE(nullptr, observation.get_connection());
  int64_t count = 0;
  ASSERT_EQ(
      OB_SUCCESS,
      query_test_count(
          *observation.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_operation WHERE operation_id=? "
          "AND state=?",
          [&](ObSQLiteBinder &binder) {
            int ret = bind_test_string(binder, stale.operation_id_);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::ABORTED));
            }
            return ret;
          },
          count));
  EXPECT_EQ(1, count);
  observation.release();

  std::unique_ptr<ObPluginActivationPermit> fresh;
  ASSERT_EQ(OB_SUCCESS, begin_attempt(*catalog_, spec, fresh, error)) << error;
  ASSERT_NE(nullptr, fresh.get());
  EXPECT_GT(fresh->generation(), stale.generation_);
  EXPECT_NE(fresh->runtime_incarnation(), stale.runtime_incarnation_);
  EXPECT_NE(fresh->operation_id(), stale.operation_id_);
  ASSERT_EQ(OB_SUCCESS,
            fresh->abort(make_abort_result(*fresh), error))
      << error;
}

TEST_F(TestPluginCatalog,
       startup_orders_consumer_after_provider_with_archived_precommit_fence)
{
  const std::string provider_id = "org.seekdb.precommit-provider";
  const std::string consumer_id = "org.seekdb.precommit-consumer";
  const std::string provider_service =
      "org.seekdb.precommit-provider.service";
  ObPluginPackageInstallSpec provider_spec = make_install_spec(provider_id);
  provider_spec.relative_path_ = "precommit-provider/plugin.so";
  provider_spec.artifact_.build_id_ = "precommit-provider-build-v1";
  provider_spec.artifact_.package_digest_ =
      "sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
      "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
  ObPluginPackageInstallSpec consumer_spec = make_install_spec(consumer_id);
  consumer_spec.relative_path_ = "precommit-consumer/plugin.so";
  consumer_spec.artifact_.build_id_ = "precommit-consumer-build-v1";
  consumer_spec.artifact_.package_digest_ =
      "sha256:ffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffff";

  std::string error;
  ASSERT_EQ(OB_SUCCESS,
            install_package(*catalog_, provider_spec, error))
      << error;
  ASSERT_EQ(OB_SUCCESS,
            install_package(*catalog_, consumer_spec, error))
      << error;
  AttemptIdentity provider_active;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_metadata(
                *catalog_, provider_spec, provider_service,
                "org.seekdb.precommit-provider.function", nullptr,
                provider_active, error))
      << error;
  const ObPluginRuntimeServiceDependency dependency =
      make_runtime_service_dependency(
          provider_id, provider_active.generation_, provider_service);
  AttemptIdentity consumer_active;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete_with_metadata(
                *catalog_, consumer_spec,
                "org.seekdb.precommit-consumer.service",
                "org.seekdb.precommit-consumer.function", &dependency,
                consumer_active, error))
      << error;

  ASSERT_EQ(OB_SUCCESS, reopen_catalog());
  std::vector<ObPluginStartupEntry> entries;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->prepare_startup_recovery(entries, error))
      << error;
  ASSERT_EQ(2U, entries.size());
  ASSERT_EQ(provider_id, entries[0].plugin_id_);
  ASSERT_EQ(consumer_id, entries[1].plugin_id_);

  std::unique_ptr<ObPluginActivationPermit> precommit;
  ASSERT_EQ(OB_SUCCESS,
            begin_attempt(*catalog_, provider_spec, precommit, error))
      << error;
  ASSERT_NE(nullptr, precommit.get());
  const AttemptIdentity crashed = identity_of(*precommit);
  ASSERT_EQ(provider_active.generation_ + 1, crashed.generation_);

  // Destruction marks an in-process unresolved token RECOVERY_REQUIRED.  Put
  // the row back into the exact CATALOG_BEGIN image left by a crash before any
  // candidate metadata or ownership decision was committed.
  precommit.reset();
  {
    ObSQLiteConnectionGuard injection(&pool_);
    ASSERT_NE(nullptr, injection.get_connection());
    ASSERT_EQ(
        OB_SUCCESS,
        injection->execute(
            "UPDATE __all_plugin_operation SET state=?,phase=?,status=0,"
            "actual_state=?,start_entered=0,candidate_prepared=0,error='' "
            "WHERE operation_id=?",
            [&](ObSQLiteBinder &binder) {
              int ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::CATALOG_BEGIN));
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginActivationPhase::CATALOG_BEGIN));
              }
              if (OB_SUCCESS == ret) {
                ret = binder.bind_int(
                    static_cast<int32_t>(ObPluginState::DISCOVERED));
              }
              if (OB_SUCCESS == ret) {
                ret = bind_test_string(binder, crashed.operation_id_);
              }
              return ret;
            }));
  }
  ASSERT_EQ(OB_SUCCESS, reopen_catalog());

  entries.clear();
  ASSERT_EQ(OB_SUCCESS,
            catalog_->prepare_startup_recovery(entries, error))
      << error;
  ASSERT_EQ(2U, entries.size());
  // consumer sorts first lexically.  Its still-current edge names provider's
  // previous production generation, while the package row now carries the
  // archived precommit generation.  Startup must retain the semantic edge.
  EXPECT_EQ(provider_id, entries[0].plugin_id_);
  EXPECT_EQ(consumer_id, entries[1].plugin_id_);
  EXPECT_FALSE(entries[0].exact_recovery_);
  EXPECT_FALSE(entries[1].exact_recovery_);

  ObPluginCatalogRecord provider_record;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->get_record(provider_id, provider_record));
  EXPECT_EQ(crashed.generation_, provider_record.generation_);
  EXPECT_EQ(ObPluginState::DISCOVERED, provider_record.actual_state_);

  ObSQLiteConnectionGuard observation(&pool_);
  ASSERT_NE(nullptr, observation.get_connection());
  int64_t count = 0;
  ASSERT_EQ(
      OB_SUCCESS,
      query_test_count(
          *observation.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_operation WHERE operation_id=? "
          "AND generation=? AND state=? AND candidate_prepared=0",
          [&](ObSQLiteBinder &binder) {
            int ret = bind_test_string(binder, crashed.operation_id_);
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int64(
                  static_cast<int64_t>(crashed.generation_));
            }
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::ABORTED));
            }
            return ret;
          },
          count));
  EXPECT_EQ(1, count);
  ASSERT_EQ(
      OB_SUCCESS,
      query_test_count(
          *observation.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_dependency "
          "WHERE consumer_kind=? AND consumer_plugin_id=? "
          "AND consumer_generation=? AND provider_plugin_id=? "
          "AND provider_generation=? AND dependency_id=?",
          [&](ObSQLiteBinder &binder) {
            int ret = binder.bind_int(static_cast<int32_t>(
                ObPluginDependencyConsumerKind::PLUGIN));
            if (OB_SUCCESS == ret) {
              ret = bind_test_string(binder, consumer_id);
            }
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int64(
                  static_cast<int64_t>(consumer_active.generation_));
            }
            if (OB_SUCCESS == ret) {
              ret = bind_test_string(binder, provider_id);
            }
            if (OB_SUCCESS == ret) {
              ret = binder.bind_int64(
                  static_cast<int64_t>(provider_active.generation_));
            }
            if (OB_SUCCESS == ret) {
              ret = bind_test_string(binder, provider_service);
            }
            return ret;
          },
          count));
  EXPECT_EQ(1, count);
}

TEST_F(TestPluginCatalog, startup_plans_a_fresh_attempt_for_completed_active)
{
  const ObPluginPackageInstallSpec spec = make_install_spec();
  std::string error;
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  AttemptIdentity completed;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete(*catalog_, spec, completed, error))
      << error;
  ASSERT_EQ(OB_SUCCESS, reopen_catalog());

  std::vector<ObPluginStartupEntry> entries;
  ASSERT_EQ(OB_SUCCESS,
            catalog_->prepare_startup_recovery(entries, error))
      << error;
  const ObPluginStartupEntry *entry =
      find_startup_entry(entries, TEST_PLUGIN_ID);
  ASSERT_NE(nullptr, entry);
  EXPECT_FALSE(entry->exact_recovery_);
  EXPECT_EQ(spec.relative_path_, entry->relative_path_);
  EXPECT_EQ(TEST_PLUGIN_ID, entry->plugin_id_);
  EXPECT_NE(OB_SUCCESS, catalog_->check_server_ready(error));

  AttemptIdentity restarted;
  ASSERT_EQ(OB_SUCCESS,
            activate_and_complete(*catalog_, spec, restarted, error))
      << error;
  EXPECT_EQ(completed.generation_ + 1, restarted.generation_);
  EXPECT_NE(completed.runtime_incarnation_, restarted.runtime_incarnation_);
  EXPECT_NE(completed.operation_id_, restarted.operation_id_);
  EXPECT_EQ(OB_SUCCESS, catalog_->check_server_ready(error)) << error;
}

TEST_F(TestPluginCatalog, embedded_nul_and_identity_mismatch_fail_closed)
{
  std::string error;
  ObPluginPackageInstallSpec invalid =
      make_install_spec(with_embedded_nul("org.seekdb.invalid"));
  EXPECT_NE(OB_SUCCESS, install_package(*catalog_, invalid, error));
  ObPluginCatalogRecord absent;
  EXPECT_NE(OB_SUCCESS,
            catalog_->get_record("org.seekdb.invalid", absent));

  const ObPluginPackageInstallSpec spec = make_install_spec();
  ASSERT_EQ(OB_SUCCESS, install_package(*catalog_, spec, error)) << error;
  const ObPluginActivationRequest valid = make_activation_request(spec);
  std::vector<ObPluginActivationRequest> invalid_requests;

  ObPluginActivationRequest request = valid;
  request.plugin_id_ = "org.seekdb.different-package";
  invalid_requests.push_back(request);
  request = valid;
  request.build_id_ = "different-build";
  invalid_requests.push_back(request);
  request = valid;
  request.package_digest_ =
      "sha256:ffffffffffffffffffffffffffffffff"
      "ffffffffffffffffffffffffffffffff";
  invalid_requests.push_back(request);
  request = valid;
  request.relative_path_ = "catalog-test/different-plugin.so";
  invalid_requests.push_back(request);
  request = valid;
  request.plugin_id_ = with_embedded_nul(TEST_PLUGIN_ID);
  invalid_requests.push_back(request);
  request = valid;
  request.build_id_ = with_embedded_nul(TEST_BUILD_ID);
  invalid_requests.push_back(request);
  request = valid;
  request.package_digest_ = with_embedded_nul(TEST_PACKAGE_DIGEST);
  invalid_requests.push_back(request);
  request = valid;
  request.relative_path_ = with_embedded_nul(TEST_RELATIVE_PATH);
  invalid_requests.push_back(request);

  for (const ObPluginActivationRequest &invalid_request : invalid_requests) {
    SCOPED_TRACE(invalid_request.plugin_id_);
    std::unique_ptr<ObPluginActivationPermit> permit;
    error.clear();
    const int ret = catalog_->begin_activation(
        invalid_request, permit, error);
    EXPECT_NE(OB_SUCCESS, ret);
    EXPECT_EQ(nullptr, permit.get());
    EXPECT_FALSE(error.empty());
  }

  // Invalid requests must be rejected before assigning any durable attempt.
  ObPluginCatalogRecord record;
  ASSERT_EQ(OB_SUCCESS, catalog_->get_record(TEST_PLUGIN_ID, record));
  EXPECT_EQ(0U, record.generation_);
  EXPECT_TRUE(record.runtime_incarnation_.empty());
  EXPECT_TRUE(record.operation_id_.empty());
  EXPECT_EQ(ObPluginState::DISCOVERED, record.actual_state_);
}

} // namespace plugin
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
