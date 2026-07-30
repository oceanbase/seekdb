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

#include "share/plugin/ob_plugin_registry.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "lib/ob_errno.h"

namespace oceanbase
{
namespace share
{
namespace plugin
{

using namespace oceanbase::common;

namespace
{

struct TestServiceV1
{
  uint32_t struct_size_;
  int (*value_)();
};

int value_one()
{
  return 1;
}

int value_two()
{
  return 2;
}

const TestServiceV1 SERVICE_ONE = {sizeof(TestServiceV1), value_one};
const TestServiceV1 SERVICE_TWO = {sizeof(TestServiceV1), value_two};

ObPluginImplementationSpec make_implementation(const char *service_id)
{
  ObPluginImplementationSpec implementation;
  implementation.service_id_ = service_id;
  implementation.version_range_.struct_size =
      sizeof(seekdb_plugin_version_range_t);
  implementation.version_range_.minimum_inclusive = {1, 0, 0};
  implementation.version_range_.maximum_exclusive = {2, 0, 0};
  implementation.required_capabilities_ =
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE;
  return implementation;
}

ObPluginExtensionSpec make_extension(
    const seekdb_plugin_extension_kind_t kind,
    const char *object_id,
    const char *implementation_service = "com.seekdb.extensions.impl")
{
  ObPluginExtensionSpec extension;
  extension.kind_ = kind;
  extension.object_id_ = object_id;
  extension.flags_ = SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC;
  extension.implementation_ = make_implementation(implementation_service);
  switch (kind) {
    case SEEKDB_PLUGIN_EXTENSION_TYPE:
      extension.sql_name_ = "extension_value";
      extension.physical_format_id_ = "com.seekdb.format.extension-value";
      extension.physical_format_version_ = 1;
      break;
    case SEEKDB_PLUGIN_EXTENSION_FUNCTION:
      extension.sql_name_ = "extension_echo";
      extension.minimum_arity_ = 1;
      extension.maximum_arity_ = 1;
      extension.static_result_type_id_ = "core.type.bytes";
      break;
    case SEEKDB_PLUGIN_EXTENSION_CAST:
      extension.source_type_id_ = "core.type.bytes";
      extension.target_type_id_ = "com.seekdb.type.extension-value";
      extension.cast_context_ = SEEKDB_PLUGIN_CAST_EXPLICIT;
      extension.cost_ = 10;
      break;
    case SEEKDB_PLUGIN_EXTENSION_INDEX_ACCESS_METHOD:
      extension.sql_name_ = "extension_index";
      break;
    case SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK:
      extension.hook_point_ = "optimizer.logical-rewrite";
      extension.priority_ = 20;
      break;
    case SEEKDB_PLUGIN_EXTENSION_DAS_HOOK:
      extension.hook_point_ = "das.table-scan";
      extension.priority_ = 20;
      break;
    case SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT:
      extension.catalog_object_kind_ = "system-view";
      extension.schema_name_ = "sys";
      extension.sql_name_ = "extension_objects";
      extension.definition_digest_ =
          "sha256:0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef";
      extension.flags_ = SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT |
                         SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG;
      extension.implementation_ = ObPluginImplementationSpec();
      break;
    default:
      break;
  }
  return extension;
}

std::shared_ptr<ObPluginGeneration> make_initializing_generation(
    const char *plugin_id,
    const uint64_t generation)
{
  std::shared_ptr<ObPluginGeneration> owner(
      new ObPluginGeneration(plugin_id, generation));
  EXPECT_EQ(OB_SUCCESS, owner->transition_to(ObPluginState::VALIDATED));
  EXPECT_EQ(OB_SUCCESS, owner->transition_to(ObPluginState::LOADED));
  EXPECT_EQ(OB_SUCCESS, owner->transition_to(ObPluginState::INITIALIZING));
  return owner;
}

std::string with_embedded_nul(const char *prefix)
{
  std::string value(prefix);
  value.push_back('\0');
  value.append("hidden");
  return value;
}

} // namespace

TEST(TestPluginRegistry, rejects_invalid_lifecycle_transition)
{
  ObPluginGeneration generation("com.seekdb.test", 1);
  EXPECT_EQ(OB_STATE_NOT_MATCH, generation.transition_to(ObPluginState::ACTIVE));
  EXPECT_EQ(ObPluginState::DISCOVERED, generation.state());
  EXPECT_EQ(OB_SUCCESS, generation.transition_to(ObPluginState::VALIDATED));
  EXPECT_EQ(OB_STATE_NOT_MATCH, generation.transition_to(ObPluginState::STOPPED));
}

TEST(TestPluginRegistry, blocked_generation_cannot_bypass_terminal_loader)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.blocked", 9);
  ASSERT_EQ(OB_SUCCESS, owner->transition_to(ObPluginState::BLOCKED));
  EXPECT_EQ(ObPluginState::BLOCKED, owner->state());
  EXPECT_EQ(OB_STATE_NOT_MATCH, owner->transition_to(ObPluginState::ACTIVE));
  EXPECT_EQ(OB_STATE_NOT_MATCH, registry.quiesce(owner));
  EXPECT_EQ(OB_STATE_NOT_MATCH, registry.mark_stopped(owner));
  EXPECT_EQ(ObPluginState::BLOCKED, owner->state());
}

TEST(TestPluginRegistry, registration_is_staged_and_versioned)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.example", 1);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  EXPECT_EQ(OB_NOT_SUPPORTED,
            registration.add_service(
                "com.seekdb.example.discovery", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service("com.seekdb.example.value", 1, 2, 3, 0x5, &SERVICE_ONE));

