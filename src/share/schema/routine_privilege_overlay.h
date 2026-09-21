/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SHARE_SCHEMA_ROUTINE_PRIVILEGE_OVERLAY_H_
#define SEEKDB_SHARE_SCHEMA_ROUTINE_PRIVILEGE_OVERLAY_H_

#include "share/schema/ob_routine_info.h"
#include <map>
#include <set>
#include <string>
#include <new>
#include <vector>
#include <algorithm>

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
  struct Key { uint64_t database_; ObRoutineType type_; std::string name_; };
  static common::ObString string(const std::string &s)
  { return common::ObString(static_cast<int32_t>(s.size()), s.data()); }
  struct Less {
    bool operator()(const Key &a, const Key &b) const {
      if (a.database_ != b.database_) return a.database_ < b.database_;
      return a.type_ != b.type_ ? a.type_ < b.type_ :
          ObSchemaNameComparator{}.compare(string(a.name_), string(b.name_)) < 0;
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
  { return record(routine, false, automatic_privileges); }
  int record_drop(const ObRoutineInfo &routine)
  { return record(routine, true, false); }

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
              std::string(routine.get_routine_name().ptr(), routine.get_routine_name().length())};
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
  bool retired_ = false;
};

} } }
#endif
