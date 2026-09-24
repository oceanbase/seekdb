/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SHARE_SCHEMA_ROUTINE_PRIVILEGE_OVERLAY_H_
#define SEEKDB_SHARE_SCHEMA_ROUTINE_PRIVILEGE_OVERLAY_H_

#include "share/schema/ob_routine_info.h"
#include "share/ob_priv_common.h"
#include <map>
#include <set>
#include <string>
#include <new>
#include <vector>
#include <algorithm>
#include <tuple>

namespace oceanbase { namespace share { namespace schema {
class RoutineCatalogSavepoint;

// Host-authorized, transaction-local routine grants. Metadata/ownership alone
// never creates a grant: Root records CREATE only after ordinary admission and
// records DROP only after reserving its durable deletion. This is not plugin ABI.
// A replacement shadows name-keyed base grants (including roles), not global or
// database grants. The durable DROP path must revoke those same base grants.
// The owner must abort the sequence if recording or schema staging fails.
class RoutinePrivilegeOverlay final
{
  struct Key { uint64_t database_; ObRoutineType type_; std::string name_; uint64_t object_ = 0; };
  static common::ObString string(const std::string &s)
  { return common::ObString(static_cast<int32_t>(s.size()), s.data()); }
  struct Less {
    bool operator()(const Key &a, const Key &b) const {
      if (a.database_ != b.database_) return a.database_ < b.database_;
      if (a.type_ != b.type_) return a.type_ < b.type_;
      const int order = ObSchemaNameComparator{}.compare(string(a.name_), string(b.name_));
      return order != 0 ? order < 0 : a.object_ < b.object_;
    }
  };
  struct Entry { uint64_t id_; uint64_t owner_; int64_t version_; bool dropped_; ObPrivSet grants_; };
  using NameIndex = std::map<Key, Entry, Less>;
  struct Undo {
    NameIndex::iterator name_slot_;
    Entry old_;
    std::set<uint64_t>::iterator id_slot_;
    bool inserted_id_;
    size_t parent_;
  };
  struct ObjectKey {
    uint64_t object_, grantee_, grantor_;
    bool operator<(const ObjectKey &other) const {
      return std::tie(object_, grantee_, grantor_) < std::tie(other.object_, other.grantee_, other.grantor_);
    }
  };
  struct ObjectEntry {
    uint64_t database_ = 0, owner_ = 0;
    int64_t slot_ = 0, schema_version_ = 0, acl_version_ = 0;
    ObPackedObjPriv after_ = 0;
    bool active_ = false;
  };
  using ObjectIndex = std::map<ObjectKey, ObjectEntry>;
  struct ObjectUndo { ObjectIndex::iterator slot_; ObjectEntry old_; size_t parent_; };
public:
  static constexpr size_t MAX_IDENTITIES = 16384;
  static constexpr size_t MAX_RECORDS = 16384;
  // A transaction can cross databases and admitted creator identities. Grants
  // belong to each recorded routine's owner, as in RoutineCatalogWriter; the
  // view itself never authorizes a creator, definer or database transition.
  RoutinePrivilegeOverlay() : scoped_(false), database_(0), principal_(0) {}
  // Installation sequences may retain their stricter single-scope contract.
  RoutinePrivilegeOverlay(uint64_t database, uint64_t principal)
      : scoped_(true), database_(database), principal_(principal) {}
  RoutinePrivilegeOverlay(const RoutinePrivilegeOverlay &) = delete;
  RoutinePrivilegeOverlay &operator=(const RoutinePrivilegeOverlay &) = delete;
  // Serialized with the owning session, including transaction completion.
  void retire() noexcept { retired_ = true; }
  bool is_retired() const noexcept { return retired_; }