  ObPluginLease invisible;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.example.value", 1, 0, invisible));
  ASSERT_EQ(OB_SUCCESS, registration.commit());
  EXPECT_EQ(ObPluginState::ACTIVE, owner->state());

  ObPluginLease compatible;
  ASSERT_EQ(OB_SUCCESS,
            registry.acquire("com.seekdb.example.value", 1, 1, compatible));
  ASSERT_TRUE(compatible.is_valid());
  EXPECT_EQ(2U, compatible.service_minor());
  EXPECT_EQ(3U, compatible.service_patch());
  EXPECT_EQ(0x5U, compatible.service_capabilities());
  EXPECT_STREQ("com.seekdb.example", compatible.owner_plugin_id());
  EXPECT_EQ(1U, compatible.owner_generation());
  const auto *service = static_cast<const TestServiceV1 *>(compatible.service());
  ASSERT_NE(nullptr, service);
  EXPECT_EQ(1, service->value_());

  ObPluginLease too_new;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.example.value", 1, 3, too_new));
  ObPluginLease patch_and_capability;
  EXPECT_EQ(OB_SUCCESS,
            registry.acquire("com.seekdb.example.value", 1, 2, 3, 0x1,
                             patch_and_capability));
  ObPluginLease missing_capability;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.example.value", 1, 2, 3, 0x8,
                             missing_capability));
  ObPluginLease wrong_major;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.example.value", 2, 0, wrong_major));
}

TEST(TestPluginRegistry, duplicate_commit_has_no_partial_publication)
{
  ObPluginServiceRegistry registry;
  const auto first = make_initializing_generation("com.seekdb.first", 1);
  ObPluginRegistration first_registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(first, first_registration));
  ASSERT_EQ(OB_SUCCESS,
            first_registration.add_service("com.seekdb.shared.value", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, first_registration.commit());

  const auto second = make_initializing_generation("com.seekdb.second", 1);
  ObPluginRegistration second_registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(second, second_registration));
  ASSERT_EQ(OB_SUCCESS,
            second_registration.add_service("com.seekdb.second.unique", 1, 0, &SERVICE_TWO));
  ASSERT_EQ(OB_SUCCESS,
            second_registration.add_service("com.seekdb.shared.value", 1, 1, &SERVICE_TWO));
  EXPECT_EQ(OB_ENTRY_EXIST, second_registration.commit());
  EXPECT_TRUE(second_registration.is_open());
  EXPECT_EQ(1, registry.service_count());

  ObPluginLease unique;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.second.unique", 1, 0, unique));
  second_registration.rollback();
  EXPECT_FALSE(second_registration.is_open());
}

TEST(TestPluginRegistry, prepared_candidate_is_invisible_until_atomic_promote)
{
  ObPluginServiceRegistry registry;
  const auto owner =
      make_initializing_generation("com.seekdb.candidate", 1);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service(
                "com.seekdb.extensions.impl", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_extension(make_extension(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                "com.seekdb.function.candidate")));

  ObPluginActivationCandidate candidate;
  ASSERT_EQ(OB_SUCCESS, registration.prepare(candidate));
  EXPECT_FALSE(registration.is_open());
  EXPECT_TRUE(candidate.is_prepared());
  EXPECT_EQ(0U, candidate.base_epoch());
  ASSERT_EQ(1U, candidate.contributed_services().size());
  EXPECT_EQ("com.seekdb.extensions.impl",
            candidate.contributed_services()[0].name_);
  ASSERT_EQ(1U, candidate.contributed_extensions().size());
  EXPECT_EQ("com.seekdb.function.candidate",
            candidate.contributed_extensions()[0].spec_.object_id_);
  EXPECT_EQ("com.seekdb.candidate",
            candidate.contributed_extensions()[0].owner_plugin_id_);
  EXPECT_EQ(ObPluginState::INITIALIZING, owner->state());
  EXPECT_EQ(0, registry.service_count());
  EXPECT_EQ(0, registry.extension_count());
  EXPECT_EQ(0U, registry.registry_epoch());

  std::vector<ObPluginServiceInfo> services;
  ASSERT_EQ(OB_SUCCESS, registry.list_services(services));
  EXPECT_TRUE(services.empty());
  std::vector<ObPluginExtensionInfo> functions;
  uint64_t lookup_epoch = 99;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_extensions_by_sql_name(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION, "extension_echo",
                functions, lookup_epoch));
  EXPECT_TRUE(functions.empty());
  EXPECT_EQ(0U, lookup_epoch);
  ObPluginLease invisible;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.extensions.impl", 1, 0,
                             invisible));
  // The activation reservation also prevents a callback/lifecycle race from
  // invalidating a catalog change after it has succeeded.
  EXPECT_EQ(OB_EAGAIN, owner->transition_to(ObPluginState::FAILED));

  candidate.promote();
  EXPECT_FALSE(candidate.is_prepared());
  EXPECT_EQ(ObPluginState::ACTIVE, owner->state());
  EXPECT_EQ(1, registry.service_count());
  EXPECT_EQ(1, registry.extension_count());
  EXPECT_EQ(1U, registry.registry_epoch());

  ObPluginLease visible;
  ASSERT_EQ(OB_SUCCESS,
            registry.acquire("com.seekdb.extensions.impl", 1, 0, visible));
  EXPECT_EQ(&SERVICE_ONE, visible.service());
}

