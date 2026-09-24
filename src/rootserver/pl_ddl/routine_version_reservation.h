/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_ROOTSERVER_ROUTINE_VERSION_RESERVATION_H_
#define SEEKDB_ROOTSERVER_ROUTINE_VERSION_RESERVATION_H_

#include <cstdint>
#include <memory>

namespace oceanbase {
namespace common { class ObMySQLTransaction; }
namespace share { namespace schema {
class ObMultiVersionSchemaService;
class ObRoutineInfo;
} }
namespace rootserver {

// Host-only ownership of real schema versions for one CREATE/replace/DROP write.
// Reserve after identity/permission admission, then stage version() in the
// transaction's resolver view. No version is invented or substituted at apply.
// ALTER also reserves the old-parameter deletion version, before the new version.
// Native tokens pin the slot and input signature across reservation and take;
// changing either requires a different object, not an ALTER of this identity.
// This token is NOT authorization, a wire field, or a durable transaction ID.
// Its owner must destroy it on transaction end and never restart/reuse that
// transaction object while tokens remain. Abandoned versions are not recycled.
class RoutineVersionReservation final
{
public:
  RoutineVersionReservation();
  ~RoutineVersionReservation();
  RoutineVersionReservation(RoutineVersionReservation &&) noexcept;
  RoutineVersionReservation &operator=(RoutineVersionReservation &&) noexcept;
  RoutineVersionReservation(const RoutineVersionReservation &) = delete;
  RoutineVersionReservation &operator=(const RoutineVersionReservation &) = delete;

  // routine already has its reserved (CREATE) or existing (ALTER) object ID.
  // old_routine is null only for CREATE. Input schema is not modified.
  static int reserve(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const share::schema::ObRoutineInfo &routine,
      const share::schema::ObRoutineInfo *old_routine, RoutineVersionReservation &output);
  static int reserve_drop(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const share::schema::ObRoutineInfo &routine,
      RoutineVersionReservation &output);
  int64_t version() const;
  // Every take attempt consumes the token, including mismatch/failure. The
  // input routine must carry version(); all identity fields and, for ALTER,
  // the old version/parameter count must still match. Body may be re-resolved.
  int take(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const share::schema::ObRoutineInfo &routine,
      const share::schema::ObRoutineInfo *old_routine,
      int64_t &version, int64_t &delete_parameters_version);
  int take_drop(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const share::schema::ObRoutineInfo &routine,
      int64_t &version);

private:
  static int reserve_impl(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const share::schema::ObRoutineInfo &routine,
      const share::schema::ObRoutineInfo *old_routine, bool drop, RoutineVersionReservation &output);
  int take_impl(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const share::schema::ObRoutineInfo &routine,
      const share::schema::ObRoutineInfo *old_routine, bool drop,
      int64_t &version, int64_t &delete_parameters_version);
  struct Identity;
  std::unique_ptr<Identity> identity_;
};

} }
#endif