  int record_create(const ObRoutineInfo &routine, bool automatic_privileges)
  {
    using namespace common;
    if (!routine.is_native()) return record(routine, false, automatic_privileges);
    const size_t mark = head_, object_mark = object_head_;
    int ret = record(routine, false, false); // Native declarations never mint name-keyed grants.
    if (ret != OB_SUCCESS) return ret;
    try {
      for (auto i = objects_.lower_bound({routine.get_routine_id(), 0, 0});
          i != objects_.end() && i->first.object_ == routine.get_routine_id(); ++i)
        if (i->second.active_) { ret = OB_STATE_NOT_MATCH; break; }
      ObPackedObjPriv grants = 0;
      if (automatic_privileges) for (const ObRawObjPriv right : {OBJ_PRIV_ID_EXECUTE, OBJ_PRIV_ID_ALTER}) {
        ObPackedObjPriv bits = 0;
        if (ret == OB_SUCCESS) ret = ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, right, bits);
        grants |= bits;
      }
      if (ret == OB_SUCCESS && (object_history_.size() >= MAX_RECORDS ||
          (objects_.size() >= MAX_IDENTITIES && objects_.find({routine.get_routine_id(),
              routine.get_owner_id(), routine.get_owner_id()}) == objects_.end())))
        ret = OB_SIZE_OVERFLOW;
      if (ret == OB_SUCCESS) {
        if (object_history_.size() == object_history_.capacity())
          object_history_.reserve(std::min(MAX_RECORDS, std::max(size_t{16}, object_history_.capacity() * 2)));
        const auto entry = objects_.try_emplace(
            ObjectKey{routine.get_routine_id(), routine.get_owner_id(), routine.get_owner_id()}, ObjectEntry{});
        object_history_.push_back({entry.first, entry.first->second, object_head_});
        // This is the host's provisional CREATE declaration, not an ACL write
        // receipt or invented SQL version. The creation version is only its
        // ordering floor; actual automatic ACL SQL reserves a later version.
        entry.first->second = {routine.get_database_id(), routine.get_owner_id(), routine.get_overload(),
            routine.get_schema_version(), routine.get_schema_version(), grants, true};
        object_head_ = object_history_.size();
      }
    } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { ret = OB_ERR_UNEXPECTED; }
    if (ret != OB_SUCCESS) { rollback_objects_to(object_mark); rollback_to(mark); }
    return ret;
  }
  int record_drop(const ObRoutineInfo &routine)
  { return record(routine, true, false); }