TEST(TestPluginRegistry, candidate_destruction_is_safe_after_registry_lifetime)
{
  const auto owner =
      make_initializing_generation("com.seekdb.candidate.lifetime", 1);
  ObPluginActivationCandidate candidate;
  {
    ObPluginServiceRegistry registry;
    ObPluginRegistration registration;
    ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
    ASSERT_EQ(OB_SUCCESS,
              registration.add_service(
                  "com.seekdb.candidate.lifetime.value", 1, 0,
                  &SERVICE_ONE));
    ASSERT_EQ(OB_SUCCESS, registration.prepare(candidate));
    ASSERT_TRUE(candidate.is_prepared());
  }

  // Registry destruction disarms its outstanding reservation.  The candidate
  // can subsequently be aborted/destroyed without following a stale registry
  // pointer, and the generation is no longer lifecycle-frozen.
  EXPECT_FALSE(candidate.is_prepared());
  candidate.abort();
  EXPECT_FALSE(candidate.is_prepared());
  EXPECT_EQ(OB_SUCCESS, owner->transition_to(ObPluginState::FAILED));
}

TEST(TestPluginRegistry, reservation_prevents_epoch_staleness_until_abort)
{
  ObPluginServiceRegistry registry;
  const auto active_owner =
      make_initializing_generation("com.seekdb.active", 1);
  ObPluginRegistration active_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(active_owner, active_registration));
  ASSERT_EQ(OB_SUCCESS,
            active_registration.add_service(
                "com.seekdb.active.value", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, active_registration.commit());
  ASSERT_EQ(1U, registry.registry_epoch());

  const auto candidate_owner =
      make_initializing_generation("com.seekdb.reserved", 1);
  ObPluginRegistration candidate_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(candidate_owner,
                                        candidate_registration));
  ASSERT_EQ(OB_SUCCESS,
            candidate_registration.add_service(
                "com.seekdb.reserved.value", 1, 0, &SERVICE_TWO));
  ObPluginActivationCandidate candidate;
  ASSERT_EQ(OB_SUCCESS, candidate_registration.prepare(candidate));
  EXPECT_EQ(1U, candidate.base_epoch());

  // Reads of the current snapshot continue while catalog work is in flight.
  ObPluginLease active_lease;
  ASSERT_EQ(OB_SUCCESS,
            registry.acquire("com.seekdb.active.value", 1, 0,
                             active_lease));
  EXPECT_EQ(&SERVICE_ONE, active_lease.service());

  const auto competing_owner =
      make_initializing_generation("com.seekdb.competing", 1);
  ObPluginRegistration competing_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(competing_owner,
                                        competing_registration));
  ASSERT_EQ(OB_SUCCESS,
            competing_registration.add_service(
                "com.seekdb.competing.value", 1, 0, &SERVICE_ONE));
  EXPECT_EQ(OB_EAGAIN, competing_registration.commit());
  EXPECT_TRUE(competing_registration.is_open());
  EXPECT_EQ(OB_EAGAIN, registry.quiesce(active_owner));
  EXPECT_EQ(1U, registry.registry_epoch());

  candidate.abort();
  EXPECT_FALSE(candidate.is_prepared());
  EXPECT_EQ(ObPluginState::INITIALIZING, candidate_owner->state());
  EXPECT_EQ(1, registry.service_count());
  EXPECT_EQ(1U, registry.registry_epoch());

  // Releasing the reservation lets the already-staged competing transaction
  // prepare against the unchanged epoch and publish normally.
  ASSERT_EQ(OB_SUCCESS, competing_registration.commit());
  EXPECT_EQ(2, registry.service_count());
  EXPECT_EQ(2U, registry.registry_epoch());
}

TEST(TestPluginRegistry, abort_allows_same_generation_to_prepare_again)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.retry", 1);
  ObPluginRegistration first_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(owner, first_registration));
  ASSERT_EQ(OB_SUCCESS,
            first_registration.add_service(
                "com.seekdb.retry.value", 1, 0, &SERVICE_ONE));
  ObPluginActivationCandidate discarded;
  ASSERT_EQ(OB_SUCCESS, first_registration.prepare(discarded));
  discarded.abort();

  ObPluginRegistration retry_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(owner, retry_registration));
  ASSERT_EQ(OB_SUCCESS,
            retry_registration.add_service(
                "com.seekdb.retry.value", 1, 1, &SERVICE_TWO));
  ObPluginActivationCandidate retry;
  ASSERT_EQ(OB_SUCCESS, retry_registration.prepare(retry));
  retry.promote();
  EXPECT_EQ(ObPluginState::ACTIVE, owner->state());
  EXPECT_EQ(1U, registry.registry_epoch());

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS,
            registry.acquire("com.seekdb.retry.value", 1, 0, lease));
  EXPECT_EQ(1U, lease.service_minor());
  EXPECT_EQ(&SERVICE_TWO, lease.service());
}

