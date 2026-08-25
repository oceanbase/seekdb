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
#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <new>
#include <thread>

#if !defined(_WIN32)
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "lib/ob_errno.h"
#include "share/rc/ob_module_provider.h"

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

#ifndef SEEKDB_TEST_INVALID_EXTENSIONS_PLUGIN_FILE
#error "SEEKDB_TEST_INVALID_EXTENSIONS_PLUGIN_FILE must name the invalid-extension plugin file"
#endif

#ifndef SEEKDB_TEST_INVALID_MANIFEST_PLUGIN_FILE
#error "SEEKDB_TEST_INVALID_MANIFEST_PLUGIN_FILE must name the invalid-manifest plugin file"
#endif

#ifndef SEEKDB_TEST_REGISTRATION_CONFLICT_PLUGIN_FILE
#error "SEEKDB_TEST_REGISTRATION_CONFLICT_PLUGIN_FILE must name the registration-conflict plugin file"
#endif

#ifndef SEEKDB_TEST_MISSING_ENTRY_PLUGIN_FILE
#error "SEEKDB_TEST_MISSING_ENTRY_PLUGIN_FILE must name the entry-less shared object"
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

const char *TEST_PACKAGE_DIGEST =
    "sha256:0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef";

template <typename Predicate>
bool wait_until(const Predicate &predicate,
                const std::chrono::milliseconds timeout =
                    std::chrono::milliseconds(5000))
{
  const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + timeout;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return predicate();
    }
    std::this_thread::yield();
  }
  return true;
}

class TestVerifiedArtifact final : public ObPluginVerifiedArtifact
{
public:
  TestVerifiedArtifact(const std::string &path,
                       const std::string &plugin_id,
                       const std::string &build_id,
                       const std::string &package_digest,
                       const uint32_t catalog_version,
                       const uint32_t data_format_version)
      : path_(path), metadata_()
  {
    metadata_.plugin_id_ = plugin_id;
    metadata_.build_id_ = build_id;
    metadata_.package_digest_ = package_digest;
    metadata_.package_version_ = {1, 0, 0};
    metadata_.catalog_version_ = catalog_version;
    metadata_.data_format_version_ = data_format_version;
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
      const std::string &artifact_build_id = "reference-abi-v1",
      const uint32_t artifact_catalog_version = 1,
      const std::string &artifact_package_digest = TEST_PACKAGE_DIGEST,
      const uint32_t artifact_data_format_version = 0)
      : result_(result), calls_(0), artifact_plugin_id_(artifact_plugin_id),
        artifact_build_id_(artifact_build_id),
        artifact_catalog_version_(artifact_catalog_version),
        artifact_package_digest_(artifact_package_digest),
        artifact_data_format_version_(artifact_data_format_version),
        verify_entered_(nullptr), verify_release_(nullptr)
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
    if (nullptr != verify_release_ &&
        !wait_until([&]() { return verify_release_->load(); })) {
      error = "test verifier barrier timed out";
      return OB_TIMEOUT;
    }
    if (canonical_path.empty()) {
      error = "empty canonical plugin path";
      return OB_INVALID_ARGUMENT;
    }
    if (OB_SUCCESS != result_) {
      error = "test verifier rejected plugin";
    } else {
      artifact.reset(new TestVerifiedArtifact(
          canonical_path, artifact_plugin_id_, artifact_build_id_,
          artifact_package_digest_, artifact_catalog_version_,
          artifact_data_format_version_));
    }
    return result_;
  }

  int result_;
  mutable int calls_;
  std::string artifact_plugin_id_;
  std::string artifact_build_id_;
  uint32_t artifact_catalog_version_;
  std::string artifact_package_digest_;
  uint32_t artifact_data_format_version_;
  std::atomic<bool> *verify_entered_;
  std::atomic<bool> *verify_release_;
};