  // Record only a successful, host-admitted transactional ACL writer result.
  // The exact grantor's complete after-image replaces its cached base record;
  // zero is an explicit revoke, not permission to fall back to that record.
  // Failure requires the caller to roll back BOTH SQL and this view savepoint.
  // An explicit actor is for the host's checked writer: a role can be the
  // recorded grantor while the private view still belongs to the invoking user.
  int record_object_change(const ObRoutineInfo &routine, uint64_t grantor, uint64_t grantee,
      int64_t acl_version, ObPackedObjPriv before, ObPackedObjPriv after,
      uint64_t actor = common::OB_INVALID_ID)
  {
    using namespace common;
    if (retired_) return OB_STATE_NOT_MATCH;
    if (!valid_object_routine(routine) || !valid_id(grantor) || !valid_id(grantee) ||
        (actor != OB_INVALID_ID && !valid_id(actor)) ||
        (scoped_ && (actor == OB_INVALID_ID ? grantor : actor) != principal_) ||
        acl_version <= routine.get_schema_version() ||
        !valid_object_bits(before) || !valid_object_bits(after)) return OB_INVALID_ARGUMENT;
    try {
      const ObjectKey key{routine.get_routine_id(), grantee, grantor};
      const auto found = objects_.find(key);
      if (found != objects_.end() && found->second.active_) {
        const auto &old = found->second;
        if (!matches_object(old, routine) || old.after_ != before || acl_version <= old.acl_version_)
          return OB_STATE_NOT_MATCH;
      }
      if (object_history_.size() >= MAX_RECORDS ||
          (found == objects_.end() && objects_.size() >= MAX_IDENTITIES)) return OB_SIZE_OVERFLOW;
      if (object_history_.size() == object_history_.capacity())
        object_history_.reserve(std::min(MAX_RECORDS, std::max(size_t{16}, object_history_.capacity() * 2)));
      const auto entry = objects_.try_emplace(key, ObjectEntry{});
      // No allocation after index insertion: history capacity was reserved and
      // iterator/entry copies are trivial. Rollback retains slots for live marks.
      object_history_.push_back({entry.first, entry.first->second, object_head_});
      entry.first->second = {routine.get_database_id(), routine.get_owner_id(), routine.get_overload(),
          routine.get_schema_version(), acl_version, after, true};
      object_head_ = object_history_.size();
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }

  bool has_object_changes(uint64_t object, uint64_t grantee) const
  {
    for (auto i = objects_.lower_bound({object, grantee, 0}); i != objects_.end() &&
        i->first.object_ == object && i->first.grantee_ == grantee; ++i)
      if (i->second.active_) return true;
    return false;
  }

  int merge_object_privileges(const ObRoutineInfo &current, uint64_t grantee,
      const common::ObIArray<const ObObjPriv *> &base, ObPackedObjPriv &grants) const
  {
    using namespace common;
    grants = 0;
    if (retired_) return OB_STATE_NOT_MATCH;
    if (!valid_object_routine(current) || !valid_id(grantee)) return OB_INVALID_ARGUMENT;
    ObPackedObjPriv value = 0;
    const uint64_t object = current.get_routine_id();
    for (int64_t i = 0; i < base.count(); ++i) {
      const auto *row = base.at(i);
      if (!row || row->get_obj_id() != object || row->get_grantee_id() != grantee ||
          row->get_objtype() != uint64_t(ObObjectType::FUNCTION) ||
          row->get_col_id() != OBJ_LEVEL_FOR_TAB_PRIV || !valid_id(row->get_grantor_id())) return OB_INVALID_ARGUMENT;
      const auto found = objects_.find({object, grantee, row->get_grantor_id()});
      if (found == objects_.end() || !found->second.active_) value |= row->get_obj_privs();
    }
    for (auto i = objects_.lower_bound({object, grantee, 0}); i != objects_.end() &&
        i->first.object_ == object && i->first.grantee_ == grantee; ++i) {
      if (!i->second.active_) continue;
      if (!matches_object(i->second, current)) return OB_STATE_NOT_MATCH;
      value |= i->second.after_;
    }
    grants = value;
    return OB_SUCCESS;
  }

  // Complete, owned per-grantor ACL for host-side sequential DCL planning.
  // Unlike merge_object_privileges, this preserves delegation provenance and
  // column groups. Caller supplies a complete pinned base snapshot and the
  // CURRENT schema-overlay lookup. This is not a locking SQL read or authority
  // to execute a plan; execution must revalidate against its own transaction.
  // A zero private after-image removes only that function-level grantor key.
  // Output is sorted by (grantee, grantor, column), supports base/output aliasing,
  // and is cleared on failure. This overlay and a non-aliased base are unchanged.
  int merge_object_snapshot(const ObRoutineInfo &current,
      const common::ObIArray<ObObjPriv> &base, common::ObIArray<ObObjPriv> &output) const
  {
    using namespace common;
    int ret = OB_SUCCESS;
    if (retired_) ret = OB_STATE_NOT_MATCH;
    else if (!valid_object_routine(current)) ret = OB_INVALID_ARGUMENT;
    else if (base.count() > MAX_IDENTITIES) ret = OB_SIZE_OVERFLOW;
    if (ret != OB_SUCCESS) { output.reset(); return ret; }
    try {
      using SnapshotKey = std::tuple<uint64_t, uint64_t, uint64_t>;
      std::map<SnapshotKey, ObObjPriv> merged;
      const uint64_t object = current.get_routine_id();
      for (int64_t i = 0; i < base.count(); ++i) {
        const auto &row = base.at(i);
        if (!row.is_valid() || row.get_obj_id() != object ||
            row.get_objtype() != uint64_t(ObObjectType::FUNCTION) ||
            !valid_id(row.get_grantor_id()) || !valid_id(row.get_grantee_id()) ||
            row.get_col_id() > INT64_MAX ||
            (row.get_col_id() == OBJ_LEVEL_FOR_TAB_PRIV && !valid_object_bits(row.get_obj_privs())) ||
            !merged.emplace(SnapshotKey{row.get_grantee_id(), row.get_grantor_id(), row.get_col_id()}, row).second) {
          output.reset(); return OB_INVALID_DATA;
        }
      }
      for (auto i = objects_.lower_bound({object, 0, 0});
          i != objects_.end() && i->first.object_ == object; ++i) {
        if (!i->second.active_) continue;
        const auto &entry = i->second;
        if (!matches_object(entry, current)) { output.reset(); return OB_STATE_NOT_MATCH; }
        const SnapshotKey key{i->first.grantee_, i->first.grantor_, OBJ_LEVEL_FOR_TAB_PRIV};
        if (entry.after_ == 0) merged.erase(key);
        else {
          ObObjPriv row;
          row.set_obj_id(object); row.set_objtype(uint64_t(ObObjectType::FUNCTION));
          row.set_col_id(OBJ_LEVEL_FOR_TAB_PRIV); row.set_grantor_id(i->first.grantor_);
          row.set_grantee_id(i->first.grantee_); row.set_user_id(i->first.grantee_);
          row.set_schema_version(entry.acl_version_); row.set_obj_privs(entry.after_);
          merged[key] = row;
        }
      }
      // Check the final union after removals. Temporary size is bounded by the
      // separately bounded base and private indexes; key order must not turn a
      // same-size delete/add replacement at the limit into a spurious failure.
      if (merged.size() > MAX_IDENTITIES) { output.reset(); return OB_SIZE_OVERFLOW; }
      output.reset(); // Read all input before clearing: base may be output.
      for (const auto &entry : merged) if (OB_FAIL(output.push_back(entry.second))) break;
      if (ret != OB_SUCCESS) output.reset();
      return ret;
    } catch (const std::bad_alloc &) { output.reset(); return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { output.reset(); return OB_ERR_UNEXPECTED; }
  }

  // Call with the CURRENT schema overlay lookup, not an earlier borrowed view.
  // handled=true with zero grants deliberately suppresses stale named grants.
  int lookup(uint64_t database, const common::ObString &name, ObRoutineType type,
             uint64_t user, bool schema_handled, const ObRoutineInfo *current,
             bool &handled, ObPrivSet &grants) const
  {
    using namespace common;
    handled = false;
    grants = 0;
    if (retired_) return OB_STATE_NOT_MATCH;
    if (!valid_scope() || !valid_id(database) || !valid_id(user) || !valid_name(name))
      return OB_INVALID_ARGUMENT;
    if ((scoped_ && database != database_) || !standalone(type)) return OB_SUCCESS;
    try {
      const auto found = names_.find(Key{database, type, std::string(name.ptr(), name.length())});
      if (found == names_.end() || found->second.id_ == 0) return OB_SUCCESS;
      const Entry &entry = found->second;
      if (!schema_handled || (entry.dropped_ ? current != nullptr : current == nullptr))
        return OB_STATE_NOT_MATCH;
      if (current != nullptr && (!valid(*current) || current->get_database_id() != database ||
          current->get_routine_id() != entry.id_ ||
          current->get_owner_id() != entry.owner_ || current->get_schema_version() < entry.version_ ||
          current->get_routine_type() != type ||
          ObSchemaNameComparator{}.compare(current->get_routine_name(), name) != 0))
        return OB_STATE_NOT_MATCH;
      handled = true;
      if (!entry.dropped_ && user == entry.owner_) grants = entry.grants_;
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }

private:
  friend class RoutineCatalogSavepoint;
  bool object_ancestor(size_t mark) const noexcept {
    for (size_t cursor = object_head_; cursor != 0; cursor = object_history_[cursor - 1].parent_)
      if (cursor == mark) return true;
    return mark == 0;
  }
  void rollback_objects_to(size_t mark) noexcept {
    while (object_head_ != mark) {
      const auto &undo = object_history_[object_head_ - 1];
      undo.slot_->second = undo.old_;
      object_head_ = undo.parent_;
    }
  }
  static bool valid_object_bits(ObPackedObjPriv bits) {
    ObPackedObjPriv allowed = 0;
    for (const ObRawObjPriv right : {OBJ_PRIV_ID_EXECUTE, OBJ_PRIV_ID_ALTER}) {
      ObPackedObjPriv plain = 0, grantable = 0;
      if (ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, right, plain) != common::OB_SUCCESS ||
          ObPrivPacker::raw_obj_priv_to_packed_info(GRANT_OPTION, right, grantable) != common::OB_SUCCESS) return false;
      if ((bits & (grantable ^ plain)) && !(bits & plain)) return false;
      allowed |= grantable;
    }
    return (bits & ~allowed) == 0;
  }
  bool valid_object_routine(const ObRoutineInfo &routine) const {
    return valid_scope() && valid_id(routine.get_database_id()) &&
        (!scoped_ || routine.get_database_id() == database_) && valid_id(routine.get_routine_id()) &&
        valid_id(routine.get_owner_id()) && routine.get_schema_version() > 0 &&
        routine.get_routine_type() == ROUTINE_FUNCTION_TYPE && routine.get_package_id() == common::OB_INVALID_ID &&
        routine.get_overload() >= 0 && routine.is_native() && routine.is_native_binding_valid() &&
        valid_name(routine.get_routine_name());
  }
  static bool matches_object(const ObjectEntry &entry, const ObRoutineInfo &routine) {
    return entry.database_ == routine.get_database_id() && entry.owner_ == routine.get_owner_id() &&
        entry.slot_ == routine.get_overload() && entry.schema_version_ <= routine.get_schema_version();
  }
  bool ancestor(size_t mark) const noexcept {
    for (size_t cursor = head_; cursor != 0; cursor = history_[cursor - 1].parent_) {
      if (cursor == mark) return true;
    }
    return mark == 0;
  }
  void rollback_to(size_t mark) noexcept {
    while (head_ != mark) {
      const auto &undo = history_[head_ - 1];
      undo.name_slot_->second = undo.old_;
      if (undo.inserted_id_) ids_.erase(undo.id_slot_);
      head_ = undo.parent_;
    }
  }
  static bool valid_id(uint64_t id) { return id > 0 && id <= static_cast<uint64_t>(INT64_MAX); }
  bool valid_scope() const
  { return !scoped_ || (valid_id(database_) && valid_id(principal_)); }
  static bool standalone(ObRoutineType type)
  { return type == ROUTINE_FUNCTION_TYPE || type == ROUTINE_PROCEDURE_TYPE; }
  static bool valid_name(const common::ObString &name)
  { return name.ptr() != nullptr && name.length() > 0 && name.length() <= common::OB_MAX_ROUTINE_NAME_BINARY_LENGTH; }
  bool valid(const ObRoutineInfo &routine) const
  {
    if (routine.is_native()) return valid_object_routine(routine);
    return valid_scope() && valid_id(routine.get_database_id()) &&
        (!scoped_ || routine.get_database_id() == database_) &&
        valid_id(routine.get_routine_id()) && valid_id(routine.get_owner_id()) &&
        routine.get_schema_version() > 0 && standalone(routine.get_routine_type()) &&
        routine.get_package_id() == common::OB_INVALID_ID && routine.get_overload() == 0 &&
        valid_name(routine.get_routine_name());
  }
  int record(const ObRoutineInfo &routine, bool drop, bool automatic)
  {
    using namespace common;
    if (retired_) return OB_STATE_NOT_MATCH;
    if (!valid(routine) || (scoped_ && !drop && routine.get_owner_id() != principal_)) return OB_INVALID_ARGUMENT;
    try {
      Key key{routine.get_database_id(), routine.get_routine_type(),
              std::string(routine.get_routine_name().ptr(), routine.get_routine_name().length()),
              routine.is_native() ? routine.get_routine_id() : 0};
      const auto old = names_.find(key);
      const bool has_old = old != names_.end() && old->second.id_ != 0;
      const bool used = ids_.count(routine.get_routine_id()) != 0;
      if (!drop) {
        if (used || (has_old && !old->second.dropped_)) return OB_STATE_NOT_MATCH;
      } else if (has_old) {
        if (old->second.dropped_ || old->second.id_ != routine.get_routine_id() ||
            old->second.owner_ != routine.get_owner_id() || old->second.version_ > routine.get_schema_version())
          return OB_STATE_NOT_MATCH;
      } else if (used) return OB_STATE_NOT_MATCH;
      if (!used && ids_.size() >= MAX_IDENTITIES) return OB_SIZE_OVERFLOW;
      if (history_.size() >= MAX_RECORDS) return OB_SIZE_OVERFLOW;
      if (history_.size() == history_.capacity())
        history_.reserve(std::min(MAX_RECORDS, std::max(size_t{16}, history_.capacity() * 2)));
      Entry entry{routine.get_routine_id(), routine.get_owner_id(), routine.get_schema_version(), drop,
                  !drop && automatic ? OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE : 0};
      const auto id = ids_.insert(entry.id_);
      try {
        // Allocation precedes publication; replacement of the trivial Entry
        // cannot throw. Retain IDs even after DROP to prohibit identity reuse.
        const auto name = names_.try_emplace(std::move(key), Entry{});
        history_.push_back({name.first, name.first->second, id.first, id.second, head_});
        name.first->second = entry;
        head_ = history_.size();
      } catch (...) {
        if (id.second) ids_.erase(id.first);
        throw;
      }
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }
  const bool scoped_;
  const uint64_t database_;
  const uint64_t principal_;
  NameIndex names_;
  std::set<uint64_t> ids_;
  std::vector<Undo> history_;
  size_t head_ = 0;
  ObjectIndex objects_;
  std::vector<ObjectUndo> object_history_;
  size_t object_head_ = 0;
  bool retired_ = false;
};

} } }
#endif