TEST(TestPluginRegistry, services_and_all_extension_kinds_publish_atomically)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.extensions", 11);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service(
                "com.seekdb.extensions.impl", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_ONE));

  const seekdb_plugin_extension_kind_t kinds[] = {
      SEEKDB_PLUGIN_EXTENSION_TYPE,
      SEEKDB_PLUGIN_EXTENSION_FUNCTION,
      SEEKDB_PLUGIN_EXTENSION_CAST,
      SEEKDB_PLUGIN_EXTENSION_INDEX_ACCESS_METHOD,
      SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK,
      SEEKDB_PLUGIN_EXTENSION_DAS_HOOK,
      SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT};
  const char *object_ids[] = {
      "com.seekdb.type.extension-value",
      "com.seekdb.function.extension-echo",
      "com.seekdb.cast.bytes-to-extension-value",
      "com.seekdb.index.extension",
      "com.seekdb.optimizer.extension-rewrite",
      "com.seekdb.das.extension-scan",
      "com.seekdb.catalog.extension-objects"};
  for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
    ASSERT_EQ(OB_SUCCESS,
              registration.add_extension(make_extension(kinds[i], object_ids[i])));
  }

  EXPECT_EQ(0, registry.service_count());
  EXPECT_EQ(0, registry.extension_count());
  EXPECT_EQ(0U, registry.registry_epoch());
  ASSERT_EQ(OB_SUCCESS, registration.commit());
  EXPECT_EQ(1, registry.service_count());
  EXPECT_EQ(7, registry.extension_count());
  EXPECT_EQ(1U, registry.registry_epoch());

  std::vector<ObPluginExtensionInfo> extensions;
  ASSERT_EQ(OB_SUCCESS, registry.list_extensions(extensions));
  ASSERT_EQ(7U, extensions.size());
  for (const ObPluginExtensionInfo &extension : extensions) {
    EXPECT_EQ("com.seekdb.extensions", extension.owner_plugin_id_);
    EXPECT_EQ(11U, extension.owner_generation_);
  }
  uint64_t lookup_epoch = 0;
  std::vector<ObPluginExtensionInfo> functions;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_extensions_by_sql_name(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION, "extension_echo", functions,
                lookup_epoch));
  ASSERT_EQ(1U, functions.size());
  EXPECT_EQ(registry.registry_epoch(), lookup_epoch);
  std::vector<ObPluginExtensionInfo> casts;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_casts("core.type.bytes",
                                "com.seekdb.type.extension-value",
                                SEEKDB_PLUGIN_CAST_EXPLICIT, casts,
                                lookup_epoch));
  ASSERT_EQ(1U, casts.size());
  std::vector<ObPluginExtensionInfo> optimizer_hooks;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_hooks(SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK,
                                "optimizer.logical-rewrite",
                                optimizer_hooks, lookup_epoch));
  ASSERT_EQ(1U, optimizer_hooks.size());
  std::vector<ObPluginExtensionInfo> catalog_objects;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_catalog_objects("system-view", "sys",
                                          "extension_objects", catalog_objects,
                                          lookup_epoch));
  ASSERT_EQ(1U, catalog_objects.size());

  ObPluginExtensionLease extension_lease;
  ObPluginLease implementation_lease;
  ASSERT_EQ(OB_SUCCESS,
            registry.acquire_extension_with_implementation(
                functions[0], extension_lease, implementation_lease));
  ASSERT_TRUE(extension_lease.is_valid());
  ASSERT_TRUE(implementation_lease.is_valid());
  ASSERT_NE(nullptr, extension_lease.info());
  EXPECT_EQ("extension_echo", extension_lease.info()->spec_.sql_name_);
  EXPECT_EQ("core.type.bytes",
            extension_lease.info()->spec_.static_result_type_id_);
  EXPECT_EQ(&SERVICE_ONE, implementation_lease.service());
  EXPECT_EQ(2, owner->lease_count());
}

