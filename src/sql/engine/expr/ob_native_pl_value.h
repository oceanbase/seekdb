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

#pragma once

#include "common/datum/datum.h"
#include "common/datum/datum_parameter.h"
#include "common/datum/sql_type.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace pl { class ObRecordType; class ObPLINS; }
namespace sql
{
class ObSQLSessionInfo;
class ObExecContext;

// A borrowed metadata lookup backed by the real compile/runtime declaration
// owner. For requested!=nullptr it must resolve that declaration, not simply
// accept whatever identity happens to be present in a non-NULL payload.
// For heterogeneous leaves requested==nullptr: it must resolve a complete Type
// from the existing registry/context. Unknown metadata returns an error.
class INativePlTypeResolver
{
public:
  virtual ~INativePlTypeResolver() = default;
  virtual int resolve(const common::SqlType *requested,
      const common::DatumParameter &source, const common::SqlType *&resolved,
      const pl::ObRecordType *&record_declaration) const = 0;
};

// Uses the explicit callee/compile declaration namespace and its existing
// definition origins. metadata_owner owns returned Types and must outlive their
// users. No session-global namespace or payload-address lookup is used.
class NativePlDeclarationResolver final : public INativePlTypeResolver
{
public:
  NativePlDeclarationResolver(ObExecContext &context, const pl::ObPLINS &declarations,
                              common::ObIAllocator &metadata_owner)
      : context_(context), declarations_(declarations), metadata_owner_(metadata_owner) {}
  int resolve(const common::SqlType *requested, const common::DatumParameter &source,
      const common::SqlType *&resolved, const pl::ObRecordType *&record_declaration) const override;
private:
  ObExecContext &context_;
  const pl::ObPLINS &declarations_;
  common::ObIAllocator &metadata_owner_;
};

// Owns both carrier bytes and the recursively cloned PL/SQL-array pointees.
// Type and resolver are explicit; this never changes the old PL clone policy.
// Output is unchanged on failure. The resolver's borrowed metadata need only
// survive this call; copied field attributes and pointees are independent.
// The caller separately owns the complete SqlType associated with the result.
int clone_owned_parameter_graph(const common::SqlType &type,
    const common::DatumParameter &source, common::DatumParameter &output,
    common::ObIAllocator &allocator, const INativePlTypeResolver *resolver = nullptr,
    ObSQLSessionInfo *session = nullptr, bool need_new_allocator = true);
// Only graphs produced by this ownership service may be released here.
int release_owned_parameter_graph(common::DatumParameter &value,
    common::ObIAllocator &allocator, ObSQLSessionInfo *session = nullptr);

class NativeSqlValueOwner final
{
public:
  explicit NativeSqlValueOwner(common::ObIAllocator &allocator,
      ObSQLSessionInfo *session = nullptr, const INativePlTypeResolver *resolver = nullptr)
      : allocator_(allocator), session_(session), resolver_(resolver),
        resources_(nullptr), count_(0) {}
  ~NativeSqlValueOwner();
  // Must succeed before arena reuse. Failed tokens remain reachable; clear
  // still visits the other tokens and returns the first error.
  int clear();
  // Checked terminal gate; callers must not destroy an arena on failure.
  int enforce_empty_before_destroy();
  int64_t resource_count() const { return count_; }
  common::ObIAllocator &allocator() { return allocator_; }
  ObSQLSessionInfo *session() const { return session_; }

private:
  struct Resource
  {
    explicit Resource(common::ObIAllocator &allocator)
        : value_(), next_(nullptr), metadata_owner_(allocator) {}
    // clear() releases the payload graph before destroying this bulk Type
    // owner. A failed graph release keeps both the token and metadata alive.
    common::DatumParameter value_;
    Resource *next_;
    common::ObArenaAllocator metadata_owner_;
  };
  NativeSqlValueOwner(const NativeSqlValueOwner &) = delete;
  NativeSqlValueOwner &operator=(const NativeSqlValueOwner &) = delete;
  friend int clone_native_owned_value(const common::SqlType &, const common::Datum &,
                                      NativeSqlValueOwner &, common::Datum &);
  friend int clone_native_owned_value(const common::SqlType &, const common::Datum &,
      NativeSqlValueOwner &, common::Datum &, const INativePlTypeResolver *, bool);
  int clone(const common::SqlType &, const common::Datum &, common::Datum &,
            const INativePlTypeResolver *, bool need_new_allocator, bool prepared_native_type);
  void keep(Resource *resource)
  { resource->next_ = resources_; resources_ = resource; ++count_; }
  common::ObIAllocator &allocator_;
  ObSQLSessionInfo *session_;
  const INativePlTypeResolver *resolver_;
  Resource *resources_;
  int64_t count_;
};

// Native retained SQL-array leaf Types and pointer directories use a private
// bulk metadata arena per Resource; payloads still use the original allocator.
// Primitive values require neither a token nor a declaration lookup. Resource
// token allocation precedes clone; success is published only after typed import.
// Cursor retaining remains explicitly NOT_SUPPORTED until session ownership is
// closed. No existing PL callback is removed, redirected or weakened here.
int clone_native_owned_value(const common::SqlType &type, const common::Datum &source,
                             NativeSqlValueOwner &owner, common::Datum &output);

// Native producer already paired source with this complete Type. The resolver
// is borrowed only for unknown heterogeneous leaves; no current namespace is
// inferred and no borrowed resolver is saved in the owner.
int clone_native_owned_value(const common::SqlType &type, const common::Datum &source,
    NativeSqlValueOwner &owner, common::Datum &output,
    const INativePlTypeResolver *call_resolver, bool need_new_allocator);

} // namespace sql
} // namespace oceanbase
