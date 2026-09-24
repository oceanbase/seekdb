// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_NATIVE_ACTIVATION_FIXTURE_H_
#define SEEKDB_TEST_NATIVE_ACTIVATION_FIXTURE_H_
#include "share/plugin/ob_plugin_loader.h"
#include "lib/ob_errno.h"

namespace native_activation_test {
using namespace oceanbase::share::plugin;
using namespace oceanbase::common;

// These test doubles model the catalog commit protocol, NOT durable SQL or a
// production verifier. Build outputs remain immutable for the test lifetime.
struct Observation {
  std::shared_ptr<ObPluginServiceRegistry> registry = std::make_shared<ObPluginServiceRegistry>();
  bool reject = false;
  bool committed = false;
  bool completed = false;
  bool aborted = false;
  bool gis = false;
  size_t expected_services = 7;
  size_t expected_extensions = 9; // SQL reference plugin: seven public objects + two implementation-only functions.
  std::vector<ObPluginExtensionInfo> objects;
};
class TestArtifact final : public ObPluginVerifiedArtifact {
public:
  TestArtifact(std::string path, bool services, bool gis, bool rust, bool scaffold = false, bool candidate = false) : path_(std::move(path)) {
    metadata_.plugin_id_ = "org.seekdb.sql_extension";
    metadata_.build_id_ = "sql-extension-catalog-v1";
    metadata_.package_digest_ = "sha256:" + std::string(64, '1');
    metadata_.package_version_ = {1, 0, 0};
    metadata_.catalog_version_ = metadata_.data_format_version_ = 1;
    if (services) {
      metadata_.plugin_id_ = "org.seekdb.reference.registration-conflict";
      metadata_.build_id_ = "reference-registration-conflict-abi-v1";
      metadata_.catalog_version_ = metadata_.data_format_version_ = 0;
    }
    if (gis) {
      metadata_.plugin_id_ = "org.seekdb.gis";
      metadata_.build_id_ = "gis-execution-spi-v1";
    }
    if (rust) {
      metadata_.plugin_id_ = "org.seekdb.rust-text";
      metadata_.build_id_ = "rust-text-owned-memory-v15";
      metadata_.data_format_version_ = 1;
    }
    if (scaffold) {
      metadata_.plugin_id_ = "org.seekdb.generated";
      metadata_.build_id_ = "generated-v1";
      metadata_.data_format_version_ = 0;
    }
    if (candidate) {
      metadata_.plugin_id_ = "org.seekdb.rust-candidate";
      metadata_.build_id_ = "rust-candidate-v18";
      metadata_.data_format_version_ = 0;
    }
  }
  const std::string &load_path() const override { return path_; }
  const ObPluginArtifactMetadata &metadata() const override { return metadata_; }
private:
  std::string path_;
  ObPluginArtifactMetadata metadata_;
};
class TestVerifier final : public ObPluginVerifier {
public:
  explicit TestVerifier(bool services, bool gis, bool rust, bool scaffold = false, bool candidate = false)
      : services_(services), gis_(gis), rust_(rust), scaffold_(scaffold), candidate_(candidate) {}
  int verify_and_pin(const std::string &path, std::unique_ptr<ObPluginVerifiedArtifact> &out,
                     std::string &) const override {
    out.reset(new TestArtifact(path, services_, gis_, rust_, scaffold_, candidate_));
    return OB_SUCCESS;
  }
private:
  bool services_;
  bool gis_;
  bool rust_;
  bool scaffold_;
  bool candidate_;
};
class TestCommit final : public ObPluginActivationCommit {
public:
  explicit TestCommit(Observation &observation) : observation_(observation) {}
  int complete(const ObPluginRuntimeActivationResult &result, std::string &) noexcept override {
    CHECK(result.actual_state_ == ObPluginState::ACTIVE);
    CHECK(observation_.registry->extension_count() == static_cast<int64_t>(observation_.expected_extensions));
    observation_.completed = true;
    return OB_SUCCESS;
  }
private:
  Observation &observation_;
};
class TestPermit final : public ObPluginActivationPermit {
public:
  explicit TestPermit(Observation &observation) : observation_(observation) {}
  uint64_t generation() const noexcept override { return 1; }
  const std::string &runtime_incarnation() const noexcept override { return incarnation_; }
  const std::string &operation_id() const noexcept override { return operation_; }
  int commit_candidate(const ObPluginRuntimeActivationResult &result,
                       ObPluginActivationDecision &decision,
                       std::unique_ptr<ObPluginActivationCommit> &commit,
                       std::string &) noexcept override {
    CHECK(observation_.registry->extension_count() == 0);
    CHECK(observation_.registry->service_count() == 0);
    if (observation_.gis) {
      CHECK(result.extensions_.size() > 1 && result.services_.size() > 1);
      observation_.expected_extensions = result.extensions_.size();
      observation_.expected_services = result.services_.size();
    }
    CHECK(result.extensions_.size() == observation_.expected_extensions);
    CHECK(result.services_.size() == observation_.expected_services);
    observation_.objects = result.extensions_;
    decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
    if (observation_.reject) return OB_STATE_NOT_MATCH;
    commit.reset(new TestCommit(observation_));
    observation_.committed = true;
    decision = OB_PLUGIN_ACTIVATION_PROMOTE;
    return OB_SUCCESS;
  }
  int abort(const ObPluginRuntimeActivationResult &, std::string &) noexcept override {
    observation_.aborted = true;
    return OB_SUCCESS;
  }
private:
  Observation &observation_;
  const std::string incarnation_ = "test.runtime.1";
  const std::string operation_ = "test.operation.1";
};
class TestGuard final : public ObPluginActivationGuard, public ObPluginDisableGuard {
public:
  explicit TestGuard(Observation &observation) : observation_(observation) {}
  int begin_activation(const ObPluginActivationRequest &,
                       std::unique_ptr<ObPluginActivationPermit> &permit,
                       std::string &) const noexcept override {
    permit.reset(new TestPermit(observation_)); return OB_SUCCESS;
  }
  int begin_restricted_disable(const std::string &, uint64_t,
                              std::unique_ptr<ObPluginDisablePermit> &,
                              std::string &) const noexcept override { return OB_NOT_SUPPORTED; }
private:
  Observation &observation_;
};
} // namespace native_activation_test
#endif