class TestDisableGuard : public ObPluginDisableGuard,
                         public ObPluginActivationGuard
{
public:
  class DisablePermit final : public ObPluginDisablePermit
  {
  public:
    DisablePermit(int *finish_calls,
                  int *last_runtime_result,
                  ObPluginState *last_runtime_state,
                  ObPluginDisablePhase *last_runtime_phase,
                  uint64_t *last_generation,
                  bool *last_stop_entered,
                  int *checkpoint_calls,
                  const int *checkpoint_result,
                  const int finish_result)
        : finish_calls_(finish_calls), last_runtime_result_(last_runtime_result),
          last_runtime_state_(last_runtime_state),
          last_runtime_phase_(last_runtime_phase), last_generation_(last_generation),
          last_stop_entered_(last_stop_entered),
          checkpoint_calls_(checkpoint_calls),
          checkpoint_result_(checkpoint_result), finish_result_(finish_result),
          finished_(false)
    {}

    ~DisablePermit() noexcept override = default;

    int record_stop_entered(std::string &error) noexcept override
    {
      ++*checkpoint_calls_;
      if (OB_SUCCESS != *checkpoint_result_) {
        try {
          error = "test catalog failed to persist stop checkpoint";
        } catch (...) {
        }
      }
      return *checkpoint_result_;
    }

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
    int *checkpoint_calls_;
    const int *checkpoint_result_;
    int finish_result_;
    bool finished_;
  };

  class ActivationCommit final : public ObPluginActivationCommit
  {
  public:
    explicit ActivationCommit(TestDisableGuard *guard)
        : guard_(guard), completed_(false)
    {}

    ~ActivationCommit() noexcept override = default;

    int complete(const ObPluginRuntimeActivationResult &runtime_result,
                 std::string &error) noexcept override
    {
      int ret = guard_->activation_complete_result_;
      try {
        if (completed_) {
          error = "test activation commit was completed twice";
          ret = OB_STATE_NOT_MATCH;
        } else {
          completed_ = true;
          ++guard_->activation_complete_calls_;
          guard_->last_activation_complete_result_ = runtime_result.status_;
          guard_->last_activation_complete_state_ = runtime_result.actual_state_;
          guard_->last_activation_complete_phase_ = runtime_result.phase_;
          if (OB_SUCCESS != ret) {
            error = "test catalog failed to persist active completion";
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
    TestDisableGuard *guard_;
    bool completed_;
  };

  class ActivationPermit final : public ObPluginActivationPermit
  {
  public:
    explicit ActivationPermit(TestDisableGuard *guard)
        : guard_(guard), committed_(false), aborted_(false)
    {}

    ~ActivationPermit() noexcept override = default;

    uint64_t generation() const noexcept override
    {
      return guard_->activation_generation_;
    }

    const std::string &runtime_incarnation() const noexcept override
    {
      return guard_->activation_runtime_incarnation_;
    }

    const std::string &operation_id() const noexcept override
    {
      return guard_->activation_operation_id_;
    }

    int commit_candidate(
        const ObPluginRuntimeActivationResult &candidate_result,
        ObPluginActivationDecision &decision,
        std::unique_ptr<ObPluginActivationCommit> &commit,
        std::string &error) noexcept override
    {
      int ret = guard_->activation_commit_result_;
      try {
        decision = guard_->activation_decision_;
        commit.reset();
        if (committed_ || aborted_) {
          error = "test activation permit was finalized twice";
          return OB_STATE_NOT_MATCH;
        }
        committed_ = true;
        ++guard_->activation_commit_calls_;
        guard_->last_activation_candidate_status_ = candidate_result.status_;
        guard_->last_activation_candidate_state_ = candidate_result.actual_state_;
        guard_->last_activation_candidate_phase_ = candidate_result.phase_;
        guard_->last_activation_candidate_prepared_ =
            candidate_result.candidate_prepared_;
        guard_->last_activation_candidate_epoch_ =
            candidate_result.candidate_base_epoch_;
        guard_->last_activation_service_count_ = candidate_result.services_.size();
        guard_->last_activation_extension_count_ =
            candidate_result.extensions_.size();
        guard_->last_activation_dependency_count_ =
            candidate_result.dependencies_.size();
        if (nullptr != guard_->activation_commit_entered_) {
          guard_->activation_commit_entered_->store(true);
        }
        if (nullptr != guard_->activation_commit_release_ &&
            !wait_until([&]() {
              return guard_->activation_commit_release_->load();
            })) {
          error = "test activation commit barrier timed out";
          ret = OB_TIMEOUT;
        }
        if (guard_->provide_activation_commit_ &&
            (OB_PLUGIN_ACTIVATION_PROMOTE == decision ||
             guard_->force_activation_commit_token_)) {
          commit.reset(new (std::nothrow) ActivationCommit(guard_));
          if (!commit) {
            error = "test activation commit allocation failed";
            ret = OB_ALLOCATE_MEMORY_FAILED;
          }
        }
        if (OB_SUCCESS != ret && error.empty()) {
          error = "test catalog failed to commit activation candidate";
        }
      } catch (const std::bad_alloc &) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } catch (...) {
        ret = OB_ERR_UNEXPECTED;
      }
      return ret;
    }

    int abort(const ObPluginRuntimeActivationResult &runtime_result,
              std::string &error) noexcept override
    {
      int ret = guard_->activation_abort_result_;
      try {
        if (aborted_ ||
            (committed_ &&
             OB_PLUGIN_ACTIVATION_NOT_COMMITTED !=
                 guard_->activation_decision_)) {
          error = "test activation permit cannot be aborted";
          ret = OB_STATE_NOT_MATCH;
        } else {
          aborted_ = true;
          ++guard_->activation_abort_calls_;
          guard_->last_activation_abort_status_ = runtime_result.status_;
          guard_->last_activation_abort_state_ = runtime_result.actual_state_;
          guard_->last_activation_abort_phase_ = runtime_result.phase_;
          if (OB_SUCCESS != ret) {
            error = "test catalog failed to persist activation abort";
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
    TestDisableGuard *guard_;
    bool committed_;
    bool aborted_;
  };

  explicit TestDisableGuard(const int begin_result = OB_SUCCESS,
                            const bool provide_permit = true,
                            const int finish_result = OB_SUCCESS)
      : begin_result_(begin_result), provide_permit_(provide_permit),
        finish_result_(finish_result), checkpoint_result_(OB_SUCCESS),
        calls_(0), checkpoint_calls_(0), finish_calls_(0),
        last_runtime_result_(OB_SUCCESS),
        last_runtime_state_(ObPluginState::DISCOVERED),
        last_runtime_phase_(ObPluginDisablePhase::NONE), last_generation_(0),
        last_stop_entered_(false), expected_generation_(0), begin_signal_(nullptr),
        begin_entered_(nullptr), begin_release_(nullptr),
        activation_begin_result_(OB_SUCCESS),
        provide_activation_permit_(true),
        activation_commit_result_(OB_SUCCESS),
        activation_decision_(OB_PLUGIN_ACTIVATION_PROMOTE),
        provide_activation_commit_(true),
        force_activation_commit_token_(false),
        activation_complete_result_(OB_SUCCESS),
        activation_abort_result_(OB_SUCCESS), activation_generation_(1),
        activation_runtime_incarnation_("test-runtime-1"),
        activation_operation_id_("test-activation-1"),
        activation_begin_calls_(0), activation_commit_calls_(0),
        activation_complete_calls_(0), activation_abort_calls_(0),
        last_activation_mode_(ObPluginActivationMode::ACTIVATE),
        last_activation_expected_generation_(0),
        last_activation_candidate_status_(OB_SUCCESS),
        last_activation_candidate_state_(ObPluginState::DISCOVERED),
        last_activation_candidate_phase_(ObPluginActivationPhase::NONE),
        last_activation_candidate_prepared_(false),
        last_activation_candidate_epoch_(0),
        last_activation_service_count_(0), last_activation_extension_count_(0),
        last_activation_dependency_count_(0),
        last_activation_complete_result_(OB_SUCCESS),
        last_activation_complete_state_(ObPluginState::DISCOVERED),
        last_activation_complete_phase_(ObPluginActivationPhase::NONE),
        last_activation_abort_status_(OB_SUCCESS),
        last_activation_abort_state_(ObPluginState::DISCOVERED),
        last_activation_abort_phase_(ObPluginActivationPhase::NONE),
        activation_commit_entered_(nullptr), activation_commit_release_(nullptr)
  {}

  void set_begin_signal(std::atomic<bool> *signal) { begin_signal_ = signal; }
  void set_begin_barrier(std::atomic<bool> *entered, std::atomic<bool> *release)
  {
    begin_entered_ = entered;
    begin_release_ = release;
  }

  void set_activation_commit_barrier(std::atomic<bool> *entered,
                                     std::atomic<bool> *release)
  {
    activation_commit_entered_ = entered;
    activation_commit_release_ = release;
  }

  int begin_activation(
      const ObPluginActivationRequest &request,
      std::unique_ptr<ObPluginActivationPermit> &permit,
      std::string &error) const noexcept override
  {
    int ret = activation_begin_result_;
    try {
      ++activation_begin_calls_;
      permit.reset();
      last_activation_mode_ = request.mode_;
      last_activation_plugin_id_ = request.plugin_id_;
      last_activation_digest_ = request.package_digest_;
      last_activation_expected_generation_ = request.expected_generation_;
      last_activation_expected_incarnation_ =
          request.expected_runtime_incarnation_;
      last_activation_expected_operation_id_ = request.expected_operation_id_;
      if (request.plugin_id_.empty() || request.package_digest_.empty()) {
        error = "test activation request has incomplete identity";
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_SUCCESS != ret) {
        error = "test catalog rejected activation";
      } else if (provide_activation_permit_) {
        permit.reset(new (std::nothrow) ActivationPermit(
            const_cast<TestDisableGuard *>(this)));
        if (!permit) {
          error = "test activation permit allocation failed";
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
      if (nullptr != begin_release_ &&
          !wait_until([&]() { return begin_release_->load(); })) {
        error = "test disable begin barrier timed out";
        ret = OB_TIMEOUT;
      }
      if (plugin_id.empty() || 0 == expected_generation) {
        error = "empty plugin id or generation";
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_SUCCESS != begin_result_) {
        error = "test catalog dependency blocks disable";
      } else if (provide_permit_) {
        permit.reset(new (std::nothrow) DisablePermit(
            &finish_calls_, &last_runtime_result_, &last_runtime_state_,
            &last_runtime_phase_, &last_generation_, &last_stop_entered_,
            &checkpoint_calls_, &checkpoint_result_, finish_result_));
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
  int checkpoint_result_;
  mutable int calls_;
  mutable int checkpoint_calls_;
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

  int activation_begin_result_;
  bool provide_activation_permit_;
  int activation_commit_result_;
  ObPluginActivationDecision activation_decision_;
  bool provide_activation_commit_;
  bool force_activation_commit_token_;
  int activation_complete_result_;
  int activation_abort_result_;
  uint64_t activation_generation_;
  std::string activation_runtime_incarnation_;
  std::string activation_operation_id_;
  mutable int activation_begin_calls_;
  mutable int activation_commit_calls_;
  mutable int activation_complete_calls_;
  mutable int activation_abort_calls_;
  mutable ObPluginActivationMode last_activation_mode_;
  mutable std::string last_activation_plugin_id_;
  mutable std::string last_activation_digest_;
  mutable uint64_t last_activation_expected_generation_;
  mutable std::string last_activation_expected_incarnation_;
  mutable std::string last_activation_expected_operation_id_;
  mutable int last_activation_candidate_status_;
  mutable ObPluginState last_activation_candidate_state_;
  mutable ObPluginActivationPhase last_activation_candidate_phase_;
  mutable bool last_activation_candidate_prepared_;
  mutable uint64_t last_activation_candidate_epoch_;
  mutable size_t last_activation_service_count_;
  mutable size_t last_activation_extension_count_;
  mutable size_t last_activation_dependency_count_;
  mutable int last_activation_complete_result_;
  mutable ObPluginState last_activation_complete_state_;
  mutable ObPluginActivationPhase last_activation_complete_phase_;
  mutable int last_activation_abort_status_;
  mutable ObPluginState last_activation_abort_state_;
  mutable ObPluginActivationPhase last_activation_abort_phase_;
  std::atomic<bool> *activation_commit_entered_;
  std::atomic<bool> *activation_commit_release_;
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
const char *REFERENCE_FUNCTION_ID = "org.seekdb.reference.function.echo";
const char *REFERENCE_PAIR_FUNCTION_ID =
    "org.seekdb.reference.function.echo-pair";
const char *REGISTRATION_CONFLICT_PLUGIN_ID =
    "org.seekdb.reference.registration-conflict";
const char *REGISTRATION_CONFLICT_SHARED_SERVICE_ID =
    "org.seekdb.reference.registration-conflict.shared";
const char *REGISTRATION_CONFLICT_AFTER_ABORT_SERVICE_ID =
    "org.seekdb.reference.registration-conflict.after-abort";

#if defined(SEEKDB_TEST_SQL_EXTENSION_PLUGIN_DIR)
struct SqlExtensionRows
{
  std::vector<int64_t> values_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL collect_sql_extension_row(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_table_row_v1_t *row)
{
  if (nullptr == host || nullptr == row || row->column_count != 1 ||
      nullptr == row->columns ||
      row->columns[0].data_size != sizeof(int64_t) ||
      nullptr == row->columns[0].data) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  int64_t value = 0;
  std::memcpy(&value, row->columns[0].data, sizeof(value));
  reinterpret_cast<SqlExtensionRows *>(host)->values_.push_back(value);
  return SEEKDB_PLUGIN_STATUS_OK;
}
#endif

std::string with_embedded_nul(const char *prefix, const char *suffix)
{
  std::string value(prefix, std::strlen(prefix));
  value.push_back('\0');
  value.append(suffix, std::strlen(suffix));
  return value;
}

#if !defined(_WIN32)
class RequiredServiceManifestOverride
{
public:
  RequiredServiceManifestOverride()
      : handle_(nullptr), manifest_(nullptr), original_requirements_(nullptr),
        original_requirement_count_(0), requirement_(), service_slot_(nullptr),
        page_start_(nullptr), page_length_(0), installed_(false)
  {
    std::memset(&requirement_, 0, sizeof(requirement_));
    requirement_.struct_size = sizeof(requirement_);
    requirement_.service_id = "org.seekdb.reference.missing-required";
    requirement_.version_range.struct_size =
        sizeof(requirement_.version_range);
    requirement_.version_range.minimum_inclusive = {1, 0, 0};
    requirement_.version_range.maximum_exclusive = {2, 0, 0};
    requirement_.service_slot = &service_slot_;
  }

  ~RequiredServiceManifestOverride()
  {
    if (installed_ && 0 == make_manifest_writable()) {
      manifest_->required_services = original_requirements_;
      manifest_->required_services_count = original_requirement_count_;
      (void)mprotect(page_start_, page_length_, PROT_READ);
    }
    if (nullptr != handle_) dlclose(handle_);
  }

  bool install(std::string &error)
  {
    const std::string path =
        std::string(SEEKDB_TEST_PLUGIN_DIR) + "/" + SEEKDB_TEST_PLUGIN_FILE;
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (nullptr == handle_) {
      const char *message = dlerror();
      error = nullptr == message ? "test dlopen failed" : message;
      return false;
    }

    dlerror();
    void *symbol = dlsym(handle_, "seekdb_plugin_entry_v1");
    const char *message = dlerror();
    if (nullptr != message || nullptr == symbol) {
      error = nullptr == message ? "test plugin entry is null" : message;
      return false;
    }
    seekdb_plugin_entry_v1_fn entry = nullptr;
    static_assert(sizeof(entry) == sizeof(symbol),
                  "test platform must support converting the plugin entry");
    std::memcpy(&entry, &symbol, sizeof(entry));
    manifest_ = const_cast<seekdb_plugin_manifest_v1_t *>(entry());
    if (nullptr == manifest_) {
      error = "test plugin returned a null manifest";
      return false;
    }

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
      error = "cannot determine the test page size";
      return false;
    }
    const uintptr_t page_mask = static_cast<uintptr_t>(page_size - 1);
    const uintptr_t manifest_address =
        reinterpret_cast<uintptr_t>(manifest_);
    const uintptr_t page_begin = manifest_address & ~page_mask;
    const uintptr_t page_end =
        (manifest_address + sizeof(*manifest_) + page_mask) & ~page_mask;
    page_start_ = reinterpret_cast<void *>(page_begin);
    page_length_ = page_end - page_begin;
    if (0 != make_manifest_writable()) {
      error = "cannot make the test plugin manifest writable";
      return false;
    }

    original_requirements_ = manifest_->required_services;
    original_requirement_count_ = manifest_->required_services_count;
    manifest_->required_services = &requirement_;
    manifest_->required_services_count = 1;
    installed_ = true;
    return true;
  }

private:
  int make_manifest_writable() const
  {
    return nullptr == page_start_ || 0 == page_length_
        ? -1
        : mprotect(page_start_, page_length_, PROT_READ | PROT_WRITE);
  }

  void *handle_;
  seekdb_plugin_manifest_v1_t *manifest_;
  const seekdb_plugin_service_require_descriptor_t *original_requirements_;
  uint32_t original_requirement_count_;
  seekdb_plugin_service_require_descriptor_t requirement_;
  const void *service_slot_;
  void *page_start_;
  size_t page_length_;
  bool installed_;
};
#endif

} // namespace

#if defined(SEEKDB_TEST_SQL_EXTENSION_PLUGIN_DIR)
TEST(TestPluginLoader, typed_sql_functions_and_table_cursor_hold_generation_leases)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, "org.seekdb.sql_extension", "sql-extension-catalog-v1", 1,
      TEST_PACKAGE_DIGEST, 1);
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS, loader.init(SEEKDB_TEST_SQL_EXTENSION_PLUGIN_DIR,
                                     verifier, guard, guard, registry));
  ASSERT_EQ(OB_SUCCESS,
            loader.load(SEEKDB_TEST_SQL_EXTENSION_PLUGIN_FILE))
      << loader.last_error();

  seekdb_plugin_sql_binding_v1_t payload = {};
  ASSERT_EQ(OB_SUCCESS, loader.resolve_sql_extension(
      SEEKDB_PLUGIN_EXTENSION_TYPE, "seekdb_payload", nullptr, 0, payload));
  EXPECT_STREQ("org.seekdb.sql-extension.type.payload", payload.object_id);
  EXPECT_STREQ("org.seekdb.sql-extension.format.payload",
               payload.physical_format_id);
  EXPECT_EQ(1U, payload.physical_format_version);
  EXPECT_NE(0U, payload.flags & SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT);

  const char *integer_types[] = {"core.type.int64"};
  const char *bytes_types[] = {"core.type.bytes"};
  seekdb_plugin_sql_binding_v1_t integer_identity = {};
  seekdb_plugin_sql_binding_v1_t bytes_identity = {};
  ASSERT_EQ(OB_SUCCESS, loader.resolve_sql_extension(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_identity",
      integer_types, 1, integer_identity));
  ASSERT_EQ(OB_SUCCESS, loader.resolve_sql_extension(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_identity",
      bytes_types, 1, bytes_identity));
  EXPECT_STRNE(integer_identity.object_id, bytes_identity.object_id);
  EXPECT_STREQ("core.type.int64", integer_identity.result_type_id);
  EXPECT_STREQ("core.type.bytes", bytes_identity.result_type_id);

  const char *table_types[] = {"core.type.int64", "core.type.int64"};
  seekdb_plugin_sql_binding_v1_t series = {};
  ASSERT_EQ(OB_SUCCESS, loader.resolve_sql_extension(
      SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION, "seekdb_generate_series",
      table_types, 2, series));
  ASSERT_EQ(sizeof(series), series.struct_size);
  ASSERT_EQ(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION, series.kind);
  ASSERT_EQ(2U, series.minimum_arity);
  ASSERT_EQ(2U, series.maximum_arity);
  ASSERT_EQ(1U, series.column_count);
  seekdb_plugin_sql_column_v1_t column = {};
  ASSERT_EQ(OB_SUCCESS, loader.describe_sql_column(series, 0, column));
  EXPECT_STREQ("value", column.sql_name);
  EXPECT_STREQ("core.type.int64", column.type_id);
  EXPECT_EQ(0, column.nullable);

  int64_t start = 2;
  int64_t finish = 4;
  seekdb_plugin_execution_value_v1_t arguments[2] = {};
  for (size_t i = 0; i < 2; ++i) {
    arguments[i].struct_size = sizeof(arguments[i]);
    arguments[i].type_id = "core.type.int64";
    arguments[i].data = reinterpret_cast<const uint8_t *>(
        i == 0 ? &start : &finish);
    arguments[i].data_size = sizeof(int64_t);
  }
  SqlExtensionRows rows;
  seekdb_plugin_table_execution_context_v1_t context = {};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&rows);
  context.emit_row = collect_sql_extension_row;
  ASSERT_NE(nullptr, context.emit_row);
  ASSERT_EQ(sizeof(context), context.struct_size);
  std::unique_ptr<IPluginTableCursor> cursor;
  ASSERT_EQ(OB_SUCCESS, loader.open_bound_table_function(
      series, &context, arguments, 2, cursor));
  ASSERT_NE(nullptr, cursor.get());

  ObPluginStatusSnapshot status;
  ASSERT_EQ(OB_SUCCESS,
            loader.get_status("org.seekdb.sql_extension", status));
  EXPECT_GE(status.lease_count_, 2);

  uint32_t emitted_rows = 0;
  ASSERT_EQ(OB_SUCCESS, cursor->next(&context, 2, &emitted_rows));
  EXPECT_EQ(2U, emitted_rows);
  ASSERT_EQ(OB_SUCCESS, cursor->next(&context, 2, &emitted_rows));
  EXPECT_EQ(1U, emitted_rows);
  EXPECT_EQ(OB_ITER_END, cursor->next(&context, 2, &emitted_rows));
  ASSERT_EQ(3U, rows.values_.size());
  EXPECT_EQ(2, rows.values_[0]);
  EXPECT_EQ(3, rows.values_[1]);
  EXPECT_EQ(4, rows.values_[2]);

  ASSERT_EQ(OB_SUCCESS, cursor->rescan(arguments, 2));
  ASSERT_EQ(OB_SUCCESS, cursor->next(&context, 1, &emitted_rows));
  EXPECT_EQ(2, rows.values_.back());
  ASSERT_EQ(OB_SUCCESS, cursor->close());
  cursor.reset();
  ASSERT_EQ(OB_SUCCESS,
            loader.get_status("org.seekdb.sql_extension", status));
  EXPECT_EQ(0U, status.lease_count_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}
#endif

TEST(TestPluginLoader, runtime_service_dependency_defaults_are_zero)
{
  const ObPluginRuntimeServiceDependency dependency;
  EXPECT_TRUE(dependency.service_id_.empty());
  EXPECT_EQ(0U, dependency.requested_version_.struct_size);
  EXPECT_EQ(0U, dependency.requested_version_.minimum_inclusive.major);
  EXPECT_EQ(0U, dependency.requested_version_.minimum_inclusive.minor);
  EXPECT_EQ(0U, dependency.requested_version_.minimum_inclusive.patch);
  EXPECT_EQ(0U, dependency.requested_version_.maximum_exclusive.major);
  EXPECT_EQ(0U, dependency.requested_version_.maximum_exclusive.minor);
  EXPECT_EQ(0U, dependency.requested_version_.maximum_exclusive.patch);
  EXPECT_EQ(0U, dependency.requested_version_.reserved[0]);
  EXPECT_EQ(0U, dependency.requested_version_.reserved[1]);
  EXPECT_EQ(0U, dependency.required_capabilities_);
  EXPECT_FALSE(dependency.optional_);
  EXPECT_TRUE(dependency.provider_plugin_id_.empty());
  EXPECT_EQ(0U, dependency.provider_generation_);
  EXPECT_EQ(0U, dependency.provider_version_.major);
  EXPECT_EQ(0U, dependency.provider_version_.minor);
  EXPECT_EQ(0U, dependency.provider_version_.patch);
}

#if !defined(_WIN32)
TEST(TestPluginLoader, classifies_only_an_unavailable_required_service)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard,
                        registry));
  EXPECT_EQ(ObPluginLoadFailureReason::NONE,
            loader.last_failure_reason());

  {
    RequiredServiceManifestOverride requirement_override;
    std::string override_error;
    ASSERT_TRUE(requirement_override.install(override_error))
        << override_error;
    EXPECT_EQ(OB_ENTRY_NOT_EXIST, loader.load(SEEKDB_TEST_PLUGIN_FILE));
    EXPECT_EQ(ObPluginLoadFailureReason::REQUIRED_SERVICE_UNAVAILABLE,
              loader.last_failure_reason());
    EXPECT_NE(std::string::npos,
              loader.last_error().find("required service is unavailable"));
  }

  // A later attempt gets a new catalog identity and clears the previous
  // classification before loading the restored reference manifest.
  guard->activation_generation_ = 2;
  guard->activation_runtime_incarnation_ = "test-runtime-2";
  guard->activation_operation_id_ = "test-activation-2";
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(ObPluginLoadFailureReason::NONE,
            loader.last_failure_reason());
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, missing_entry_symbol_is_an_other_load_failure)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard,
                        registry));

  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            loader.load(SEEKDB_TEST_MISSING_ENTRY_PLUGIN_FILE));
  EXPECT_EQ(ObPluginLoadFailureReason::OTHER,
            loader.last_failure_reason());
  EXPECT_NE(ObPluginLoadFailureReason::REQUIRED_SERVICE_UNAVAILABLE,
            loader.last_failure_reason());
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}
#endif

TEST(TestPluginLoader, validates_configuration_and_untrusted_paths)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;

  EXPECT_EQ(OB_INVALID_ARGUMENT,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, nullptr, guard, guard, registry));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, nullptr, guard, registry));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, nullptr, registry));
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
  EXPECT_TRUE(loader.is_initialized());
  EXPECT_EQ(OB_INVALID_ARGUMENT, loader.load("../" SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            loader.load(SEEKDB_TEST_PLUGIN_DIR "/" SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(0, verifier->calls_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  EXPECT_FALSE(loader.is_initialized());
  EXPECT_EQ(OB_STATE_NOT_MATCH,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
}

TEST(TestPluginLoader, verifier_failure_has_no_runtime_effect)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(OB_CHECKSUM_ERROR);
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
  EXPECT_EQ(OB_CHECKSUM_ERROR, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, verifier->calls_);
  EXPECT_EQ(0, registry->service_count());
  EXPECT_NE(std::string::npos, loader.last_error().find("rejected"));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, verified_metadata_rejects_embedded_nul_before_catalog_begin)
{
  struct MetadataCase
  {
    const char *name_;
    std::string plugin_id_;
    std::string build_id_;
    std::string package_digest_;
  };
  const MetadataCase cases[] = {
      {"plugin_id",
       with_embedded_nul(REFERENCE_PLUGIN_ID, "hidden"),
       std::string("reference-abi-v1", sizeof("reference-abi-v1") - 1),
       std::string(TEST_PACKAGE_DIGEST, std::strlen(TEST_PACKAGE_DIGEST))},
      {"build_id",
       std::string(REFERENCE_PLUGIN_ID, std::strlen(REFERENCE_PLUGIN_ID)),
       with_embedded_nul("reference-abi-v1", "hidden"),
       std::string(TEST_PACKAGE_DIGEST, std::strlen(TEST_PACKAGE_DIGEST))},
      {"package_digest",
       std::string(REFERENCE_PLUGIN_ID, std::strlen(REFERENCE_PLUGIN_ID)),
       std::string("reference-abi-v1", sizeof("reference-abi-v1") - 1),
       with_embedded_nul(TEST_PACKAGE_DIGEST, "hidden")},
  };

  for (const MetadataCase &test_case : cases) {
    SCOPED_TRACE(test_case.name_);
    const auto registry = std::make_shared<ObPluginServiceRegistry>();
    const auto verifier = std::make_shared<TestVerifier>(
        OB_SUCCESS, test_case.plugin_id_, test_case.build_id_, 1,
        test_case.package_digest_);
    const auto guard = std::make_shared<TestDisableGuard>();
    ObPluginLoader loader;
    ASSERT_EQ(OB_SUCCESS,
              loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard,
                          registry));

    EXPECT_EQ(OB_INVALID_DATA, loader.load(SEEKDB_TEST_PLUGIN_FILE));
    EXPECT_EQ(1, verifier->calls_);
    EXPECT_EQ(0, guard->activation_begin_calls_);
    EXPECT_EQ(0, guard->activation_abort_calls_);
    EXPECT_EQ(0, registry->service_count());
    EXPECT_EQ(0, registry->extension_count());
    EXPECT_EQ(0U, registry->registry_epoch());
    EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  }
}

TEST(TestPluginLoader, verification_does_not_hold_loader_mutex_or_race_shutdown)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  std::atomic<bool> verify_entered(false);
  std::atomic<bool> verify_release(false);
  std::atomic<int> load_result(OB_SUCCESS);
  verifier->set_verify_barrier(&verify_entered, &verify_release);
  std::thread loading([&]() {
    load_result.store(loader.load(SEEKDB_TEST_PLUGIN_FILE));
  });
  const bool entered = wait_until([&]() { return verify_entered.load(); });
  EXPECT_TRUE(entered);

  // Both calls must complete while verify is deliberately blocked.  shutdown
  // installs the terminal barrier but cannot close an in-flight artifact.
  if (entered) {
    EXPECT_TRUE(loader.is_initialized());
    EXPECT_EQ(OB_EAGAIN, loader.shutdown_for_process_exit(1000000));
  }
  verify_release.store(true);
  if (loading.joinable()) loading.join();
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
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  EXPECT_EQ(OB_INVALID_DATA, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(0, registry->service_count());
  EXPECT_NE(std::string::npos, loader.last_error().find("does not match"));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, invalid_extension_snapshot_has_no_partial_publication)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, "org.seekdb.reference.invalid-extensions",
      "reference-invalid-extensions-abi-v1", 0);
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  EXPECT_EQ(OB_INVALID_DATA,
            loader.load(SEEKDB_TEST_INVALID_EXTENSIONS_PLUGIN_FILE));
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(0, registry->extension_count());
  EXPECT_EQ(0U, registry->registry_epoch());
  EXPECT_NE(std::string::npos,
            loader.last_error().find("invalid function"));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, manifest_cannot_claim_loader_only_catalog_capability)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, "org.seekdb.reference.invalid-manifest",
      "reference-invalid-manifest-abi-v1", 0);
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  EXPECT_EQ(OB_INVALID_DATA,
            loader.load(SEEKDB_TEST_INVALID_MANIFEST_PLUGIN_FILE));
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(0, registry->extension_count());
  EXPECT_EQ(0U, registry->registry_epoch());
  EXPECT_NE(std::string::npos, loader.last_error().find("manifest"));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, registration_commit_conflict_remains_abortable)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, REGISTRATION_CONFLICT_PLUGIN_ID,
      "reference-registration-conflict-abi-v1", 0);
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  ASSERT_EQ(OB_SUCCESS,
            loader.load(SEEKDB_TEST_REGISTRATION_CONFLICT_PLUGIN_FILE));
  EXPECT_EQ(2, registry->service_count());
  EXPECT_EQ(0, registry->extension_count());
  EXPECT_EQ(1U, registry->registry_epoch());

  ObPluginLease shared;
  ASSERT_EQ(OB_SUCCESS,
            registry->acquire(REGISTRATION_CONFLICT_SHARED_SERVICE_ID,
                              1, 0, shared));
  EXPECT_NE(nullptr, shared.service());
  shared.reset();

  ObPluginLease after_abort;
  ASSERT_EQ(OB_SUCCESS,
            registry->acquire(REGISTRATION_CONFLICT_AFTER_ABORT_SERVICE_ID,
                              1, 0, after_abort));
  EXPECT_NE(nullptr, after_abort.service());
  after_abort.reset();

  ObPluginStatusSnapshot active;
  ASSERT_EQ(OB_SUCCESS,
            loader.get_status(REGISTRATION_CONFLICT_PLUGIN_ID, active));
  EXPECT_EQ(ObPluginState::ACTIVE, active.state_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  EXPECT_EQ(0, registry->service_count());
}

