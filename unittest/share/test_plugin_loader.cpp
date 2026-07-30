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

#include "share/plugin/ob_plugin_loader.h"

#include <atomic>
#include <cstring>
#include <gtest/gtest.h>
#include <new>
#include <thread>

#include "lib/ob_errno.h"

#ifndef SEEKDB_TEST_PLUGIN_DIR
#error "SEEKDB_TEST_PLUGIN_DIR must name the reference plugin directory"
#endif

#ifndef SEEKDB_TEST_PLUGIN_FILE
#error "SEEKDB_TEST_PLUGIN_FILE must name the reference plugin file"
#endif

#ifndef SEEKDB_TEST_BLOCKED_PLUGIN_FILE
#error "SEEKDB_TEST_BLOCKED_PLUGIN_FILE must name the rollback-failure plugin file"
#endif

#ifndef SEEKDB_TEST_STOP_BLOCKED_PLUGIN_FILE
#error "SEEKDB_TEST_STOP_BLOCKED_PLUGIN_FILE must name the stop-failure plugin file"
#endif

namespace oceanbase
{
namespace share
{
namespace plugin
{

using namespace oceanbase::common;

namespace
{

class TestVerifiedArtifact final : public ObPluginVerifiedArtifact
{
public:
  TestVerifiedArtifact(const std::string &path,
                       const std::string &plugin_id,
                       const std::string &build_id)
      : path_(path), metadata_()
  {
    metadata_.plugin_id_ = plugin_id;
    metadata_.build_id_ = build_id;
    metadata_.package_version_ = {1, 0, 0};
  }

  const std::string &load_path() const override { return path_; }
  const ObPluginArtifactMetadata &metadata() const override { return metadata_; }

private:
  std::string path_;
  ObPluginArtifactMetadata metadata_;
};

class TestVerifier : public ObPluginVerifier
{
public:
  explicit TestVerifier(
      const int result = OB_SUCCESS,
      const std::string &artifact_plugin_id = "org.seekdb.reference",
      const std::string &artifact_build_id = "reference-abi-v1")
      : result_(result), calls_(0), artifact_plugin_id_(artifact_plugin_id),
        artifact_build_id_(artifact_build_id), verify_entered_(nullptr),
        verify_release_(nullptr)
  {}

  void set_verify_barrier(std::atomic<bool> *entered,
                          std::atomic<bool> *release)
  {
    verify_entered_ = entered;
    verify_release_ = release;
  }

  int verify_and_pin(const std::string &canonical_path,
                     std::unique_ptr<ObPluginVerifiedArtifact> &artifact,
                     std::string &error) const override
  {
    ++calls_;
    artifact.reset();
    if (nullptr != verify_entered_) verify_entered_->store(true);
    while (nullptr != verify_release_ && !verify_release_->load()) {
      std::this_thread::yield();
    }
    if (canonical_path.empty()) {
      error = "empty canonical plugin path";
      return OB_INVALID_ARGUMENT;
    }
    if (OB_SUCCESS != result_) {
      error = "test verifier rejected plugin";
    } else {
      artifact.reset(new TestVerifiedArtifact(
          canonical_path, artifact_plugin_id_, artifact_build_id_));
    }
    return result_;
  }

  int result_;
  mutable int calls_;
  std::string artifact_plugin_id_;
  std::string artifact_build_id_;
  std::atomic<bool> *verify_entered_;
  std::atomic<bool> *verify_release_;
};

class TestDisableGuard : public ObPluginDisableGuard
{
public:
  class Permit final : public ObPluginDisablePermit
  {
  public:
    Permit(int *finish_calls,
           int *last_runtime_result,
           ObPluginState *last_runtime_state,
           ObPluginDisablePhase *last_runtime_phase,
           uint64_t *last_generation,
           bool *last_stop_entered,
           const int finish_result)
        : finish_calls_(finish_calls), last_runtime_result_(last_runtime_result),
          last_runtime_state_(last_runtime_state),
          last_runtime_phase_(last_runtime_phase), last_generation_(last_generation),
          last_stop_entered_(last_stop_entered), finish_result_(finish_result),
          finished_(false)
    {}

