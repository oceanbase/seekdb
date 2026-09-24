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

#define USING_LOG_PREFIX RS

#include "ob_pl_ddl_service.h"
#include "share/schema/native_routine_create_slot.h"
#include "rootserver/pl_ddl/routine_catalog_writer.h"
#include "rootserver/pl_ddl/native_routine_grant_writer.h"
#include "rootserver/pl_ddl/native_routine_grant_plan.h"
#include "rootserver/pl_ddl/native_routine_acl_version_reservation.h"
#include "rootserver/pl_ddl/native_routine_revoke_plan.h"
#include "rootserver/pl_ddl/native_routine_revoke_writer.h"
#include <map>
#include <set>
#include <vector>
#include <tuple>
#include "sql/pl/pl_cache/ob_pl_cache_mgr.h"
#include "share/schema/routine_schema_overlay.h"
#include "share/schema/routine_catalog_savepoint.h"
#include "rootserver/ob_dependency_ddl_helper.h"
#include "lib/utility/ob_smart_call.h"
#include "rootserver/ob_ddl_service.h"
#include "share/schema/ob_error_info.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_priv_sql_service.h"
#include "share/schema/ob_routine_info.h"
#include "share/schema/ob_package_info.h"
#include "share/schema/ob_trigger_info.h"
#include "share/rc/ob_module_provider.h"
#include "share/schema/native_routine_admission.h"
#include "sql/engine/expr/plugin_function_expr.h"
#include "share/plugin/extension_install.h"
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
#include "share/plugin/extension_routine_update.h"
#include "share/plugin/ob_plugin_sql_catalog.h"
#include <set>
#include <map>
#include <new>
#include <memory>
#endif

namespace oceanbase
{
using rootserver::ObDDLSQLTransaction;
using rootserver::ObDDLOperator;

namespace rootserver
{

struct NativeRoutineAclVersionReservation::Identity {
  ObMultiVersionSchemaService *service_ = nullptr;
  ObSchemaService *sql_service_ = nullptr;
  ObMySQLTransaction *transaction_ = nullptr;
  obcall::NativeRoutinePrivilegeTarget target_;
  Kind kind_ = Kind::GRANT;
  ObSEArray<NativeRoutineAclChange, 4> changes_;
  ObSEArray<int64_t, 4> versions_;
};
NativeRoutineAclVersionReservation::NativeRoutineAclVersionReservation() = default;
NativeRoutineAclVersionReservation::~NativeRoutineAclVersionReservation() = default;
NativeRoutineAclVersionReservation::NativeRoutineAclVersionReservation(NativeRoutineAclVersionReservation &&) noexcept = default;
NativeRoutineAclVersionReservation &NativeRoutineAclVersionReservation::operator=(NativeRoutineAclVersionReservation &&) noexcept = default;
int64_t NativeRoutineAclVersionReservation::count() const
{ return identity_ ? identity_->versions_.count() : 0; }
int64_t NativeRoutineAclVersionReservation::version_at(int64_t index) const
{ return identity_ && index >= 0 && index < count() ? identity_->versions_.at(index) : OB_INVALID_VERSION; }

int NativeRoutineAclVersionReservation::reserve(ObMultiVersionSchemaService &service,
    ObMySQLTransaction &transaction, const obcall::NativeRoutinePrivilegeTarget &target,
    Kind kind, const ObIArray<NativeRoutineAclChange> &changes, NativeRoutineAclVersionReservation &output)
{
  if (output.identity_) return OB_INIT_TWICE;
  if (!transaction.is_started()) return OB_STATE_NOT_MATCH;
  if (!service.get_schema_service()) return OB_NOT_INIT;
  if (!target.is_valid() || !target.resolved_ || target.actor_id_ == OB_INVALID_ID ||
      (kind != Kind::GRANT && kind != Kind::REVOKE) || changes.empty()) return OB_INVALID_ARGUMENT;
  if (changes.count() > 16384) return OB_SIZE_OVERFLOW;
  try {
    ObPackedObjPriv plain[2] = {}, grantable[2] = {}, allowed = 0;
    int ret = OB_SUCCESS;
    for (int i = 0; i < 2; ++i) {
      const auto right = i == 0 ? OBJ_PRIV_ID_EXECUTE : OBJ_PRIV_ID_ALTER;
      if (OB_FAIL(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION,right,plain[i])) ||
          OB_FAIL(ObPrivPacker::raw_obj_priv_to_packed_info(GRANT_OPTION,right,grantable[i]))) return ret;
      allowed |= grantable[i];
    }
    const auto valid_bits = [&](ObPackedObjPriv bits) {
      if (bits & ~allowed) return false;
      for (int i = 0; i < 2; ++i) if ((bits & (grantable[i] ^ plain[i])) && !(bits & plain[i])) return false;
      return true;
    };
    std::pair<uint64_t,uint64_t> previous{};
    for (int64_t i = 0; i < changes.count(); ++i) {
      const auto &change = changes.at(i);
      const std::pair<uint64_t,uint64_t> key{change.grantee_,change.grantor_};
      if (!change.grantee_ || change.grantee_ > INT64_MAX || !change.grantor_ || change.grantor_ > INT64_MAX ||
          (i != 0 && !(previous < key)) || !valid_bits(change.before_) || !valid_bits(change.after_) ||
          (kind == Kind::GRANT ? (change.before_ & ~change.after_) != 0 : (change.after_ & ~change.before_) != 0))
        return OB_INVALID_ARGUMENT;
      previous = key;
    }
    auto identity = std::make_unique<Identity>();
    identity->service_ = &service; identity->sql_service_ = service.get_schema_service();
    identity->transaction_ = &transaction; identity->kind_ = kind;
    if (OB_FAIL(identity->target_.assign(target.routine_,target.signature_qualified_)) ||
        OB_FAIL(identity->target_.bind_actor(target.actor_id_,target.enabled_roles_)) ||
        OB_FAIL(identity->changes_.assign(changes)) || OB_FAIL(identity->versions_.reserve(changes.count()))) return ret;
    int64_t previous_version = target.routine_.get_schema_version();
    for (int64_t i = 0; i < changes.count(); ++i) {
      int64_t version = 0;
      if (OB_FAIL(service.gen_new_schema_version(version))) return ret;
      if (version <= previous_version) return OB_STATE_NOT_MATCH;
      previous_version = version;
      if (OB_FAIL(identity->versions_.push_back(version))) return ret;
    }
    if (identity->sql_service_ != service.get_schema_service() || !transaction.is_started()) return OB_STATE_NOT_MATCH;
    output.identity_ = std::move(identity);
    return OB_SUCCESS;
  } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { return OB_ERR_UNEXPECTED; }
}

int NativeRoutineAclVersionReservation::take(ObMultiVersionSchemaService &service,
    ObMySQLTransaction &transaction, const obcall::NativeRoutinePrivilegeTarget &target,
    Kind kind, const ObIArray<NativeRoutineAclChange> &changes, ObIArray<int64_t> &versions)
{
  versions.reset();
  auto identity = std::move(identity_);
  if (!identity || identity->service_ != &service || identity->sql_service_ != service.get_schema_service() ||
      identity->transaction_ != &transaction || !transaction.is_started() || identity->kind_ != kind ||
      !target.is_valid() || !target.resolved_ || identity->target_.check(&target.routine_) != OB_SUCCESS ||
      identity->target_.actor_id_ != target.actor_id_ || identity->target_.signature_qualified_ != target.signature_qualified_ ||
      identity->target_.enabled_roles_.count() != target.enabled_roles_.count() ||
      identity->changes_.count() != changes.count()) return OB_STATE_NOT_MATCH;
  for (int64_t i = 0; i < target.enabled_roles_.count(); ++i)
    if (identity->target_.enabled_roles_.at(i) != target.enabled_roles_.at(i)) return OB_STATE_NOT_MATCH;
  for (int64_t i = 0; i < changes.count(); ++i) {
    const auto &a = identity->changes_.at(i), &b = changes.at(i);
    if (a.grantor_ != b.grantor_ || a.grantee_ != b.grantee_ || a.before_ != b.before_ || a.after_ != b.after_)
      return OB_STATE_NOT_MATCH;
  }
  int ret = OB_SUCCESS;
  try { ret = versions.assign(identity->versions_); }
  catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { ret = OB_ERR_UNEXPECTED; }
  if (ret != OB_SUCCESS) versions.reset();
  return ret;
}

namespace {
int create_owner_acl_plan(const ObRoutineInfo &routine, obcall::NativeRoutinePrivilegeTarget &target,
    ObIArray<NativeRoutineAclChange> &changes)
{
  ObSEArray<uint64_t, 1> roles;
  ObPackedObjPriv execute = 0, alter = 0;
  int ret = OB_SUCCESS;
  if (OB_FAIL(target.assign(routine, false)) ||
      OB_FAIL(target.bind_actor(routine.get_owner_id(), roles)) ||
      OB_FAIL(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, OBJ_PRIV_ID_EXECUTE, execute)) ||
      OB_FAIL(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, OBJ_PRIV_ID_ALTER, alter))) return ret;
  return changes.push_back({routine.get_owner_id(), routine.get_owner_id(), 0, execute | alter});
}
}

int NativeRoutineAclVersionReservation::reserve_create_owner(ObMultiVersionSchemaService &service,
    ObMySQLTransaction &transaction, const ObRoutineInfo &routine, NativeRoutineAclVersionReservation &output)
{
  obcall::NativeRoutinePrivilegeTarget target;
  ObSEArray<NativeRoutineAclChange, 1> changes;
  int ret = create_owner_acl_plan(routine, target, changes);
  return ret == OB_SUCCESS ? reserve(service, transaction, target, Kind::GRANT, changes, output) : ret;
}

int NativeRoutineAclVersionReservation::take_create_owner(ObMultiVersionSchemaService &service,
    ObMySQLTransaction &transaction, const ObRoutineInfo &routine, int64_t &version)
{
  version = OB_INVALID_VERSION;
  NativeRoutineAclVersionReservation reserved = std::move(*this);
  obcall::NativeRoutinePrivilegeTarget target;
  ObSEArray<NativeRoutineAclChange, 1> changes;
  ObSEArray<int64_t, 1> versions;
  int ret = create_owner_acl_plan(routine, target, changes);
  if (OB_SUCC(ret)) ret = reserved.take(service, transaction, target, Kind::GRANT, changes, versions);
  if (OB_SUCC(ret)) {
    if (versions.count() != 1) ret = OB_STATE_NOT_MATCH;
    else version = versions.at(0);
  }
  return ret;
}

int NativeRoutineGrantPlan::build(const ObRoutineInfo &expected,
    const ObIArray<ObObjPriv> &snapshot, const ObIArray<NativeRoutineGrantRequest> &requests,
    ObIArray<NativeRoutineGrantDelta> &output)
{
  output.reset();
  const auto valid_id = [](uint64_t id) { return id > 0 && id <= INT64_MAX; };
  constexpr ObPrivSet supported = OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE;
  if (!expected.is_native() || !expected.is_native_binding_valid() ||
      expected.get_routine_type() != ROUTINE_FUNCTION_TYPE || expected.get_package_id() != OB_INVALID_ID ||
      !valid_id(expected.get_routine_id()) || !valid_id(expected.get_database_id()) ||
      !valid_id(expected.get_owner_id()) || expected.get_schema_version() <= 0 || expected.get_overload() < 0 ||
      requests.empty()) return OB_INVALID_ARGUMENT;
  if (snapshot.count() > 16384 || requests.count() > 16384) return OB_SIZE_OVERFLOW;
  try {
    ObPackedObjPriv plain[2] = {}, grantable[2] = {}, allowed = 0;
    int ret = OB_SUCCESS;
    for (int i = 0; i < 2; ++i) {
      const ObRawObjPriv right = i == 0 ? OBJ_PRIV_ID_EXECUTE : OBJ_PRIV_ID_ALTER;
      if (OB_FAIL(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, right, plain[i])) ||
          OB_FAIL(ObPrivPacker::raw_obj_priv_to_packed_info(GRANT_OPTION, right, grantable[i]))) return ret;
      allowed |= grantable[i];
    }
    using Key = std::pair<uint64_t, uint64_t>; // grantee, grantor
    std::map<Key, ObPackedObjPriv> current, additions;
    std::set<std::tuple<uint64_t, uint64_t, uint64_t>> row_keys;
    for (int64_t i = 0; i < snapshot.count(); ++i) {
      const auto &row = snapshot.at(i);
      if (!row.is_valid() || row.get_obj_id() != expected.get_routine_id() ||
          row.get_objtype() != uint64_t(ObObjectType::FUNCTION) ||
          !valid_id(row.get_grantor_id()) || !valid_id(row.get_grantee_id()) || row.get_col_id() > INT64_MAX ||
          !row_keys.emplace(row.get_grantee_id(), row.get_grantor_id(), row.get_col_id()).second)
        return OB_INVALID_DATA;
      if (row.get_col_id() != OBJ_LEVEL_FOR_TAB_PRIV) continue;
      const auto bits = row.get_obj_privs();
      if (bits & ~allowed) return OB_INVALID_DATA;
      for (int right = 0; right < 2; ++right)
        if ((bits & (grantable[right] ^ plain[right])) && !(bits & plain[right])) return OB_INVALID_DATA;
      current.emplace(Key{row.get_grantee_id(), row.get_grantor_id()}, bits);
    }
    for (int64_t i = 0; i < requests.count(); ++i) {
      const auto &request = requests.at(i);
      if (!valid_id(request.grantor_) || !valid_id(request.grantee_) || !request.rights_ ||
          (request.rights_ & ~supported)) return OB_INVALID_ARGUMENT;
      ObPackedObjPriv bits = 0;
      if (request.rights_ & OB_PRIV_EXECUTE) bits |= request.grant_option_ ? grantable[0] : plain[0];
      if (request.rights_ & OB_PRIV_ALTER_ROUTINE) bits |= request.grant_option_ ? grantable[1] : plain[1];
      additions[{request.grantee_, request.grantor_}] |= bits;
    }
    for (const auto &entry : additions) {
      const auto found = current.find(entry.first);
      const auto before = found == current.end() ? ObPackedObjPriv{0} : found->second;
      if (OB_FAIL(output.push_back({entry.first.second, entry.first.first, before, before | entry.second}))) {
        output.reset(); return ret;
      }
    }
    return OB_SUCCESS;
  } catch (const std::bad_alloc &) { output.reset(); return OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { output.reset(); return OB_ERR_UNEXPECTED; }
}

