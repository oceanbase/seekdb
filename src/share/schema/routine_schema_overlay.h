/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SHARE_SCHEMA_ROUTINE_SCHEMA_OVERLAY_H_
#define SEEKDB_SHARE_SCHEMA_ROUTINE_SCHEMA_OVERLAY_H_

#include "share/schema/ob_routine_info.h"
#include "share/schema/routine_privilege_overlay.h"
#include <algorithm>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace oceanbase { namespace share { namespace schema {
class RoutineCatalogSavepoint;

// Host-only, statement-sequence-local schema view, never a global cache or an
// implicit authorization surface. Only standalone routines are currently covered.
// Optional grants are recorded separately by Root, never inferred from stage().
//
// A lookup distinguishes an untouched key (handled=false, use the base guard)
// from a deletion (handled=true, routine=nullptr, do NOT use the base guard).
// Every returned routine remains alive until this overlay is destroyed, even
// after subsequent ALTER/DROP/savepoint rollback. History is never freed early.
// The owner must keep the overlay alive as long as any borrowed schema pointer.
//
// IDs must already be reserved by the host's schema identity allocator. This
// view neither invents temporary IDs nor authorizes them for durable writes.
class RoutineSchemaOverlay final
{
  struct Key {
    uint64_t database_;
    ObRoutineType type_;
    std::string name_;
  };
  struct NameLess {
    bool operator()(const Key &a, const Key &b) const {
      if (a.database_ != b.database_) return a.database_ < b.database_;
      if (a.type_ != b.type_) return a.type_ < b.type_;
      ObSchemaNameComparator comparator;
      return comparator.compare(string(a.name_), string(b.name_)) < 0;
    }
  };
  struct Record;
  using NameIndex = std::map<Key, Record *, NameLess>;
  using IdIndex = std::map<uint64_t, Record *>;
  struct Record {
    Key key_;
    uint64_t id_;
    // ObSchema's default allocator may borrow the current request arena. This
    // snapshot survives that request (including parameters and string payloads),
    // so bind it to record-owned storage. Destroy the routine before its arena.
    common::ObArenaAllocator arena_;
    std::unique_ptr<ObRoutineInfo> routine_; // null is a tombstone
    size_t parent_ = 0;
    NameIndex::iterator name_slot_;
    IdIndex::iterator id_slot_;
    Record *old_name_ = nullptr;
    Record *old_id_ = nullptr;
  };
public:
  static constexpr size_t MAX_RECORDS = 16384;
  static constexpr int64_t MAX_SCHEMA_BYTES = 64 * 1024 * 1024;
  RoutineSchemaOverlay() = default;
  explicit RoutineSchemaOverlay(std::shared_ptr<const RoutinePrivilegeOverlay> privileges)
      : privileges_(std::move(privileges)) {}
  const RoutinePrivilegeOverlay *privileges() const { return privileges_.get(); }
  // Session-serialized retirement revokes future access, not borrowed storage.
  // Existing savepoint cleanup may still undo indices, never this lifecycle bit.
  void retire() noexcept { retired_ = true; }
  bool is_retired() const noexcept { return retired_ || (privileges_ && privileges_->is_retired()); }
  RoutineSchemaOverlay(const RoutineSchemaOverlay &) = delete;
  RoutineSchemaOverlay &operator=(const RoutineSchemaOverlay &) = delete;