    ~Permit() noexcept override = default;

    int finish(const ObPluginRuntimeDisableResult &runtime_result,
               std::string &error) noexcept override
    {
      int ret = finish_result_;
      try {
        if (finished_) {
          error = "test permit was finalized twice";
          ret = OB_STATE_NOT_MATCH;
        } else {
          finished_ = true;
          ++*finish_calls_;
          *last_runtime_result_ = runtime_result.status_;
          *last_runtime_state_ = runtime_result.actual_state_;
          *last_runtime_phase_ = runtime_result.phase_;
          *last_generation_ = runtime_result.generation_;
          *last_stop_entered_ = runtime_result.stop_entered_;
          if (OB_SUCCESS != finish_result_) {
            error = "test catalog failed to persist disable result";
          }
        }
      } catch (const std::bad_alloc &) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } catch (...) {
        ret = OB_ERR_UNEXPECTED;
      }
      return ret;
    }

  private:
    int *finish_calls_;
    int *last_runtime_result_;
    ObPluginState *last_runtime_state_;
    ObPluginDisablePhase *last_runtime_phase_;
    uint64_t *last_generation_;
    bool *last_stop_entered_;
    int finish_result_;
    bool finished_;
  };

  explicit TestDisableGuard(const int begin_result = OB_SUCCESS,
                            const bool provide_permit = true,
                            const int finish_result = OB_SUCCESS)
      : begin_result_(begin_result), provide_permit_(provide_permit),
        finish_result_(finish_result), calls_(0), finish_calls_(0),
        last_runtime_result_(OB_SUCCESS),
        last_runtime_state_(ObPluginState::DISCOVERED),
        last_runtime_phase_(ObPluginDisablePhase::NONE), last_generation_(0),
        last_stop_entered_(false), expected_generation_(0), begin_signal_(nullptr),
        begin_entered_(nullptr), begin_release_(nullptr)
  {}

  void set_begin_signal(std::atomic<bool> *signal) { begin_signal_ = signal; }
  void set_begin_barrier(std::atomic<bool> *entered, std::atomic<bool> *release)
  {
    begin_entered_ = entered;
    begin_release_ = release;
  }

  int begin_restricted_disable(
      const std::string &plugin_id,
      const uint64_t expected_generation,
      std::unique_ptr<ObPluginDisablePermit> &permit,
      std::string &error) const noexcept override
  {
    int ret = begin_result_;
    try {
      ++calls_;
      permit.reset();
      expected_generation_ = expected_generation;
      if (nullptr != begin_signal_) begin_signal_->store(true);
      if (nullptr != begin_entered_) begin_entered_->store(true);
      while (nullptr != begin_release_ && !begin_release_->load()) {
        std::this_thread::yield();
      }
      if (plugin_id.empty() || 0 == expected_generation) {
        error = "empty plugin id or generation";
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_SUCCESS != begin_result_) {
        error = "test catalog dependency blocks disable";
      } else if (provide_permit_) {
        permit.reset(new (std::nothrow) Permit(
            &finish_calls_, &last_runtime_result_, &last_runtime_state_,
            &last_runtime_phase_, &last_generation_, &last_stop_entered_,
            finish_result_));
        if (!permit) {
          error = "test permit allocation failed";
          ret = OB_ALLOCATE_MEMORY_FAILED;
        }
      }
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
    return ret;
  }

  int begin_result_;
  bool provide_permit_;
  int finish_result_;
  mutable int calls_;
  mutable int finish_calls_;
  mutable int last_runtime_result_;
  mutable ObPluginState last_runtime_state_;
  mutable ObPluginDisablePhase last_runtime_phase_;
  mutable uint64_t last_generation_;
  mutable bool last_stop_entered_;
  mutable uint64_t expected_generation_;
  std::atomic<bool> *begin_signal_;
  std::atomic<bool> *begin_entered_;
  std::atomic<bool> *begin_release_;
};

struct ReferenceEchoServiceV1
{
  uint32_t struct_size_;
  seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *echo_)(
      const uint8_t *input,
      uint64_t input_size,
      uint8_t *output,
      uint64_t *in_out_output_size);
};

const char *REFERENCE_PLUGIN_ID = "org.seekdb.reference";
const char *BLOCKED_PLUGIN_ID = "org.seekdb.reference.blocked";
const char *STOP_BLOCKED_PLUGIN_ID = "org.seekdb.reference.stop-blocked";
const char *REFERENCE_SERVICE_ID = "org.seekdb.reference.echo";
const char *REFERENCE_DYNAMIC_SERVICE_ID = "org.seekdb.reference.dynamic-echo";

} // namespace

TEST(TestPluginLoader, validates_configuration_and_untrusted_paths)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;