int NativeRoutineRevokePlan::collect_roots(ObSchemaGetterGuard &guard, const ObRoutineInfo &expected,
    const ObIArray<ObObjPriv> &snapshot, ObIArray<NativeRoutineGrantRoot> &output)
{
  output.reset();
  if (snapshot.count() > 16384) return OB_SIZE_OVERFLOW;
  try {
    obcall::NativeRoutinePrivilegeTarget pinned;
    const ObRoutineInfo *current = nullptr;
    const ObDatabaseSchema *database = nullptr;
    int ret = pinned.assign(expected);
    if (OB_FAIL(ret) || OB_FAIL(guard.get_routine_info(expected.get_routine_id(), current)) ||
        OB_FAIL(pinned.check(current)) || OB_FAIL(guard.get_database_schema(expected.get_database_id(), database))) return ret;
    if (!database) return OB_ERR_BAD_DATABASE;
    // Ownership is the persistent grant-option root, including an empty ACL.
    // Requiring the owner schema also detects a dangling owner identity.
    std::set<uint64_t> principals{expected.get_owner_id()};
    for (int64_t i = 0; i < snapshot.count(); ++i) {
      const auto &row = snapshot.at(i);
      if (!row.is_valid() || row.get_obj_id() != expected.get_routine_id() ||
          row.get_objtype() != uint64_t(ObObjectType::FUNCTION) || row.get_grantor_id() == 0 ||
          row.get_grantor_id() > INT64_MAX || row.get_grantee_id() == 0 || row.get_grantee_id() > INT64_MAX)
        return OB_INVALID_DATA;
      if (row.get_col_id() != OBJ_LEVEL_FOR_TAB_PRIV) continue;
      principals.insert(row.get_grantor_id()); principals.insert(row.get_grantee_id());
    }
    ObSEArray<NativeRoutineGrantRoot, 4> roots;
    for (uint64_t id : principals) {
      const ObUserInfo *principal = nullptr;
      ObPrivSet database_privileges = 0, rights = 0;
      if (OB_FAIL(guard.get_user_info(id, principal))) return ret;
      if (!principal) return OB_USER_NOT_EXIST;
      if (OB_FAIL(guard.get_db_priv_set(id, database->get_database_name_str(), database_privileges))) return ret;
      const ObPrivSet broad = principal->get_priv_set() | database_privileges;
      for (const ObPrivSet permission : {OB_PRIV_EXECUTE, OB_PRIV_ALTER_ROUTINE})
        if (id == expected.get_owner_id() || OB_TEST_PRIVS(broad, permission | OB_PRIV_GRANT)) rights |= permission;
      if (rights) {
        if (roots.count() >= 16384) return OB_SIZE_OVERFLOW;
        if (OB_FAIL(roots.push_back({id, rights}))) return ret;
      }
    }
    ret = output.assign(roots);
    if (OB_FAIL(ret)) output.reset();
    return ret;
  } catch (const std::bad_alloc &) { output.reset(); return OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { output.reset(); return OB_ERR_UNEXPECTED; }
}

int NativeRoutineRevokePlan::build(const ObRoutineInfo &expected,
    const ObIArray<ObObjPriv> &snapshot, const ObIArray<NativeRoutineGrantRoot> &roots,
    const ObIArray<NativeRoutineRevokeRequest> &requests, Behavior behavior,
    ObIArray<NativeRoutineRevokeDelta> &output)
{
  output.reset();
  const auto valid_id = [](uint64_t id) { return id > 0 && id <= INT64_MAX; };
  constexpr ObPrivSet supported = OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE;
  if (!expected.is_native() || !expected.is_native_binding_valid() ||
      expected.get_routine_type() != ROUTINE_FUNCTION_TYPE || expected.get_package_id() != OB_INVALID_ID ||
      !valid_id(expected.get_routine_id()) || !valid_id(expected.get_database_id()) ||
      !valid_id(expected.get_owner_id()) || expected.get_schema_version() <= 0 || expected.get_overload() < 0 ||
      requests.empty() || (behavior != Behavior::RESTRICT && behavior != Behavior::CASCADE)) return OB_INVALID_ARGUMENT;
  if (snapshot.count() > 16384 || roots.count() > 16384 || requests.count() > 16384) return OB_SIZE_OVERFLOW;
  try {
    ObPackedObjPriv plain[2] = {}, grantable[2] = {}, allowed = 0;
    int ret = OB_SUCCESS;
    for (int i = 0; i < 2; ++i) {
      const ObRawObjPriv right = i == 0 ? OBJ_PRIV_ID_EXECUTE : OBJ_PRIV_ID_ALTER;
      if (OB_FAIL(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, right, plain[i])) ||
          OB_FAIL(ObPrivPacker::raw_obj_priv_to_packed_info(GRANT_OPTION, right, grantable[i]))) return ret;
      allowed |= grantable[i];
    }
    const auto mask = [](ObPrivSet rights) -> uint8_t {
      return ((rights & OB_PRIV_EXECUTE) ? 1 : 0) | ((rights & OB_PRIV_ALTER_ROUTINE) ? 2 : 0);
    };
    struct Edge {
      uint64_t grantor, grantee;
      ObPackedObjPriv before;
      uint8_t rights = 0, options = 0;
    };
    using Key = std::pair<uint64_t, uint64_t>; // grantee, grantor
    std::map<Key, Edge> edges;
    std::set<std::tuple<uint64_t, uint64_t, uint64_t>> row_keys;
    for (int64_t i = 0; i < snapshot.count(); ++i) {
      const auto &row = snapshot.at(i);
      if (!row.is_valid() || row.get_obj_id() != expected.get_routine_id() ||
          row.get_objtype() != uint64_t(ObObjectType::FUNCTION) ||
          !valid_id(row.get_grantee_id()) || !valid_id(row.get_grantor_id()) || row.get_col_id() > INT64_MAX ||
          !row_keys.emplace(row.get_grantee_id(), row.get_grantor_id(), row.get_col_id()).second)
        return OB_INVALID_DATA;
      if (row.get_col_id() != OBJ_LEVEL_FOR_TAB_PRIV) continue;
      const auto bits = row.get_obj_privs();
      if (bits & ~allowed) return OB_INVALID_DATA;
      Edge edge{row.get_grantor_id(), row.get_grantee_id(), bits};
      for (int right = 0; right < 2; ++right) {
        if (bits & plain[right]) edge.rights |= 1 << right;
        if (bits & (grantable[right] ^ plain[right])) {
          if (!(edge.rights & (1 << right))) return OB_INVALID_DATA;
          edge.options |= 1 << right;
        }
      }
      edges.emplace(Key{edge.grantee, edge.grantor}, edge);
    }
    std::map<uint64_t, uint8_t> intrinsic;
    for (int64_t i = 0; i < roots.count(); ++i) {
      const auto &root = roots.at(i);
      if (!valid_id(root.principal_) || !root.rights_ || (root.rights_ & ~supported) ||
          !intrinsic.emplace(root.principal_, mask(root.rights_)).second) return OB_INVALID_ARGUMENT;
    }
    std::map<uint64_t, std::vector<const Edge *>> outgoing;
    for (const auto &entry : edges) outgoing[entry.second.grantor].push_back(&entry.second);
    const auto reach = [&]() {
      std::map<uint64_t, uint8_t> reached;
      std::vector<std::pair<uint64_t, uint8_t>> queue;
      const auto visit = [&](uint64_t principal, uint8_t rights) {
        auto &known = reached[principal];
        const uint8_t fresh = rights & ~known;
        if (fresh) { known |= fresh; queue.emplace_back(principal, fresh); }
      };
      for (const auto &root : intrinsic) visit(root.first, root.second);
      for (size_t i = 0; i < queue.size(); ++i) {
        const auto current = queue[i]; // visit may grow/reallocate the queue.
        const auto found = outgoing.find(current.first);
        if (found != outgoing.end()) for (const auto *edge : found->second)
          visit(edge->grantee, current.second & edge->options);
      }
      return reached;
    };
    const auto available = [](const std::map<uint64_t, uint8_t> &reached, uint64_t principal) {
      const auto found = reached.find(principal);
      return found == reached.end() ? uint8_t{0} : found->second;
    };
    const auto before = reach();
    for (const auto &entry : edges)
      if (entry.second.rights & ~available(before, entry.second.grantor)) return OB_STATE_NOT_MATCH;
    // Direct requests are monotone: duplicate/overlapping requests commute;
    // removing a right includes its option and dominates option-only removal.
    for (int64_t i = 0; i < requests.count(); ++i) {
      const auto &request = requests.at(i);
      if (!valid_id(request.grantor_) || !valid_id(request.grantee_) || !request.rights_ ||
          (request.rights_ & ~supported)) return OB_INVALID_ARGUMENT;
      const auto found = edges.find({request.grantee_, request.grantor_});
      if (found != edges.end()) {
        auto &edge = found->second;
        edge.options &= ~mask(request.rights_);
        if (!request.grant_option_only_) edge.rights &= ~mask(request.rights_);
      }
    }
    const auto after = reach();
    ObSEArray<NativeRoutineRevokeDelta, 4> deltas;
    for (auto &entry : edges) {
      auto &edge = entry.second;
      const uint8_t dependent = edge.rights & ~available(after, edge.grantor);
      if (dependent && behavior == Behavior::RESTRICT) return OB_OP_NOT_ALLOW;
      edge.rights &= ~dependent;
      edge.options &= edge.rights;
      ObPackedObjPriv bits = 0;
      for (int i = 0; i < 2; ++i) if (edge.rights & (1 << i))
        bits |= (edge.options & (1 << i)) ? grantable[i] : plain[i];
      if (bits != edge.before && OB_FAIL(deltas.push_back({edge.grantor, edge.grantee, edge.before, bits}))) return ret;
    }
    // Edges from unreachable grantors never contributed to `after`, so removing
    // them cannot remove a reachable path. No recursive pruning/re-scan is needed.
    ret = output.assign(deltas);
    if (OB_FAIL(ret)) output.reset();
    return ret;
  } catch (const std::bad_alloc &) { output.reset(); return OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { output.reset(); return OB_ERR_UNEXPECTED; }
}

int NativeRoutineRevokeWriter::revoke(const obcall::NativeRoutinePrivilegeTarget &target,
    const ObIArray<uint64_t> &grantees, ObPrivSet rights, bool grant_option_only,
    NativeRoutineRevokePlan::Behavior behavior, const ObString *sql, int64_t &changed_version,
    std::shared_ptr<RoutineSchemaOverlay> view, std::shared_ptr<RoutinePrivilegeOverlay> privileges,
    NativeRoutineAclVersionReservation *reservation)
{
  changed_version = 0;
  NativeRoutineAclVersionReservation reserved;
  if (reservation) reserved = std::move(*reservation);
  if (attempted_) return OB_INIT_TWICE;
  attempted_ = true;
  if (!transaction_.is_started()) return OB_STATE_NOT_MATCH;
  if (!guard_.is_inited()) return OB_NOT_INIT;
  auto *schema_sql = service_.get_schema_service();
  if (!schema_sql) return OB_ERR_UNEXPECTED;
  if (!target.is_valid() || !target.resolved_ || target.actor_id_ == OB_INVALID_ID ||
      grantees.empty() || grantees.count() > 16384 || !rights ||
      (rights & ~(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE)) || bool(view) != bool(privileges) ||
      bool(view) != guard_.has_routine_overlay() ||
      (behavior != NativeRoutineRevokePlan::Behavior::RESTRICT && behavior != NativeRoutineRevokePlan::Behavior::CASCADE))
    return OB_INVALID_ARGUMENT;
  RoutineCatalogSavepoint savepoint(view, privileges);
  int ret = OB_SUCCESS;
  if (view) {
    std::shared_ptr<const RoutineSchemaOverlay> attached;
    if (!savepoint.valid()) return OB_STATE_NOT_MATCH;
    if (OB_FAIL(guard_.capture_routine_overlay(attached))) return ret;
    if (attached.get() != view.get()) return OB_INVALID_ARGUMENT;
  }
  try {
    const ObUserInfo *user = nullptr;
    if (OB_FAIL(guard_.get_user_info(target.actor_id_, user))) return ret;
    if (!user) return OB_USER_NOT_EXIST;
    if (user->is_role()) return OB_INVALID_ARGUMENT;
    ObSessionPrivInfo actor;
    actor.user_id_ = user->get_user_id(); actor.user_name_ = user->get_user_name_str();
    actor.host_name_ = user->get_host_name_str();
    std::set<uint64_t> recipients;
    for (int64_t i = 0; i < grantees.count(); ++i) {
      const uint64_t id = grantees.at(i);
      if (id == 0 || id > INT64_MAX) return OB_INVALID_ARGUMENT;
      recipients.insert(id);
    }
    for (uint64_t id : recipients) {
      const ObUserInfo *recipient = nullptr;
      if (OB_FAIL(guard_.get_user_info(id, recipient))) return ret;
      if (!recipient) return OB_USER_NOT_EXIST;
    }
    const auto &expected = target.routine_;
    auto &writer = schema_sql->get_priv_sql_service();
    ObSEArray<ObObjPriv, 4> snapshot;
    if (OB_FAIL(writer.read_native_routine_privileges(expected, transaction_, snapshot))) return ret;
    NativeRoutineGrantors sources;
    if (OB_FAIL(guard_.select_native_routine_grantors(actor, target.enabled_roles_, expected, rights, snapshot, sources))) return ret;
    std::map<uint64_t, ObPrivSet> by_source;
    if (rights & OB_PRIV_EXECUTE) by_source[sources.execute_] |= OB_PRIV_EXECUTE;
    if (rights & OB_PRIV_ALTER_ROUTINE) by_source[sources.alter_] |= OB_PRIV_ALTER_ROUTINE;
    if (recipients.size() * by_source.size() > 16384) return OB_SIZE_OVERFLOW;
    ObSEArray<NativeRoutineRevokeRequest, 4> requests;
    for (uint64_t id : recipients) for (const auto &source : by_source)
      if (OB_FAIL(requests.push_back({source.first, id, source.second, grant_option_only}))) return ret;
    ObSEArray<NativeRoutineGrantRoot, 4> roots;
    ObSEArray<NativeRoutineRevokeDelta, 4> plan;
    if (OB_FAIL(NativeRoutineRevokePlan::collect_roots(guard_, expected, snapshot, roots)) ||
        OB_FAIL(NativeRoutineRevokePlan::build(expected, snapshot, roots, requests, behavior, plan))) return ret;
    using Key = std::pair<uint64_t, uint64_t>; // grantee, grantor
    std::map<Key, ObPackedObjPriv> current;
    for (int64_t i = 0; i < snapshot.count(); ++i) {
      const auto &row = snapshot.at(i);
      if (row.get_col_id() == OBJ_LEVEL_FOR_TAB_PRIV) current[{row.get_grantee_id(), row.get_grantor_id()}] = row.get_obj_privs();
    }
    struct Change { ObPackedObjPriv before = 0, after = 0; int64_t version = 0; };
    std::map<Key, Change> changes;
    // Include no-op direct keys, so an absent transaction grant cannot leave a
    // stale private grant visible. No-op compare-and-reduce writes no SQL log.
    for (int64_t i = 0; i < requests.count(); ++i) {
      const auto &request = requests.at(i);
      const Key key{request.grantee_, request.grantor_};
      const auto found = current.find(key);
      const auto bits = found == current.end() ? ObPackedObjPriv{0} : found->second;
      changes[key] = {bits, bits, 0};
    }
    for (int64_t i = 0; i < plan.count(); ++i) {
      const auto &delta = plan.at(i);
      changes[{delta.grantee_, delta.grantor_}] = {delta.before_, delta.after_, 0};
    }
    if (changes.size() > 16384) return OB_SIZE_OVERFLOW;
    ObSEArray<NativeRoutineAclChange, 4> version_changes;
    ObSEArray<int64_t, 4> versions;
    for (const auto &entry : changes)
      if (OB_FAIL(version_changes.push_back({entry.first.second,entry.first.first,
          entry.second.before,entry.second.after}))) return ret;
    if (!reservation && OB_FAIL(NativeRoutineAclVersionReservation::reserve(service_,transaction_,target,
        NativeRoutineAclVersionReservation::Kind::REVOKE,version_changes,reserved))) return ret;
    if (OB_FAIL(reserved.take(service_,transaction_,target,NativeRoutineAclVersionReservation::Kind::REVOKE,
        version_changes,versions))) return ret;
    int64_t version_index = 0;
    for (auto &entry : changes) entry.second.version = versions.at(version_index++);
    // Reservations are not authority. Before applying ANY reduction, re-read
    // the whole locked ACL, roots and chosen direct grantors. A new alternate
    // path or lost role membership must invalidate the plan, not be ignored.
    ObSEArray<ObObjPriv, 4> confirmed;
    ObSEArray<NativeRoutineGrantRoot, 4> confirmed_roots;
    if (OB_FAIL(writer.read_native_routine_privileges(expected, transaction_, confirmed))) return ret;
    if (confirmed.count() != snapshot.count()) return OB_STATE_NOT_MATCH;
    for (int64_t i = 0; i < snapshot.count(); ++i) {
      const auto &a = snapshot.at(i), &b = confirmed.at(i);
      if (a.get_grantor_id() != b.get_grantor_id() || a.get_grantee_id() != b.get_grantee_id() ||
          a.get_col_id() != b.get_col_id() || a.get_obj_privs() != b.get_obj_privs()) return OB_STATE_NOT_MATCH;
    }
    if (OB_FAIL(NativeRoutineRevokePlan::collect_roots(guard_, expected, confirmed, confirmed_roots))) return ret;
    if (confirmed_roots.count() != roots.count()) return OB_STATE_NOT_MATCH;
    for (int64_t i = 0; i < roots.count(); ++i)
      if (roots.at(i).principal_ != confirmed_roots.at(i).principal_ || roots.at(i).rights_ != confirmed_roots.at(i).rights_)
        return OB_STATE_NOT_MATCH;
    for (const auto &source : by_source)
      if (OB_FAIL(guard_.check_native_routine_priv(actor, target.enabled_roles_, expected,
          source.second | OB_PRIV_GRANT, &confirmed, source.first))) return ret;
    if (service_.get_schema_service() != schema_sql || user->is_role()) return OB_STATE_NOT_MATCH;
    int64_t highest = 0;
    for (const auto &entry : changes) {
      const auto &change = entry.second;
      if (OB_FAIL(writer.apply_native_routine_privilege_reduction(expected, entry.first.second,
          entry.first.first, change.before, change.after, change.version, transaction_, sql))) return ret;
      if (change.before != change.after) highest = change.version;
    }
    if (privileges) for (const auto &entry : changes) {
      const auto &change = entry.second;
      if (OB_FAIL(privileges->record_object_change(expected, entry.first.second, entry.first.first,
          change.version, change.before, change.after, actor.user_id_))) return ret;
    }
    savepoint.release();
    changed_version = highest;
    return OB_SUCCESS;
  } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { return OB_ERR_UNEXPECTED; }
}

int NativeRoutineGrantWriter::grant(const obcall::NativeRoutinePrivilegeTarget &target,
    const ObIArray<uint64_t> &grantees, ObPrivSet rights, bool grant_option,
    const ObString *sql, int64_t &changed_version, std::shared_ptr<RoutineSchemaOverlay> view,
    std::shared_ptr<RoutinePrivilegeOverlay> privileges, NativeRoutineAclVersionReservation *reservation)
{
  changed_version = 0;
  NativeRoutineAclVersionReservation reserved;
  if (reservation) reserved = std::move(*reservation);
  if (attempted_) return OB_INIT_TWICE;
  attempted_ = true;
  if (!transaction_.is_started()) return OB_STATE_NOT_MATCH;
  if (!guard_.is_inited()) return OB_NOT_INIT;
  auto *schema_sql = service_.get_schema_service();
  if (!schema_sql) return OB_ERR_UNEXPECTED;
  if (!target.is_valid() || !target.resolved_ || target.actor_id_ == OB_INVALID_ID ||
      grantees.count() <= 0 || grantees.count() > 16384 || !rights ||
      (rights & ~(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE)) || bool(view) != bool(privileges) ||
      bool(view) != guard_.has_routine_overlay())
    return OB_INVALID_ARGUMENT;
  RoutineCatalogSavepoint savepoint(view, privileges);
  int ret = OB_SUCCESS;
  if (view) {
    std::shared_ptr<const RoutineSchemaOverlay> attached;
    if (!savepoint.valid()) return OB_STATE_NOT_MATCH;
    if (OB_FAIL(guard_.capture_routine_overlay(attached))) return ret;
    if (attached.get() != view.get()) return OB_INVALID_ARGUMENT;
  }
  try {
    const ObUserInfo *user = nullptr;
    if (OB_FAIL(guard_.get_user_info(target.actor_id_, user))) return ret;
    if (!user) return OB_USER_NOT_EXIST;
    if (user->is_role()) return OB_INVALID_ARGUMENT;
    ObSessionPrivInfo actor;
    actor.user_id_ = user->get_user_id();
    actor.user_name_ = user->get_user_name_str();
    actor.host_name_ = user->get_host_name_str();
    // Validate every recipient before taking catalog locks or reserving versions.
    std::set<uint64_t> recipients;
    for (int64_t i = 0; i < grantees.count(); ++i) {
      const uint64_t id = grantees.at(i);
      if (id == 0 || id > INT64_MAX) return OB_INVALID_ARGUMENT;
      recipients.insert(id);
    }
    for (const uint64_t id : recipients) {
      const ObUserInfo *recipient = nullptr;
      if (OB_FAIL(guard_.get_user_info(id, recipient))) return ret;
      if (!recipient) return OB_USER_NOT_EXIST;
    }
    const auto &expected = target.routine_;
    auto &writer = schema_sql->get_priv_sql_service();
    ObSEArray<ObObjPriv, 4> snapshot;
    if (OB_FAIL(writer.read_native_routine_privileges(expected, transaction_, snapshot))) return ret;
    NativeRoutineGrantors sources;
    if (OB_FAIL(guard_.select_native_routine_grantors(actor, target.enabled_roles_, expected,
        rights, snapshot, sources))) return ret;
    std::map<uint64_t, ObPrivSet> by_source;
    if (rights & OB_PRIV_EXECUTE) by_source[sources.execute_] |= OB_PRIV_EXECUTE;
    if (rights & OB_PRIV_ALTER_ROUTINE) by_source[sources.alter_] |= OB_PRIV_ALTER_ROUTINE;
    if (recipients.size() * by_source.size() > 16384) return OB_SIZE_OVERFLOW;
    ObSEArray<NativeRoutineGrantRequest, 4> requests;
    ObSEArray<NativeRoutineGrantDelta, 4> plan;
    for (const uint64_t id : recipients) for (const auto &source : by_source)
      if (OB_FAIL(requests.push_back({source.first, id, source.second, grant_option}))) return ret;
    if (OB_FAIL(NativeRoutineGrantPlan::build(expected, snapshot, requests, plan))) return ret;
    struct Delta {
      uint64_t grantee, grantor;
      ObPrivSet rights;
      int64_t version;
      ObPackedObjPriv before = 0, after = 0;
    };
    std::vector<Delta> changes;
    changes.reserve(plan.count());
    // Match the complete ACL reader's grantee/grantor ordering. Reserve the
    // whole batch before writes: allocation failures cannot leave partial SQL.
    ObSEArray<NativeRoutineAclChange, 4> version_changes;
    ObSEArray<int64_t, 4> versions;
    for (int64_t i = 0; i < plan.count(); ++i) {
      const auto &delta = plan.at(i);
      if (OB_FAIL(version_changes.push_back({delta.grantor_,delta.grantee_,delta.before_,delta.after_}))) return ret;
    }
    if (!reservation && OB_FAIL(NativeRoutineAclVersionReservation::reserve(service_,transaction_,target,
        NativeRoutineAclVersionReservation::Kind::GRANT,version_changes,reserved))) return ret;
    if (OB_FAIL(reserved.take(service_,transaction_,target,NativeRoutineAclVersionReservation::Kind::GRANT,
        version_changes,versions))) return ret;
    for (int64_t i = 0; i < plan.count(); ++i) {
      const auto &delta = plan.at(i);
      changes.push_back({delta.grantee_, delta.grantor_, by_source.at(delta.grantor_), versions.at(i)});
    }
    if (service_.get_schema_service() != schema_sql) return OB_STATE_NOT_MATCH;
    int64_t highest = 0;
    for (size_t i = 0; i < changes.size(); ++i) {
      auto &change = changes[i];
      // A selected source is not a token. Recheck its current grant option on
      // this same locked transaction before each exact-key delta mutation.
      if (OB_FAIL(writer.change_native_routine_privileges_authorized(guard_, actor,
          target.enabled_roles_, expected, change.grantor, change.grantee, change.rights,
          ObPrivSqlService::NativePrivilegeChange::GRANT, grant_option, change.version,
          transaction_, sql, change.before, change.after))) return ret;
      // The locked exact-key writer must produce the planned ACL image. Never
      // silently rebase a future UPDATE plan or publish a divergent prefix.
      // Any mismatch requires the caller to roll back all SQL already issued.
      if (change.before != plan.at(i).before_ || change.after != plan.at(i).after_)
        return OB_STATE_NOT_MATCH;
      if (change.before != change.after) highest = change.version;
    }
    // Do not expose a successful prefix while later SQL groups may still fail.
    // If view publication fails, its savepoint unwinds ALL groups; the caller
    // must still roll back the corresponding SQL transaction/savepoint.
    if (privileges) {
      for (const auto &change : changes) {
        if (OB_FAIL(privileges->record_object_change(expected, change.grantor, change.grantee,
            change.version, change.before, change.after, actor.user_id_))) return ret;
      }
    }
    savepoint.release();
    changed_version = highest;
    return OB_SUCCESS;
  } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { return OB_ERR_UNEXPECTED; }
}

// Reserved Extension identities must not inherit a historical name-keyed ACL,
// even if a prior ordinary DROP left one behind with automatic grants disabled.
// Reserved identities use this policy; ordinary PL DDL is unchanged. The host
// admission layer must hold the serial DDL lock before calling this helper.
static int clear_reserved_routine_privileges(const ObRoutineInfo &routine,
                                           ObSchemaGetterGuard &guard,
                                           ObPLDDLOperator &operation,
                                           ObMySQLTransaction &transaction)
{
  int ret = OB_SUCCESS;
  const ObDatabaseSchema *database = nullptr;
  ObSEArray<const ObUserInfo *, 10> users;
  if (!transaction.is_started()) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(guard.get_database_schema(routine.get_database_id(), database))) {
  } else if (database == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(guard.get_user_infos_by_id(users))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < users.count(); ++i) {
    if (users.at(i) == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const ObRoutinePrivSortKey key(users.at(i)->get_user_id(), database->get_database_name_str(),
                                    routine.get_routine_name(), routine.get_routine_type());
      ret = operation.revoke_routine(key, OB_PRIV_ROUTINE_ACC | OB_PRIV_GRANT, transaction,
                                     false, false, "", "", true /* transaction ACL, not final overlay */);
    }
  }
  return ret;
}

int RoutineCatalogWriter::begin()
{
  if (attempted_) return OB_INIT_TWICE;
  attempted_ = true;
  if (!transaction_.is_started()) return OB_STATE_NOT_MATCH;
  if (!guard_.is_inited()) return OB_NOT_INIT;
  if (service_.get_schema_service() == nullptr) return OB_ERR_UNEXPECTED;
  return OB_SUCCESS;
}

static int mutate_native_routine_dependency(ObMySQLTransaction &transaction,
                                            const ObRoutineInfo &routine, bool add,
                                            uint64_t expected_generation = 0)
{
  if (!routine.is_native()) return OB_SUCCESS;
  if (!routine.is_native_binding_valid()) return OB_INVALID_ARGUMENT;
  if (share::g_mp == nullptr) return OB_NOT_INIT;
  return share::g_mp->mutate_native_routine_dependency(transaction,
      routine.get_native_module_id(), routine.get_native_implementation_id(),
      routine.get_routine_id(), add, expected_generation);
}

