/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SCHEMA_ROUTINE_CATALOG_SAVEPOINT_H_
#define SEEKDB_SCHEMA_ROUTINE_CATALOG_SAVEPOINT_H_
#include "share/schema/routine_schema_overlay.h"

namespace oceanbase { namespace share { namespace schema {
// Host-only savepoint of an already admitted routine/privilege view, not a data
// transaction or authorization to write catalog. Holding shared ownership keeps
// marks and borrowed schema records alive; no pointer-address ABA is possible.
// Mark/release are O(1), rollback O(changes) and allocation-free. A descendant of
// a rolled-back branch cannot be restored. Tokens are single-use; an SQL owner
// implementing reusable SAVEPOINT names must establish a fresh mark afterwards.
class RoutineCatalogSavepoint final
{
public:
  RoutineCatalogSavepoint(std::shared_ptr<RoutineSchemaOverlay> schema,
                         std::shared_ptr<RoutinePrivilegeOverlay> privileges)
      : schema_(std::move(schema)), privileges_(std::move(privileges)),
        schema_mark_(schema_ ? schema_->head_ : 0), privilege_mark_(privileges_ ? privileges_->head_ : 0),
        object_privilege_mark_(privileges_ ? privileges_->object_head_ : 0),
        active_(schema_ && privileges_ && !schema_->is_retired() && !privileges_->is_retired()
                && schema_->privileges() == privileges_.get()) {}
  ~RoutineCatalogSavepoint() { if (active_) (void)rollback(); }
  RoutineCatalogSavepoint(const RoutineCatalogSavepoint &) = delete;
  RoutineCatalogSavepoint &operator=(const RoutineCatalogSavepoint &) = delete;
  bool valid() const noexcept { return active_; }
  // Releases only the view mark. It does not commit the caller's transaction.
  void release() noexcept { active_ = false; }
  int rollback() noexcept {
    using namespace common;
    if (!active_) return OB_STATE_NOT_MATCH;
    active_ = false;
    if (!schema_->ancestor(schema_mark_) || !privileges_->ancestor(privilege_mark_) ||
        !privileges_->object_ancestor(object_privilege_mark_)) return OB_STATE_NOT_MATCH;
    schema_->rollback_to(schema_mark_);
    privileges_->rollback_to(privilege_mark_);
    privileges_->rollback_objects_to(object_privilege_mark_);
    return OB_SUCCESS;
  }
private:
  const std::shared_ptr<RoutineSchemaOverlay> schema_;
  const std::shared_ptr<RoutinePrivilegeOverlay> privileges_;
  const size_t schema_mark_, privilege_mark_, object_privilege_mark_;
  bool active_;
};
} } }
#endif
