/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SHARE_EXTENSION_ROUTINE_UPDATE_H_
#define SEEKDB_SHARE_EXTENSION_ROUTINE_UPDATE_H_

#include <cstdint>
#include <functional>
#include <string>
#include "lib/ob_errno.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase {
namespace obcall { struct ObCreateRoutineArg; struct ObDropRoutineArg; }
namespace share { namespace schema { class ObSchemaGetterGuard; } }
namespace share { namespace plugin {
struct ExtensionUpdateRequest;
struct ExtensionInstallSpec;

// Core-only ordered DDL input, NOT an installed plugin ABI or authority token.
// CREATE and ALTER use the existing create/alter RPC payload; DROP uses its own
// payload. Exactly one pointer is present. No sorting/deduplication is permitted:
// DROP x; CREATE x and CREATE x; DROP x have different schema/member effects.
// The executor must still resolve/fence objects, check privileges/dependencies,
// and use the update coordinator's transaction. These views grant no authority.
struct ExtensionRoutineUpdateOperation
{
  enum class Kind : uint8_t { INVALID, CREATE, DROP, ALTER };
  Kind kind_ = Kind::INVALID;
  const obcall::ObCreateRoutineArg *create_arg_ = nullptr;
  const obcall::ObDropRoutineArg *drop_arg_ = nullptr;
  // Container diagnostics intentionally omit SQL text and argument pointers.
  TO_STRING_KV("kind", static_cast<uint32_t>(kind_));

  bool has_valid_shape() const
  {
    return ((kind_ == Kind::CREATE || kind_ == Kind::ALTER)
             && create_arg_ != nullptr && drop_arg_ == nullptr)
        || (kind_ == Kind::DROP && create_arg_ == nullptr && drop_arg_ != nullptr);
  }
};

// In-process Query -> Root semantic callback, NOT a native plugin service. The
// host binds an authenticated session and immutable parsed script. Root supplies
// the current transaction-local schema view; the callback never executes DDL.
// Successful operation views must remain owned/alive until this object dies,
// including after later resolve calls. The caller must not retain them longer.
class IExtensionRoutineScript
{
public:
  virtual ~IExtensionRoutineScript() = default;
  virtual int64_t statement_count() const = 0;
  virtual int preflight(const ExtensionUpdateRequest &request, std::string &error) = 0;
  // An update-only callback must never be silently reused for installation.
  virtual int preflight_install(const ExtensionInstallSpec &, std::string &)
  { return common::OB_NOT_SUPPORTED; }
  // Runs under the locked host view even for an empty/no-op script.
  virtual int validate_view(schema::ObSchemaGetterGuard &view, std::string &error) = 0;
  virtual int resolve(int64_t index, schema::ObSchemaGetterGuard &view,
      const ExtensionRoutineUpdateOperation *&operation, std::string &error) = 0;
  using StageRoutine = std::function<int(const ExtensionRoutineUpdateOperation &, uint64_t &)>;
  virtual bool has_builder() const { return false; }
  // Installation only. Root supplies admission/reservation/staging and retains
  // transaction ownership. Returned IDs are provisional until final commit.
  virtual int build(schema::ObSchemaGetterGuard &, const StageRoutine &, std::string &)
  { return common::OB_SUCCESS; }
};

// Shared synchronous driver. Preflight of the entire script must precede this
// call. admit_and_stage must authorize/reserve/stage ONE successful operation
// before returning; otherwise the next resolver would see the wrong schema.
// No transaction ownership or authorization is conferred by this loop itself.
template <typename AdmitAndStage>
int resolve_extension_routine_sequence(IExtensionRoutineScript &script, int64_t count,
    schema::ObSchemaGetterGuard &view, AdmitAndStage &&admit_and_stage, std::string &error)
{
  if (count < 0 || count > 4096 || count != script.statement_count()) return common::OB_INVALID_ARGUMENT;
  int ret = script.validate_view(view, error);
  for (int64_t i = 0; ret == common::OB_SUCCESS && i < count; ++i) {
    const ExtensionRoutineUpdateOperation *operation = nullptr;
    if (script.statement_count() != count) ret = common::OB_STATE_NOT_MATCH;
    else ret = script.resolve(i, view, operation, error);
    if (ret == common::OB_SUCCESS && script.statement_count() != count) ret = common::OB_STATE_NOT_MATCH;
    if (ret == common::OB_SUCCESS && (operation == nullptr || !operation->has_valid_shape()))
      ret = common::OB_ERR_UNEXPECTED;
    if (ret == common::OB_SUCCESS) ret = admit_and_stage(*operation);
  }
  return ret;
}

} } }
#endif