TEST(TestPluginRegistry, extension_specs_reject_embedded_nul_suffixes)
{
  ObPluginServiceRegistry registry;
  const auto owner =
      make_initializing_generation("com.seekdb.embedded-nul", 1);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));

  ObPluginExtensionSpec function = make_extension(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION, "com.seekdb.function.nul");
  function.object_id_ = with_embedded_nul("com.seekdb.function.valid");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(function));
  function = make_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                            "com.seekdb.function.nul-sql");
  function.sql_name_ = with_embedded_nul("valid_function");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(function));
  function = make_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                            "com.seekdb.function.nul-result");
  function.static_result_type_id_ = with_embedded_nul("core.type.bytes");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(function));
  function = make_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                            "com.seekdb.function.nul-service");
  function.implementation_.service_id_ =
      with_embedded_nul("com.seekdb.extensions.impl");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(function));

  ObPluginExtensionSpec type = make_extension(
      SEEKDB_PLUGIN_EXTENSION_TYPE, "com.seekdb.type.nul-format");
  type.physical_format_id_ =
      with_embedded_nul("com.seekdb.format.extension-value");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(type));

  ObPluginExtensionSpec cast = make_extension(
      SEEKDB_PLUGIN_EXTENSION_CAST, "com.seekdb.cast.nul-source");
  cast.source_type_id_ = with_embedded_nul("core.type.bytes");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(cast));
  cast = make_extension(SEEKDB_PLUGIN_EXTENSION_CAST,
                        "com.seekdb.cast.nul-target");
  cast.target_type_id_ =
      with_embedded_nul("com.seekdb.type.extension-value");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(cast));

  ObPluginExtensionSpec hook = make_extension(
      SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK,
      "com.seekdb.optimizer.nul-hook");
  hook.hook_point_ = with_embedded_nul("optimizer.logical-rewrite");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(hook));

  ObPluginExtensionSpec catalog = make_extension(
      SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT,
      "com.seekdb.catalog.nul-kind");
  catalog.catalog_object_kind_ = with_embedded_nul("system-view");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(catalog));
  catalog = make_extension(SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT,
                           "com.seekdb.catalog.nul-schema");
  catalog.schema_name_ = with_embedded_nul("sys");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(catalog));
  catalog = make_extension(SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT,
                           "com.seekdb.catalog.nul-name");
  catalog.sql_name_ = with_embedded_nul("extension_objects");
  EXPECT_EQ(OB_INVALID_ARGUMENT, registration.add_extension(catalog));

  EXPECT_EQ(0, registry.extension_count());
  EXPECT_EQ(0U, registry.registry_epoch());

  ASSERT_EQ(OB_SUCCESS,
            registration.add_service(
                "com.seekdb.extensions.impl", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_extension(make_extension(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                "com.seekdb.function.valid-nul-lookup")));
  ASSERT_EQ(OB_SUCCESS, registration.commit());
  std::vector<ObPluginExtensionInfo> functions;
  uint64_t lookup_epoch = 0;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_extensions_by_sql_name(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION, "extension_echo",
                functions, lookup_epoch));
  ASSERT_EQ(1U, functions.size());
  ObPluginExtensionInfo forged = functions[0];
  forged.spec_.object_id_ =
      with_embedded_nul("com.seekdb.function.valid-nul-lookup");
  ObPluginExtensionLease extension_lease;
  ObPluginLease implementation_lease;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            registry.acquire_extension_with_implementation(
                forged, extension_lease, implementation_lease));
  EXPECT_FALSE(extension_lease.is_valid());
  EXPECT_FALSE(implementation_lease.is_valid());
}

TEST(TestPluginRegistry, missing_extension_implementation_has_no_partial_publication)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.missing-impl", 3);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service("com.seekdb.missing-impl.unique", 1, 0,
                                     &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_extension(make_extension(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                "com.seekdb.function.missing-impl",
                "com.seekdb.missing-impl.not-provided")));

  EXPECT_EQ(OB_ENTRY_NOT_EXIST, registration.commit());
  EXPECT_TRUE(registration.is_open());
  EXPECT_EQ(ObPluginState::INITIALIZING, owner->state());
  EXPECT_EQ(0, registry.service_count());
  EXPECT_EQ(0, registry.extension_count());
  registration.rollback();
}

TEST(TestPluginRegistry, incompatible_implementation_has_no_partial_publication)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.incompatible", 6);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service(
                "com.seekdb.extensions.impl", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_ONE));
  ObPluginExtensionSpec extension = make_extension(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION,
      "com.seekdb.function.incompatible");
  extension.implementation_.version_range_.minimum_inclusive = {1, 1, 0};
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(extension));

  EXPECT_EQ(OB_NOT_SUPPORTED, registration.commit());
  EXPECT_TRUE(registration.is_open());
  EXPECT_EQ(ObPluginState::INITIALIZING, owner->state());
  EXPECT_EQ(0, registry.service_count());
  EXPECT_EQ(0, registry.extension_count());
  EXPECT_EQ(0U, registry.registry_epoch());
  registration.rollback();
}

TEST(TestPluginRegistry, physical_format_and_cast_identity_are_unambiguous)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.identities", 7);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service(
                "com.seekdb.extensions.impl", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_ONE));

  ObPluginExtensionSpec first_type = make_extension(
      SEEKDB_PLUGIN_EXTENSION_TYPE, "com.seekdb.type.first");
  ObPluginExtensionSpec alias_type = make_extension(
      SEEKDB_PLUGIN_EXTENSION_TYPE, "com.seekdb.type.alias");
  alias_type.sql_name_ = "extension_alias";
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(first_type));
  EXPECT_EQ(OB_ENTRY_EXIST, registration.add_extension(alias_type));

  ObPluginExtensionSpec first_cast = make_extension(
      SEEKDB_PLUGIN_EXTENSION_CAST, "com.seekdb.cast.first");
  ObPluginExtensionSpec duplicate_cast = make_extension(
      SEEKDB_PLUGIN_EXTENSION_CAST, "com.seekdb.cast.duplicate");
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(first_cast));
  EXPECT_EQ(OB_ENTRY_EXIST, registration.add_extension(duplicate_cast));
  ObPluginExtensionSpec implicit_cast = make_extension(
      SEEKDB_PLUGIN_EXTENSION_CAST, "com.seekdb.cast.implicit");
  implicit_cast.cast_context_ = SEEKDB_PLUGIN_CAST_IMPLICIT;
  implicit_cast.cost_ = 5;
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(implicit_cast));
  ASSERT_EQ(OB_SUCCESS, registration.commit());

  std::vector<ObPluginExtensionInfo> assignment_casts;
  uint64_t lookup_epoch = 0;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_casts("core.type.bytes",
                                "com.seekdb.type.extension-value",
                                SEEKDB_PLUGIN_CAST_ASSIGNMENT,
                                assignment_casts, lookup_epoch));
  ASSERT_EQ(1U, assignment_casts.size());
  EXPECT_EQ("com.seekdb.cast.implicit",
            assignment_casts[0].spec_.object_id_);
  std::vector<ObPluginExtensionInfo> explicit_casts;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_casts("core.type.bytes",
                                "com.seekdb.type.extension-value",
                                SEEKDB_PLUGIN_CAST_EXPLICIT, explicit_casts,
                                lookup_epoch));
  ASSERT_EQ(2U, explicit_casts.size());
  EXPECT_EQ("com.seekdb.cast.implicit",
            explicit_casts[0].spec_.object_id_);
}

