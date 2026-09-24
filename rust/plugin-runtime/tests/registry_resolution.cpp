// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/plugin/ob_plugin_registry.h"
#include "seekdb/plugin/execution_spi.h"
#include "lib/ob_errno.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace oceanbase::share::plugin;
using namespace oceanbase::common;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)

static ObPluginExtensionSpec function(const char *id, const char *name,
                                     std::vector<std::string> signature)
{
  ObPluginExtensionSpec spec;
  spec.kind_ = SEEKDB_PLUGIN_EXTENSION_FUNCTION;
  spec.object_id_ = id;
  spec.sql_name_ = name;
  spec.minimum_arity_ = spec.maximum_arity_ = 1;
  spec.argument_type_ids_ = std::move(signature);
  spec.implementation_.service_id_ = "test.execute";
  auto &version = spec.implementation_.version_range_;
  version.struct_size = sizeof(version);
  version.minimum_inclusive = {1, 0, 0};
  version.maximum_exclusive = {2, 0, 0};
  return spec;
}

static std::shared_ptr<ObPluginGeneration> publish(ObPluginServiceRegistry &registry, uint64_t generation)
{
  static const int service = 42; // resolution/lease test, never called as an execution table
  auto owner = std::make_shared<ObPluginGeneration>("test.resolve", generation);
  CHECK(owner->transition_to(ObPluginState::VALIDATED) == OB_SUCCESS);
  CHECK(owner->transition_to(ObPluginState::LOADED) == OB_SUCCESS);
  CHECK(owner->transition_to(ObPluginState::INITIALIZING) == OB_SUCCESS);
  ObPluginRegistration registration;
  CHECK(registry.begin_registration(owner, registration) == OB_SUCCESS);
  CHECK(registration.add_service("test.execute", 1, 0, &service) == OB_SUCCESS);
  auto type_spec = function("test.logical-type", "sql_type_name", {});
  type_spec.kind_ = SEEKDB_PLUGIN_EXTENSION_TYPE;
  type_spec.minimum_arity_ = type_spec.maximum_arity_ = 0;
  type_spec.physical_format_id_ = "test.type-format";
  type_spec.physical_format_version_ = 1;
  CHECK(registration.add_extension(type_spec) == OB_SUCCESS);
  CHECK(registration.add_extension(function("f.legacy", "convert", {})) == OB_SUCCESS);
  CHECK(registration.add_extension(function("f.typed", "convert", {"core.type.float64"})) == OB_SUCCESS);
  auto implementation = function("f.native", "convert", {"core.type.float64"});
  implementation.flags_ = SEEKDB_PLUGIN_EXTENSION_FLAG_IMPLEMENTATION_ONLY;
  CHECK(registration.add_extension(implementation) == OB_SUCCESS); // No SQL-name conflict.
  CHECK(registration.add_extension(implementation) == OB_ENTRY_EXIST); // IDs remain unique.
  implementation.object_id_ = "f.unnamed"; implementation.sql_name_.clear();
  CHECK(registration.add_extension(implementation) == OB_SUCCESS);
  implementation.object_id_ = "f.hidden"; implementation.sql_name_ = "hidden_label";
  CHECK(registration.add_extension(implementation) == OB_SUCCESS);
  auto invalid_implementation = type_spec;
  invalid_implementation.flags_ |= SEEKDB_PLUGIN_EXTENSION_FLAG_IMPLEMENTATION_ONLY;
  CHECK(registration.add_extension(invalid_implementation) == OB_INVALID_ARGUMENT);
  implementation.object_id_ = "f.unknown-flag"; implementation.flags_ |= UINT64_C(1) << 63;
  CHECK(registration.add_extension(implementation) == OB_INVALID_ARGUMENT);
  CHECK(registration.add_extension(function("probe.a", "probe", {"core.type.int64"})) == OB_SUCCESS);
  CHECK(registration.add_extension(function("probe.b", "probe", {"core.type.float64"})) == OB_SUCCESS);
  CHECK(registration.add_extension(function("tie.a", "tie", {"core.type.bytes"})) == OB_SUCCESS);
  CHECK(registration.add_extension(function("tie.b", "tie", {"core.type.geometry"})) == OB_SUCCESS);
  auto cast = function("cast.float", "", {});
  cast.kind_ = SEEKDB_PLUGIN_EXTENSION_CAST;
  cast.source_type_id_ = "core.type.int64";
  cast.target_type_id_ = "core.type.float64";
  cast.cast_context_ = SEEKDB_PLUGIN_CAST_IMPLICIT;
  cast.cost_ = std::numeric_limits<uint32_t>::max();
  CHECK(registration.add_extension(cast) == OB_SUCCESS);
  cast.object_id_ = "cast.bytes";
  cast.target_type_id_ = "core.type.bytes";
  cast.cost_ = 2;
  CHECK(registration.add_extension(cast) == OB_SUCCESS);
  cast.object_id_ = "cast.geometry";
  cast.target_type_id_ = "core.type.geometry";
  CHECK(registration.add_extension(cast) == OB_SUCCESS);
  cast.object_id_ = "cast.bytes.assignment";
  cast.target_type_id_ = "core.type.bytes";
  cast.cast_context_ = SEEKDB_PLUGIN_CAST_ASSIGNMENT;
  CHECK(registration.add_extension(cast) == OB_SUCCESS); // Tie with the implicit cast.
  cast.object_id_ = "cast.float.assignment";
  cast.target_type_id_ = "core.type.float64"; cast.cost_ = 1;
  CHECK(registration.add_extension(cast) == OB_SUCCESS);
  cast.object_id_ = "cast.float.explicit";
  cast.cast_context_ = SEEKDB_PLUGIN_CAST_EXPLICIT; cast.cost_ = 0;
  CHECK(registration.add_extension(cast) == OB_SUCCESS);
  // Independent pair with equally cheap common targets. Neither registration
  // order nor duplicated input branches may choose one of these targets.
  cast.object_id_ = "cast.bytes.geometry";
  cast.source_type_id_ = "core.type.bytes";
  cast.target_type_id_ = "core.type.geometry";
  cast.cast_context_ = SEEKDB_PLUGIN_CAST_IMPLICIT;
  CHECK(registration.add_extension(cast) == OB_SUCCESS);
  cast.object_id_ = "cast.geometry.bytes";
  cast.source_type_id_ = "core.type.geometry";
  cast.target_type_id_ = "core.type.bytes";
  CHECK(registration.add_extension(cast) == OB_SUCCESS);
  ObPluginActivationCandidate candidate;
  CHECK(registration.prepare(candidate) == OB_SUCCESS);
  ObPluginExtensionInfo found;
  uint64_t epoch = 0;
  CHECK(registry.find_type_by_id("test.logical-type", found, epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(found.spec_.object_id_.empty() && epoch == 0);
  const char *type = "core.type.int64";
  CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "convert", &type, 1,
                                       found, epoch) == OB_ENTRY_NOT_EXIST);
  const char *branches[] = {type, "core.type.float64"};
  std::string common_type = "stale";
  epoch = 99;
  CHECK(registry.resolve_common_type(branches, 2, common_type, epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(common_type.empty() && epoch == 0); // Prepared objects are not visible.
  candidate.promote();
  return owner;
}

int main()
{
  ObPluginServiceRegistry registry;
  auto owner = publish(registry, 1);
  {
    ObPluginExtensionInfo selected;
    uint64_t epoch = 99;
    const char *type = "core.type.float64";
    for (const char *id : {"f.native", "f.unnamed", "f.hidden"}) {
      CHECK(registry.resolve_native_function("test.resolve", id, &type, 1, selected, epoch) == OB_SUCCESS);
      CHECK(selected.spec_.object_id_ == id && (selected.spec_.flags_ & SEEKDB_PLUGIN_EXTENSION_FLAG_IMPLEMENTATION_ONLY));
    }
    std::vector<ObPluginExtensionInfo> names;
    CHECK(registry.find_extensions_by_sql_name(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "convert", names, epoch) == OB_SUCCESS);
    CHECK(names.size() == 2);
    CHECK(registry.find_extensions_by_sql_name(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "hidden_label", names, epoch) == OB_SUCCESS);
    CHECK(names.empty());
    CHECK(registry.list_extensions(names) == OB_SUCCESS);
    size_t implementations = 0;
    for (const auto &entry : names) {
      if (entry.spec_.flags_ & SEEKDB_PLUGIN_EXTENSION_FLAG_IMPLEMENTATION_ONLY) ++implementations;
    }
    CHECK(implementations == 3); // Hidden from SQL lookup, not from lifecycle inventory.
    CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "hidden_label", &type, 1, selected, epoch) == OB_ENTRY_NOT_EXIST);
    CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "f.hidden", &type, 1, selected, epoch) == OB_ENTRY_NOT_EXIST);
    // SQL overload resolution would choose f.typed. A native declaration of
    // f.legacy must retain that exact identity, not redirect by shared SQL name.
    CHECK(registry.resolve_native_function("test.resolve", "f.legacy", &type, 1, selected, epoch) == OB_SUCCESS);
    CHECK(selected.spec_.object_id_ == "f.legacy" && epoch != 0);
    CHECK(registry.resolve_native_function("test.resolve", "f.typed", &type, 1, selected, epoch) == OB_SUCCESS);
    CHECK(selected.spec_.object_id_ == "f.typed");
    CHECK(registry.resolve_native_function("test.other", "f.typed", &type, 1, selected, epoch) == OB_ENTRY_NOT_EXIST);
    CHECK(selected.spec_.object_id_.empty() && epoch == 0);
    CHECK(registry.resolve_native_function("test.resolve", "convert", &type, 1, selected, epoch) == OB_ENTRY_NOT_EXIST);
    CHECK(registry.resolve_native_function("test.resolve", "test.logical-type", nullptr, 0, selected, epoch) == OB_ENTRY_NOT_EXIST);
    CHECK(registry.resolve_native_function(nullptr, "f.typed", &type, 1, selected, epoch) == OB_INVALID_ARGUMENT);
    CHECK(registry.resolve_native_function("test.resolve", "../f.typed", &type, 1, selected, epoch) == OB_INVALID_ARGUMENT);
    CHECK(registry.resolve_native_function("test.resolve", "f.typed", &type, 0, selected, epoch) == OB_ENTRY_NOT_EXIST);
    type = "core.type.bytes";
    CHECK(registry.resolve_native_function("test.resolve", "f.typed", &type, 1, selected, epoch) == OB_ENTRY_NOT_EXIST);
    type = "core.type.int64";
    CHECK(registry.resolve_native_function("test.resolve", "f.typed", &type, 1, selected, epoch) == OB_SUCCESS);
  }
  ObPluginExtensionInfo logical_type;
  uint64_t type_epoch = 0;
  CHECK(registry.find_type_by_id("test.logical-type", logical_type, type_epoch) == OB_SUCCESS);
  CHECK(logical_type.spec_.sql_name_ == "sql_type_name" && logical_type.owner_generation_ == 1);
  const auto first_type = logical_type;
  const auto first_type_epoch = type_epoch;
  CHECK(owner->lease_count() == 0); // Looking up metadata never pins code.
  const char *bad_ids[] = {nullptr, "", "BAD_ID", "sql_type_name", "f.typed", "missing.type"};
  for (const char *bad_id : bad_ids) {
    logical_type = first_type; type_epoch = 99;
    const int expected = !bad_id || !*bad_id || std::string(bad_id) == "BAD_ID"
        ? OB_INVALID_ARGUMENT : OB_ENTRY_NOT_EXIST;
    CHECK(registry.find_type_by_id(bad_id, logical_type, type_epoch) == expected);
    CHECK(logical_type.spec_.object_id_.empty() && type_epoch == 0);
  }
  CHECK(registry.find_type_by_id("test.logical-type", logical_type, type_epoch, first_type_epoch + 1)
      == OB_STATE_NOT_MATCH);
  CHECK(logical_type.spec_.object_id_.empty() && type_epoch == 0);
  CHECK(registry.find_type_by_id("test.logical-type", logical_type, type_epoch, first_type_epoch) == OB_SUCCESS);
  // Publishing/removing another generation copies the immutable registry
  // image. Cached FFI signature spans must outlive the discarded base image.
  static const int marker_service = 0;
  auto marker = std::make_shared<ObPluginGeneration>("test.marker", 1);
  CHECK(marker->transition_to(ObPluginState::VALIDATED) == OB_SUCCESS);
  CHECK(marker->transition_to(ObPluginState::LOADED) == OB_SUCCESS);
  CHECK(marker->transition_to(ObPluginState::INITIALIZING) == OB_SUCCESS);
  ObPluginRegistration marker_registration;
  CHECK(registry.begin_registration(marker, marker_registration) == OB_SUCCESS);
  CHECK(marker_registration.add_service("test.marker", 1, 0, &marker_service) == OB_SUCCESS);
  CHECK(marker_registration.commit() == OB_SUCCESS);
  CHECK(registry.quiesce(marker) == OB_SUCCESS);
  CHECK(registry.mark_stopped(marker) == OB_SUCCESS);
  {
    ObPluginExtensionLease type_object;
    ObPluginLease type_code;
    // Even unrelated publication between lookup and lease acquisition makes
    // the old epoch unusable; no callback can start on a stale catalog view.
    CHECK(registry.acquire_extension_with_implementation(first_type, type_object, type_code,
        first_type_epoch) == OB_STATE_NOT_MATCH);
    CHECK(!type_object.is_valid() && !type_code.is_valid());
  }
  uint64_t epoch = 0;
  ObPluginExtensionInfo chosen;
  const char *integer = "core.type.int64";
  std::string common_type = "stale";
  uint64_t common_epoch = 99;
  const char *branches[] = {integer, "core.type.float64", integer, nullptr};
  CHECK(registry.resolve_common_type(branches, 4, common_type, common_epoch) == OB_SUCCESS);
  CHECK(common_type == "core.type.float64" && common_epoch == registry.registry_epoch());
  const uint64_t original_common_epoch = common_epoch;
  const char *tied_branches[] = {"core.type.bytes", "core.type.geometry", "core.type.bytes"};
  CHECK(registry.resolve_common_type(tied_branches, 3, common_type, common_epoch) == OB_ENTRY_EXIST);
  CHECK(common_type.empty() && common_epoch == 0);
  const char *unknown_branches[] = {nullptr, nullptr};
  CHECK(registry.resolve_common_type(unknown_branches, 2, common_type, common_epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(common_type.empty() && common_epoch == 0);
  CHECK(registry.resolve_common_type(nullptr, 1, common_type, common_epoch) == OB_INVALID_ARGUMENT);
  CHECK(registry.resolve_common_type(branches, SEEKDB_PLUGIN_MAX_ARGUMENTS + 1,
                                      common_type, common_epoch) == OB_INVALID_ARGUMENT);
  const char *invalid_branch[] = {"NOT_A_TYPE"};
  CHECK(registry.resolve_common_type(invalid_branch, 1, common_type, common_epoch) == OB_INVALID_ARGUMENT);
  CHECK(common_type.empty() && common_epoch == 0);
  const char *cast_names[] = {"cast.float.explicit", "cast.float.assignment", "cast.float"};
  ObPluginExtensionInfo selected_cast;
  uint64_t cast_epoch = 0;
  for (int context = SEEKDB_PLUGIN_CAST_EXPLICIT; context <= SEEKDB_PLUGIN_CAST_IMPLICIT; ++context) {
    CHECK(registry.resolve_cast(integer, "core.type.float64", context, selected_cast, cast_epoch) == OB_SUCCESS);
    CHECK(selected_cast.spec_.object_id_ == cast_names[context - 1]);
    CHECK(cast_epoch == registry.registry_epoch());
  }
  CHECK(registry.resolve_cast(integer, "core.type.bytes", SEEKDB_PLUGIN_CAST_ASSIGNMENT,
                               chosen, epoch) == OB_ENTRY_EXIST);
  CHECK(registry.resolve_cast(integer, "missing.type", SEEKDB_PLUGIN_CAST_ASSIGNMENT,
                               chosen, epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(registry.resolve_cast(nullptr, "core.type.bytes", SEEKDB_PLUGIN_CAST_ASSIGNMENT,
                               chosen, epoch) == OB_INVALID_ARGUMENT);
  {
    ObPluginExtensionLease cast_object;
    ObPluginLease cast_implementation;
    CHECK(registry.acquire_extension_with_implementation(selected_cast, cast_object,
        cast_implementation, cast_epoch + 1) == OB_STATE_NOT_MATCH);
    CHECK(!cast_object.is_valid() && !cast_implementation.is_valid());
    CHECK(registry.acquire_extension_with_implementation(selected_cast, cast_object,
        cast_implementation, cast_epoch) == OB_SUCCESS);
  }
  // Crosses the C++ snapshot adapter and the production Rust resolver.
  CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "convert", &integer, 1,
                                       chosen, epoch) == OB_SUCCESS);
  CHECK(chosen.spec_.object_id_ == "f.typed");
  CHECK(epoch == registry.registry_epoch());
  CHECK(chosen.owner_generation_ == 1);
  ObPluginExtensionInfo native;
  uint64_t native_epoch = 0;
  CHECK(registry.resolve_native_function("test.resolve", "f.typed", &integer, 1, native, native_epoch) == OB_SUCCESS);
  CHECK(native.spec_.object_id_ == chosen.spec_.object_id_ && native_epoch == epoch);
  const auto old = native;
  CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "probe", &integer, 1,
                                       chosen, epoch) == OB_SUCCESS);
  CHECK(chosen.spec_.object_id_ == "probe.a");
  const char *unknown = nullptr;
  CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "probe", &unknown, 1,
                                       chosen, epoch) == OB_SUCCESS);
  CHECK(chosen.spec_.object_id_ == "probe.a");
  CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "tie", &integer, 1,
                                       chosen, epoch) == OB_ENTRY_EXIST);
  const char *invalid = "NOT_A_TYPE";
  CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "absent", &invalid, 1,
                                       chosen, epoch) == OB_INVALID_ARGUMENT);
  CHECK(registry.quiesce(owner) == OB_SUCCESS);
  CHECK(registry.resolve_native_function("test.resolve", "f.hidden", &integer, 1, native, native_epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(registry.resolve_native_function("test.resolve", "f.typed", &integer, 1, native, native_epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(native.spec_.object_id_.empty() && native_epoch == 0);
  CHECK(registry.find_type_by_id("test.logical-type", logical_type, type_epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(logical_type.spec_.object_id_.empty() && type_epoch == 0);
  CHECK(registry.resolve_common_type(branches, 4, common_type, common_epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(common_type.empty() && common_epoch == 0);
  CHECK(registry.mark_stopped(owner) == OB_SUCCESS);
  auto replacement = publish(registry, 2);
  CHECK(registry.resolve_native_function("test.resolve", "f.hidden", &integer, 1, native, native_epoch) == OB_SUCCESS);
  CHECK(native.owner_generation_ == 2 && (native.spec_.flags_ & SEEKDB_PLUGIN_EXTENSION_FLAG_IMPLEMENTATION_ONLY));
  CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "hidden_label", &integer, 1,
                                       chosen, epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(registry.resolve_native_function("test.resolve", "f.typed", &integer, 1, native, native_epoch) == OB_SUCCESS);
  CHECK(native.owner_generation_ == 2 && old.owner_generation_ == 1);
  CHECK(registry.find_type_by_id("test.logical-type", logical_type, type_epoch) == OB_SUCCESS);
  CHECK(logical_type.owner_generation_ == 2 && type_epoch != first_type_epoch);
  CHECK(first_type.owner_generation_ == 1 && first_type.spec_.sql_name_ == "sql_type_name");
  CHECK(registry.resolve_common_type(branches, 4, common_type, common_epoch) == OB_SUCCESS);
  CHECK(common_type == "core.type.float64" && common_epoch == registry.registry_epoch());
  CHECK(common_epoch != original_common_epoch); // Same result is not the same binding.
  {
    ObPluginExtensionLease cast_object;
    ObPluginLease cast_implementation;
    CHECK(registry.acquire_extension_with_implementation(selected_cast, cast_object,
        cast_implementation, cast_epoch) == OB_STATE_NOT_MATCH);
    CHECK(!cast_object.is_valid() && !cast_implementation.is_valid());
    CHECK(registry.acquire_extension_with_implementation(selected_cast, cast_object,
        cast_implementation) == OB_ENTRY_NOT_EXIST);
  }
  ObPluginExtensionLease object_lease;
  ObPluginLease implementation_lease;
  CHECK(registry.acquire_extension_with_implementation(old, object_lease, implementation_lease) == OB_ENTRY_NOT_EXIST);
  CHECK(!object_lease.is_valid() && !implementation_lease.is_valid());
  CHECK(registry.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "convert", &integer, 1,
                                       chosen, epoch) == OB_SUCCESS);
  CHECK(chosen.owner_generation_ == 2);
  CHECK(registry.acquire_extension_with_implementation(chosen, object_lease, implementation_lease) == OB_SUCCESS);
  CHECK(registry.quiesce(replacement) == OB_SUCCESS);
  CHECK(replacement->wait_for_drain(0) == OB_TIMEOUT);
  object_lease.reset();
  implementation_lease.reset();
  CHECK(registry.mark_stopped(replacement) == OB_SUCCESS);
}