  // Copies the complete schema, including parameters/body/environment. An
  // existing live name may only be updated with the same ID; replacement must
  // first erase the old identity. Once deleted, an ID cannot be reused.
  int stage(const ObRoutineInfo &routine)
  {
    using namespace common;
    if (is_retired()) return OB_STATE_NOT_MATCH;
    if (!valid_id(routine.get_routine_id()) || !valid_id(routine.get_database_id()) ||
        !standalone(routine.get_routine_type()) || routine.get_package_id() != OB_INVALID_ID ||
        routine.get_overload() != 0 || !valid_name(routine.get_routine_name())) return OB_INVALID_ARGUMENT;
    const int64_t bytes = routine.get_convert_size();
    if (bytes <= 0 || bytes > MAX_SCHEMA_BYTES - schema_bytes_ || records_.size() >= MAX_RECORDS)
      return OB_SIZE_OVERFLOW;
    try {
      auto record = std::make_unique<Record>();
      record->key_ = key(routine.get_database_id(), routine.get_routine_type(), routine.get_routine_name());
      record->id_ = routine.get_routine_id();
      record->routine_ = std::make_unique<ObRoutineInfo>(&record->arena_);
      int ret = record->routine_->assign(routine);
      if (ret == OB_SUCCESS) ret = publish(std::move(record));
      if (ret == OB_SUCCESS) schema_bytes_ += bytes;
      return ret;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }

  // May seed a tombstone for an object from the base guard without first
  // staging its full schema. A mismatched live name/ID is never erased.
  int erase(uint64_t database, const common::ObString &name, ObRoutineType type, uint64_t id)
  {
    using namespace common;
    if (is_retired()) return OB_STATE_NOT_MATCH;
    if (!valid_id(database) || !valid_id(id) || !standalone(type) || !valid_name(name))
      return OB_INVALID_ARGUMENT;
    if (records_.size() >= MAX_RECORDS) return OB_SIZE_OVERFLOW;
    try {
      auto record = std::make_unique<Record>();
      record->key_ = key(database, type, name);
      record->id_ = id;
      return publish(std::move(record));
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }

  int lookup(uint64_t database, uint64_t package, const common::ObString &name,
             uint64_t overload, ObRoutineType type, bool &handled, const ObRoutineInfo *&routine) const
  {
    using namespace common;
    handled = false;
    routine = nullptr;
    if (is_retired()) return OB_STATE_NOT_MATCH;
    if (!valid_id(database) || !valid_name(name) || overload == OB_INVALID_INDEX || type == INVALID_ROUTINE_TYPE)
      return OB_INVALID_ARGUMENT;
    // Package routines have a different namespace, never shadow them.
    if (package != OB_INVALID_ID || overload != 0 || !standalone(type)) return OB_SUCCESS;
    try {
      const auto found = names_.find(key(database, type, name));
      if (found != names_.end() && found->second != nullptr) {
        handled = true;
        routine = found->second->routine_.get();
      }
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }

  int lookup(uint64_t id, bool &handled, const ObRoutineInfo *&routine) const
  {
    handled = false;
    routine = nullptr;
    if (is_retired()) return common::OB_STATE_NOT_MATCH;
    if (!valid_id(id)) return common::OB_INVALID_ARGUMENT;
    const auto found = ids_.find(id);
    if (found != ids_.end() && found->second != nullptr) {
      handled = true;
      routine = found->second->routine_.get();
    }
    return common::OB_SUCCESS;
  }

  size_t record_count() const { return records_.size(); }
  int64_t schema_bytes() const { return schema_bytes_; }

private:
  friend class RoutineCatalogSavepoint;
  bool ancestor(size_t mark) const noexcept {
    for (size_t cursor = head_; cursor != 0; cursor = records_[cursor - 1]->parent_) {
      if (cursor == mark) return true;
    }
    return mark == 0;
  }
  void rollback_to(size_t mark) noexcept {
    while (head_ != mark) {
      auto &record = *records_[head_ - 1];
      record.name_slot_->second = record.old_name_;
      record.id_slot_->second = record.old_id_;
      head_ = record.parent_;
    }
    // Records, their schema bytes and index slots remain allocated: guards may
    // still borrow old schema pointers. Capacity cannot be recycled by rollback.
  }
  static common::ObString string(const std::string &value)
  { return common::ObString(static_cast<int32_t>(value.size()), value.data()); }
  static bool valid_id(uint64_t id) { return id != 0 && id <= static_cast<uint64_t>(INT64_MAX); }
  static bool standalone(ObRoutineType type)
  { return type == ROUTINE_FUNCTION_TYPE || type == ROUTINE_PROCEDURE_TYPE; }
  static bool valid_name(const common::ObString &name)
  { return name.length() > 0 && name.length() <= common::OB_MAX_ROUTINE_NAME_BINARY_LENGTH && name.ptr() != nullptr; }
  static Key key(uint64_t database, ObRoutineType type, const common::ObString &name)
  { return Key{database, type, std::string(name.ptr(), name.length())}; }
  static bool same_key(const Key &a, const Key &b)
  { return !NameLess{}(a, b) && !NameLess{}(b, a); }

  int publish(std::unique_ptr<Record> record)
  {
    using namespace common;
    const auto old_name = names_.find(record->key_);
    const auto old_id = ids_.find(record->id_);
    if (old_name != names_.end() && old_name->second && old_name->second->routine_ && old_name->second->id_ != record->id_)
      return OB_STATE_NOT_MATCH;
    if (old_id != ids_.end() && old_id->second && (!same_key(old_id->second->key_, record->key_) ||
        (!old_id->second->routine_ && record->routine_))) return OB_STATE_NOT_MATCH;
    // Allocate before changing any published index. If the second insertion
    // fails, undo ONLY the newly inserted name entry; old state stays intact.
    if (records_.size() == records_.capacity())
      records_.reserve(std::min(MAX_RECORDS, std::max(size_t{16}, records_.capacity() * 2)));
    const auto name_entry = names_.try_emplace(record->key_, nullptr);
    try {
      const auto id_entry = ids_.try_emplace(record->id_, nullptr);
      record->parent_ = head_;
      record->name_slot_ = name_entry.first;
      record->id_slot_ = id_entry.first;
      record->old_name_ = name_entry.first->second;
      record->old_id_ = id_entry.first->second;
      Record *published = record.get();
      records_.push_back(std::move(record)); // capacity reserved, unique_ptr move is noexcept
      name_entry.first->second = published;
      id_entry.first->second = published;
      head_ = records_.size();
    } catch (...) {
      if (name_entry.second) names_.erase(name_entry.first);
      throw;
    }
    return OB_SUCCESS;
  }

  std::vector<std::unique_ptr<Record>> records_;
  const std::shared_ptr<const RoutinePrivilegeOverlay> privileges_;
  NameIndex names_;
  IdIndex ids_;
  size_t head_ = 0;
  int64_t schema_bytes_ = 0;
  bool retired_ = false;
};

} } }
#endif