TEST(TestPluginLoader, catalog_not_committed_rolls_back_hidden_candidate)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  guard->activation_commit_result_ = OB_STATE_NOT_MATCH;
  guard->activation_decision_ = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  EXPECT_EQ(OB_STATE_NOT_MATCH, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(1, guard->activation_commit_calls_);
  EXPECT_TRUE(guard->last_activation_candidate_prepared_);
  EXPECT_EQ(0U, guard->last_activation_candidate_epoch_);
  EXPECT_EQ(2U, guard->last_activation_service_count_);
  EXPECT_EQ(8U, guard->last_activation_extension_count_);
  EXPECT_EQ(0U, guard->last_activation_dependency_count_);
  EXPECT_EQ(1, guard->activation_abort_calls_);
  EXPECT_EQ(0, guard->activation_complete_calls_);
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(0, registry->extension_count());
  EXPECT_EQ(0U, registry->registry_epoch());
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, catalog_permit_rejects_embedded_nul_and_aborts_before_dso_load)
{
  const char *fields[] = {"runtime_incarnation", "operation_id"};
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
    SCOPED_TRACE(fields[i]);
    const auto registry = std::make_shared<ObPluginServiceRegistry>();
    const auto verifier = std::make_shared<TestVerifier>();
    const auto guard = std::make_shared<TestDisableGuard>();
    if (0 == i) {
      guard->activation_runtime_incarnation_ =
          with_embedded_nul("test-runtime-1", "hidden");
    } else {
      guard->activation_operation_id_ =
          with_embedded_nul("test-activation-1", "hidden");
    }
    ObPluginLoader loader;
    ASSERT_EQ(OB_SUCCESS,
              loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard,
                          registry));

    EXPECT_EQ(OB_INVALID_DATA, loader.load(SEEKDB_TEST_PLUGIN_FILE));
    EXPECT_EQ(1, verifier->calls_);
    EXPECT_EQ(1, guard->activation_begin_calls_);
    EXPECT_EQ(1, guard->activation_abort_calls_);
    EXPECT_EQ(OB_INVALID_DATA, guard->last_activation_abort_status_);
    EXPECT_EQ(ObPluginActivationPhase::CATALOG_BEGIN,
              guard->last_activation_abort_phase_);
    EXPECT_EQ(0, guard->activation_commit_calls_);
    EXPECT_EQ(0, guard->activation_complete_calls_);
    EXPECT_EQ(0, registry->service_count());
    EXPECT_EQ(0, registry->extension_count());
    EXPECT_EQ(0U, registry->registry_epoch());
    EXPECT_NE(std::string::npos,
              loader.last_error().find(
                  "catalog activation permit identity is invalid"));
    ObPluginStatusSnapshot absent;
    EXPECT_EQ(OB_ENTRY_NOT_EXIST,
              loader.get_status(REFERENCE_PLUGIN_ID, absent));
    EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  }
}