TEST(TestPluginRegistry, extension_lease_blocks_drain_and_quiesce_unpublishes)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.extension-drain", 4);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service(
                "com.seekdb.extensions.impl", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_extension(make_extension(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                "com.seekdb.function.extension-drain")));
  ASSERT_EQ(OB_SUCCESS, registration.commit());

  std::vector<ObPluginExtensionInfo> bindings;
  uint64_t lookup_epoch = 0;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_extensions_by_sql_name(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION, "extension_echo", bindings,
                lookup_epoch));
  ASSERT_EQ(1U, bindings.size());
  ObPluginExtensionLease lease;
  ObPluginLease implementation_lease;
  ASSERT_EQ(OB_SUCCESS,
            registry.acquire_extension_with_implementation(
                bindings[0], lease, implementation_lease));
  EXPECT_EQ(2, owner->lease_count());
  ASSERT_EQ(OB_SUCCESS, registry.quiesce(owner));
  EXPECT_EQ(0, registry.service_count());
  EXPECT_EQ(0, registry.extension_count());
  EXPECT_EQ(2U, registry.registry_epoch());
  EXPECT_EQ(OB_TIMEOUT, owner->wait_for_drain(0));

  ObPluginExtensionLease after_quiesce;
  ObPluginLease implementation_after_quiesce;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire_extension_with_implementation(
                bindings[0], after_quiesce,
                implementation_after_quiesce));
  lease.reset();
  implementation_lease.reset();
  EXPECT_EQ(OB_SUCCESS, owner->wait_for_drain(1000));
  EXPECT_EQ(OB_SUCCESS, registry.mark_stopped(owner));
}

TEST(TestPluginRegistry, function_overloads_and_schema_scoped_objects_can_coexist)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.namespaces", 5);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service(
                "com.seekdb.extensions.impl", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_ONE));

  ObPluginExtensionSpec unary = make_extension(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION, "com.seekdb.function.echo-unary");
  ObPluginExtensionSpec binary = make_extension(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION, "com.seekdb.function.echo-binary");
  binary.minimum_arity_ = 2;
  binary.maximum_arity_ = 2;
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(unary));
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(binary));

  ObPluginExtensionSpec sys_object = make_extension(
      SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT,
      "com.seekdb.catalog.extension-objects-sys");
  ObPluginExtensionSpec app_object = make_extension(
      SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT,
      "com.seekdb.catalog.extension-objects-app");
  app_object.schema_name_ = "app";
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(sys_object));
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(app_object));
  ObPluginExtensionSpec low_priority = make_extension(
      SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK,
      "com.seekdb.optimizer.low-priority");
  ObPluginExtensionSpec high_priority = make_extension(
      SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK,
      "com.seekdb.optimizer.high-priority");
  low_priority.priority_ = 10;
  high_priority.priority_ = 30;
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(low_priority));
  ASSERT_EQ(OB_SUCCESS, registration.add_extension(high_priority));
  ASSERT_EQ(OB_SUCCESS, registration.commit());
  EXPECT_EQ(6, registry.extension_count());
  std::vector<ObPluginExtensionInfo> overloads;
  uint64_t lookup_epoch = 0;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_extensions_by_sql_name(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION, "extension_echo", overloads,
                lookup_epoch));
  EXPECT_EQ(2U, overloads.size());
  EXPECT_EQ(1U, lookup_epoch);
  std::vector<ObPluginExtensionInfo> hooks;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_hooks(SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK,
                                "optimizer.logical-rewrite", hooks,
                                lookup_epoch));
  ASSERT_EQ(2U, hooks.size());
  EXPECT_EQ("com.seekdb.optimizer.high-priority",
            hooks[0].spec_.object_id_);
  EXPECT_EQ("com.seekdb.optimizer.low-priority",
            hooks[1].spec_.object_id_);
}