  EXPECT_EQ(OB_INVALID_ARGUMENT,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, nullptr, guard, registry));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, nullptr, registry));
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  EXPECT_TRUE(loader.is_initialized());
  EXPECT_EQ(OB_INVALID_ARGUMENT, loader.load("../" SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            loader.load(SEEKDB_TEST_PLUGIN_DIR "/" SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(0, verifier->calls_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  EXPECT_FALSE(loader.is_initialized());
  EXPECT_EQ(OB_STATE_NOT_MATCH,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
}

TEST(TestPluginLoader, verifier_failure_has_no_runtime_effect)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(OB_CHECKSUM_ERROR);
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  EXPECT_EQ(OB_CHECKSUM_ERROR, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, verifier->calls_);
  EXPECT_EQ(0, registry->service_count());
  EXPECT_NE(std::string::npos, loader.last_error().find("rejected"));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, verification_does_not_hold_loader_mutex_or_race_shutdown)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));

  std::atomic<bool> verify_entered(false);
  std::atomic<bool> verify_release(false);
  std::atomic<int> load_result(OB_SUCCESS);
  verifier->set_verify_barrier(&verify_entered, &verify_release);
  std::thread loading([&]() {
    load_result.store(loader.load(SEEKDB_TEST_PLUGIN_FILE));
  });
  while (!verify_entered.load()) std::this_thread::yield();

  // Both calls must complete while verify is deliberately blocked.  shutdown
  // installs the terminal barrier but cannot close an in-flight artifact.
  EXPECT_TRUE(loader.is_initialized());
  EXPECT_EQ(OB_EAGAIN, loader.shutdown_for_process_exit(1000000));
  verify_release.store(true);
  loading.join();
  EXPECT_EQ(OB_STATE_NOT_MATCH, load_result.load());
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, verified_metadata_must_match_binary_manifest)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, "org.seekdb.not-the-reference-package");
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));

  EXPECT_EQ(OB_INVALID_DATA, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(0, registry->service_count());
  EXPECT_NE(std::string::npos, loader.last_error().find("does not match"));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, rollback_stop_failure_blocks_identity_until_process_exit)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, BLOCKED_PLUGIN_ID, "reference-blocked-abi-v1");
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));

  EXPECT_EQ(OB_ERR_UNEXPECTED,
            loader.load(SEEKDB_TEST_BLOCKED_PLUGIN_FILE));
  ObPluginStatusSnapshot blocked;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(BLOCKED_PLUGIN_ID, blocked));
  EXPECT_EQ(ObPluginState::BLOCKED, blocked.state_);
  EXPECT_NE(std::string::npos, blocked.last_error_.find("rollback stop failed"));
  EXPECT_EQ(OB_ENTRY_EXIST,
            loader.load(SEEKDB_TEST_BLOCKED_PLUGIN_FILE));
  EXPECT_EQ(OB_STATE_NOT_MATCH, loader.disable(BLOCKED_PLUGIN_ID, 1000000));
  EXPECT_EQ(0, guard->calls_);

  // Only the terminal shutdown path may retry the failed stop callback.
  ASSERT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  ObPluginStatusSnapshot stopped;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(BLOCKED_PLUGIN_ID, stopped));
  EXPECT_EQ(ObPluginState::STOPPED, stopped.state_);
}