TEST(TestPluginLoader, activation_abort_failure_returns_catalog_error_and_reserves_identity)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, "org.seekdb.reference.invalid-extensions",
      "reference-invalid-extensions-abi-v1", 0);
  const auto guard = std::make_shared<TestDisableGuard>();
  guard->activation_abort_result_ = OB_ERR_UNEXPECTED;
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  EXPECT_EQ(OB_ERR_UNEXPECTED,
            loader.load(SEEKDB_TEST_INVALID_EXTENSIONS_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(0, guard->activation_commit_calls_);
  EXPECT_EQ(1, guard->activation_abort_calls_);
  EXPECT_EQ(OB_INVALID_DATA, guard->last_activation_abort_status_);
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(0, registry->extension_count());
  EXPECT_EQ(0U, registry->registry_epoch());
  EXPECT_NE(std::string::npos,
            loader.last_error().find("catalog activation abort failed"));
  EXPECT_NE(std::string::npos,
            loader.last_error().find("test catalog failed to persist activation abort"));

  ObPluginStatusSnapshot retained;
  ASSERT_EQ(OB_SUCCESS,
            loader.get_status("org.seekdb.reference.invalid-extensions",
                              retained));
  EXPECT_EQ(1U, retained.generation_);
  EXPECT_NE(ObPluginState::ACTIVE, retained.state_);

  // The failed durable abort leaves this catalog/runtime identity fenced.  A
  // retry is rejected locally and must not create a second durable begin.
  EXPECT_EQ(OB_ENTRY_EXIST,
            loader.load(SEEKDB_TEST_INVALID_EXTENSIONS_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(1, guard->activation_abort_calls_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, promote_without_commit_token_is_treated_as_unknown)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  guard->provide_activation_commit_ = false;
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  EXPECT_EQ(OB_TRANS_UNKNOWN, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(1, guard->activation_commit_calls_);
  EXPECT_EQ(0, guard->activation_abort_calls_);
  EXPECT_EQ(0, guard->activation_complete_calls_);
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(0, registry->extension_count());
  EXPECT_EQ(0U, registry->registry_epoch());

  ObPluginStatusSnapshot retained;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, retained));
  EXPECT_NE(ObPluginState::ACTIVE, retained.state_);
  EXPECT_EQ(OB_ENTRY_EXIST, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(0, guard->activation_abort_calls_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, not_committed_with_commit_token_is_treated_as_unknown)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  guard->activation_decision_ = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
  guard->force_activation_commit_token_ = true;
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  EXPECT_EQ(OB_TRANS_UNKNOWN, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(1, guard->activation_commit_calls_);
  EXPECT_EQ(0, guard->activation_abort_calls_);
  EXPECT_EQ(0, guard->activation_complete_calls_);
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(0, registry->extension_count());
  EXPECT_EQ(0U, registry->registry_epoch());

  ObPluginStatusSnapshot retained;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, retained));
  EXPECT_NE(ObPluginState::ACTIVE, retained.state_);
  EXPECT_EQ(OB_ENTRY_EXIST, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(0, guard->activation_abort_calls_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, unknown_catalog_commit_never_publishes_or_reuses_identity)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  guard->activation_commit_result_ = OB_TRANS_UNKNOWN;
  guard->activation_decision_ = OB_PLUGIN_ACTIVATION_UNKNOWN;
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  EXPECT_EQ(OB_TRANS_UNKNOWN, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_commit_calls_);
  EXPECT_EQ(0, guard->activation_abort_calls_);
  EXPECT_EQ(0, guard->activation_complete_calls_);
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(0, registry->extension_count());
  EXPECT_EQ(0U, registry->registry_epoch());

  ObPluginStatusSnapshot retained;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, retained));
  EXPECT_EQ(1U, retained.generation_);
  EXPECT_EQ("test-runtime-1", retained.runtime_incarnation_);
  EXPECT_EQ("test-activation-1", retained.operation_id_);
  EXPECT_NE(ObPluginState::ACTIVE, retained.state_);

  // UNKNOWN owns this exact runtime identity until durable recovery resolves
  // the transaction, even though the safely stopped code is not published.
  EXPECT_EQ(OB_ENTRY_EXIST, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(0, guard->activation_abort_calls_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, catalog_complete_failure_leaves_runtime_active)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  guard->activation_complete_result_ = OB_ERR_UNEXPECTED;
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  EXPECT_EQ(OB_ERR_UNEXPECTED, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(1, guard->activation_commit_calls_);
  EXPECT_EQ(1, guard->activation_complete_calls_);
  EXPECT_EQ(0, guard->activation_abort_calls_);
  EXPECT_EQ(OB_SUCCESS, guard->last_activation_complete_result_);
  EXPECT_EQ(ObPluginState::ACTIVE, guard->last_activation_complete_state_);
  EXPECT_EQ(ObPluginActivationPhase::COMPLETE,
            guard->last_activation_complete_phase_);
  EXPECT_EQ(2, registry->service_count());
  EXPECT_EQ(8, registry->extension_count());
  EXPECT_EQ(1U, registry->registry_epoch());

  ObPluginStatusSnapshot active;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, active));
  EXPECT_EQ(ObPluginState::ACTIVE, active.state_);
  EXPECT_EQ(1U, active.generation_);
  EXPECT_EQ("test-runtime-1", active.runtime_incarnation_);
  EXPECT_EQ("test-activation-1", active.operation_id_);
  EXPECT_EQ(OB_ENTRY_EXIST, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, commit_window_is_hidden_and_shutdown_cannot_overtake)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  std::atomic<bool> commit_entered(false);
  std::atomic<bool> release_commit(false);
  std::atomic<int> load_result(OB_SUCCESS);
  guard->set_activation_commit_barrier(&commit_entered, &release_commit);
  std::thread loading([&]() {
    load_result.store(loader.load(SEEKDB_TEST_PLUGIN_FILE));
  });
  const bool entered = wait_until([&]() { return commit_entered.load(); });
  EXPECT_TRUE(entered);

  if (entered) {
    EXPECT_TRUE(guard->last_activation_candidate_prepared_);
    EXPECT_EQ(2U, guard->last_activation_service_count_);
    EXPECT_EQ(8U, guard->last_activation_extension_count_);
    EXPECT_EQ(0U, guard->last_activation_dependency_count_);
    EXPECT_EQ(0, registry->service_count());
    EXPECT_EQ(0, registry->extension_count());
    EXPECT_EQ(0U, registry->registry_epoch());
    ObPluginLease hidden;
    EXPECT_EQ(OB_ENTRY_NOT_EXIST,
              registry->acquire(REFERENCE_SERVICE_ID, 1, 0, hidden));

    // The shutdown call establishes its terminal barrier, but the catalog
    // decision already in progress must reach no-fail promote before teardown.
    EXPECT_EQ(OB_EAGAIN, loader.shutdown_for_process_exit(1000000));
  }
  release_commit.store(true);
  if (loading.joinable()) loading.join();
  EXPECT_EQ(OB_SUCCESS, load_result.load());
  EXPECT_EQ(1, guard->activation_complete_calls_);
  EXPECT_EQ(2, registry->service_count());
  EXPECT_EQ(8, registry->extension_count());
  EXPECT_EQ(1U, registry->registry_epoch());

  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  EXPECT_EQ(0, registry->service_count());
}

TEST(TestPluginLoader, startup_recovery_rejects_catalog_permit_fence_mismatch)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  ObPluginRecoveryActivation recovery;
  recovery.relative_path_ = SEEKDB_TEST_PLUGIN_FILE;
  recovery.plugin_id_ = REFERENCE_PLUGIN_ID;
  recovery.package_digest_ = TEST_PACKAGE_DIGEST;
  recovery.generation_ = 7;
  recovery.runtime_incarnation_ = "test-runtime-7";
  recovery.operation_id_ = "test-activation-7";
  EXPECT_EQ(OB_INVALID_DATA, loader.recover_startup_activation(recovery));
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(ObPluginActivationMode::STARTUP_RECOVERY,
            guard->last_activation_mode_);
  EXPECT_EQ(7U, guard->last_activation_expected_generation_);
  EXPECT_EQ("test-runtime-7", guard->last_activation_expected_incarnation_);
  EXPECT_EQ("test-activation-7", guard->last_activation_expected_operation_id_);
  EXPECT_EQ(0, guard->activation_commit_calls_);
  EXPECT_EQ(0, guard->activation_complete_calls_);
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(0U, registry->registry_epoch());
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, startup_recovery_rejects_embedded_nul_before_verification)
{
  ObPluginRecoveryActivation valid;
  valid.relative_path_ = SEEKDB_TEST_PLUGIN_FILE;
  valid.plugin_id_ = REFERENCE_PLUGIN_ID;
  valid.package_digest_ = TEST_PACKAGE_DIGEST;
  valid.generation_ = 1;
  valid.runtime_incarnation_ = "test-runtime-1";
  valid.operation_id_ = "test-activation-1";

  const auto expect_rejected = [&](const char *field,
                                   const ObPluginRecoveryActivation &recovery) {
    SCOPED_TRACE(field);
    const auto registry = std::make_shared<ObPluginServiceRegistry>();
    const auto verifier = std::make_shared<TestVerifier>();
    const auto guard = std::make_shared<TestDisableGuard>();
    ObPluginLoader loader;
    ASSERT_EQ(OB_SUCCESS,
              loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard,
                          registry));

    EXPECT_EQ(OB_INVALID_ARGUMENT,
              loader.recover_startup_activation(recovery));
    EXPECT_EQ(0, verifier->calls_);
    EXPECT_EQ(0, guard->activation_begin_calls_);
    EXPECT_EQ(0, guard->activation_abort_calls_);
    EXPECT_EQ(0, registry->service_count());
    EXPECT_EQ(0, registry->extension_count());
    EXPECT_EQ(0U, registry->registry_epoch());
    EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
  };

  ObPluginRecoveryActivation recovery = valid;
  recovery.plugin_id_ = with_embedded_nul(REFERENCE_PLUGIN_ID, "hidden");
  expect_rejected("plugin_id", recovery);

  recovery = valid;
  recovery.package_digest_ = with_embedded_nul(TEST_PACKAGE_DIGEST, "hidden");
  expect_rejected("package_digest", recovery);

  recovery = valid;
  recovery.runtime_incarnation_ =
      with_embedded_nul("test-runtime-1", "hidden");
  expect_rejected("runtime_incarnation", recovery);

  recovery = valid;
  recovery.operation_id_ =
      with_embedded_nul("test-activation-1", "hidden");
  expect_rejected("operation_id", recovery);
}