TEST(TestPluginRegistry, stale_extension_metadata_cannot_bind_new_generation)
{
  ObPluginServiceRegistry registry;
  const auto first = make_initializing_generation("com.seekdb.rebind", 1);
  ObPluginRegistration first_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(first, first_registration));
  ASSERT_EQ(OB_SUCCESS,
            first_registration.add_service(
                "com.seekdb.extensions.impl", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS,
            first_registration.add_extension(make_extension(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                "com.seekdb.function.rebind")));
  ASSERT_EQ(OB_SUCCESS, first_registration.commit());

  std::vector<ObPluginExtensionInfo> first_binding;
  uint64_t first_epoch = 0;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_extensions_by_sql_name(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION, "extension_echo",
                first_binding, first_epoch));
  ASSERT_EQ(1U, first_binding.size());
  ASSERT_EQ(OB_SUCCESS, registry.quiesce(first));
  ASSERT_EQ(OB_SUCCESS, registry.mark_stopped(first));

  const auto second = make_initializing_generation("com.seekdb.rebind", 2);
  ObPluginRegistration second_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(second, second_registration));
  ASSERT_EQ(OB_SUCCESS,
            second_registration.add_service(
                "com.seekdb.extensions.impl", 1, 1, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_TWO));
  ASSERT_EQ(OB_SUCCESS,
            second_registration.add_extension(make_extension(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                "com.seekdb.function.rebind")));
  ASSERT_EQ(OB_SUCCESS, second_registration.commit());

  ObPluginExtensionLease stale_extension;
  ObPluginLease stale_implementation;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire_extension_with_implementation(
                first_binding[0], stale_extension, stale_implementation));
  EXPECT_FALSE(stale_extension.is_valid());
  EXPECT_FALSE(stale_implementation.is_valid());

  std::vector<ObPluginExtensionInfo> second_binding;
  uint64_t second_epoch = 0;
  ASSERT_EQ(OB_SUCCESS,
            registry.find_extensions_by_sql_name(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION, "extension_echo",
                second_binding, second_epoch));
  ASSERT_EQ(1U, second_binding.size());
  EXPECT_EQ(2U, second_binding[0].owner_generation_);
  EXPECT_GT(second_epoch, first_epoch);
  ASSERT_EQ(OB_SUCCESS,
            registry.acquire_extension_with_implementation(
                second_binding[0], stale_extension, stale_implementation));
  EXPECT_EQ(&SERVICE_TWO, stale_implementation.service());
}

TEST(TestPluginRegistry, registration_and_live_registry_enforce_service_limit)
{
  ObPluginServiceRegistry registry;
  const auto service_owner =
      make_initializing_generation("com.seekdb.service-limit", 1);
  ObPluginRegistration service_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(service_owner, service_registration));
  for (uint32_t i = 0; i < SEEKDB_PLUGIN_MAX_SERVICES; ++i) {
    const std::string service_id =
        "com.seekdb.limit.service." + std::to_string(i);
    ASSERT_EQ(OB_SUCCESS,
              service_registration.add_service(
                  service_id.c_str(), 1, 0, &SERVICE_ONE));
  }
  EXPECT_EQ(OB_SIZE_OVERFLOW,
            service_registration.add_service(
                "com.seekdb.limit.service.overflow", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, service_registration.commit());
  ASSERT_EQ(static_cast<int64_t>(SEEKDB_PLUGIN_MAX_SERVICES),
            registry.service_count());
  ASSERT_EQ(1U, registry.registry_epoch());

  const auto overflow_owner =
      make_initializing_generation("com.seekdb.service-limit-overflow", 1);
  ObPluginRegistration overflow_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(overflow_owner,
                                        overflow_registration));
  ASSERT_EQ(OB_SUCCESS,
            overflow_registration.add_service(
                "com.seekdb.limit.service.next-generation", 1, 0,
                &SERVICE_TWO));
  ObPluginActivationCandidate candidate;
  EXPECT_EQ(OB_SIZE_OVERFLOW, overflow_registration.prepare(candidate));
  EXPECT_FALSE(candidate.is_prepared());
  EXPECT_TRUE(overflow_registration.is_open());
  EXPECT_EQ(OB_SIZE_OVERFLOW, overflow_registration.commit());
  EXPECT_EQ(static_cast<int64_t>(SEEKDB_PLUGIN_MAX_SERVICES),
            registry.service_count());
  EXPECT_EQ(0, registry.extension_count());
  EXPECT_EQ(1U, registry.registry_epoch());
}

TEST(TestPluginRegistry, registration_and_live_registry_enforce_extension_limit)
{
  ObPluginServiceRegistry registry;
  const auto extension_owner =
      make_initializing_generation("com.seekdb.extension-limit", 1);
  ObPluginRegistration extension_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(extension_owner, extension_registration));
  ASSERT_EQ(OB_SUCCESS,
            extension_registration.add_service(
                "com.seekdb.extensions.impl", 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_ONE));
  for (uint32_t i = 0; i < SEEKDB_PLUGIN_MAX_EXTENSIONS; ++i) {
    const std::string object_id =
        "com.seekdb.limit.function." + std::to_string(i);
    ASSERT_EQ(OB_SUCCESS,
              extension_registration.add_extension(
                  make_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                                 object_id.c_str())));
  }
  EXPECT_EQ(OB_SIZE_OVERFLOW,
            extension_registration.add_extension(
                make_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                               "com.seekdb.limit.function.overflow")));
  ASSERT_EQ(OB_SUCCESS, extension_registration.commit());
  ASSERT_EQ(1, registry.service_count());
  ASSERT_EQ(static_cast<int64_t>(SEEKDB_PLUGIN_MAX_EXTENSIONS),
            registry.extension_count());
  ASSERT_EQ(1U, registry.registry_epoch());

  const auto overflow_owner =
      make_initializing_generation("com.seekdb.extension-limit-overflow", 1);
  ObPluginRegistration overflow_registration;
  ASSERT_EQ(OB_SUCCESS,
            registry.begin_registration(overflow_owner,
                                        overflow_registration));
  const char *overflow_service = "com.seekdb.extensions.overflow-impl";
  ASSERT_EQ(OB_SUCCESS,
            overflow_registration.add_service(
                overflow_service, 1, 0, 0,
                SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, &SERVICE_TWO));
  ASSERT_EQ(OB_SUCCESS,
            overflow_registration.add_extension(make_extension(
                SEEKDB_PLUGIN_EXTENSION_FUNCTION,
                "com.seekdb.limit.function.next-generation",
                overflow_service)));
  ObPluginActivationCandidate candidate;
  EXPECT_EQ(OB_SIZE_OVERFLOW, overflow_registration.prepare(candidate));
  EXPECT_FALSE(candidate.is_prepared());
  EXPECT_TRUE(overflow_registration.is_open());
  EXPECT_EQ(OB_SIZE_OVERFLOW, overflow_registration.commit());
  EXPECT_EQ(1, registry.service_count());
  EXPECT_EQ(static_cast<int64_t>(SEEKDB_PLUGIN_MAX_EXTENSIONS),
            registry.extension_count());
  EXPECT_EQ(1U, registry.registry_epoch());
}