TEST(TestPluginLoader, runtime_stop_failure_is_not_a_retryable_drain_timeout)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, STOP_BLOCKED_PLUGIN_ID, "reference-stop-blocked-abi-v1");
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_STOP_BLOCKED_PLUGIN_FILE));

  EXPECT_EQ(OB_ERR_UNEXPECTED,
            loader.disable(STOP_BLOCKED_PLUGIN_ID, 1000000));
  EXPECT_EQ(1, guard->finish_calls_);
  EXPECT_EQ(ObPluginState::BLOCKED, guard->last_runtime_state_);
  EXPECT_EQ(ObPluginDisablePhase::STOP, guard->last_runtime_phase_);
  EXPECT_TRUE(guard->last_stop_entered_);
  ObPluginStatusSnapshot blocked;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(STOP_BLOCKED_PLUGIN_ID, blocked));
  EXPECT_EQ(ObPluginState::BLOCKED, blocked.state_);

  // A normal retry is forbidden after stop was entered and failed.
  EXPECT_EQ(OB_STATE_NOT_MATCH,
            loader.disable(STOP_BLOCKED_PLUGIN_ID, 1000000));
  EXPECT_EQ(1, guard->calls_);
  EXPECT_EQ(OB_ENTRY_EXIST,
            loader.load(SEEKDB_TEST_STOP_BLOCKED_PLUGIN_FILE));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, load_publish_drain_disable_and_shutdown)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));

  uint64_t generation = 0;
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE, &generation));
  EXPECT_EQ(1U, generation);
  EXPECT_EQ(1, verifier->calls_);
  EXPECT_EQ(2, registry->service_count());
  EXPECT_EQ(OB_ENTRY_EXIST, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  ObPluginStatusSnapshot active;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, active));
  EXPECT_EQ(ObPluginState::ACTIVE, active.state_);
  EXPECT_EQ(generation, active.generation_);

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS,
            registry->acquire(REFERENCE_SERVICE_ID, 1, 0, 0,
                              SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, lease));
  const auto *service = static_cast<const ReferenceEchoServiceV1 *>(lease.service());
  ASSERT_NE(nullptr, service);
  ASSERT_GE(service->struct_size_, sizeof(ReferenceEchoServiceV1));
  const uint8_t input[] = {'s', 'e', 'e', 'k', 'd', 'b'};
  uint8_t output[sizeof(input)] = {};
  uint64_t output_size = sizeof(output);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            service->echo_(input, sizeof(input), output, &output_size));
  EXPECT_EQ(sizeof(input), output_size);
  EXPECT_EQ(0, std::memcmp(input, output, sizeof(input)));

  ObPluginLease dynamic_lease;
  ASSERT_EQ(OB_SUCCESS,
            registry->acquire(REFERENCE_DYNAMIC_SERVICE_ID, 1, 0, dynamic_lease));
  EXPECT_EQ(service, dynamic_lease.service());
  dynamic_lease.reset();

  // Quiesce removes the service atomically, but the held lease prevents stop.
  EXPECT_EQ(OB_TIMEOUT, loader.disable(REFERENCE_PLUGIN_ID, 0));
  EXPECT_EQ(1, guard->calls_);
  EXPECT_EQ(1, guard->finish_calls_);
  EXPECT_EQ(OB_TIMEOUT, guard->last_runtime_result_);
  EXPECT_EQ(ObPluginState::QUIESCING, guard->last_runtime_state_);
  EXPECT_EQ(ObPluginDisablePhase::DRAIN, guard->last_runtime_phase_);
  EXPECT_EQ(generation, guard->last_generation_);
  EXPECT_EQ(generation, guard->expected_generation_);
  EXPECT_EQ(0, registry->service_count());
  ObPluginLease after_quiesce;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry->acquire(REFERENCE_SERVICE_ID, 1, 0, after_quiesce));
  lease.reset();

  ASSERT_EQ(OB_SUCCESS, loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  EXPECT_EQ(2, guard->calls_);
  EXPECT_EQ(2, guard->finish_calls_);
  EXPECT_EQ(OB_SUCCESS, guard->last_runtime_result_);
  EXPECT_EQ(ObPluginState::STOPPED, guard->last_runtime_state_);
  EXPECT_EQ(ObPluginDisablePhase::COMPLETE, guard->last_runtime_phase_);
  ObPluginStatusSnapshot stopped;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, stopped));
  EXPECT_EQ(ObPluginState::STOPPED, stopped.state_);
  EXPECT_EQ(0, stopped.lease_count_);
  // R0 has no online re-enable/upgrade protocol.  A logically stopped DSO and
  // its identity stay resident until the terminal shutdown boundary.
  EXPECT_EQ(OB_ENTRY_EXIST, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_NE(std::string::npos, loader.last_error().find("already resident"));

  ASSERT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  EXPECT_FALSE(loader.is_initialized());
}