TEST(TestPluginLoader, startup_recovery_exact_tuple_promotes_and_completes)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  ObPluginRecoveryActivation recovery;
  recovery.relative_path_ = SEEKDB_TEST_PLUGIN_FILE;
  recovery.plugin_id_ = REFERENCE_PLUGIN_ID;
  recovery.package_digest_ = TEST_PACKAGE_DIGEST;
  recovery.generation_ = 1;
  recovery.runtime_incarnation_ = "test-runtime-1";
  recovery.operation_id_ = "test-activation-1";
  uint64_t loaded_generation = 0;
  ASSERT_EQ(OB_SUCCESS,
            loader.recover_startup_activation(recovery, &loaded_generation));

  EXPECT_EQ(1U, loaded_generation);
  EXPECT_EQ(1, guard->activation_begin_calls_);
  EXPECT_EQ(ObPluginActivationMode::STARTUP_RECOVERY,
            guard->last_activation_mode_);
  EXPECT_EQ(REFERENCE_PLUGIN_ID, guard->last_activation_plugin_id_);
  EXPECT_EQ(TEST_PACKAGE_DIGEST, guard->last_activation_digest_);
  EXPECT_EQ(1U, guard->last_activation_expected_generation_);
  EXPECT_EQ("test-runtime-1", guard->last_activation_expected_incarnation_);
  EXPECT_EQ("test-activation-1", guard->last_activation_expected_operation_id_);
  EXPECT_EQ(1, guard->activation_commit_calls_);
  EXPECT_EQ(OB_SUCCESS, guard->last_activation_candidate_status_);
  EXPECT_TRUE(guard->last_activation_candidate_prepared_);
  EXPECT_EQ(ObPluginActivationPhase::CATALOG_FINISH,
            guard->last_activation_candidate_phase_);
  EXPECT_EQ(1, guard->activation_complete_calls_);
  EXPECT_EQ(OB_SUCCESS, guard->last_activation_complete_result_);
  EXPECT_EQ(ObPluginState::ACTIVE, guard->last_activation_complete_state_);
  EXPECT_EQ(ObPluginActivationPhase::COMPLETE,
            guard->last_activation_complete_phase_);
  EXPECT_EQ(0, guard->activation_abort_calls_);
  EXPECT_EQ(2, registry->service_count());
  EXPECT_EQ(8, registry->extension_count());
  EXPECT_EQ(1U, registry->registry_epoch());

  ObPluginStatusSnapshot active;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, active));
  EXPECT_EQ(ObPluginState::ACTIVE, active.state_);
  EXPECT_EQ(1U, active.generation_);
  EXPECT_EQ("test-runtime-1", active.runtime_incarnation_);
  EXPECT_EQ("test-activation-1", active.operation_id_);
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, rollback_stop_failure_blocks_identity_until_process_exit)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>(
      OB_SUCCESS, BLOCKED_PLUGIN_ID, "reference-blocked-abi-v1", 0);
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

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
      OB_SUCCESS, STOP_BLOCKED_PLUGIN_ID, "reference-stop-blocked-abi-v1", 0);
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
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
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));

  uint64_t generation = 0;
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE, &generation));
  EXPECT_EQ(1U, generation);
  EXPECT_EQ(1, verifier->calls_);
  EXPECT_EQ(2, registry->service_count());
  EXPECT_EQ(8, registry->extension_count());
  EXPECT_EQ(0U, guard->last_activation_dependency_count_);
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

  std::vector<ObPluginExtensionInfo> candidates;
  uint64_t resolution_epoch = 0;
  ASSERT_EQ(OB_SUCCESS,
            registry->find_extensions_by_sql_name(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION, "reference_echo",
                candidates, resolution_epoch));
  ASSERT_EQ(1U, candidates.size());
  EXPECT_EQ(REFERENCE_FUNCTION_ID, candidates[0].spec_.object_id_);
  EXPECT_EQ(generation, candidates[0].owner_generation_);
  EXPECT_EQ(registry->registry_epoch(), resolution_epoch);

  std::vector<ObPluginExtensionInfo> pair_candidates;
  uint64_t pair_resolution_epoch = 0;
  ASSERT_EQ(OB_SUCCESS,
            registry->find_extensions_by_sql_name(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION, "reference_echo_pair",
                pair_candidates, pair_resolution_epoch));
  ASSERT_EQ(1U, pair_candidates.size());
  EXPECT_EQ(REFERENCE_PAIR_FUNCTION_ID,
            pair_candidates[0].spec_.object_id_);
  EXPECT_EQ(generation, pair_candidates[0].owner_generation_);
  EXPECT_EQ(registry->registry_epoch(), pair_resolution_epoch);

  ObPluginExtensionLease extension_lease;
  ObPluginLease implementation_lease;
  ASSERT_EQ(OB_SUCCESS,
            registry->acquire_extension_with_implementation(
                candidates[0], extension_lease, implementation_lease));
  ASSERT_NE(nullptr, extension_lease.info());
  EXPECT_EQ(service, implementation_lease.service());
  EXPECT_EQ("reference_echo", extension_lease.info()->spec_.sql_name_);
  EXPECT_EQ("org.seekdb.reference.type.echo",
            extension_lease.info()->spec_.static_result_type_id_);
  EXPECT_EQ(REFERENCE_SERVICE_ID,
            extension_lease.info()->spec_.implementation_.service_id_);
  EXPECT_STREQ(REFERENCE_PLUGIN_ID, extension_lease.owner_plugin_id());
  EXPECT_EQ(generation, extension_lease.owner_generation());

  // The resolved metadata and implementation leases pin one generation.
  // Quiesce atomically removes both services and extensions.
  lease.reset();
  EXPECT_EQ(OB_TIMEOUT, loader.disable(REFERENCE_PLUGIN_ID, 0));
  EXPECT_EQ(1, guard->calls_);
  EXPECT_EQ(1, guard->finish_calls_);
  EXPECT_EQ(OB_TIMEOUT, guard->last_runtime_result_);
  EXPECT_EQ(ObPluginState::QUIESCING, guard->last_runtime_state_);
  EXPECT_EQ(ObPluginDisablePhase::DRAIN, guard->last_runtime_phase_);
  EXPECT_EQ(generation, guard->last_generation_);
  EXPECT_EQ(generation, guard->expected_generation_);
  EXPECT_EQ(0, registry->service_count());
  EXPECT_EQ(0, registry->extension_count());
  ObPluginLease after_quiesce;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry->acquire(REFERENCE_SERVICE_ID, 1, 0, after_quiesce));
  ObPluginExtensionLease stale_extension_lease;
  ObPluginLease stale_implementation_lease;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry->acquire_extension_with_implementation(
                candidates[0], stale_extension_lease,
                stale_implementation_lease));
  EXPECT_LT(resolution_epoch, registry->registry_epoch());
  implementation_lease.reset();
  extension_lease.reset();

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
  // A stopped generation is no longer active, but its identity may never be
  // reused.  A later install/re-enable becomes valid after the catalog assigns
  // a fresh fenced generation and operation identity.
  EXPECT_EQ(OB_ENTRY_EXIST, loader.load(SEEKDB_TEST_PLUGIN_FILE));
  EXPECT_NE(std::string::npos,
            loader.last_error().find("identity was already used"))
      << loader.last_error();
  guard->activation_generation_ = generation + 1;
  guard->activation_runtime_incarnation_ = "test-runtime-reenabled";
  guard->activation_operation_id_ = "test-activation-reenabled";
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE))
      << loader.last_error();
  ObPluginStatusSnapshot reenabled;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, reenabled));
  EXPECT_EQ(ObPluginState::ACTIVE, reenabled.state_);
  EXPECT_EQ(generation + 1, reenabled.generation_);
  EXPECT_GT(registry->service_count(), 0);

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
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  EXPECT_EQ(OB_STATE_NOT_MATCH, loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  EXPECT_EQ(1, guard->calls_);
  EXPECT_EQ(2, registry->service_count());
  EXPECT_EQ(8, registry->extension_count());
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
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS,
            registry->acquire(REFERENCE_SERVICE_ID, 1, 0, lease));
  std::atomic<bool> worker_ready(false);
  std::atomic<bool> begin_seen(false);
  std::atomic<bool> worker_timed_out(false);
  guard->set_begin_signal(&begin_seen);
  std::thread releaser([&loader, &registry, &worker_ready, &begin_seen,
                        &worker_timed_out,
                        held = std::move(lease)]() mutable {
    worker_ready.store(true);
    if (!wait_until([&]() { return begin_seen.load(); }) ||
        !wait_until([&]() { return registry->service_count() == 0; })) {
      worker_timed_out.store(true);
    }
    ObPluginStatusSnapshot status;
    (void)loader.get_status(REFERENCE_PLUGIN_ID, status);
    held.reset();
  });
  EXPECT_TRUE(wait_until([&]() { return worker_ready.load(); }));

  // If drain held the global loader mutex, get_status() could not complete and
  // this call would time out waiting for the lease owned by the worker.
  EXPECT_EQ(OB_SUCCESS, loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  if (releaser.joinable()) releaser.join();
  EXPECT_FALSE(worker_timed_out.load());
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, process_shutdown_cannot_overtake_disable_permit)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  std::atomic<bool> begin_entered(false);
  std::atomic<bool> release_begin(false);
  std::atomic<int> disable_result(OB_SUCCESS);
  guard->set_begin_barrier(&begin_entered, &release_begin);
  std::thread disabler([&]() {
    disable_result.store(loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  });
  const bool entered = wait_until([&]() { return begin_entered.load(); });
  EXPECT_TRUE(entered);

  if (entered) {
    EXPECT_EQ(OB_EAGAIN, loader.shutdown_for_process_exit(1000000));
  }
  release_begin.store(true);
  if (disabler.joinable()) disabler.join();
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
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  std::atomic<bool> begin_entered(false);
  std::atomic<bool> release_begin(false);
  std::atomic<int> first_result(OB_SUCCESS);
  guard->set_begin_barrier(&begin_entered, &release_begin);
  std::thread first([&]() {
    first_result.store(loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  });
  const bool entered = wait_until([&]() { return begin_entered.load(); });
  EXPECT_TRUE(entered);

  if (entered) {
    EXPECT_EQ(OB_EAGAIN, loader.disable(REFERENCE_PLUGIN_ID, 1000000));
    EXPECT_EQ(1, guard->calls_);
  }
  release_begin.store(true);
  if (first.joinable()) first.join();
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
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS,
            registry->acquire(REFERENCE_SERVICE_ID, 1, 0, lease));
  std::atomic<int> first_result(OB_SUCCESS);
  std::thread first([&]() {
    first_result.store(loader.shutdown_for_process_exit(10000000));
  });
  const bool quiesced = wait_until(
      [&]() { return registry->service_count() == 0; });
  EXPECT_TRUE(quiesced);

  if (quiesced) {
    EXPECT_EQ(OB_EAGAIN, loader.shutdown_for_process_exit(10000000));
  }
  lease.reset();
  if (first.joinable()) first.join();
  EXPECT_EQ(OB_SUCCESS, first_result.load());
}