TEST(TestPluginRegistry, quiesce_unpublishes_then_drains_leases)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.drain", 7);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service("com.seekdb.drain.value", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, registration.commit());

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS, registry.acquire("com.seekdb.drain.value", 1, 0, lease));
  EXPECT_EQ(1, owner->lease_count());
  ASSERT_EQ(OB_SUCCESS, registry.quiesce(owner));
  EXPECT_EQ(ObPluginState::QUIESCING, owner->state());
  EXPECT_EQ(0, registry.service_count());

  ObPluginLease after_quiesce;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.drain.value", 1, 0, after_quiesce));
  EXPECT_EQ(OB_TIMEOUT, owner->wait_for_drain(0));
  EXPECT_EQ(OB_EAGAIN, registry.mark_stopped(owner));

  ObPluginLease moved(std::move(lease));
  EXPECT_FALSE(lease.is_valid());
  EXPECT_TRUE(moved.is_valid());
  moved.reset();
  EXPECT_EQ(OB_SUCCESS, owner->wait_for_drain(1000));
  EXPECT_EQ(OB_SUCCESS, registry.mark_stopped(owner));
  EXPECT_EQ(ObPluginState::STOPPED, owner->state());
}

TEST(TestPluginRegistry, new_generation_can_replace_drained_generation)
{
  ObPluginServiceRegistry registry;
  const auto first = make_initializing_generation("com.seekdb.upgrade", 1);
  ObPluginRegistration first_registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(first, first_registration));
  ASSERT_EQ(OB_SUCCESS,
            first_registration.add_service("com.seekdb.upgrade.value", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, first_registration.commit());
  ASSERT_EQ(OB_SUCCESS, registry.quiesce(first));
  ASSERT_EQ(OB_SUCCESS, first->wait_for_drain(0));
  ASSERT_EQ(OB_SUCCESS, registry.mark_stopped(first));

  const auto second = make_initializing_generation("com.seekdb.upgrade", 2);
  ObPluginRegistration second_registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(second, second_registration));
  ASSERT_EQ(OB_SUCCESS,
            second_registration.add_service("com.seekdb.upgrade.value", 1, 1, &SERVICE_TWO));
  ASSERT_EQ(OB_SUCCESS, second_registration.commit());

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS, registry.acquire("com.seekdb.upgrade.value", 1, 0, lease));
  EXPECT_EQ(2U, lease.owner_generation());
  const auto *service = static_cast<const TestServiceV1 *>(lease.service());
  ASSERT_NE(nullptr, service);
  EXPECT_EQ(2, service->value_());
}

TEST(TestPluginRegistry, acquire_and_quiesce_are_linearizable)
{
  static const int THREAD_COUNT = 16;
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.concurrent", 1);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service("com.seekdb.concurrent.value", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, registration.commit());

  std::atomic<int> ready(0);
  std::atomic<int> attempted(0);
  std::atomic<int> acquired(0);
  std::atomic<int> missing(0);
  std::atomic<int> unexpected(0);
  std::atomic<bool> go(false);
  std::atomic<bool> release(false);
  std::vector<std::thread> threads;
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&]() {
      ready.fetch_add(1);
      while (!go.load()) {
        std::this_thread::yield();
      }
      ObPluginLease lease;
      const int ret = registry.acquire("com.seekdb.concurrent.value", 1, 0, lease);
      if (OB_SUCCESS == ret) {
        acquired.fetch_add(1);
      } else if (OB_ENTRY_NOT_EXIST == ret) {
        missing.fetch_add(1);
      } else {
        unexpected.fetch_add(1);
      }
      attempted.fetch_add(1);
      while (lease.is_valid() && !release.load()) {
        std::this_thread::yield();
      }
    });
  }
  while (THREAD_COUNT != ready.load()) {
    std::this_thread::yield();
  }
  go.store(true);
  EXPECT_EQ(OB_SUCCESS, registry.quiesce(owner));
  while (THREAD_COUNT != attempted.load()) {
    std::this_thread::yield();
  }

  EXPECT_EQ(0, unexpected.load());
  EXPECT_EQ(THREAD_COUNT, acquired.load() + missing.load());
  if (acquired.load() > 0) {
    EXPECT_EQ(OB_TIMEOUT, owner->wait_for_drain(0));
  }
  release.store(true);
  for (std::thread &thread : threads) {
    thread.join();
  }
  EXPECT_EQ(OB_SUCCESS, owner->wait_for_drain(1000000));
  EXPECT_EQ(OB_SUCCESS, registry.mark_stopped(owner));
}

} // namespace plugin
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