TEST(TestPluginLoader, catalog_guard_blocks_disable_but_not_process_shutdown)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>(OB_STATE_NOT_MATCH);
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  EXPECT_EQ(OB_STATE_NOT_MATCH, loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  EXPECT_EQ(1, guard->calls_);
  EXPECT_EQ(2, registry->service_count());
  ObPluginStatusSnapshot active;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, active));
  EXPECT_EQ(ObPluginState::ACTIVE, active.state_);

  // Process shutdown drains runtime state but deliberately does not authorize a
  // catalog mutation; persisted plugin state is restored on the next startup.
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  EXPECT_EQ(1, guard->calls_);
  EXPECT_EQ(0, registry->service_count());
}

TEST(TestPluginLoader, drain_does_not_hold_global_loader_mutex)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS,
            registry->acquire(REFERENCE_SERVICE_ID, 1, 0, lease));
  std::atomic<bool> worker_ready(false);
  std::atomic<bool> begin_seen(false);
  guard->set_begin_signal(&begin_seen);
  std::thread releaser([&loader, &registry, &worker_ready, &begin_seen,
                        held = std::move(lease)]() mutable {
    worker_ready.store(true);
    while (!begin_seen.load()) std::this_thread::yield();
    while (registry->service_count() != 0) std::this_thread::yield();
    ObPluginStatusSnapshot status;
    (void)loader.get_status(REFERENCE_PLUGIN_ID, status);
    held.reset();
  });
  while (!worker_ready.load()) std::this_thread::yield();

  // If drain held the global loader mutex, get_status() could not complete and
  // this call would time out waiting for the lease owned by the worker.
  EXPECT_EQ(OB_SUCCESS, loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  releaser.join();
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, process_shutdown_cannot_overtake_disable_permit)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  std::atomic<bool> begin_entered(false);
  std::atomic<bool> release_begin(false);
  std::atomic<int> disable_result(OB_SUCCESS);
  guard->set_begin_barrier(&begin_entered, &release_begin);
  std::thread disabler([&]() {
    disable_result.store(loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  });
  while (!begin_entered.load()) std::this_thread::yield();

  EXPECT_EQ(OB_EAGAIN, loader.shutdown_for_process_exit(1000000));
  release_begin.store(true);
  disabler.join();
  EXPECT_EQ(OB_STATE_NOT_MATCH, disable_result.load());
  EXPECT_EQ(1, guard->finish_calls_);
  EXPECT_EQ(ObPluginState::ACTIVE, guard->last_runtime_state_);

  // The first call established the terminal barrier; retry performs teardown
  // only after the catalog-coordinated operation has released its reservation.
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  EXPECT_EQ(0, registry->service_count());
}