int RoutineCatalogWriter::create(ObRoutineInfo &routine_info, const ObRoutineInfo *old_routine_info,
    ObErrorInfo &error_info, ObIArray<ObDependencyInfo> &dep_infos, const ObString *ddl_stmt_str,
    RoutineIdReservation *reservation, RoutineVersionReservation *version_reservation,
    NativeRoutineAclVersionReservation *owner_grant)
{
  NativeRoutineAclVersionReservation reserved_owner_grant;
  if (owner_grant != nullptr) reserved_owner_grant = std::move(*owner_grant);
  int ret = begin();
  if (ret != OB_SUCCESS) return ret;
  if (routine_info.is_native() && (routine_info.get_overload() < 0 || !routine_info.is_native_binding_valid()))
    return OB_INVALID_ARGUMENT;
  const bool replace = old_routine_info != nullptr;
  // CREATE OR REPLACE cannot silently translate between name and object ACLs,
  // nor transfer ownership without rewriting the grant dependency graph.
  if (replace && (routine_info.is_native() != old_routine_info->is_native() ||
      (routine_info.is_native() && routine_info.get_owner_id() != old_routine_info->get_owner_id())))
    return OB_NOT_SUPPORTED;
  if ((reservation != nullptr && replace) ||
      ((reservation != nullptr || version_reservation != nullptr) && !transaction_privileges_))
    return OB_INVALID_ARGUMENT;
  if (owner_grant != nullptr && (replace || !routine_info.is_native() || !transaction_privileges_ ||
      reservation == nullptr || version_reservation == nullptr)) return OB_INVALID_ARGUMENT;
  auto &schema_guard = guard_;
  auto &trans = transaction_;
  const bool same_binding = replace && NativeRoutineAdmission::same_binding(*old_routine_info, routine_info);
  const bool preserve_binding = same_binding && NativeRoutineAdmission::same_signature(*old_routine_info, routine_info);
  uint64_t expected_generation = 0;
  if (routine_info.is_native() || (replace && old_routine_info->is_native())) {
    if (OB_FAIL(NativeRoutineAdmission::check_catalog(trans))) return ret;
  }
  if (routine_info.is_native() && !preserve_binding) {
    if (share::g_mp == nullptr) return OB_NOT_INIT;
    seekdb_plugin_sql_binding_v1_t binding{};
    std::vector<std::string> arguments;
    if (OB_FAIL(sql::PluginFunctionExpr::resolve_native_binding(routine_info, binding, arguments))) return ret;
    expected_generation = binding.owner_generation;
    if (expected_generation == 0) return OB_STATE_NOT_MATCH;
  }
  int64_t reserved_acl_version = OB_INVALID_VERSION;
  if (owner_grant != nullptr) {
    // Validate the admitted implicit grant before the first CREATE SQL. The
    // authoritative owner/empty-ACL checks below still run after the insert.
    const ObSysVarSchema *variable = nullptr;
    ObMalloc allocator(ObModIds::OB_TEMP_VARIABLES);
    ObObj value;
    if (OB_FAIL(schema_guard.get_system_variable(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, variable))) return ret;
    if (variable == nullptr) return OB_ERR_UNEXPECTED;
    if (OB_FAIL(variable->get_value(&allocator, nullptr, value))) return ret;
    if (!value.get_bool()) return OB_STATE_NOT_MATCH;
    if (OB_FAIL(reserved_owner_grant.take_create_owner(service_, trans, routine_info,
        reserved_acl_version))) return ret;
  }
  ObPLDDLOperator pl_operator(service_, proxy_);
  if (OB_SUCC(ret)) {
    if (replace) {
      if (OB_FAIL(pl_operator.replace_routine(routine_info,
                                               old_routine_info,
                                               trans,
                                               error_info,
                                               dep_infos,
                                               ddl_stmt_str,
                                               version_reservation))) {
      }
    } else {
      if (OB_FAIL(pl_operator.create_routine(routine_info,
                                             trans,
                                             error_info,
                                             dep_infos,
                                             ddl_stmt_str,
                                             reservation,
                                             version_reservation))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    // Attribute-only ALTER preserves the existing edge even if the module is
    // stopped. Rebinding admits the new package before removing the old edge;
    // both writes belong to the routine's transaction and must roll back with it.
    if (!preserve_binding) ret = mutate_native_routine_dependency(trans, routine_info, true, expected_generation);
    if (OB_SUCC(ret) && replace && !same_binding)
      ret = mutate_native_routine_dependency(trans, *old_routine_info, false);
  }
  if (OB_SUCC(ret) && !replace && !routine_info.is_native() && version_reservation != nullptr) {
    ret = clear_reserved_routine_privileges(routine_info, schema_guard, pl_operator, trans);
  }
  if (OB_FAIL(ret)) {
  } else if (replace) {
  } else {
    const ObSysVarSchema *sys_var = NULL;
    ObMalloc alloc(ObModIds::OB_TEMP_VARIABLES);
    ObObj val;
    if (OB_FAIL(schema_guard.get_system_variable(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, sys_var))) {
    } else if (OB_ISNULL(sys_var)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sys variable schema is null", KR(ret));
    } else if (OB_FAIL(sys_var->get_value(&alloc, NULL, val))) {
    } else {
      bool grant_priv = val.get_bool();
      if (routine_info.is_native()) {
        // A fresh identity must not inherit any old ACL, even when automatic
        // grants are disabled. Both identity/ACL locks and the subsequent
        // owner grant use the same transaction as the routine creation.
        const ObUserInfo *owner = nullptr;
        ObSEArray<ObObjPriv, 4> existing;
        auto &privileges = service_.get_schema_service()->get_priv_sql_service();
        if (OB_FAIL(schema_guard.get_user_info(routine_info.get_owner_id(), owner))) {
        } else if (!owner) ret = OB_USER_NOT_EXIST;
        else if (OB_FAIL(privileges.read_native_routine_privileges(routine_info, trans, existing))) {
        } else if (!existing.empty()) ret = OB_STATE_NOT_MATCH;
        else if (grant_priv) {
          int64_t acl_version = reserved_acl_version;
          ObPackedObjPriv before = 0, after = 0;
          if (owner_grant == nullptr && OB_FAIL(service_.gen_new_schema_version(acl_version))) {
          } else if (acl_version <= routine_info.get_schema_version()) ret = OB_STATE_NOT_MATCH;
          else if (OB_FAIL(privileges.change_native_routine_privileges(routine_info,
              routine_info.get_owner_id(), routine_info.get_owner_id(), OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE,
              ObPrivSqlService::NativePrivilegeChange::GRANT, false, acl_version, trans, ddl_stmt_str, before, after))) {
          } else if (before != 0) ret = OB_STATE_NOT_MATCH;
        }
      } else if (grant_priv) {
        int64_t db_id = routine_info.get_database_id();
        const ObDatabaseSchema* database_schema = NULL;
        if (OB_FAIL(schema_guard.get_database_schema( db_id, database_schema))) {
        } else if (OB_ISNULL(database_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("database schema should not be null", K(ret));
        } else {
          ObRoutinePrivSortKey routine_key(routine_info.get_owner_id(),
                                            database_schema->get_database_name_str(),
                                            routine_info.get_routine_name(), routine_info.is_procedure() ?
                                            ObRoutineType::ROUTINE_PROCEDURE_TYPE : ObRoutineType::ROUTINE_FUNCTION_TYPE);
          ObPrivSet priv_set = (OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE);
          int64_t option = 0;
          const bool gen_ddl_stmt = false;
          const ObUserInfo *user_info = NULL;
          if (OB_FAIL(schema_guard.get_user_info(routine_info.get_owner_id(),
                                                 user_info))) {
          } else if (OB_ISNULL(user_info)) {
            ret = OB_ERR_PARALLEL_DDL_CONFLICT;
            LOG_WARN("user info is null, may be parallel ddl conflict", K(ret));
          } else if (OB_FAIL(pl_operator.grant_routine(routine_key,
                                                        priv_set,
                                                        trans,
                                                        option,
                                                        gen_ddl_stmt,
                                                        user_info->get_user_name_str(),
                                                        user_info->get_host_name_str(),
                                                        transaction_privileges_))) {
          }
        }
      }
    }
  }
  return ret;
}

int RoutineCatalogWriter::alter(const ObRoutineInfo &routine_info, ObErrorInfo &error_info,
    const ObString *ddl_stmt_str)
{
  int ret = begin();
  if (ret != OB_SUCCESS) return ret;
  auto &trans = transaction_;
  ObPLDDLOperator pl_operator(service_, proxy_);
  if (OB_FAIL(ObDependencyDDLHelper::modify_dep_obj_status(trans,
                                                              routine_info.get_routine_id(),
                                                              pl_operator,
                                                              service_))) {
  } else if (OB_FAIL(pl_operator.alter_routine(routine_info, trans, error_info, ddl_stmt_str))) {
  }
  return ret;
}

int RoutineCatalogWriter::drop(const ObRoutineInfo &routine_info, ObErrorInfo &error_info,
    const ObString *ddl_stmt_str, IRoutineCacheInvalidation &invalidation,
    RoutineVersionReservation *version_reservation)
{
  int ret = begin();
  if (ret != OB_SUCCESS) return ret;
  // Native DROP clears only this object's ACL, independent of sibling slots.
  if (routine_info.is_native() && (routine_info.get_overload() < 0 || !routine_info.is_native_binding_valid()))
    return OB_INVALID_ARGUMENT;
  if (version_reservation != nullptr && !transaction_privileges_) return OB_INVALID_ARGUMENT;
  auto &schema_guard = guard_;
  auto &trans = transaction_;
  ObPLDDLOperator pl_operator(service_, proxy_);
  if (OB_FAIL(ObDependencyDDLHelper::modify_dep_obj_status(trans,
                                                             routine_info.get_routine_id(),
                                                             pl_operator,
                                                             service_))) {
  } else if (OB_FAIL(pl_operator.drop_routine(routine_info, trans, error_info, ddl_stmt_str,
                                            version_reservation, &invalidation))) {
  } else if (OB_FAIL(mutate_native_routine_dependency(trans, routine_info, false))) {
  } else {
    const ObSysVarSchema *sys_var = NULL;
    ObMalloc alloc(ObModIds::OB_TEMP_VARIABLES);
    ObObj val;
    if (OB_FAIL(schema_guard.get_system_variable(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, sys_var))) {
    } else if (OB_ISNULL(sys_var)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sys variable schema is null", KR(ret));
    } else if (OB_FAIL(sys_var->get_value(&alloc, NULL, val))) {
    } else if (routine_info.is_native()) {
      // Native object ACLs were removed with the routine by the PL operator.
      // Do not revoke a sibling/previous routine's name-keyed grants.
    } else if (version_reservation != nullptr) {
      // Extension updates replace object identities, not names. Always clear
      // old named grants on their reserved DROP, including when automatic
      // grants were disabled since the old object was created. Ordinary DROP
      // (without a host reservation) retains its existing policy.
      ret = clear_reserved_routine_privileges(routine_info, schema_guard, pl_operator, trans);
    } else if (val.get_bool()) {
      const int64_t db_id = routine_info.get_database_id();
      const ObDatabaseSchema *database_schema = NULL;
      ObSEArray<const ObUserInfo *, 10> user_infos;
      if (OB_FAIL(schema_guard.get_database_schema(db_id, database_schema))) {
      } else if (OB_ISNULL(database_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("database schema is null", K(ret));
      } else if (OB_FAIL(schema_guard.get_user_infos_by_id(user_infos))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < user_infos.count(); ++i) {
        const ObUserInfo *user_info = user_infos.at(i);
        if (OB_ISNULL(user_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null user info", K(ret));
        } else {
          ObRoutinePrivSortKey routine_key(
              user_info->get_user_id(),
              database_schema->get_database_name_str(),
              routine_info.get_routine_name(),
              routine_info.is_procedure()
                  ? ObRoutineType::ROUTINE_PROCEDURE_TYPE
                  : ObRoutineType::ROUTINE_FUNCTION_TYPE);
          const ObPrivSet priv_set = OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE;
          bool gen_ddl_stmt = false;
          if (OB_FAIL(pl_operator.revoke_routine(
                  routine_key, priv_set, trans, false, gen_ddl_stmt, "", "",
                  transaction_privileges_))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObPLDDLService::install_routine_extension(
    const share::plugin::ExtensionInstallSpec &spec,
    const obcall::ObCreateRoutineArg &arg,
    const ObSessionPrivInfo &session_priv,
    const ObIArray<uint64_t> &enabled_roles,
    share::plugin::IExtensionCatalogInstaller &catalog,
    ObDDLService &ddl_service,
    uint64_t &extension_id, int &publication_status, std::string &error)
{
  extension_id = 0;
  publication_status = OB_NOT_INIT;
  error.clear();
  ObSEArray<const ObCreateRoutineArg *, 1> args;
  int ret = args.push_back(&arg);
  if (OB_SUCC(ret)) {
    ret = install_routines_extension(spec, args, session_priv, enabled_roles,
                                    catalog, ddl_service, extension_id, publication_status, error);
  }
  return ret;
}

std::unique_ptr<share::plugin::IExtensionSchemaInstaller> ObPLDDLService::make_routine_extension_installer(
    const ObIArray<const ObCreateRoutineArg *> &args, const ObSessionPrivInfo &session_priv,
    const ObIArray<uint64_t> &enabled_roles, ObSchemaGetterGuard &guard, ObDDLService &ddl_service,
    ObDDLSQLTransaction &transaction, share::plugin::IExtensionRoutineScript *script)
{
#if !defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  UNUSEDx(args, session_priv, enabled_roles, guard, ddl_service, transaction, script);
  return nullptr;
#else
  using namespace share::plugin;
    class RoutineInstaller final : public IExtensionSchemaInstaller
    {
    public:
      RoutineInstaller(const ObIArray<const ObCreateRoutineArg *> &args, const ObSessionPrivInfo &priv,
                       const ObIArray<uint64_t> &roles, ObSchemaGetterGuard &guard,
                       ObDDLService &ddl, ObDDLSQLTransaction &transaction, IExtensionRoutineScript *script,
                       int64_t count)
          : args_(args), priv_(priv), roles_(roles), guard_(guard), ddl_(ddl), transaction_(transaction),
            script_(script), count_(count) {}

      int preflight(const ExtensionInstallSpec &spec, std::string &error) override
      {
        int ret = OB_SUCCESS;
        if (consumed_) return OB_STATE_NOT_MATCH;
        if (spec.tenant_id_ != 1) {
          ret = OB_NOT_SUPPORTED;
          error = "routine extension adapter requires the local tenant";
        } else if (!priv_.is_valid() || spec.owner_id_ != priv_.user_id_) {
          ret = OB_ERR_NO_PRIVILEGE;
        } else if (spec.requires_superuser_ && !(priv_.user_priv_set_ & OB_PRIV_SUPER)) {
          ret = OB_ERR_NO_PRIVILEGE;
        } else if (script_ != nullptr) {
          if (!args_.empty() || count_ != script_->statement_count()) ret = OB_STATE_NOT_MATCH;
          else ret = script_->preflight_install(spec, error);
        }
        // Use the same comparison as ObRoutineNameHashWrapper; bytewise or
        // ASCII-only folding would miss conflicting names in this transaction.
        // FUNCTION and PROCEDURE have distinct kernel namespaces.
        struct NameLess {
          bool operator()(const ObCreateRoutineArg *lhs, const ObCreateRoutineArg *rhs) const
          {
            const auto &left = lhs->routine_info_;
            const auto &right = rhs->routine_info_;
            ObSchemaNameComparator comparator;
            return left.get_routine_type() != right.get_routine_type()
                ? left.get_routine_type() < right.get_routine_type()
                : comparator.compare(left.get_routine_name(), right.get_routine_name()) < 0;
          }
        };
        std::set<const ObCreateRoutineArg *, NameLess> names;
        for (int64_t i = 0; OB_SUCC(ret) && i < args_.count(); ++i) {
          if (nullptr == args_.at(i) || !args_.at(i)->is_valid()) {
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(check_routine(spec, *args_.at(i), error))) {
          } else if (!names.insert(args_.at(i)).second) {
            ret = OB_ERR_SP_ALREADY_EXISTS;
            error = "duplicate routine name inside extension installation";
          }
        }
        return ret;
      }

      int check_routine(const ExtensionInstallSpec &spec, const ObCreateRoutineArg &arg,
                        std::string &error)
      {
        int ret = OB_SUCCESS;
        const ObDatabaseSchema *database = nullptr;
        const auto type = arg.routine_info_.get_routine_type();
        if (arg.is_or_replace_ || arg.is_need_alter_ || arg.with_if_not_exist_ ||
            (type != ROUTINE_FUNCTION_TYPE && type != ROUTINE_PROCEDURE_TYPE)) {
          ret = OB_NOT_SUPPORTED;
          error = "extension routine installation requires new functions or procedures";
        } else if (arg.error_info_.get_error_status() == ERROR_STATUS_HAS_ERROR) {
          ret = OB_ERR_RESOLVE_SQL;
          error = "extension routine body contains unresolved compilation errors";
        } else if (arg.routine_info_.is_native() && !(priv_.user_priv_set_ & OB_PRIV_SUPER)) {
          ret = OB_ERR_NO_PRIVILEGE;
        } else if (arg.routine_info_.get_owner_id() != priv_.user_id_) {
          ret = OB_ERR_NO_PRIVILEGE;
        } else if (OB_FAIL(guard_.get_database_schema(arg.db_name_, database))) {
        } else if (nullptr == database || database->is_in_recyclebin() ||
                   database->get_database_id() != spec.database_id_) {
          ret = OB_ERR_BAD_DATABASE;
        } else if (OB_FAIL(ddl_.check_parallel_ddl_conflict(guard_, arg))) {
        } else {
          ObArenaAllocator allocator;
          ObStmtNeedPrivs privileges(allocator);
          ObNeedPriv need;
          need.db_ = arg.db_name_;
          need.table_ = arg.routine_info_.get_routine_name();
          need.obj_type_ = type == ROUTINE_PROCEDURE_TYPE ? ObObjectType::PROCEDURE : ObObjectType::FUNCTION;
          need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
          need.priv_set_ = OB_PRIV_CREATE_ROUTINE;
          if (OB_FAIL(privileges.need_privs_.reserve(1))) {
          } else if (OB_FAIL(privileges.need_privs_.push_back(need))) {
          } else if (OB_FAIL(guard_.check_priv(priv_, roles_, privileges))) {
          } else if (!(priv_.user_priv_set_ & OB_PRIV_SUPER) && OB_FAIL(guard_.verify_read_only(privileges))) {
          }
          bool exists = false;
          if (OB_SUCC(ret)) {
            if (arg.routine_info_.is_native()) {
              ObRoutineInfo candidate;
              ret = candidate.assign(arg.routine_info_);
              if (OB_SUCC(ret)) {
                candidate.set_database_id(spec.database_id_);
                ret = NativeRoutineCreateSlot::assign(guard_, candidate);
              }
            } else {
              if (type == ROUTINE_PROCEDURE_TYPE) {
                ret = guard_.check_standalone_procedure_exist(spec.database_id_, need.table_, exists);
              } else {
                ObSEArray<const ObRoutineInfo *, 4> family;
                ret = guard_.get_standalone_function_infos(spec.database_id_, need.table_, family);
                exists = !family.empty();
              }
            }
            if (OB_SUCC(ret) && exists) {
              ret = OB_ERR_SP_ALREADY_EXISTS;
              error = "extension routine already exists in target database";
            }
          }
        }
        return ret;
      }

      int apply(ObPluginSqlConnection &connection, const ExtensionInstallSpec &spec,
                std::vector<ExtensionMemberIdentity> &members, std::string &error) override
      {
        int ret = OB_SUCCESS;
        members.clear();
        if (!transaction_.is_started() || !connection.is_in_transaction()) {
          ret = OB_STATE_NOT_MATCH;
        } else if (OB_FAIL(preflight(spec, error))) {
        } else {
          consumed_ = true;
          // Each successful CREATE is written in the same borrowed transaction
          // before resolving the next statement. Following DCL can then lock
          // its routine/ACL rows. Nothing is published or committed here; the
          // Rust coordinator owns rollback of the entire script on any error.
          const ObSysVarSchema *variable = nullptr;
          ObMalloc allocator(ObModIds::OB_TEMP_VARIABLES);
          ObObj value;
          if (OB_FAIL(guard_.get_system_variable(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, variable))) {}
          else if (variable == nullptr) ret = OB_ERR_UNEXPECTED;
          else if (OB_FAIL(variable->get_value(&allocator, nullptr, value))) {}
          else automatic_privileges_ = value.get_bool();
          if (OB_SUCC(ret)) {
            privileges_ = std::make_shared<RoutinePrivilegeOverlay>(spec.database_id_, priv_.user_id_);
            overlay_ = std::make_shared<RoutineSchemaOverlay>(privileges_);
            ret = guard_.attach_routine_overlay(overlay_);
          }
          const auto admit_and_stage = [&](const ExtensionRoutineUpdateOperation &op) {
            if (!op.has_valid_shape()) return OB_INVALID_ARGUMENT;
            if (op.kind_ == ExtensionRoutineUpdateOperation::Kind::CREATE)
              return stage(spec, *op.create_arg_, error);
            if (op.kind_ == ExtensionRoutineUpdateOperation::Kind::GRANT ||
                op.kind_ == ExtensionRoutineUpdateOperation::Kind::REVOKE)
              return apply_privilege(spec, op);
            return OB_NOT_SUPPORTED;
          };
          if (OB_SUCC(ret) && script_ != nullptr) {
            ret = resolve_extension_routine_sequence(*script_, count_, guard_, admit_and_stage, error);
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < args_.count(); ++i) ret = stage(spec, *args_.at(i), error);
          }
          if (OB_SUCC(ret) && script_ != nullptr && script_->has_builder()) {
            ret = script_->build(guard_, [&](const ExtensionRoutineUpdateOperation &op, uint64_t &object_id) {
              object_id = 0;
              if (!transaction_.is_started() || !connection.is_in_transaction()) return OB_STATE_NOT_MATCH;
              if (op.kind_ != ExtensionRoutineUpdateOperation::Kind::CREATE) return OB_NOT_SUPPORTED;
              if (nodes_.size() >= 4096) return OB_SIZE_OVERFLOW;
              const int code = admit_and_stage(op);
              if (code == OB_SUCCESS) object_id = nodes_.back()->routine_.get_routine_id();
              return code;
            }, error);
          }
          if (OB_SUCC(ret)) members.reserve(nodes_.size());
          for (const auto &node : nodes_) {
            if (OB_FAIL(ret)) break;
            members.push_back({static_cast<uint32_t>(ROUTINE_SCHEMA), node->routine_.get_routine_id()});
          }
        }
        if (OB_FAIL(ret)) members.clear();
        return ret;
      }
    private:
      struct Node {
        const ObCreateRoutineArg *arg_ = nullptr;
        ObRoutineInfo routine_;
        ObSArray<ObDependencyInfo> dependencies_;
        RoutineIdReservation identity_;
        RoutineVersionReservation version_;
      };
      int apply_privilege(const ExtensionInstallSpec &spec, const ExtensionRoutineUpdateOperation &op)
      {
        if (!transaction_.is_started() || !overlay_ || !privileges_) return OB_STATE_NOT_MATCH;
        const bool grant = op.kind_ == ExtensionRoutineUpdateOperation::Kind::GRANT;
        const auto &target = grant ? op.grant_arg_->native_target_ : op.revoke_arg_->native_target_;
        if (!target.is_valid() || !target.resolved_ || target.actor_id_ != priv_.user_id_)
          return OB_ERR_NO_PRIVILEGE;
        if (target.routine_.get_database_id() != spec.database_id_) return OB_ERR_BAD_DATABASE;
        obcall::NativeRoutinePrivilegeTarget actor;
        int ret = actor.assign(target.routine_, target.signature_qualified_);
        if (OB_SUCC(ret)) ret = actor.bind_actor(priv_.user_id_, roles_);
        if (OB_FAIL(ret)) return ret;
        if (actor.enabled_roles_.count() != target.enabled_roles_.count()) return OB_ERR_NO_PRIVILEGE;
        for (int64_t i = 0; i < actor.enabled_roles_.count(); ++i)
          if (actor.enabled_roles_.at(i) != target.enabled_roles_.at(i)) return OB_ERR_NO_PRIVILEGE;
        int64_t changed_version = 0;
        if (grant) {
          const auto &arg = *op.grant_arg_;
          const ObPrivSet rights = arg.priv_set_ & ~OB_PRIV_GRANT;
          if (!arg.is_valid() || !rights || (rights & ~(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE)) ||
              arg.hosts_.empty() || arg.hosts_.count() > 16384 || arg.users_passwd_.count() != arg.hosts_.count() * 2 ||
              !arg.remain_roles_.empty() || !arg.sys_priv_array_.empty() || !arg.obj_priv_array_.empty() ||
              !arg.column_names_priv_.empty() || !arg.ins_col_ids_.empty() || !arg.upd_col_ids_.empty() ||
              !arg.ref_col_ids_.empty() || !arg.sel_col_ids_.empty() || arg.option_ > GRANT_OPTION)
            return OB_INVALID_ARGUMENT;
          if (OB_FAIL(ddl_.check_parallel_ddl_conflict(guard_, arg)) ||
              OB_FAIL(target.revalidate(guard_, arg.db_, arg.table_, arg.object_id_))) return ret;
          ObSEArray<uint64_t, 4> grantees;
          for (int64_t i = 0; i < arg.hosts_.count(); ++i) {
            const ObUserInfo *user = nullptr;
            if (!arg.users_passwd_.at(i * 2 + 1).empty()) return OB_NOT_SUPPORTED;
            if (OB_FAIL(guard_.get_user_info(arg.users_passwd_.at(i * 2), arg.hosts_.at(i), user))) return ret;
            if (!user) return OB_USER_NOT_EXIST;
            if (OB_FAIL(grantees.push_back(user->get_user_id()))) return ret;
          }
          NativeRoutineGrantWriter writer(ddl_.get_schema_service(), guard_, transaction_);
          ret = writer.grant(target, grantees, rights,
              (arg.priv_set_ & OB_PRIV_GRANT) || arg.option_ == GRANT_OPTION,
              &arg.ddl_stmt_str_, changed_version, overlay_, privileges_);
        } else {
          const auto &arg = *op.revoke_arg_;
          if (!arg.is_valid() || arg.native_grantees_.empty() || !arg.priv_set_ ||
              (arg.priv_set_ & ~(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE)) ||
              !arg.obj_priv_array_.empty() || arg.revoke_all_ora_) return OB_INVALID_ARGUMENT;
          if (OB_FAIL(ddl_.check_parallel_ddl_conflict(guard_, arg)) ||
              OB_FAIL(target.revalidate(guard_, arg.db_, arg.routine_, arg.obj_id_))) return ret;
          NativeRoutineRevokeWriter writer(ddl_.get_schema_service(), guard_, transaction_);
          ret = writer.revoke(target, arg.native_grantees_, arg.priv_set_, arg.grant_option_only_,
              arg.revoke_behavior_ == ObRevokeRoutineArg::REVOKE_CASCADE
                  ? NativeRoutineRevokePlan::Behavior::CASCADE : NativeRoutineRevokePlan::Behavior::RESTRICT,
              &arg.ddl_stmt_str_, changed_version, overlay_, privileges_);
        }
        return ret;
      }
      int stage(const ExtensionInstallSpec &spec, const ObCreateRoutineArg &arg, std::string &error)
      {
        int ret = OB_SUCCESS;
        if (!arg.is_valid()) return OB_INVALID_ARGUMENT;
        if (OB_FAIL(check_routine(spec, arg, error))) return ret;
        auto node = std::make_unique<Node>();
        node->arg_ = &arg;
        if (OB_FAIL(node->routine_.assign(arg.routine_info_))) return ret;
        if (OB_FAIL(node->dependencies_.assign(arg.dependency_infos_))) return ret;
        node->routine_.set_database_id(spec.database_id_);
        node->routine_.set_routine_id(OB_INVALID_ID);
        if (node->routine_.is_native() && OB_FAIL(NativeRoutineCreateSlot::assign(guard_, node->routine_))) return ret;
        auto *schema = ddl_.get_schema_service().get_schema_service();
        if (schema == nullptr) return OB_ERR_UNEXPECTED;
        if (OB_FAIL(RoutineIdReservation::reserve(*schema, node->routine_, node->identity_))) return ret;
        node->routine_.set_routine_id(node->identity_.id());
        if (OB_FAIL(RoutineVersionReservation::reserve(ddl_.get_schema_service(), transaction_,
            node->routine_, nullptr, node->version_))) return ret;
        node->routine_.set_schema_version(node->version_.version());
        auto &parameters = node->routine_.get_routine_params();
        for (int64_t i = 0; i < parameters.count(); ++i) {
          if (parameters.at(i) == nullptr) return OB_ERR_UNEXPECTED;
          parameters.at(i)->set_routine_id(node->routine_.get_routine_id());
          parameters.at(i)->set_schema_version(node->routine_.get_schema_version());
        }
        RoutineCatalogSavepoint view_savepoint(overlay_, privileges_);
        if (!view_savepoint.valid()) return OB_STATE_NOT_MATCH;
        if (OB_FAIL(overlay_->stage(node->routine_))) return ret;
        if (OB_FAIL(privileges_->record_create(node->routine_, automatic_privileges_))) return ret;
        ObErrorInfo errors = arg.error_info_;
        if (OB_FAIL(ObPLDDLService::create_routine(node->routine_, nullptr, false, errors,
            node->dependencies_, &arg.ddl_stmt_str_, guard_, ddl_, &transaction_,
            &node->identity_, &node->version_))) return ret;
        nodes_.push_back(std::move(node));
        view_savepoint.release();
        return OB_SUCCESS;
      }
      const ObIArray<const ObCreateRoutineArg *> &args_;
      const ObSessionPrivInfo &priv_;
      const ObIArray<uint64_t> &roles_;
      ObSchemaGetterGuard &guard_;
      ObDDLService &ddl_;
      ObDDLSQLTransaction &transaction_;
      IExtensionRoutineScript *script_;
      const int64_t count_;
      bool consumed_ = false;
      bool automatic_privileges_ = false;
      std::shared_ptr<RoutinePrivilegeOverlay> privileges_;
      std::shared_ptr<RoutineSchemaOverlay> overlay_;
      std::vector<std::unique_ptr<Node>> nodes_;
    };
    return std::make_unique<RoutineInstaller>(args, session_priv, enabled_roles, guard, ddl_service,
        transaction, script, script ? script->statement_count() : args.count());
#endif
}

int ObPLDDLService::install_routines_extension(
    const share::plugin::ExtensionInstallSpec &spec,
    const ObIArray<const ObCreateRoutineArg *> &args,
    const ObSessionPrivInfo &session_priv,
    const ObIArray<uint64_t> &enabled_roles,
    share::plugin::IExtensionCatalogInstaller &catalog,
    ObDDLService &ddl_service,
    uint64_t &extension_id, int &publication_status, std::string &error,
    share::plugin::IExtensionRoutineScript *script)
{
  extension_id = 0;
  publication_status = OB_NOT_INIT; // not attempted unless installation commits
  error.clear();
#if !defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  UNUSEDx(spec, args, session_priv, enabled_roles, catalog, ddl_service, script);
  return OB_NOT_SUPPORTED;
#else
  using namespace share::plugin;
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard guard;
  int64_t schema_version = 0;
  const int64_t count = script ? script->statement_count() : args.count();
  if (count < 0 || (count == 0 && (script == nullptr || !script->has_builder())) ||
      count > 4096 || (script != nullptr && !args.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ddl_service.check_inner_stat())) {
  } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(guard))) {
  } else if (OB_FAIL(guard.get_schema_version(schema_version))) {
  } else {
    // Non-parallel DDL owns the schema epoch/ordering locks and end marker.
    // An ordinary ObMySQLTransaction would omit the schema watermark at commit.
    ObDDLSQLTransaction transaction(&ddl_service.get_schema_service());
    std::unique_ptr<IExtensionSchemaInstaller> installer;
    try {
      installer = make_routine_extension_installer(args, session_priv, enabled_roles,
          guard, ddl_service, transaction, script);
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCC(ret) && !installer) ret = OB_ERR_UNEXPECTED;
    if (OB_SUCC(ret))
      ret = catalog.install_extension(spec, *installer, extension_id, error, &transaction, schema_version);
    if (OB_SUCC(ret)) {
      // The transaction has committed. Never convert publication failure into
      // a retryable installation error, or claim that schema writes rolled back.
      try {
        publication_status = ddl_service.publish_schema();
      } catch (const std::bad_alloc &) {
        publication_status = OB_ALLOCATE_MEMORY_FAILED;
      } catch (...) {
        publication_status = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
#endif
}

int ObPLDDLService::drop_routines_extension(
    const share::plugin::ExtensionDropRequest &request, const ObSessionPrivInfo &session_priv,
    const ObIArray<uint64_t> &enabled_roles, share::plugin::IExtensionCatalogDropper &catalog,
    ObDDLService &ddl_service, uint64_t &dropped_extension_id, int &publication_status, std::string &error)
{
  dropped_extension_id = 0;
  publication_status = OB_NOT_INIT;
  error.clear();
#if !defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  UNUSEDx(request, session_priv, enabled_roles, catalog, ddl_service);
  return OB_NOT_SUPPORTED;
#else
  using namespace share::plugin;
  int ret = OB_SUCCESS;
  try {
    ObSchemaGetterGuard guard;
    int64_t schema_version = 0;
    if (OB_FAIL(ddl_service.check_inner_stat())) {
    } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(guard))) {
    } else if (OB_FAIL(guard.get_schema_version(schema_version))) {
    } else {
      ObDDLSQLTransaction transaction(&ddl_service.get_schema_service());
      class RoutineDropper final : public IExtensionSchemaDropper {
      public:
        RoutineDropper(const ObSessionPrivInfo &priv, const ObIArray<uint64_t> &roles,
                       ObSchemaGetterGuard &guard, ObDDLService &ddl, ObDDLSQLTransaction &transaction)
          : priv_(priv), roles_(roles), guard_(guard), ddl_(ddl), transaction_(transaction) {}

        int preflight(const ExtensionDropRequest &request, std::string &error) override {
          if (!priv_.is_valid()) return OB_ERR_NO_PRIVILEGE;
          if (request.tenant_id_ != 1 || request.cascade_) {
            error = "routine Extension removal currently supports RESTRICT, not CASCADE";
            return OB_NOT_SUPPORTED;
          }
          return OB_SUCCESS;
        }

        int admit(ObPluginSqlConnection &connection, const ExtensionDropRequest &request,
                  const ExtensionDropSnapshot &snapshot, std::string &error) override {
          int ret = preflight(request, error);
          const auto &spec = snapshot.installed_;
          routines_.clear();
          const ObDatabaseSchema *database = nullptr;
          const bool super = (priv_.user_priv_set_ & OB_PRIV_SUPER) != 0;
          if (OB_FAIL(ret)) {
          } else if (!transaction_.is_started() || !connection.is_in_transaction()) {
            ret = OB_STATE_NOT_MATCH;
          } else if (priv_.user_id_ != spec.owner_id_ && !super) {
            ret = OB_ERR_NO_PRIVILEGE;
          } else if (OB_FAIL(guard_.get_database_schema(spec.database_id_, database))) {
          } else if (nullptr == database || database->is_in_recyclebin()) {
            ret = OB_ERR_BAD_DATABASE;
          } else if (!super && database->is_read_only()) {
            // Apply even when the extension currently has no members.
            ret = OB_ERR_DB_READ_ONLY;
            LOG_USER_ERROR(OB_ERR_DB_READ_ONLY, database->get_database_name_str().length(),
                           database->get_database_name_str().ptr());
          }
          std::map<uint64_t, ObObjectType> members;
          for (const auto &member : spec.members_) {
            if (OB_FAIL(ret)) break;
            const ObRoutineInfo *routine = nullptr;
            if (member.object_class_ != static_cast<uint32_t>(ROUTINE_SCHEMA)) {
              ret = OB_NOT_SUPPORTED;
              error = "Extension contains non-routine members; no objects have been detached";
            } else if (OB_FAIL(guard_.get_routine_info(member.object_id_, routine))) {
            } else if (nullptr == routine || routine->get_database_id() != spec.database_id_ ||
                       (routine->get_routine_type() != ROUTINE_FUNCTION_TYPE &&
                        routine->get_routine_type() != ROUTINE_PROCEDURE_TYPE)) {
              ret = OB_STATE_NOT_MATCH;
            } else {
              ObArenaAllocator allocator;
              ObStmtNeedPrivs privileges(allocator);
              ObNeedPriv need;
              need.db_ = database->get_database_name_str();
              need.table_ = routine->get_routine_name();
              need.obj_type_ = routine->get_object_type();
              need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
              need.priv_set_ = OB_PRIV_ALTER_ROUTINE;
              if (OB_FAIL(privileges.need_privs_.reserve(1))) {
              } else if (OB_FAIL(privileges.need_privs_.push_back(need))) {
              } else if (OB_FAIL(guard_.check_priv(priv_, roles_, privileges))) {
              } else if (!super && OB_FAIL(guard_.verify_read_only(privileges))) {
              } else {
                routines_.push_back(routine);
                members.emplace(member.object_id_, routine->get_object_type());
              }
            }
          }
          // Check incoming, typed schema dependencies before detaching anything.
          // Internal references between members do not block dropping the group.
          // Missing/corrupt/failed reads are errors, never 'no dependencies'.
          for (const auto *routine : routines_) {
            if (OB_FAIL(ret)) break;
            ret = connection.query(
                "SELECT dep_obj_id,dep_obj_type FROM __all_dependency WHERE ref_obj_id=? "
                "AND ref_obj_type=? ORDER BY dep_obj_id,dep_obj_type FOR UPDATE",
                [&](ObPluginSqlBinder &binder) {
                  int code = binder.bind_int64(routine->get_routine_id());
                  if (OB_SUCCESS == code) code = binder.bind_int64(static_cast<int64_t>(routine->get_object_type()));
                  return code;
                },
                [&](ObPluginSqlRowReader &reader) {
                  int64_t id = 0, type = 0;
                  int code = reader.read_int64(0, id);
                  if (OB_SUCCESS == code) code = reader.read_int64(1, type);
                  if (OB_SUCCESS != code) return code;
                  if (id <= 0 || type <= static_cast<int64_t>(ObObjectType::INVALID) ||
                      type >= static_cast<int64_t>(ObObjectType::MAX_TYPE)) return OB_INVALID_DATA;
                  const auto member = members.find(static_cast<uint64_t>(id));
                  if (member == members.end() || static_cast<int64_t>(member->second) != type) {
                    error = "Extension member has an external schema dependency; RESTRICT refuses removal";
                    return OB_OP_NOT_ALLOW;
                  }
                  return OB_SUCCESS;
                });
          }
          if (OB_FAIL(ret)) routines_.clear();
          return ret;
        }

        int apply(ObPluginSqlConnection &connection, const ExtensionDropSnapshot &snapshot,
                  std::string &error) override {
          UNUSED(error);
          if (!transaction_.is_started() || !connection.is_in_transaction() ||
              routines_.size() != snapshot.installed_.members_.size()) return OB_STATE_NOT_MATCH;
          int ret = OB_SUCCESS;
          for (const auto *routine : routines_) {
            ObErrorInfo errors;
            if (OB_FAIL(ObPLDDLService::drop_routine(*routine, errors, nullptr, guard_, ddl_, &transaction_))) break;
          }
          return ret;
        }
      private:
        const ObSessionPrivInfo &priv_;
        const ObIArray<uint64_t> &roles_;
        ObSchemaGetterGuard &guard_;
        ObDDLService &ddl_;
        ObDDLSQLTransaction &transaction_;
        std::vector<const ObRoutineInfo *> routines_;
      } dropper(session_priv, enabled_roles, guard, ddl_service, transaction);
      ret = catalog.drop_extension(request, dropper, dropped_extension_id, error, &transaction, schema_version);
      if (OB_SUCC(ret)) {
        try { publication_status = ddl_service.publish_schema(); }
        catch (const std::bad_alloc &) { publication_status = OB_ALLOCATE_MEMORY_FAILED; }
        catch (...) { publication_status = OB_ERR_UNEXPECTED; }
      }
    }
  } catch (const std::bad_alloc &) {
    if (dropped_extension_id != 0) publication_status = OB_ALLOCATE_MEMORY_FAILED;
    else ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    if (dropped_extension_id != 0) publication_status = OB_ERR_UNEXPECTED;
    else ret = OB_ERR_UNEXPECTED;
  }
  return ret;
#endif
}

std::unique_ptr<share::plugin::IExtensionSchemaUpdater> ObPLDDLService::make_routine_extension_updater(
    const ObIArray<share::plugin::ExtensionRoutineUpdateOperation> &operations,
    const ObSessionPrivInfo &session_priv, const ObIArray<uint64_t> &enabled_roles,
    ObSchemaGetterGuard &guard, ObDDLService &ddl_service, ObDDLSQLTransaction &transaction,
    share::plugin::IExtensionRoutineScript *script)
{
#if !defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  UNUSEDx(operations, session_priv, enabled_roles, guard, ddl_service, transaction, script);
  return nullptr;
#else
  using namespace share::plugin;
      class RoutineUpdater final : public IExtensionSchemaUpdater {
        using Operation = ExtensionRoutineUpdateOperation;
        using Kind = Operation::Kind;
        struct Key { ObRoutineType type; std::string name; int64_t slot = 0; };
        struct NameLess {
          bool operator()(const Key &a, const Key &b) const {
            ObSchemaNameComparator comparator;
            if (a.type != b.type) return a.type < b.type;
            const int names = comparator.compare(ObString(a.name.size(), a.name.data()),
                ObString(b.name.size(), b.name.data()));
            return names != 0 ? names < 0 : a.slot < b.slot;
          }
        };
        struct Node {
          ObRoutineInfo routine_;
          RoutineIdReservation reservation_;
          RoutineVersionReservation version_reservation_;
          std::unique_ptr<NativeRoutineAclVersionReservation> owner_grant_;
          ObSArray<ObDependencyInfo> dependencies_;
          bool dependencies_loaded_ = false;
          bool member_ = false;
          bool published_ = false;
          bool altered_ = false;
        };
        struct Step {
          const Operation *operation_; Node *before_; Node *after_;
          std::unique_ptr<RoutineVersionReservation> deletion_;
          std::unique_ptr<NativeRoutineAclVersionReservation> acl_;
        };
      public:
        RoutineUpdater(const ObIArray<Operation> &operations, const ObSessionPrivInfo &priv,
                       const ObIArray<uint64_t> &roles, ObSchemaGetterGuard &guard,
                       ObDDLService &ddl, ObDDLSQLTransaction &transaction, IExtensionRoutineScript *script)
          : operations_(operations), priv_(priv), roles_(roles), guard_(guard), ddl_(ddl), transaction_(transaction),
            script_(script), count_(script ? script->statement_count() : operations.count()) {}

        int preflight(const ExtensionUpdateRequest &request, std::string &error) override {
          if (!priv_.is_valid()) return OB_ERR_NO_PRIVILEGE;
          if (request.requires_superuser_ && !(priv_.user_priv_set_ & OB_PRIV_SUPER)) return OB_ERR_NO_PRIVILEGE;
          if (request.tenant_id_ != 1) return OB_NOT_SUPPORTED;
          if (count_ < 0 || count_ > 4096) return OB_SIZE_OVERFLOW;
          if (script_ != nullptr) {
            if (!operations_.empty() || count_ != script_->statement_count()) return OB_INVALID_ARGUMENT;
            const int ret = script_->preflight(request, error);
            if (ret != OB_SUCCESS) return ret;
          }
          if (request.from_version_ == request.to_version_ && count_ != 0) {
            error = "same-version update must not discard a nonempty DDL plan";
            return OB_INVALID_ARGUMENT;
          }
          for (int64_t i = 0; i < operations_.count(); ++i) {
            const auto &op = operations_.at(i);
            if (!op.has_valid_shape()) return OB_INVALID_ARGUMENT;
            if (!op.is_schema_change()) {
              if (op.kind_ != Kind::GRANT && op.kind_ != Kind::REVOKE) return OB_NOT_SUPPORTED;
              continue; // Full object/actor/recipient admission needs the sequential view.
            }
            if (op.kind_ == Kind::DROP) {
              if (!op.drop_arg_->is_valid()) return OB_INVALID_ARGUMENT;
            } else if (!op.create_arg_->is_valid()) {
              return OB_INVALID_ARGUMENT;
            } else if (op.create_arg_->is_or_replace_ || op.create_arg_->with_if_not_exist_ ||
                       (op.kind_ == Kind::CREATE && op.create_arg_->is_need_alter_) ||
                       (op.kind_ == Kind::ALTER && !op.create_arg_->is_need_alter_)) {
              error = "routine update requires explicit CREATE, DROP or resolved MySQL ALTER";
              return OB_NOT_SUPPORTED;
            }
          }
          return OB_SUCCESS;
        }

        int admit(ObPluginSqlConnection &connection, const ExtensionUpdateRequest &request,
                  const ExtensionUpdateSnapshot &snapshot, std::string &error) override {
          admitted_ = false;
          active_.clear(); steps_.clear(); nodes_.clear(); acl_bases_.clear(); acl_groups_ = 0;
          int ret = preflight(request, error);
          const auto &spec = snapshot.installed_;
          const bool super = (priv_.user_priv_set_ & OB_PRIV_SUPER) != 0;
          if (OB_FAIL(ret)) {
          } else if (!transaction_.is_started() || !connection.is_in_transaction()) {
            ret = OB_STATE_NOT_MATCH;
          } else if (snapshot.extension_id_ != request.expected_extension_id_ ||
                     spec.database_id_ != request.database_id_ || spec.version_ != request.from_version_) {
            ret = OB_STATE_NOT_MATCH;
          } else if (priv_.user_id_ != spec.owner_id_ && !super) {
            ret = OB_ERR_NO_PRIVILEGE;
          } else if (OB_FAIL(guard_.get_database_schema(spec.database_id_, database_))) {
          } else if (database_ == nullptr || database_->is_in_recyclebin()) {
            ret = OB_ERR_BAD_DATABASE;
          } else if (!super && database_->is_read_only()) {
            ret = OB_ERR_DB_READ_ONLY;
          }
          if (OB_SUCC(ret) && count_ != 0) {
            const ObSysVarSchema *variable = nullptr;
            ObMalloc allocator(ObModIds::OB_TEMP_VARIABLES);
            ObObj value;
            if (OB_FAIL(guard_.get_system_variable(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, variable))) {}
            else if (variable == nullptr) ret = OB_ERR_UNEXPECTED;
            else if (OB_FAIL(variable->get_value(&allocator, nullptr, value))) {}
            else automatic_privileges_ = value.get_bool();
          }
          // Seed ALL members, including those untouched by this script. Only
          // CREATE produces new membership; modifying an ordinary external
          // routine does not silently adopt it into the Extension.
          for (const auto &member : spec.members_) {
            if (OB_FAIL(ret)) break;
            const ObRoutineInfo *routine = nullptr;
            Node *node = nullptr;
            if (member.object_class_ != static_cast<uint32_t>(ROUTINE_SCHEMA)) ret = OB_NOT_SUPPORTED;
            else if (OB_FAIL(guard_.get_routine_info(member.object_id_, routine))) {}
            else if (routine == nullptr || routine->get_database_id() != spec.database_id_ ||
                     !standalone(routine->get_routine_type())) ret = OB_STATE_NOT_MATCH;
            else if (OB_FAIL(copy_node(*routine, true, true, node))) {}
            else if (!active_.emplace(key(*routine), node).second) ret = OB_INVALID_DATA;
          }
          if (OB_SUCC(ret)) {
            privileges_ = std::make_shared<RoutinePrivilegeOverlay>(spec.database_id_, priv_.user_id_);
            overlay_ = std::make_shared<RoutineSchemaOverlay>(privileges_);
            ret = guard_.attach_routine_overlay(overlay_);
          }
          // Planning must not leak its final permissions into execution of an
          // earlier statement. Both successful and failed admission rewind to
          // the initial view; owned nodes/operations/reservations remain alive.
          RoutineCatalogSavepoint initial_view(overlay_, privileges_);
          if (OB_SUCC(ret) && !initial_view.valid()) ret = OB_STATE_NOT_MATCH;
          const auto admit_and_stage = [&](const Operation &op) {
            RoutineCatalogSavepoint view_savepoint(overlay_, privileges_);
            if (!view_savepoint.valid()) return OB_STATE_NOT_MATCH;
            int code = op.is_schema_change() ? admit_operation(connection, op, error) : admit_privilege(op);
            if (code == OB_SUCCESS && op.is_schema_change()) {
              const auto &step = steps_.back();
              if (step.after_ != nullptr) code = overlay_->stage(step.after_->routine_);
              else if (step.before_ != nullptr) code = overlay_->erase(
                  step.before_->routine_.get_database_id(), step.before_->routine_.get_routine_name(),
                  step.before_->routine_.get_routine_type(), step.before_->routine_.get_routine_id(),
                  step.before_->routine_.get_overload());
              if (code == OB_SUCCESS && op.kind_ == Kind::CREATE && step.after_ != nullptr)
                code = privileges_->record_create(step.after_->routine_, automatic_privileges_);
              else if (code == OB_SUCCESS && op.kind_ == Kind::DROP && step.before_ != nullptr)
                code = privileges_->record_drop(step.before_->routine_);
            }
            if (code == OB_SUCCESS) view_savepoint.release();
            return code;
          };
          if (OB_SUCC(ret) && script_ != nullptr) {
            ret = resolve_extension_routine_sequence(*script_, count_, guard_, admit_and_stage, error);
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < operations_.count(); ++i)
              ret = admit_and_stage(operations_.at(i));
          }
          size_t final_member_count = 0;
          for (const auto &entry : active_) {
            if (entry.second && entry.second->member_) ++final_member_count;
          }
          if (OB_SUCC(ret) && final_member_count > 4096) ret = OB_SIZE_OVERFLOW;
          // Validate incoming dependencies of the original objects being
          // removed. A same-name replacement has a NEW ID, not a repair of an
          // existing dependency. Removal of the dependent in this plan is OK.
          std::map<uint64_t, ObObjectType> removed;
          for (const auto &step : steps_) {
            if (step.operation_->kind_ == Kind::DROP && step.before_)
              removed.emplace(step.before_->routine_.get_routine_id(), step.before_->routine_.get_object_type());
          }
          for (const auto &item : removed) {
            if (OB_FAIL(ret)) break;
            ret = connection.query(
                "SELECT dep_obj_id,dep_obj_type FROM __all_dependency WHERE ref_obj_id=? "
                "AND ref_obj_type=? ORDER BY dep_obj_id,dep_obj_type FOR UPDATE",
                [&](ObPluginSqlBinder &b) {
                  int code = b.bind_int64(item.first);
                  if (OB_SUCCESS == code) code = b.bind_int64(static_cast<int64_t>(item.second));
                  return code;
                }, [&](ObPluginSqlRowReader &r) {
                  int64_t id = 0, type = 0;
                  int code = r.read_int64(0, id);
                  if (OB_SUCCESS == code) code = r.read_int64(1, type);
                  if (OB_SUCCESS != code) return code;
                  if (id <= 0 || type <= static_cast<int64_t>(ObObjectType::INVALID) ||
                      type >= static_cast<int64_t>(ObObjectType::MAX_TYPE)) return OB_INVALID_DATA;
                  auto found = removed.find(id);
                  if (found == removed.end() || static_cast<int64_t>(found->second) != type) {
                    error = "routine update cannot remove an object with a surviving schema dependent";
                    return OB_OP_NOT_ALLOW;
                  }
                  return OB_SUCCESS;
                });
          }
          // Newly resolved bodies must not reintroduce a reference to a removed
          // published identity. Rebinding provisional references is a resolver
          // responsibility, not a name substitution in the catalog adapter.
          for (const auto &step : steps_) {
            if (OB_FAIL(ret)) break;
            if (step.after_ && step.operation_->create_arg_ &&
                active_.at(key(step.after_->routine_)) == step.after_) {
              for (const auto &dep : step.after_->dependencies_) {
                const auto found = removed.find(dep.get_ref_obj_id());
                if (found != removed.end() && found->second == dep.get_ref_obj_type()) {
                  error = "resolved routine refers to an identity removed by the update";
                  ret = OB_OP_NOT_ALLOW;
                  break;
                }
              }
            }
          }
          if (OB_SUCC(ret)) ret = initial_view.rollback();
          if (OB_SUCC(ret)) admitted_ = true;
          return ret;
        }

        int apply(ObPluginSqlConnection &connection, const ExtensionUpdateRequest &request,
                  const ExtensionUpdateSnapshot &snapshot, std::vector<ExtensionMemberIdentity> &members,
                  std::string &error) override {
          UNUSED(error);
          members.clear();
          if (!admitted_ || snapshot.extension_id_ != request.expected_extension_id_ ||
              !transaction_.is_started() || !connection.is_in_transaction()) return OB_STATE_NOT_MATCH;
          admitted_ = false; // an admitted plan is single-use, even after failure
          RoutineCatalogSavepoint execution_view(overlay_, privileges_);
          if (!execution_view.valid()) return OB_STATE_NOT_MATCH;
          int ret = OB_SUCCESS;
          for (const auto &step : steps_) {
            const auto &op = *step.operation_;
            if (!op.is_schema_change()) {
              ret = apply_privilege(op, *step.acl_);
            } else if (op.kind_ == Kind::DROP) {
              if (step.before_) {
                ObErrorInfo errors = op.drop_arg_->error_info_;
                ret = ObPLDDLService::drop_routine(step.before_->routine_, errors,
                    &op.drop_arg_->ddl_stmt_str_, guard_, ddl_, &transaction_, step.deletion_.get());
              }
            } else {
              ObErrorInfo errors = op.create_arg_->error_info_;
              ObSArray<ObDependencyInfo> dependencies;
              if (OB_FAIL(dependencies.assign(step.after_->dependencies_))) {}
              else ret = ObPLDDLService::create_routine(step.after_->routine_,
                  step.before_ ? &step.before_->routine_ : nullptr, op.kind_ == Kind::ALTER,
                  errors, dependencies, &op.create_arg_->ddl_stmt_str_, guard_, ddl_, &transaction_,
                  op.kind_ == Kind::CREATE ? &step.after_->reservation_ : nullptr,
                  &step.after_->version_reservation_, step.after_->owner_grant_.get());
            }
            if (OB_FAIL(ret)) break;
            // Recreate only the prefix that has actually executed. DCL writers
            // publish their own revalidated after-images on this same view.
            if (op.is_schema_change()) {
              if (step.after_) ret = overlay_->stage(step.after_->routine_);
              else if (step.before_) ret = overlay_->erase(step.before_->routine_.get_database_id(),
                  step.before_->routine_.get_routine_name(), step.before_->routine_.get_routine_type(),
                  step.before_->routine_.get_routine_id(), step.before_->routine_.get_overload());
              if (OB_SUCC(ret) && op.kind_ == Kind::CREATE && step.after_)
                ret = privileges_->record_create(step.after_->routine_, automatic_privileges_);
              else if (OB_SUCC(ret) && op.kind_ == Kind::DROP && step.before_)
                ret = privileges_->record_drop(step.before_->routine_);
              if (OB_FAIL(ret)) break;
            }
          }
          if (OB_SUCC(ret)) {
            for (const auto &entry : active_) {
              const auto *node = entry.second;
              if (node && node->member_) {
                const uint64_t id = node->routine_.get_routine_id();
                if (id == 0 || id == OB_INVALID_ID) { ret = OB_ERR_UNEXPECTED; break; }
                members.push_back({static_cast<uint32_t>(ROUTINE_SCHEMA), id});
              }
            }
          }
          if (OB_FAIL(ret)) members.clear();
          else execution_view.release();
          return ret;
        }
      private:
        struct PrivilegeInput {
          const obcall::NativeRoutinePrivilegeTarget *target_ = nullptr;
          ObSEArray<uint64_t, 4> grantees_;
          ObPrivSet rights_ = 0;
          bool grant_ = false, option_ = false;
          NativeRoutineRevokePlan::Behavior behavior_ = NativeRoutineRevokePlan::Behavior::RESTRICT;
          const ObString *sql_ = nullptr;
        };
        int prepare_privilege(const Operation &op, PrivilegeInput &input) {
          if (!op.has_valid_shape() || (op.kind_ != Kind::GRANT && op.kind_ != Kind::REVOKE))
            return OB_INVALID_ARGUMENT;
          if (!transaction_.is_started() || !database_ || !overlay_ || !privileges_) return OB_STATE_NOT_MATCH;
          input.grant_ = op.kind_ == Kind::GRANT;
          input.target_ = input.grant_ ? &op.grant_arg_->native_target_ : &op.revoke_arg_->native_target_;
          const auto &target = *input.target_;
          if (!target.is_valid() || !target.resolved_ || target.actor_id_ != priv_.user_id_) return OB_ERR_NO_PRIVILEGE;
          if (target.routine_.get_database_id() != database_->get_database_id()) return OB_ERR_BAD_DATABASE;
          obcall::NativeRoutinePrivilegeTarget actor;
          int ret = actor.assign(target.routine_, target.signature_qualified_);
          if (OB_SUCC(ret)) ret = actor.bind_actor(priv_.user_id_, roles_);
          if (OB_FAIL(ret)) return ret;
          if (actor.enabled_roles_.count() != target.enabled_roles_.count()) return OB_ERR_NO_PRIVILEGE;
          for (int64_t i = 0; i < actor.enabled_roles_.count(); ++i)
            if (actor.enabled_roles_.at(i) != target.enabled_roles_.at(i)) return OB_ERR_NO_PRIVILEGE;
          if (input.grant_) {
            const auto &arg = *op.grant_arg_;
            input.rights_ = arg.priv_set_ & ~OB_PRIV_GRANT;
            input.option_ = (arg.priv_set_ & OB_PRIV_GRANT) || arg.option_ == GRANT_OPTION;
            input.sql_ = &arg.ddl_stmt_str_;
            if (!arg.is_valid() || !input.rights_ || (input.rights_ & ~(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE)) ||
                arg.hosts_.empty() || arg.hosts_.count() > 16384 || arg.users_passwd_.count() != arg.hosts_.count() * 2 ||
                !arg.remain_roles_.empty() || !arg.sys_priv_array_.empty() || !arg.obj_priv_array_.empty() ||
                !arg.column_names_priv_.empty() || !arg.ins_col_ids_.empty() || !arg.upd_col_ids_.empty() ||
                !arg.ref_col_ids_.empty() || !arg.sel_col_ids_.empty() || arg.option_ > GRANT_OPTION) return OB_INVALID_ARGUMENT;
            if (OB_FAIL(ddl_.check_parallel_ddl_conflict(guard_, arg)) ||
                OB_FAIL(target.revalidate(guard_, arg.db_, arg.table_, arg.object_id_))) return ret;
            for (int64_t i = 0; i < arg.hosts_.count(); ++i) {
              const ObUserInfo *user = nullptr;
              if (!arg.users_passwd_.at(i * 2 + 1).empty()) return OB_NOT_SUPPORTED;
              if (OB_FAIL(guard_.get_user_info(arg.users_passwd_.at(i * 2), arg.hosts_.at(i), user))) return ret;
              if (!user) return OB_USER_NOT_EXIST;
              if (OB_FAIL(input.grantees_.push_back(user->get_user_id()))) return ret;
            }
          } else {
            const auto &arg = *op.revoke_arg_;
            input.rights_ = arg.priv_set_; input.option_ = arg.grant_option_only_; input.sql_ = &arg.ddl_stmt_str_;
            input.behavior_ = arg.revoke_behavior_ == ObRevokeRoutineArg::REVOKE_CASCADE
                ? NativeRoutineRevokePlan::Behavior::CASCADE : NativeRoutineRevokePlan::Behavior::RESTRICT;
            if (!arg.is_valid() || arg.native_grantees_.empty() || !arg.priv_set_ ||
                (arg.priv_set_ & ~(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE)) ||
                !arg.obj_priv_array_.empty() || arg.revoke_all_ora_) return OB_INVALID_ARGUMENT;
            if (OB_FAIL(ddl_.check_parallel_ddl_conflict(guard_, arg)) ||
                OB_FAIL(target.revalidate(guard_, arg.db_, arg.routine_, arg.obj_id_)) ||
                OB_FAIL(input.grantees_.assign(arg.native_grantees_))) return ret;
          }
          for (const auto id : input.grantees_) {
            const ObUserInfo *user = nullptr;
            if (!id || id > INT64_MAX) return OB_INVALID_ARGUMENT;
            if (OB_FAIL(guard_.get_user_info(id, user))) return ret;
            if (!user) return OB_USER_NOT_EXIST;
          }
          return OB_SUCCESS;
        }
        int admit_privilege(const Operation &op) {
          PrivilegeInput input;
          int ret = prepare_privilege(op, input);
          if (OB_FAIL(ret)) return ret;
          const auto &target = *input.target_;
          const auto &expected = target.routine_;
          auto *schema = ddl_.get_schema_service().get_schema_service();
          if (!schema) return OB_NOT_INIT;
          auto found = acl_bases_.find(expected.get_routine_id());
          if (found == acl_bases_.end()) {
            auto base = std::make_unique<ObSArray<ObObjPriv>>();
            const ObRoutineInfo *published = &expected;
            // ALTER's current version is provisional. Lock against the
            // original published identity; a private CREATE has no SQL row yet.
            for (const auto &node : nodes_) if (node->routine_.get_routine_id() == expected.get_routine_id() &&
                !node->altered_) {
              published = node->published_ ? &node->routine_ : nullptr; break;
            }
            if (published && OB_FAIL(schema->get_priv_sql_service().read_native_routine_privileges(
                *published, transaction_, *base))) return ret;
            if (base->count() > 262144 - acl_groups_) return OB_SIZE_OVERFLOW;
            acl_groups_ += base->count();
            found = acl_bases_.emplace(expected.get_routine_id(), std::move(base)).first;
          }
          ObSEArray<ObObjPriv, 4> snapshot;
          if (OB_FAIL(privileges_->merge_object_snapshot(expected, *found->second, snapshot))) return ret;
          const ObUserInfo *user = nullptr;
          if (OB_FAIL(guard_.get_user_info(target.actor_id_, user))) return ret;
          if (!user) return OB_USER_NOT_EXIST;
          if (user->is_role()) return OB_INVALID_ARGUMENT;
          ObSessionPrivInfo actor;
          actor.user_id_ = user->get_user_id(); actor.user_name_ = user->get_user_name_str();
          actor.host_name_ = user->get_host_name_str();
          NativeRoutineGrantors grantors;
          if (OB_FAIL(guard_.select_native_routine_grantors(actor, target.enabled_roles_, expected,
              input.rights_, snapshot, grantors))) return ret;
          std::map<uint64_t, ObPrivSet> sources;
          if (input.rights_ & OB_PRIV_EXECUTE) sources[grantors.execute_] |= OB_PRIV_EXECUTE;
          if (input.rights_ & OB_PRIV_ALTER_ROUTINE) sources[grantors.alter_] |= OB_PRIV_ALTER_ROUTINE;
          std::set<uint64_t> recipients(input.grantees_.begin(), input.grantees_.end());
          if (recipients.size() * sources.size() > 16384) return OB_SIZE_OVERFLOW;
          ObSEArray<NativeRoutineAclChange, 4> changes;
          if (input.grant_) {
            ObSEArray<NativeRoutineGrantRequest, 4> requests;
            ObSEArray<NativeRoutineGrantDelta, 4> plan;
            for (const auto id : recipients) for (const auto &source : sources)
              if (OB_FAIL(requests.push_back({source.first, id, source.second, input.option_}))) return ret;
            if (OB_FAIL(NativeRoutineGrantPlan::build(expected, snapshot, requests, plan))) return ret;
            for (const auto &delta : plan)
              if (OB_FAIL(changes.push_back({delta.grantor_, delta.grantee_, delta.before_, delta.after_}))) return ret;
          } else {
            ObSEArray<NativeRoutineRevokeRequest, 4> requests;
            ObSEArray<NativeRoutineGrantRoot, 4> roots;
            ObSEArray<NativeRoutineRevokeDelta, 4> plan;
            for (const auto id : recipients) for (const auto &source : sources)
              if (OB_FAIL(requests.push_back({source.first, id, source.second, input.option_}))) return ret;
            if (OB_FAIL(NativeRoutineRevokePlan::collect_roots(guard_, expected, snapshot, roots)) ||
                OB_FAIL(NativeRoutineRevokePlan::build(expected, snapshot, roots, requests, input.behavior_, plan))) return ret;
            using AclKey = std::pair<uint64_t, uint64_t>;
            std::map<AclKey, ObPackedObjPriv> current;
            std::map<AclKey, NativeRoutineAclChange> complete;
            for (const auto &row : snapshot) if (row.get_col_id() == OBJ_LEVEL_FOR_TAB_PRIV)
              current[{row.get_grantee_id(), row.get_grantor_id()}] = row.get_obj_privs();
            for (const auto &request : requests) {
              const AclKey key{request.grantee_, request.grantor_};
              const auto entry = current.find(key);
              const auto bits = entry == current.end() ? ObPackedObjPriv{0} : entry->second;
              complete[key] = {request.grantor_, request.grantee_, bits, bits};
            }
            for (const auto &delta : plan) complete[{delta.grantee_, delta.grantor_}] =
                {delta.grantor_, delta.grantee_, delta.before_, delta.after_};
            if (complete.size() > 16384) return OB_SIZE_OVERFLOW;
            for (const auto &entry : complete) if (OB_FAIL(changes.push_back(entry.second))) return ret;
          }
          if (changes.count() > 262144 - acl_groups_) return OB_SIZE_OVERFLOW;
          auto reserved = std::make_unique<NativeRoutineAclVersionReservation>();
          if (OB_FAIL(NativeRoutineAclVersionReservation::reserve(ddl_.get_schema_service(), transaction_, target,
              input.grant_ ? NativeRoutineAclVersionReservation::Kind::GRANT : NativeRoutineAclVersionReservation::Kind::REVOKE,
              changes, *reserved))) return ret;
          for (int64_t i = 0; i < changes.count(); ++i) {
            const auto &change = changes.at(i);
            if (OB_FAIL(privileges_->record_object_change(expected, change.grantor_, change.grantee_,
                reserved->version_at(i), change.before_, change.after_, actor.user_id_))) return ret;
          }
          acl_groups_ += changes.count();
          steps_.push_back({&op, nullptr, nullptr, nullptr, std::move(reserved)});
          return OB_SUCCESS;
        }
        int apply_privilege(const Operation &op, NativeRoutineAclVersionReservation &reservation) {
          PrivilegeInput input;
          int ret = prepare_privilege(op, input);
          if (OB_FAIL(ret)) return ret;
          int64_t changed = 0;
          if (input.grant_) {
            NativeRoutineGrantWriter writer(ddl_.get_schema_service(), guard_, transaction_);
            ret = writer.grant(*input.target_, input.grantees_, input.rights_, input.option_, input.sql_, changed,
                overlay_, privileges_, &reservation);
          } else {
            NativeRoutineRevokeWriter writer(ddl_.get_schema_service(), guard_, transaction_);
            ret = writer.revoke(*input.target_, input.grantees_, input.rights_, input.option_, input.behavior_, input.sql_, changed,
                overlay_, privileges_, &reservation);
          }
          return ret;
        }
        static bool standalone(ObRoutineType type) {
          return type == ROUTINE_FUNCTION_TYPE || type == ROUTINE_PROCEDURE_TYPE;
        }
        static Key key(const ObRoutineInfo &routine) {
          const auto &name = routine.get_routine_name();
          return {routine.get_routine_type(), std::string(name.ptr(), name.length()), routine.get_overload()};
        }
        int copy_node(const ObRoutineInfo &routine, bool member, bool published, Node *&node) {
          auto owned = std::make_unique<Node>();
          int ret = owned->routine_.assign(routine);
          if (OB_SUCC(ret)) {
            owned->member_ = member; owned->published_ = published;
            node = owned.get(); nodes_.push_back(std::move(owned));
          }
          return ret;
        }
        int admit_operation(ObPluginSqlConnection &connection, const Operation &op, std::string &error) {
          if (!op.is_schema_change()) return OB_NOT_SUPPORTED;
          int ret = OB_SUCCESS;
          if (!op.has_valid_shape()) return OB_INVALID_ARGUMENT;
          const bool drop = op.kind_ == Kind::DROP;
          if (drop ? !op.drop_arg_->is_valid() : !op.create_arg_->is_valid()) return OB_INVALID_ARGUMENT;
          if (!drop && (op.create_arg_->is_or_replace_ || op.create_arg_->with_if_not_exist_ ||
              op.create_arg_->is_need_alter_ != (op.kind_ == Kind::ALTER))) return OB_NOT_SUPPORTED;
          if (op.kind_ == Kind::CREATE && op.create_arg_->error_info_.get_error_status() == ERROR_STATUS_HAS_ERROR) {
            error = "extension routine body contains unresolved compilation errors";
            return OB_ERR_RESOLVE_SQL;
          }
          const ObString &db = drop ? op.drop_arg_->db_name_ : op.create_arg_->db_name_;
          const ObString &name = drop ? op.drop_arg_->routine_name_ : op.create_arg_->routine_info_.get_routine_name();
          const auto type = drop ? op.drop_arg_->routine_type_ : op.create_arg_->routine_info_.get_routine_type();
          const ObDatabaseSchema *database = nullptr;
          if (!standalone(type)) return OB_NOT_SUPPORTED;
          if (OB_FAIL(guard_.get_database_schema(db, database))) return ret;
          if (database == nullptr || database->get_database_id() != database_->get_database_id()) return OB_ERR_BAD_DATABASE;
          ret = drop ? ddl_.check_parallel_ddl_conflict(guard_, *op.drop_arg_)
                     : ddl_.check_parallel_ddl_conflict(guard_, *op.create_arg_);
          if (OB_FAIL(ret)) return ret;
          ObArenaAllocator allocator;
          ObStmtNeedPrivs privileges(allocator);
          ObNeedPriv need;
          need.db_ = db; need.table_ = name;
          need.obj_type_ = type == ROUTINE_FUNCTION_TYPE ? ObObjectType::FUNCTION : ObObjectType::PROCEDURE;
          need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
          need.priv_set_ = op.kind_ == Kind::CREATE ? OB_PRIV_CREATE_ROUTINE : OB_PRIV_ALTER_ROUTINE;
          const ObRoutineInfo *native_target = drop
              ? (op.drop_arg_->native_target_resolved_ ? &op.drop_arg_->native_target_ : nullptr)
              : (op.kind_ == Kind::ALTER && op.create_arg_->routine_info_.is_native() ? &op.create_arg_->routine_info_ : nullptr);
          if (native_target && native_target->get_routine_id() != OB_INVALID_ID) {
            need.native_routine_id_ = native_target->get_routine_id();
            need.native_routine_version_ = native_target->get_schema_version();
          }
          if (op.kind_ == Kind::CREATE && op.create_arg_->routine_info_.is_native() &&
              !(priv_.user_priv_set_ & OB_PRIV_SUPER)) return OB_ERR_NO_PRIVILEGE;
          if (OB_FAIL(privileges.need_privs_.reserve(1))) return ret;
          if (OB_FAIL(privileges.need_privs_.push_back(need))) return ret;
          if (!(priv_.user_priv_set_ & OB_PRIV_SUPER) && OB_FAIL(guard_.verify_read_only(privileges))) return ret;
          // Permission checking precedes the resolved-miss no-op as well.
          if (OB_FAIL(guard_.check_priv(priv_, roles_, privileges))) return ret;
          if (drop && op.drop_arg_->native_target_resolved_ &&
              op.drop_arg_->native_target_.get_routine_id() == OB_INVALID_ID) {
            steps_.push_back({&op, nullptr, nullptr, nullptr, nullptr});
            return OB_SUCCESS;
          }
          int64_t slot = drop ? (op.drop_arg_->native_target_resolved_
              ? op.drop_arg_->native_target_.get_overload() : 0) : op.create_arg_->routine_info_.get_overload();
          const bool new_native = op.kind_ == Kind::CREATE && op.create_arg_->routine_info_.is_native();
          if (new_native && OB_FAIL(NativeRoutineCreateSlot::select(guard_, op.create_arg_->routine_info_, slot))) return ret;
          if (op.kind_ == Kind::CREATE && !new_native && type == ROUTINE_FUNCTION_TYPE) {
            ObSEArray<const ObRoutineInfo *, 4> family;
            if (OB_FAIL(guard_.get_standalone_function_infos(database_->get_database_id(), name, family))) return ret;
            if (!family.empty()) return OB_ERR_SP_ALREADY_EXISTS;
          }
          Key object_key{type, std::string(name.ptr(), name.length()), slot};
          auto found = active_.find(object_key);
          if (found == active_.end()) {
            const ObRoutineInfo *published = nullptr;
            if (new_native) {
              // Signature admission already checked the complete current
              // family. A new slot has no published object at slot zero.
            } else if (drop && op.drop_arg_->native_target_resolved_) {
              ret = guard_.get_routine_info(op.drop_arg_->native_target_.get_routine_id(), published);
            } else if (native_target) {
              ret = guard_.get_routine_info(native_target->get_routine_id(), published);
            } else {
              ret = type == ROUTINE_FUNCTION_TYPE
                  ? guard_.get_standalone_function_info(database_->get_database_id(), name, published)
                  : guard_.get_standalone_procedure_info(database_->get_database_id(), name, published);
            }
            if (OB_FAIL(ret)) return ret;
            Node *node = nullptr;
            if (published && OB_FAIL(copy_node(*published, false, true, node))) return ret;
            found = active_.emplace(std::move(object_key), node).first;
          }
          Node *before = found->second, *after = nullptr;
          // Use the same explicit transaction-local grants as Query resolution;
          // merely owning a provisional schema must not bypass authorization.
          if (drop && op.drop_arg_->native_target_resolved_) {
            if (OB_FAIL(op.drop_arg_->check_native_target(before ? &before->routine_ : nullptr,
                database_->get_database_id()))) return ret;
          }
          if (op.kind_ == Kind::CREATE) {
            if (before) return OB_ERR_SP_ALREADY_EXISTS;
            if (op.create_arg_->routine_info_.get_owner_id() != priv_.user_id_) return OB_ERR_NO_PRIVILEGE;
            if (OB_FAIL(copy_node(op.create_arg_->routine_info_, true, false, after))) return ret;
            if (OB_FAIL(after->dependencies_.assign(op.create_arg_->dependency_infos_))) return ret;
            after->dependencies_loaded_ = true;
            after->routine_.set_database_id(database_->get_database_id());
            after->routine_.set_routine_id(OB_INVALID_ID);
            after->routine_.set_overload(slot);
            auto *schema = ddl_.get_schema_service().get_schema_service();
            if (schema == nullptr) return OB_ERR_UNEXPECTED;
            if (OB_FAIL(RoutineIdReservation::reserve(*schema, after->routine_, after->reservation_))) return ret;
            after->routine_.set_routine_id(after->reservation_.id());
            if (OB_FAIL(RoutineVersionReservation::reserve(ddl_.get_schema_service(), transaction_,
                after->routine_, nullptr, after->version_reservation_))) return ret;
            after->routine_.set_schema_version(after->version_reservation_.version());
            if (after->routine_.is_native() && automatic_privileges_) {
              after->owner_grant_ = std::make_unique<NativeRoutineAclVersionReservation>();
              if (OB_FAIL(NativeRoutineAclVersionReservation::reserve_create_owner(
                  ddl_.get_schema_service(), transaction_, after->routine_, *after->owner_grant_))) return ret;
            }
          } else if (!before) {
            if (!drop || !op.drop_arg_->if_exist_) return OB_ERR_SP_DOES_NOT_EXIST;
          } else if (drop) {
            if (!before->member_ && before->published_) {
              // Never detach another Extension's protection. Run the normal
              // membership check now, before this update detaches its own set.
              ret = connection.query(
                  "SELECT extension_id FROM __all_extension_member WHERE tenant_id=1 "
                  "AND database_id=? AND object_class=? AND object_id=? FOR UPDATE",
                  [&](ObPluginSqlBinder &b) {
                    int code = b.bind_int64(database_->get_database_id());
                    if (OB_SUCCESS == code) code = b.bind_int64(static_cast<int64_t>(ROUTINE_SCHEMA));
                    if (OB_SUCCESS == code) code = b.bind_int64(before->routine_.get_routine_id());
                    return code;
                  }, [&](ObPluginSqlRowReader &r) {
                    int64_t owner = 0;
                    int code = r.read_int64(0, owner);
                    if (OB_SUCCESS != code) return code;
                    error = "routine belongs to another Extension";
                    return owner > 0 ? OB_OP_NOT_ALLOW : OB_INVALID_DATA;
                  });
              if (OB_FAIL(ret)) return ret;
            }
          } else {
            const auto &replacement = op.create_arg_->routine_info_;
            if (replacement.get_routine_id() != before->routine_.get_routine_id() ||
                replacement.get_owner_id() != before->routine_.get_owner_id()) return OB_STATE_NOT_MATCH;
            if (script_ == nullptr && (!before->published_ || before->altered_)) {
              error = "ALTER of a new or already altered routine requires transaction-aware semantic resolution";
              return OB_NOT_SUPPORTED;
            }
            if (script_ != nullptr && replacement.get_schema_version() != before->routine_.get_schema_version())
              return OB_STATE_NOT_MATCH;
            // MySQL ALTER changes attributes, not the body. Its resolver does
            // not rebuild dependency_infos_; treating an empty array as a new
            // body would erase the existing dependency graph on replacement.
            if (!before->dependencies_loaded_) {
              if (!before->published_) return OB_ERR_UNEXPECTED;
              if (OB_FAIL(ObDependencyInfo::collect_ref_infos(before->routine_.get_routine_id(),
                  transaction_, before->dependencies_))) return ret;
              before->dependencies_loaded_ = true;
            }
            if (OB_FAIL(copy_node(replacement, before->member_, before->published_, after))) return ret;
            if (OB_FAIL(after->dependencies_.assign(before->dependencies_))) return ret;
            after->dependencies_loaded_ = true;
            after->routine_.set_database_id(database_->get_database_id());
            after->altered_ = true;
            if (OB_FAIL(RoutineVersionReservation::reserve(ddl_.get_schema_service(), transaction_,
                after->routine_, &before->routine_, after->version_reservation_))) return ret;
            after->routine_.set_schema_version(after->version_reservation_.version());
          }
          std::unique_ptr<RoutineVersionReservation> deletion;
          if (after != nullptr) {
            // Match add_routine_params' eventual identity/version stamping now,
            // before a following statement can borrow this complete schema.
            auto &parameters = after->routine_.get_routine_params();
            for (int64_t i = 0; i < parameters.count(); ++i) {
              if (parameters.at(i) == nullptr) return OB_ERR_UNEXPECTED;
              parameters.at(i)->set_routine_id(after->routine_.get_routine_id());
              parameters.at(i)->set_schema_version(after->routine_.get_schema_version());
            }
          }
          if (drop && before) {
            deletion = std::make_unique<RoutineVersionReservation>();
            if (OB_FAIL(RoutineVersionReservation::reserve_drop(ddl_.get_schema_service(), transaction_,
                before->routine_, *deletion))) return ret;
          }
          steps_.push_back({&op, before, after, std::move(deletion), nullptr});
          found->second = after; // retain a tombstone: never reload a dropped name
          return OB_SUCCESS;
        }
        const ObIArray<Operation> &operations_;
        const ObSessionPrivInfo &priv_;
        const ObIArray<uint64_t> &roles_;
        ObSchemaGetterGuard &guard_;
        ObDDLService &ddl_;
        ObDDLSQLTransaction &transaction_;
        IExtensionRoutineScript *script_;
        const int64_t count_;
        std::shared_ptr<RoutineSchemaOverlay> overlay_;
        std::shared_ptr<RoutinePrivilegeOverlay> privileges_;
        const ObDatabaseSchema *database_ = nullptr;
        std::vector<std::unique_ptr<Node>> nodes_;
        std::vector<Step> steps_;
        std::map<Key, Node *, NameLess> active_;
        std::map<uint64_t, std::unique_ptr<ObSArray<ObObjPriv>>> acl_bases_;
        int64_t acl_groups_ = 0; // Shared bound for retained base rows and reserved ACL groups.
        bool admitted_ = false;
        bool automatic_privileges_ = false;
      };
  return std::make_unique<RoutineUpdater>(operations, session_priv, enabled_roles, guard, ddl_service, transaction, script);
#endif
}

int ObPLDDLService::update_routines_extension(
    const share::plugin::ExtensionUpdateRequest &request,
    const ObIArray<share::plugin::ExtensionRoutineUpdateOperation> &operations,
    const ObSessionPrivInfo &session_priv, const ObIArray<uint64_t> &enabled_roles,
    share::plugin::IExtensionCatalogUpdater &catalog, ObDDLService &ddl_service,
    uint64_t &extension_id, bool &changed, int &publication_status, std::string &error,
    share::plugin::IExtensionRoutineScript *script)
{
  extension_id = 0; changed = false; publication_status = OB_NOT_INIT; error.clear();
#if !defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  UNUSEDx(request, operations, session_priv, enabled_roles, catalog, ddl_service, script);
  return OB_NOT_SUPPORTED;
#else
  int ret = OB_SUCCESS;
  try {
    ObSchemaGetterGuard guard;
    int64_t schema_version = 0;
    if (OB_FAIL(ddl_service.check_inner_stat())) {
    } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(guard))) {
    } else if (OB_FAIL(guard.get_schema_version(schema_version))) {
    } else {
      ObDDLSQLTransaction transaction(&ddl_service.get_schema_service());
      auto updater = make_routine_extension_updater(operations, session_priv, enabled_roles, guard, ddl_service, transaction, script);
      if (!updater) ret = OB_NOT_SUPPORTED;
      else ret = catalog.update_extension(request, *updater, extension_id, changed, error, &transaction, schema_version);
      if (OB_SUCC(ret)) {
        if (changed) {
          try { publication_status = ddl_service.publish_schema(); }
          catch (const std::bad_alloc &) { publication_status = OB_ALLOCATE_MEMORY_FAILED; }
          catch (...) { publication_status = OB_ERR_UNEXPECTED; }
        } else publication_status = OB_SUCCESS;
      }
    }
  } catch (const std::bad_alloc &) {
    if (extension_id != 0) publication_status = OB_ALLOCATE_MEMORY_FAILED;
    else ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    if (extension_id != 0) publication_status = OB_ERR_UNEXPECTED;
    else ret = OB_ERR_UNEXPECTED;
  }
  return ret;
#endif
}

int ObPLDDLService::create_routine(const obcall::ObCreateRoutineArg &arg,
                                   rootserver::ObDDLService &ddl_service,
                                   ObDDLSQLTransaction *external_trans,
                                   uint64_t *created_routine_id)
{
  int ret = OB_SUCCESS;
  if (nullptr != created_routine_id) *created_routine_id = OB_INVALID_ID;
  ObSchemaGetterGuard schema_guard;
  if (nullptr != external_trans && (!external_trans->is_started() || external_trans->is_enable_parallel())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else {
    ObRoutineInfo routine_info = arg.routine_info_;
    const ObRoutineInfo* old_routine_info = NULL;
    
    ObString database_name = arg.db_name_;
    bool is_or_replace = arg.is_need_alter_;
    bool is_inner = arg.is_or_replace_;
    const ObDatabaseSchema *db_schema = NULL;
    if (OB_FAIL(schema_guard.get_database_schema(database_name, db_schema))) {
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
    } else if (!is_inner && db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
    } else {
      routine_info.set_database_id(db_schema->get_database_id());
    }
    bool exist = false;
    if (OB_SUCC(ret)) {
      if (is_or_replace && routine_info.is_native()) {
        // ALTER already resolved and dependency-pinned an exact object. Do
        // not re-resolve its name at slot zero and replace another overload.
        // The routine declaration wire itself omits its schema version; the
        // DDL dependency fence is the authoritative version across RPC.
        int64_t target_version = OB_INVALID_VERSION;
        for (int64_t i = 0; OB_SUCC(ret) && i < arg.based_schema_object_infos_.count(); ++i) {
          const auto &dependency = arg.based_schema_object_infos_.at(i);
          if (dependency.schema_type_ == ROUTINE_SCHEMA && dependency.schema_id_ == routine_info.get_routine_id()) {
            if (target_version != OB_INVALID_VERSION && target_version != dependency.schema_version_)
              ret = OB_STATE_NOT_MATCH;
            target_version = dependency.schema_version_;
          }
        }
        if (OB_SUCC(ret) && (target_version <= 0 ||
            (routine_info.get_schema_version() > 0 && routine_info.get_schema_version() != target_version)))
          ret = OB_STATE_NOT_MATCH;
        if (OB_SUCC(ret)) routine_info.set_schema_version(target_version);
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(schema_guard.get_routine_info(routine_info.get_routine_id(), old_routine_info))) {
        } else if (old_routine_info == nullptr) {
          ret = OB_ERR_SP_DOES_NOT_EXIST;
        } else if (old_routine_info->get_database_id() != routine_info.get_database_id()
                   || old_routine_info->get_overload() != routine_info.get_overload()
                   || old_routine_info->get_schema_version() != routine_info.get_schema_version()
                   || old_routine_info->get_owner_id() != routine_info.get_owner_id()
                   || old_routine_info->get_routine_name() != routine_info.get_routine_name()
                   || !NativeRoutineAdmission::same_signature(*old_routine_info, routine_info)) {
          ret = OB_STATE_NOT_MATCH;
        } else {
          exist = true;
        }
      } else if (routine_info.is_native()) {
        ret = NativeRoutineCreateSlot::assign(schema_guard, routine_info);
      } else if (routine_info.get_routine_type() == ROUTINE_PROCEDURE_TYPE) {
        if (OB_FAIL(schema_guard.check_standalone_procedure_exist(db_schema->get_database_id(),
                                                                  routine_info.get_routine_name(), exist))) {
        } else if (exist && !is_or_replace) {
          ret = OB_ERR_SP_ALREADY_EXISTS;
          LOG_USER_ERROR(OB_ERR_SP_ALREADY_EXISTS, "PROCEDURE",
                          routine_info.get_routine_name().length(), routine_info.get_routine_name().ptr());
        } else if (exist && is_or_replace) {
          if (OB_FAIL(schema_guard.get_standalone_procedure_info(db_schema->get_database_id(),
                                                                  routine_info.get_routine_name(), old_routine_info))) {
          } else if (OB_ISNULL(old_routine_info)) {
            ret = OB_ERR_UNEXPECTED;
          }
        }
      } else {
        ObSEArray<const ObRoutineInfo *, 4> family;
        if (OB_FAIL(schema_guard.get_standalone_function_infos(db_schema->get_database_id(),
                                                               routine_info.get_routine_name(), family))) {
        } else {
          exist = !family.empty();
        }
        if (OB_FAIL(ret)) {
        } else if (exist && !is_or_replace) {
          ret = OB_ERR_SP_ALREADY_EXISTS;
          LOG_USER_ERROR(OB_ERR_SP_ALREADY_EXISTS, "FUNCTION",
                          routine_info.get_routine_name().length(), routine_info.get_routine_name().ptr());
        } else if (exist && is_or_replace) {
          if (family.count() != 1 || family.at(0)->is_native()) ret = OB_NOT_SUPPORTED;
          else old_routine_info = family.at(0);
        }
      }
      if (OB_SUCC(ret)) {
        ObErrorInfo error_info = arg.error_info_;
        ObSArray<ObDependencyInfo> &dep_infos = const_cast<ObSArray<ObDependencyInfo> &>(arg.dependency_infos_);
        if (OB_FAIL(create_routine(routine_info,
                                   old_routine_info,
                                   (exist && is_or_replace),
                                   error_info,
                                   dep_infos,
                                   &arg.ddl_stmt_str_,
                                   schema_guard,
                                   ddl_service,
                                   external_trans))) {
        } else if (nullptr != created_routine_id) {
          *created_routine_id = routine_info.get_routine_id();
        }
      }
    }
  }
  return ret;
}

int ObPLDDLService::create_routine(ObRoutineInfo &routine_info,
                                   const ObRoutineInfo* old_routine_info,
                                   bool replace,
                                   ObErrorInfo &error_info,
                                   ObIArray<ObDependencyInfo> &dep_infos,
                                   const ObString *ddl_stmt_str,
                                   share::schema::ObSchemaGetterGuard &schema_guard,
                                   rootserver::ObDDLService &ddl_service,
                                   ObDDLSQLTransaction *external_trans,
                                   RoutineIdReservation *reservation,
                                   RoutineVersionReservation *version_reservation,
                                   NativeRoutineAclVersionReservation *owner_grant)
{
  int ret = OB_SUCCESS;
  CK((replace && OB_NOT_NULL(old_routine_info)) || (!replace && OB_ISNULL(old_routine_info)));
  CK(reservation == nullptr || (!replace && external_trans != nullptr && external_trans->is_started()));
  CK(version_reservation == nullptr || (external_trans != nullptr && external_trans->is_started()));
  CK(owner_grant == nullptr || (!replace && external_trans != nullptr && external_trans->is_started()));
  CK (OB_NOT_NULL(ddl_service.schema_service_) && OB_NOT_NULL(ddl_service.sql_proxy_));
  // The old reserved-ACL cleanup rejected parallel Root transactions. Preserve
  // that restriction at the owner boundary now that the writer is generic.
  if (OB_SUCC(ret) && !replace && version_reservation != nullptr && external_trans->is_enable_parallel())
    ret = OB_STATE_NOT_MATCH;
  if (OB_SUCC(ret)) {
    
    ObDDLSQLTransaction local_trans(ddl_service.schema_service_);
    ObDDLSQLTransaction &trans = nullptr == external_trans ? local_trans : *external_trans;

    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (nullptr == external_trans && OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    }
    if (OB_SUCC(ret)) {
      RoutineCatalogWriter writer(*ddl_service.schema_service_, *ddl_service.sql_proxy_, schema_guard,
                                  trans, nullptr != external_trans);
      ret = writer.create(routine_info, old_routine_info, error_info, dep_infos, ddl_stmt_str,
                          reservation, version_reservation, owner_grant);
    }
    if (nullptr == external_trans && trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret) && nullptr == external_trans) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::alter_routine(const obcall::ObCreateRoutineArg &arg,
                                  rootserver::ObDDLService &ddl_service,
                                  ObDDLSQLTransaction *external_trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (nullptr != external_trans && (!external_trans->is_started() || external_trans->is_enable_parallel())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else {
    ObErrorInfo error_info = arg.error_info_;
    const ObRoutineInfo *routine_info = NULL;
    
    if (OB_FAIL(schema_guard.get_routine_info( arg.routine_info_.get_routine_id(), routine_info))) {
    } else if (OB_ISNULL(routine_info)) {
      ret = OB_ERR_SP_DOES_NOT_EXIST;
    }
    if (OB_FAIL(ret)) {
    } else if (arg.is_need_alter_) {
      if (OB_FAIL(create_routine(arg, ddl_service, external_trans))) {
      }
    } else {
      if (OB_FAIL(alter_routine(*routine_info, error_info, &arg.ddl_stmt_str_, schema_guard,
                                ddl_service, external_trans))) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::alter_routine(const ObRoutineInfo &routine_info,
                                  ObErrorInfo &error_info,
                                  const ObString *ddl_stmt_str,
                                  share::schema::ObSchemaGetterGuard &schema_guard,
                                  rootserver::ObDDLService &ddl_service,
                                  ObDDLSQLTransaction *external_trans)
{
  int ret = OB_SUCCESS;
  if (nullptr != external_trans && (!external_trans->is_started() || external_trans->is_enable_parallel())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    
    ObDDLSQLTransaction owned_trans(ddl_service.schema_service_);
    ObDDLSQLTransaction &trans = nullptr == external_trans ? owned_trans : *external_trans;
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (nullptr == external_trans && OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    } else {
      RoutineCatalogWriter writer(*ddl_service.schema_service_, *ddl_service.sql_proxy_, schema_guard,
                                  trans, nullptr != external_trans);
      ret = writer.alter(routine_info, error_info, ddl_stmt_str);
    }
    if (nullptr == external_trans && trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed!", K(ret), K(temp_ret));
        ret = OB_SUCCESS == ret ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret) && nullptr == external_trans) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_routine(const ObDropRoutineArg &arg,
                                 rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    
    const ObString &db_name = arg.db_name_;
    const ObString &routine_name = arg.routine_name_;
    ObRoutineType routine_type = arg.routine_type_;
    ObSchemaGetterGuard schema_guard;
    const ObDatabaseSchema *db_schema = NULL;
    /*!
     * Compatible with MySQL behavior:
     * create database test;
     * use test;
     * drop database test;
     * drop function if exists no_such_func; -- warning 1035
     * drop procedure if exists no_such_proc; -- error 1046
     * drop function no_such_func; --error 1035
     * drop procedure no_such_proc; --error 1046
     */
    if (db_name.empty()) {
      ret = OB_ERR_NO_DB_SELECTED;
    } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
    } else if (OB_FAIL(ddl_service.check_parallel_ddl_conflict(schema_guard, arg))) {
    } else if (OB_FAIL(schema_guard.get_database_schema( db_name, db_schema))) {
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, db_name.length(), db_name.ptr());
    } else if (db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
    }

    if (OB_SUCC(ret)) {
      bool exist = false;
      const ObRoutineInfo *routine_info = NULL;
      if (arg.native_target_resolved_) {
        if (arg.native_target_.get_routine_id() != OB_INVALID_ID) {
          ret = schema_guard.get_routine_info(arg.native_target_.get_routine_id(), routine_info);
        }
        if (OB_SUCC(ret)) ret = arg.check_native_target(routine_info, db_schema->get_database_id());
      } else if (ROUTINE_PROCEDURE_TYPE == routine_type) {
        if (OB_FAIL(schema_guard.check_standalone_procedure_exist(db_schema->get_database_id(),
                                                                  routine_name, exist))) {
        } else if (exist) {
          if (OB_FAIL(schema_guard.get_standalone_procedure_info(db_schema->get_database_id(),
                                                                 routine_name, routine_info))) {
          }
        } else if (!arg.if_exist_) {
          ret = OB_ERR_SP_DOES_NOT_EXIST;
          LOG_USER_ERROR(OB_ERR_SP_DOES_NOT_EXIST, "PROCEDURE", db_name.length(), db_name.ptr(),
                         routine_name.length(), routine_name.ptr());
        }
      } else {
        ObSEArray<const ObRoutineInfo *, 4> family;
        if (OB_FAIL(schema_guard.get_standalone_function_infos(db_schema->get_database_id(), routine_name, family))) {
        } else if (!family.empty()) {
          // A legacy/name-only request must not select an arbitrary native
          // overload, including after DROP+CREATE changed the name's kind.
          if (family.count() != 1 || family.at(0)->is_native()) ret = OB_SCHEMA_EAGAIN;
          else routine_info = family.at(0);
        } else if (!arg.if_exist_) {
          ret = OB_ERR_SP_DOES_NOT_EXIST;
          LOG_USER_ERROR(OB_ERR_SP_DOES_NOT_EXIST, "FUNCTION", db_name.length(), db_name.ptr(),
                         routine_name.length(), routine_name.ptr());
        }
      }

      if (OB_SUCC(ret) && !OB_ISNULL(routine_info)) {
        ObErrorInfo error_info = arg.error_info_;
        if (OB_FAIL(drop_routine(*routine_info,
                                 error_info,
                                 &arg.ddl_stmt_str_,
                                 schema_guard,
                                 ddl_service))) {
        }
      }
    }
    if (OB_ERR_NO_DB_SELECTED == ret && ROUTINE_FUNCTION_TYPE == routine_type) {
      if (arg.if_exist_) {
        ret = OB_SUCCESS;
        LOG_USER_WARN(OB_ERR_SP_DOES_NOT_EXIST, "FUNCTION (UDF)",
                      db_name.length(), db_name.ptr(),
                      routine_name.length(), routine_name.ptr());
      } else {
        ret = OB_ERR_SP_DOES_NOT_EXIST;
        LOG_USER_ERROR(OB_ERR_SP_DOES_NOT_EXIST, "FUNCTION (UDF)",
                      db_name.length(), db_name.ptr(),
                      routine_name.length(), routine_name.ptr());
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_routine(const ObRoutineInfo &routine_info,
                                 ObErrorInfo &error_info,
                                 const ObString *ddl_stmt_str,
                                 share::schema::ObSchemaGetterGuard &schema_guard,
                                 rootserver::ObDDLService &ddl_service,
                                 ObDDLSQLTransaction *external_trans,
                                 RoutineVersionReservation *version_reservation)
{
  int ret = OB_SUCCESS;
  if (version_reservation != nullptr && external_trans == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr != external_trans && (!external_trans->is_started() || external_trans->is_enable_parallel())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    
    ObDDLSQLTransaction owned_trans(ddl_service.schema_service_);
    ObDDLSQLTransaction &trans = nullptr == external_trans ? owned_trans : *external_trans;
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (nullptr == external_trans && OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    } else {
      RoutineCatalogWriter writer(*ddl_service.schema_service_, *ddl_service.sql_proxy_, schema_guard,
                                  trans, nullptr != external_trans);
      struct LegacyInvalidation final : IRoutineCacheInvalidation {
        ObMultiVersionSchemaService &service;
        ObDDLSQLTransaction *transaction;
        LegacyInvalidation(ObMultiVersionSchemaService &service, ObDDLSQLTransaction *transaction)
            : service(service), transaction(transaction) {}
        int on_drop(uint64_t id, uint64_t database) override {
          return transaction ? transaction->record_routine_invalidation(id, database)
              : pl::ObPLCacheMgr::flush_pl_cache_by_sql(id, database, service);
        }
      } invalidation(*ddl_service.schema_service_, external_trans);
      ret = writer.drop(routine_info, error_info, ddl_stmt_str, invalidation, version_reservation);
    }
    if (nullptr == external_trans && trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }

    if (OB_SUCC(ret) && nullptr == external_trans) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

//----Functions for managing package----
int ObPLDDLService::create_package(const obcall::ObCreatePackageArg &arg,
                                    rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else {
    ObPackageInfo new_package_info;
    const ObPackageInfo *old_package_info = NULL;
    
    ObString database_name = arg.db_name_;
    const ObDatabaseSchema *db_schema = NULL;
    if (OB_FAIL(new_package_info.assign(arg.package_info_))) {
    } else if (OB_FAIL(schema_guard.get_database_schema( database_name, db_schema))) {
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
    } else if (db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
    } else {
      new_package_info.set_database_id(db_schema->get_database_id());
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_guard.get_package_info( db_schema->get_database_id(), new_package_info.get_package_name(),
                                                new_package_info.get_type(),
                                                old_package_info))) {
      } else if (OB_ISNULL(old_package_info) || arg.is_replace_) {
        bool need_create = true;
        // For system packages, to avoid multiple rebuilds, compare the new system package with the existing system package to see if they are the same
        if (OB_NOT_NULL(old_package_info)) {
          if (old_package_info->get_source().length() == new_package_info.get_source().length()
              && (0 == MEMCMP(old_package_info->get_source().ptr(),
                              new_package_info.get_source().ptr(),
                              old_package_info->get_source().length()))
              && old_package_info->get_exec_env() == new_package_info.get_exec_env()) {
            need_create = false;
            LOG_INFO("do not recreate package with same source",
                     K(ret),
                     K(old_package_info->get_source()),
                     K(new_package_info.get_source()), K(need_create));
          } else {
            LOG_INFO("recreate package with diff source",
                     K(ret),
                     K(old_package_info->get_source()),
                     K(new_package_info.get_source()), K(need_create));
          }
        }
        if (need_create) {
          ObSArray<ObRoutineInfo> &public_routine_infos = const_cast<ObSArray<ObRoutineInfo> &>(arg.public_routine_infos_);
          ObErrorInfo error_info = arg.error_info_;
          ObSArray<ObDependencyInfo> &dep_infos =
                               const_cast<ObSArray<ObDependencyInfo> &>(arg.dependency_infos_);
          if (OB_FAIL(create_package(schema_guard,
                                     old_package_info,
                                     new_package_info,
                                     public_routine_infos,
                                     error_info,
                                     dep_infos,
                                     &arg.ddl_stmt_str_,
                                     ddl_service))) {
          }
        }
      } else {
        ret = OB_ERR_PACKAGE_ALREADY_EXISTS;
        const char *type = (new_package_info.get_type() == ObPackageType::PACKAGE_TYPE ? "PACKAGE" : "PACKAGE BODY");
        LOG_USER_ERROR(OB_ERR_PACKAGE_ALREADY_EXISTS, type,
                       database_name.length(), database_name.ptr(),
                       new_package_info.get_package_name().length(), new_package_info.get_package_name().ptr());
      }
    }
  }
  return ret;
}

int ObPLDDLService::create_package(ObSchemaGetterGuard &schema_guard,
                                   const ObPackageInfo *old_package_info,
                                   ObPackageInfo &new_package_info,
                                   ObIArray<ObRoutineInfo> &public_routine_infos,
                                   ObErrorInfo &error_info,
                                   ObIArray<ObDependencyInfo> &dep_infos,
                                   const ObString *ddl_stmt_str,
                                   rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    } else if (OB_FAIL(pl_operator.create_package(old_package_info,
                                                   new_package_info,
                                                   trans,
                                                   schema_guard,
                                                   public_routine_infos,
                                                   error_info,
                                                   dep_infos,
                                                   ddl_stmt_str))) {
    }
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_package(const obcall::ObDropPackageArg &arg,
                                 rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else {
    
    const ObString &db_name = arg.db_name_;
    const ObString &package_name = arg.package_name_;
    ObPackageType package_type = arg.package_type_;
    const ObDatabaseSchema *db_schema = NULL;
    if (OB_FAIL(schema_guard.get_database_schema( db_name, db_schema))) {
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, db_name.length(), db_name.ptr());
    } else if (db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
    }
    if (OB_SUCC(ret)) {
      bool exist = false;
      if (OB_FAIL(schema_guard.check_package_exist(db_schema->get_database_id(),
          package_name, package_type, exist))) {
      } else if (exist) {
        const ObPackageInfo *package_info = NULL;
        ObErrorInfo error_info = arg.error_info_;
        if (OB_FAIL(schema_guard.get_package_info(db_schema->get_database_id(), package_name, package_type, package_info))) {
        } else if (OB_FAIL(drop_package(schema_guard,
                                        *package_info,
                                        error_info,
                                        &arg.ddl_stmt_str_,
                                        ddl_service))) {
        }
      } else {
        ret = OB_ERR_PACKAGE_DOSE_NOT_EXIST;
        const char *type = (package_type == ObPackageType::PACKAGE_TYPE ? "PACKAGE" : "PACKAGE BODY");
        LOG_USER_ERROR(OB_ERR_PACKAGE_DOSE_NOT_EXIST, type,
                       db_name.length(), db_name.ptr(),
                       package_name.length(), package_name.ptr());
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_package(share::schema::ObSchemaGetterGuard &schema_guard,
                                 const ObPackageInfo &package_info,
                                 ObErrorInfo &error_info,
                                 const ObString *ddl_stmt_str,
                                 rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    } else if (OB_FAIL(pl_operator.drop_package(package_info,
                                                 trans,
                                                 schema_guard,
                                                 error_info,
                                                 ddl_stmt_str))) {
    }
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}
//----End of functions for managing package----

//----Functions for managing trigger----
int ObPLDDLService::create_trigger(const obcall::ObCreateTriggerArg &arg,
                                    obcall::ObCreateTriggerRes *res,
                                    rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else if (OB_FAIL(create_trigger(arg, schema_guard, res, ddl_service))) {
  }
  return ret;
}

int ObPLDDLService::alter_trigger(const obcall::ObAlterTriggerArg &arg,
                                  rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  
  bool is_enable = false;
  int64_t refreshed_schema_version = 0;
  OZ (check_env_before_ddl(schema_guard, arg, ddl_service));
  OX (is_enable = arg.trigger_infos_.at(0).is_enable());
  OZ (schema_guard.get_schema_version(refreshed_schema_version));
  if (OB_SUCC(ret)) {
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    OZ (trans.start(ddl_service.sql_proxy_, refreshed_schema_version), refreshed_schema_version);
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.trigger_infos_.count(); ++i) {
      const ObTriggerInfo *old_tg_info = NULL;
      ObTriggerInfo new_tg_info;
      OZ (new_tg_info.assign(arg.trigger_infos_.at(i)));
      OZ (schema_guard.get_trigger_info( new_tg_info.get_trigger_id(), old_tg_info));
      CK (OB_NOT_NULL(old_tg_info), OB_ERR_TRIGGER_NOT_EXIST);
      if (OB_SUCC(ret)) {
        if (!arg.is_set_status_) {
          const ObTriggerInfo *other_trg_info = NULL;
          ObString new_trg_name = new_tg_info.get_trigger_name();
          ObString new_trg_body = new_tg_info.get_trigger_body();
          OZ (schema_guard.get_trigger_info(
                                            new_tg_info.get_database_id(),
                                            new_trg_name,
                                            other_trg_info));
          OV (OB_ISNULL(other_trg_info), OB_OBJ_ALREADY_EXIST, new_tg_info);
          OZ (new_tg_info.deep_copy(*old_tg_info));
          OZ (new_tg_info.set_trigger_name(new_trg_name));
          OZ (new_tg_info.set_trigger_body(new_trg_body));
        } else {
          OZ (new_tg_info.deep_copy(*old_tg_info));
          OX (new_tg_info.set_is_enable(is_enable));
        }
        OZ (pl_operator.alter_trigger(new_tg_info, trans, &arg.ddl_stmt_str_));
      }
    }
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_trigger(const obcall::ObDropTriggerArg &arg,
                                 rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  
  uint64_t trigger_database_id = OB_INVALID_ID;
  const ObString &trigger_database = arg.trigger_database_;
  const ObString &trigger_name = arg.trigger_name_;
  const ObTriggerInfo *trigger_info = NULL;
  if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else if (OB_FAIL(ddl_service.get_database_id(schema_guard, trigger_database, trigger_database_id))) {
  } else if (OB_FAIL(schema_guard.get_trigger_info( trigger_database_id, trigger_name, trigger_info))) {
  } else if (OB_ISNULL(trigger_info)) {
    ret = OB_ERR_TRIGGER_NOT_EXIST;
  } else if (trigger_info->is_in_recyclebin()) {
    ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
  } else if (OB_FAIL(drop_trigger_in_trans(*trigger_info, &arg.ddl_stmt_str_, schema_guard, ddl_service))) {
  }
  if (OB_ERR_TRIGGER_NOT_EXIST == ret || OB_ERR_BAD_DATABASE == ret) {
    ret = OB_ERR_TRIGGER_NOT_EXIST;
    if (arg.if_exist_) {
      ret = OB_SUCCESS;
      LOG_MYSQL_USER_NOTE(OB_ERR_TRIGGER_NOT_EXIST);
    } else {
      LOG_MYSQL_USER_ERROR(OB_ERR_TRIGGER_NOT_EXIST);
    }
  }
  return ret;
}

int ObPLDDLService::create_trigger(const obcall::ObCreateTriggerArg &arg,
                                   ObSchemaGetterGuard &schema_guard,
                                   obcall::ObCreateTriggerRes *res,
                                   rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObTriggerInfo new_trigger_info;
  //in_second_stage_ is false, Indicates that the trigger is created normally
  //true Indicates that the error message is inserted into the system table after the trigger is created
  //So the following steps can be skipped
  
  uint64_t trigger_database_id = OB_INVALID_ID;
  uint64_t base_object_id = OB_INVALID_ID;
  ObSchemaType base_object_type = static_cast<ObSchemaType>(arg.trigger_info_.get_base_object_type());
  const ObString &trigger_database = arg.trigger_database_;
  const ObString &base_object_database = arg.base_object_database_;
  const ObString &base_object_name = arg.base_object_name_;
  if (OB_FAIL(new_trigger_info.assign(arg.trigger_info_))) {
  } else {
    const ObTriggerInfo *old_trigger_info = NULL;
    if (OB_FAIL(ddl_service.get_database_id(schema_guard, trigger_database, trigger_database_id))) {
    } else if (OB_FAIL(get_object_info(schema_guard,
                                       base_object_database,
                                       base_object_name,
                                       base_object_type,
                                       base_object_id,
                                       ddl_service))) {
    } else if (FALSE_IT(new_trigger_info.set_database_id(trigger_database_id))) {
    } else if (FALSE_IT(new_trigger_info.set_base_object_type(base_object_type))) {
    } else if (FALSE_IT(new_trigger_info.set_base_object_id(base_object_id))) {
    } else if (OB_FAIL(try_get_exist_trigger(schema_guard, new_trigger_info, old_trigger_info, arg.with_replace_))) {
    } else {
      if (NULL != old_trigger_info) {
        new_trigger_info.set_trigger_id(old_trigger_info->get_trigger_id());
      }
    }
  }
  if (OB_SUCC(ret)) {
    int64_t table_schema_version = OB_INVALID_VERSION;
    if (OB_ISNULL(res)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(create_trigger_in_trans(new_trigger_info,
                                               const_cast<ObErrorInfo &>(arg.error_info_),
                                               const_cast<ObSArray<ObDependencyInfo> &>(arg.dependency_infos_),
                                               &arg.ddl_stmt_str_,
                                               arg.in_second_stage_,
                                               schema_guard,
                                               table_schema_version,
                                               ddl_service))) {
    } else {
      res->table_schema_version_ = table_schema_version;
      res->trigger_schema_version_ = new_trigger_info.get_schema_version();
    }
  }
  return ret;
}

int ObPLDDLService::create_trigger_in_trans(share::schema::ObTriggerInfo &trigger_info,
                                            share::schema::ObErrorInfo &error_info,
                                            ObIArray<ObDependencyInfo> &dep_infos,
                                            const common::ObString *ddl_stmt_str,
                                            bool in_second_stage,
                                            share::schema::ObSchemaGetterGuard &schema_guard,
                                            int64_t &table_schema_version,
                                            rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    }
    if (OB_SUCC(ret) && !in_second_stage) {
        OZ (adjust_trigger_action_order(schema_guard, trans, pl_operator, trigger_info, true));
    }
    OZ (pl_operator.create_trigger(trigger_info, trans, error_info, dep_infos, table_schema_version, ddl_stmt_str));
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_trigger_in_trans(const share::schema::ObTriggerInfo &trigger_info,
                                          const common::ObString *ddl_stmt_str,
                                          share::schema::ObSchemaGetterGuard &schema_guard,
                                          rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    }
    OZ (adjust_trigger_action_order(schema_guard, trans, pl_operator, const_cast<ObTriggerInfo &>(trigger_info), false));
    OZ (pl_operator.drop_trigger(trigger_info, trans, ddl_stmt_str));
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::try_get_exist_trigger(share::schema::ObSchemaGetterGuard &schema_guard,
                                          const share::schema::ObTriggerInfo &new_trigger_info,
                                          const share::schema::ObTriggerInfo *&old_trigger_info,
                                          bool with_replace)
{
  int ret = OB_SUCCESS;
  const ObString &trigger_name = new_trigger_info.get_trigger_name();
  if (OB_FAIL(schema_guard.get_trigger_info(
                                            new_trigger_info.get_database_id(),
                                            trigger_name, old_trigger_info))) {
  } else if (NULL != old_trigger_info) {
    if (new_trigger_info.get_base_object_id() != old_trigger_info->get_base_object_id()) {
      ret = OB_ERR_TRIGGER_EXIST_ON_OTHER_TABLE;
      LOG_USER_ERROR(OB_ERR_TRIGGER_EXIST_ON_OTHER_TABLE, trigger_name.length(), trigger_name.ptr());
    } else if (!with_replace) {
      ret = OB_ERR_TRIGGER_ALREADY_EXIST;
      LOG_USER_ERROR(OB_ERR_TRIGGER_ALREADY_EXIST, trigger_name.length(), trigger_name.ptr());
    }
  }
  return ret;
}

int ObPLDDLService::rebuild_trigger_on_rename(share::schema::ObSchemaGetterGuard &schema_guard,
                                              const share::schema::ObTableSchema &table_schema,
                                              ObDDLOperator &ddl_operator,
                                              ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObDatabaseSchema *database_schema = NULL;
  const ObString *database_name = NULL;
  const ObString &table_name = table_schema.get_table_name_str();
  
  OZ (schema_guard.get_database_schema( table_schema.get_database_id(), database_schema),
      table_schema.get_database_id());
  OV (OB_NOT_NULL(database_schema), OB_ERR_UNEXPECTED, table_schema.get_database_id());
  OX (database_name = &database_schema->get_database_name_str());
  OZ (rebuild_trigger_on_rename(schema_guard,
                                table_schema.get_trigger_list(),
                                *database_name,
                                table_name,
                                ddl_operator,
                                trans));
  return ret;
}

int ObPLDDLService::rebuild_trigger_on_rename(share::schema::ObSchemaGetterGuard &schema_guard,
                                              const common::ObIArray<uint64_t> &trigger_list,
                                              const common::ObString &database_name,
                                              const common::ObString &table_name,
                                              ObDDLOperator &ddl_operator,
                                              ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObTriggerInfo *trigger_info = NULL;
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  for (int64_t i = 0; OB_SUCC(ret) && i < trigger_list.count(); i++) {
    OZ (schema_guard.get_trigger_info( trigger_list.at(i), trigger_info), trigger_list.at(i));
    OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_list.at(i));
    OZ (pl_operator.rebuild_trigger_on_rename(*trigger_info, database_name, table_name, trans));
  }
  return ret;
}

int ObPLDDLService::create_trigger_for_truncate_table(share::schema::ObSchemaGetterGuard &schema_guard,
                                                      const common::ObIArray<uint64_t> &origin_trigger_list,
                                                      share::schema::ObTableSchema &new_table_schema,
                                                      ObDDLOperator &ddl_operator,
                                                      ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObTriggerInfo *origin_trigger_info = NULL;
  ObTriggerInfo new_trigger_info;
  ObString spec_source;
  ObString body_source;
  ObErrorInfo error_info;
  ObArenaAllocator inner_alloc;
  new_table_schema.get_trigger_list().reset();
  bool is_update_table_schema_version = false;
  const ObDatabaseSchema *db_schema = NULL;
  
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  OZ (schema_guard.get_database_schema(
                                       new_table_schema.get_database_id(),
                                       db_schema));
  CK (db_schema != NULL);
  for (int64_t i = 0; OB_SUCC(ret) && i < origin_trigger_list.count(); i++) {
    is_update_table_schema_version = i == origin_trigger_list.count() - 1 ? true : false;
    uint64_t new_trigger_id = OB_INVALID_ID;
    OZ (schema_guard.get_trigger_info( origin_trigger_list.at(i), origin_trigger_info),
                                      origin_trigger_list.at(i));
    if (OB_SUCC(ret)) {
      if (OB_FAIL(new_trigger_info.deep_copy(*origin_trigger_info))) {
      } else if (OB_FAIL(pl_operator.get_multi_schema_service().get_schema_service()->fetch_new_trigger_id(new_trigger_id))) {
      } else {
        new_trigger_info.set_trigger_id(new_trigger_id);
        new_trigger_info.set_base_object_id(new_table_schema.get_table_id());
        new_table_schema.get_trigger_list().push_back(new_trigger_id);
        if (OB_SUCC(ret)) {
          ObSEArray<ObDependencyInfo, 1> dep_infos;
          int64_t table_schema_version = OB_INVALID_VERSION;
          if (OB_FAIL(pl_operator.create_trigger(new_trigger_info,
                                                 trans,
                                                 error_info,
                                                 dep_infos,
                                                 table_schema_version,
                                                 &origin_trigger_info->get_trigger_body(),
                                                 is_update_table_schema_version,
                                                 true))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObPLDDLService::adjust_trigger_action_order(share::schema::ObSchemaGetterGuard &schema_guard,
                                                ObDDLSQLTransaction &trans,
                                                ObPLDDLOperator &pl_operator,
                                                ObTriggerInfo &trigger_info,
                                                bool is_create_trigger)
{
  int ret = OB_SUCCESS;
#define ALTER_OLD_TRIGGER(source_trg_info) \
  ObTriggerInfo copy_trg_info;   \
  OZ (copy_trg_info.assign(*source_trg_info)); \
  OX (copy_trg_info.set_action_order(new_action_order)); \
  OZ (pl_operator.alter_trigger(copy_trg_info, trans, NULL, false/*is_update_table_schema_version*/));

  common::ObSArray<uint64_t> trg_list;
  if (OB_SUCC(ret)) {
    const ObTableSchema *table_schema = NULL;
    OZ (schema_guard.get_table_schema( trigger_info.get_base_object_id(), table_schema));
    OV (OB_NOT_NULL(table_schema));
    OZ (trg_list.assign(table_schema->get_trigger_list()));
  }
  if (OB_SUCC(ret)) {
    const ObTriggerInfo *old_trg_info = NULL;
    int64_t new_action_order = 0; // the old trigger's new action order
    if (is_create_trigger) {
      int64_t action_order = 1; // action order for the trigger being created
      const ObTriggerInfo *ref_trg_info = NULL;
      if (OB_SUCC(ret)) {
        if (!trigger_info.get_ref_trg_name().empty()) {
          OZ (schema_guard.get_trigger_info( trigger_info.get_database_id(),
                                            trigger_info.get_ref_trg_name(), ref_trg_info));
          OV (OB_NOT_NULL(ref_trg_info));
        }
        if (OB_FAIL(ret)) {
        } else {
          if (NULL == ref_trg_info) {
            for (int64_t i = 0; OB_SUCC(ret) && i < trg_list.count(); i++) {
              OZ (schema_guard.get_trigger_info( trg_list.at(i), old_trg_info));
              OV (OB_NOT_NULL(old_trg_info));
              if (OB_SUCC(ret) && ObTriggerInfo::is_same_timing_event(trigger_info, *old_trg_info)) {
                action_order++;
              }
            }
          } else {
            bool is_follows = trigger_info.is_order_follows();
            action_order = is_follows ? ref_trg_info->get_action_order() + 1 : ref_trg_info->get_action_order();
            // ref_trg_info need to modify
            for (int64_t i = 0; OB_SUCC(ret) && i < trg_list.count(); i++) {
              OZ (schema_guard.get_trigger_info( trg_list.at(i), old_trg_info));
              OV (OB_NOT_NULL(old_trg_info));
              if (OB_SUCC(ret) && ObTriggerInfo::is_same_timing_event(trigger_info, *old_trg_info)
                  && trigger_info.get_trigger_id() != old_trg_info->get_trigger_id()
                  && ref_trg_info->get_trigger_id() != old_trg_info->get_trigger_id()) {
                  if (ref_trg_info->get_action_order() < old_trg_info->get_action_order()) {
                    new_action_order = old_trg_info->get_action_order() + 1;
                    ALTER_OLD_TRIGGER(old_trg_info);
                }
              }
            }
            if (OB_SUCC(ret) && !is_follows) {
              // if `PRECEDES`, the ref_trg_info action_order need to +1
              new_action_order = ref_trg_info->get_action_order() + 1;
              ALTER_OLD_TRIGGER(ref_trg_info);
            }
          }
        }
      }
      OX (trigger_info.set_action_order(action_order));
    } else {
      if (OB_SUCC(ret)) {
        for (int64_t i = 0; OB_SUCC(ret) && i < trg_list.count(); i++) {
          OZ (schema_guard.get_trigger_info( trg_list.at(i), old_trg_info));
          OV (OB_NOT_NULL(old_trg_info));
          if (OB_SUCC(ret) && ObTriggerInfo::is_same_timing_event(trigger_info, *old_trg_info)
              && trigger_info.get_trigger_id() != old_trg_info->get_trigger_id()
              && trigger_info.get_action_order() < old_trg_info->get_action_order()) {
            new_action_order = old_trg_info->get_action_order() - 1;
            ALTER_OLD_TRIGGER(old_trg_info);
          }
        }
      }
    }
  }
#undef ALTER_OLD_TRIGGER
  return ret;
}

int ObPLDDLService::recursive_alter_ref_trigger(share::schema::ObSchemaGetterGuard &schema_guard,
                                                ObDDLSQLTransaction &trans,
                                                ObPLDDLOperator &pl_operator,
                                                const ObTriggerInfo &ref_trigger_info,
                                                const common::ObIArray<uint64_t> &trigger_list,
                                                const ObString &trigger_name,
                                                int64_t action_order)
{
  int ret = OB_SUCCESS;
  
  const ObTriggerInfo *trg_info = NULL;
  int64_t new_action_order = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < trigger_list.count(); i++) {
    OZ (schema_guard.get_trigger_info( trigger_list.at(i), trg_info));
    OV (OB_NOT_NULL(trg_info));
    if (0 != trg_info->get_trigger_name().case_compare(trigger_name)) {
      if (OB_SUCC(ret) && 0 == trg_info->get_ref_trg_name().case_compare(ref_trigger_info.get_trigger_name())) {
        ObTriggerInfo copy_trg_info;
        OX (new_action_order = action_order + 1);
        OZ (copy_trg_info.assign(*trg_info));
        OX (copy_trg_info.set_action_order(new_action_order));
        OZ (pl_operator.alter_trigger(copy_trg_info, trans, NULL, false/*is_update_table_schema_version*/));
        OZ (SMART_CALL(recursive_alter_ref_trigger(schema_guard,
                                                   trans,
                                                   pl_operator,
                                                   *trg_info,
                                                   trigger_list,
                                                   trigger_name,
                                                   new_action_order)));
      }
    }
  }
  return ret;
}

int ObPLDDLService::recursive_check_trigger_ref_cyclic(share::schema::ObSchemaGetterGuard &schema_guard,
                                                        const ObTriggerInfo &ref_trigger_info,
                                                        const common::ObIArray<uint64_t> &trigger_list,
                                                        const ObString &create_trigger_name,
                                                        const ObString &generate_cyclic_name)
{
  int ret = OB_SUCCESS;
  
  const ObTriggerInfo *trg_info = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < trigger_list.count(); i++) {
    OZ (schema_guard.get_trigger_info( trigger_list.at(i), trg_info));
    OV (OB_NOT_NULL(trg_info));
    if (OB_SUCC(ret)) {
      if (0 != trg_info->get_trigger_name().case_compare(create_trigger_name)) {
        if (0 == trg_info->get_ref_trg_name().case_compare(ref_trigger_info.get_trigger_name())) {
          if (0 == trg_info->get_trigger_name().case_compare(generate_cyclic_name)) {
            ret = OB_ERR_REF_CYCLIC_IN_TRG;
          }
          OZ (SMART_CALL(recursive_check_trigger_ref_cyclic(schema_guard,
                                                            *trg_info,
                                                            trigger_list,
                                                            create_trigger_name,
                                                            generate_cyclic_name)));
        }
      }
    }
  }
  return ret;
}
int ObPLDDLService::drop_trigger_in_drop_table(ObMySQLTransaction &trans,
                                               ObDDLOperator &ddl_operator,
                                               share::schema::ObSchemaGetterGuard &schema_guard,
                                               const share::schema::ObTableSchema &table_schema,
                                               const bool to_recyclebin)
                  {
  int ret = OB_SUCCESS;
  uint64_t trigger_id = OB_INVALID_ID;
  const ObTriggerInfo *trigger_info = NULL;
  
  const ObIArray<uint64_t> &trigger_id_list = table_schema.get_trigger_list();
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  for (int64_t i = 0; OB_SUCC(ret) && i < trigger_id_list.count(); i++) {
    OX (trigger_id = trigger_id_list.at(i));
    OZ (schema_guard.get_trigger_info( trigger_id, trigger_info), trigger_id);
    OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_id);
    OV (!trigger_info->is_in_recyclebin(), OB_ERR_UNEXPECTED, trigger_id);
    if (to_recyclebin && !table_schema.is_view_table()) {
      // Only non-view table triggers are moved to the recycle bin.
      OZ (pl_operator.drop_trigger_to_recyclebin(*trigger_info, schema_guard, trans));
    } else {
      OZ (pl_operator.drop_trigger(*trigger_info,
                                   trans,
                                   NULL,
                                   true /*is_update_table_schema_version, default true*/,
                                   table_schema.get_in_offline_ddl_white_list()));
    }
  }
  return ret;
}

int ObPLDDLService::restore_trigger(const share::schema::ObTableSchema &table_schema,
                                      const uint64_t new_database_id,
                                      const common::ObString &new_table_name,
                                      share::schema::ObSchemaGetterGuard &schema_guard,
                                      ObMySQLTransaction &trans,
                                      ObDDLOperator &ddl_operator)
{
  int ret = OB_SUCCESS;
  
  const ObIArray<uint64_t> &trigger_id_list = table_schema.get_trigger_list();
  const ObTriggerInfo *trigger_info = NULL;
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  for (int i = 0; OB_SUCC(ret) && i < trigger_id_list.count(); i++) {
    uint64_t trigger_id = trigger_id_list.at(i);
    OZ (schema_guard.get_trigger_info( trigger_id, trigger_info), trigger_id);
    OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_id);
    OZ (pl_operator.restore_trigger(*trigger_info, new_database_id, new_table_name, schema_guard, trans));
  }
  return ret;
}

int ObPLDDLService::get_object_info(ObSchemaGetterGuard &schema_guard,
                                    const ObString &object_database,
                                    const ObString &object_name,
                                    ObSchemaType &object_type,
                                    uint64_t &object_id,
                                    rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  uint64_t database_id = OB_INVALID_ID;
  const ObTableSchema *table_schema = NULL;
  if (TABLE_SCHEMA == object_type || VIEW_SCHEMA == object_type) {
    const ObTableSchema *table_schema = NULL;
    if (OB_FAIL(ddl_service.get_database_id(schema_guard, object_database, database_id))) {
    } else if (OB_FAIL(schema_guard.get_table_schema( database_id,
                                                    object_name, false, table_schema))) {
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_BAD_TABLE;
    } else if (table_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    } else if (!table_schema->is_user_table() && !table_schema->is_user_view()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "not create on user table or user view in trigger now");
    } else {
      object_type = table_schema->is_user_table() ? TABLE_SCHEMA : VIEW_SCHEMA;
      object_id = table_schema->get_table_id();
    }
  } else if (USER_SCHEMA == object_type || DATABASE_SCHEMA == object_type) {
    const ObUserInfo *user_info = NULL;
    ObString host_name("%");
    if (OB_FAIL(schema_guard.get_user_info(object_name, host_name, user_info))) {
    } else if (OB_ISNULL(user_info)) {
      ret = OB_ERR_BAD_TABLE;
    } else {
      object_id = user_info->get_user_id();
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPLDDLService::rebuild_triggers_on_hidden_table(
                  const ObTableSchema &orig_table_schema,
                  const ObTableSchema &hidden_table_schema,
                  ObSchemaGetterGuard &runtime_schema_guard,
                  ObDDLOperator &ddl_operator,
                  ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObIArray<uint64_t> &trigger_list = orig_table_schema.get_trigger_list();
  const ObTriggerInfo *trigger_info = NULL;
  ObTriggerInfo new_trigger_info;
  ObErrorInfo error_info;
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  for (int i = 0; OB_SUCC(ret) && i < trigger_list.count(); i++) {
    OZ (runtime_schema_guard.get_trigger_info( trigger_list.at(i), trigger_info));
    OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_list.at(i));
    OZ (new_trigger_info.assign(*trigger_info));
    OX (new_trigger_info.set_base_object_id(hidden_table_schema.get_table_id()));
    OX (new_trigger_info.set_trigger_id(OB_INVALID_ID));
    // Preserve the original trigger database id when rebuilding on the hidden table.
    OX (new_trigger_info.set_database_id(trigger_info->get_database_id()));
    // Offline DDL drops the original trigger before creating it on the hidden table.
    OZ (pl_operator.drop_trigger(*trigger_info, trans,
      nullptr, false/*is_update_table_schema_version*/));
    if (OB_SUCC(ret)) {
      ObSEArray<ObDependencyInfo, 1> dep_infos;
      int64_t table_schema_version = OB_INVALID_VERSION;
      OZ (pl_operator.create_trigger(new_trigger_info, trans, error_info, dep_infos,
        table_schema_version, nullptr, false/*is_update_table_schema_version*/));
    }
  }
  return ret;
}

int ObPLDDLService::drop_trigger_in_drop_user(ObMySQLTransaction &trans,
                                            rootserver::ObDDLOperator &ddl_operator,
                                            ObSchemaGetterGuard &schema_guard,
                                            const uint64_t user_id)
{
  int ret = OB_SUCCESS;
  uint64_t trigger_id = OB_INVALID_ID;
  const ObTriggerInfo *trigger_info = NULL;
  const ObUserInfo *user_info = NULL;
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  OZ (schema_guard.get_user_info(user_id, user_info));
  OV (OB_NOT_NULL(user_info));
  if (OB_SUCC(ret)) {
    const ObIArray<uint64_t> &trigger_id_list = user_info->get_trigger_list();
    for (int64_t i = 0; OB_SUCC(ret) && i < trigger_id_list.count(); i++) {
      OX (trigger_id = trigger_id_list.at(i));
      OZ (schema_guard.get_trigger_info( trigger_id, trigger_info), trigger_id);
      OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_id);
      OV (!trigger_info->is_in_recyclebin(), OB_ERR_UNEXPECTED, trigger_id);
      OZ (pl_operator.drop_trigger(*trigger_info, trans, NULL));
    }
  }
  return ret;
}
template <typename ArgType>
int ObPLDDLService::check_env_before_ddl(share::schema::ObSchemaGetterGuard &schema_guard,
                                         const ArgType &arg,
                                         rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ddl_service.check_inner_stat())) {
  } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
  } else if (OB_FAIL(ddl_service.check_parallel_ddl_conflict(schema_guard, arg))) {
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