TEST(TestPluginLoader, catalog_coordinator_must_return_disable_permit)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>(OB_SUCCESS, false);
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  EXPECT_EQ(OB_ERR_UNEXPECTED, loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  EXPECT_EQ(2, registry->service_count());
  EXPECT_EQ(8, registry->extension_count());
  EXPECT_NE(std::string::npos, loader.last_error().find("no disable permit"));
  EXPECT_EQ(OB_SUCCESS, loader.shutdown_for_process_exit(1000000));
}

TEST(TestPluginLoader, stop_callback_waits_for_durable_catalog_checkpoint)
{
  const auto registry = std::make_shared<ObPluginServiceRegistry>();
  const auto verifier = std::make_shared<TestVerifier>();
  const auto guard = std::make_shared<TestDisableGuard>();
  guard->checkpoint_result_ = OB_TRANS_UNKNOWN;
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard,
                        registry));
  ASSERT_EQ(OB_SUCCESS, loader.load(SEEKDB_TEST_PLUGIN_FILE));

  EXPECT_EQ(OB_TRANS_UNKNOWN,
            loader.disable(REFERENCE_PLUGIN_ID, 1000000));
  EXPECT_EQ(1, guard->checkpoint_calls_);
  EXPECT_EQ(0, guard->finish_calls_);
  ObPluginStatusSnapshot quiescing;
  ASSERT_EQ(OB_SUCCESS, loader.get_status(REFERENCE_PLUGIN_ID, quiescing));
  EXPECT_EQ(ObPluginState::QUIESCING, quiescing.state_);
  EXPECT_EQ(0, registry->service_count());

  // Process-exit authority may finish the resident runtime without depending
  // on the unavailable catalog checkpoint.
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
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
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
      OB_SUCCESS, STOP_BLOCKED_PLUGIN_ID, "reference-stop-blocked-abi-v1", 0);
  const auto guard = std::make_shared<TestDisableGuard>(
      OB_SUCCESS, true, OB_ERR_UNEXPECTED);
  ObPluginLoader loader;
  ASSERT_EQ(OB_SUCCESS,
            loader.init(SEEKDB_TEST_PLUGIN_DIR, verifier, guard, guard, registry));
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