TEST(TestPluginLoader, concurrent_disable_is_rejected_before_catalog_entry)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  std::atomic<bool> begin_entered(false);
  std::atomic<bool> release_begin(false);
  std::atomic<int> first_result(OB_SUCCESS);
  guard->set_begin_barrier(&begin_entered, &release_begin);
  std::thread first([&]() {
    first_result.store(loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  });
  while (!begin_entered.load()) std::this_thread::yield();

  EXPECT_EQ(OB_EAGAIN, loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  EXPECT_EQ(1, guard->calls_);
  release_begin.store(true);
  first.join();
  EXPECT_EQ(OB_SUCCESS, first_result.load());
  EXPECT_EQ(1, guard->calls_);
  EXPECT_EQ(1, guard->finish_calls_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, concurrent_terminal_shutdown_is_rejected)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS,
            registry->acquire(REFERENCE_SERVICE_ID, 1, 0, lease));
  std::atomic<int> first_result(OB_SUCCESS);
  std::thread first([&]() {
    first_result.store(loader.shutdown_for_process_exit(10000000));
  });
  while (registry->service_count() != 0) std::this_thread::yield();

  EXPECT_EQ(OB_EAGAIN, loader.shutdown_for_process_exit(10000000));
  lease.reset();
  first.join();
  EXPECT_EQ(OB_SUCCESS, first_result.load());
}

TEST(TestPluginLoader, catalog_coordinator_must_return_disable_permit)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>(OB_SUCCESS, false);
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  EXPECT_EQ(OB_ERR_UNEXPECTED, loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  EXPECT_EQ(2, registry->service_count());
  EXPECT_NE(std::string::npos, loader.last_error().find("no disable permit"));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, catalog_finalize_failure_keeps_plugin_id_reserved)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>(
      OB_SUCCESS, true, OB_ERR_UNEXPECTED);
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  EXPECT_EQ(OB_ERR_UNEXPECTED,
            loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  EXPECT_EQ(1, guard->finish_calls_);
  EXPECT_EQ(OB_SUCCESS, guard->last_runtime_result_);
  EXPECT_EQ(0, registry->service_count());

  ObPluginStatusSnapshot stopped;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, stopped));
  EXPECT_EQ(ObPluginState::STOPPED, stopped.state_);
  EXPECT_EQ(OB_ENTRY_EXIST, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_NE(std::string::npos, loader.last_error().find("already resident"));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, runtime_and_catalog_failures_are_both_reported)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, STOP_BLOCKED_PLUGIN_ID, "reference-stop-blocked-abi-v1");
  const auto guard = std::make_shared<TestDisableGuard>(
      OB_SUCCESS, true, OB_ERR_UNEXPECTED);
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_STOP_BLOCKED_PLUGIN_FILE));

  EXPECT_EQ(OB_ERR_UNEXPECTED,
            loader.disable(STOP_BLOCKED_PLUGIN_ID, 1000000));
  EXPECT_EQ(1, guard->finish_calls_);
  EXPECT_EQ(OB_ERR_UNEXPECTED, guard->last_runtime_result_);
  EXPECT_EQ(ObPluginState::BLOCKED, guard->last_runtime_state_);
  EXPECT_NE(std::string::npos,
            loader.last_error().find("plugin stop callback failed"));
  EXPECT_NE(std::string::npos,
            loader.last_error().find("catalog finalization failed"));

  // The fixture fails its first stop only; terminal cleanup retries and closes
  // the retained DSO without pretending the failed catalog write succeeded.
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

} // namespace plugin
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
