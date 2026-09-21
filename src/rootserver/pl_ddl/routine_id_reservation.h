/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_ROOTSERVER_ROUTINE_ID_RESERVATION_H_
#define SEEKDB_ROOTSERVER_ROUTINE_ID_RESERVATION_H_

#include <cstdint>
#include <memory>

namespace oceanbase {
namespace share { namespace schema {
class ObSchemaService;
class ObRoutineInfo;
} }
namespace rootserver {

// Host-only ownership of an ID obtained from the real schema allocator. Not a
// wire argument, authorization, schema version, or permission to publish DDL.
// The coordinator must still perform normal admission/locking/transaction work.
// Reserve before compiling references; retain the token until the CREATE write.
// Failed/abandoned reservations leave sequence gaps, never reusable IDs.
class RoutineIdReservation final
{
public:
  RoutineIdReservation();
  ~RoutineIdReservation();
  RoutineIdReservation(RoutineIdReservation &&) noexcept;
  RoutineIdReservation &operator=(RoutineIdReservation &&) noexcept;
  RoutineIdReservation(const RoutineIdReservation &) = delete;
  RoutineIdReservation &operator=(const RoutineIdReservation &) = delete;

  static int reserve(share::schema::ObSchemaService &service,
                     const share::schema::ObRoutineInfo &routine,
                     RoutineIdReservation &output);
  uint64_t id() const;
  // Every attempt consumes the token, including identity mismatch. Caller must
  // use the returned ID for this CREATE only; a later write error is not retryable
  // with the same token. Body/parameters may change during semantic resolution;
  // database, owner, kind, exact name, standalone namespace and ID may not.
  int take(share::schema::ObSchemaService &service,
           const share::schema::ObRoutineInfo &routine, uint64_t &id);

private:
  struct Identity;
  std::unique_ptr<Identity> identity_;
};

} }
#endif
