/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_SESSION_OB_DEADLOCK_SESSION_H_
#define OCEANBASE_QUERY_API_SESSION_OB_DEADLOCK_SESSION_H_

#include <stdint.h>
#include "lib/net/ob_addr.h"
#include "lib/ob_define.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace query
{

// Stable session observations used by deadlock detection and lock-wait
// maintenance.  The query text is a borrowed view and remains valid only while
// the guard that produced these facts is alive.
struct ObDeadlockSessionFacts
{
  ObDeadlockSessionFacts()
    : has_transaction_(false),
      transaction_id_(),
      transaction_scheduler_(),
      transaction_start_ts_(0),
      query_timeout_us_(0),
      current_query_()
  {}

  bool has_transaction_;
  int64_t transaction_id_;
  common::ObAddr transaction_scheduler_;
  int64_t transaction_start_ts_;
  int64_t query_timeout_us_;
  common::ObString current_query_;
};

// Facts used by the lock-wait queue's periodic liveness check.  Keeping this
// separate avoids making deadlock reporting invoke session-termination checks,
// which have their own logging semantics.
struct ObLockWaitSessionFacts
{
  ObLockWaitSessionFacts()
    : is_terminated_(false),
      terminate_error_(common::OB_SUCCESS),
      has_transaction_(false),
      transaction_id_(),
      autocommit_(false),
      has_explicit_transaction_(false),
      server_session_id_(common::INVALID_SESSID)
  {}

  bool is_terminated_;
  int terminate_error_;
  bool has_transaction_;
  int64_t transaction_id_;
  bool autocommit_;
  bool has_explicit_transaction_;
  uint32_t server_session_id_;
};

// Query-owned capability consumed by storage deadlock handling. Implementations
// keep the concrete session type private; Observer injects the implementation
// into tenant storage services during composition.
class ObIDeadlockSessionService
{
public:
  virtual ~ObIDeadlockSessionService() = default;
  virtual int acquire_session(uint32_t session_id, void *&session) = 0;
  virtual void release_session(void *session) = 0;
  virtual int get_deadlock_facts(
      const void *session, ObDeadlockSessionFacts &facts) const = 0;
  virtual int get_lock_wait_facts(
      const void *session, ObLockWaitSessionFacts &facts) const = 0;
  virtual int mark_transaction_victim(void *session) = 0;
  virtual int mark_statement_victim(void *session) = 0;
};

// Owns a reference to a query session without exposing its implementation type.
// A successful acquire keeps the session alive until this guard is destroyed or
// reused for another session.
class ObDeadlockSessionGuard
{
public:
  explicit ObDeadlockSessionGuard(ObIDeadlockSessionService &service);
  ObDeadlockSessionGuard(const ObDeadlockSessionGuard &) = delete;
  ObDeadlockSessionGuard &operator=(const ObDeadlockSessionGuard &) = delete;
  ~ObDeadlockSessionGuard();

  int acquire(uint32_t session_id);
  bool is_valid() const;
  int get_deadlock_facts(ObDeadlockSessionFacts &facts) const;
  int get_lock_wait_facts(ObLockWaitSessionFacts &facts) const;
  int mark_transaction_victim();
  int mark_statement_victim();

private:
  void reset_();

private:
  ObIDeadlockSessionService *service_;
  void *session_;
};

// Report whether a server session can still own session-scoped resources.
// Missing, null, killed, or otherwise terminated sessions are normal negative
// results rather than lookup failures.
int is_session_alive(ObIDeadlockSessionService &service,
                     uint32_t server_session_id,
                     bool &is_alive);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_SESSION_OB_DEADLOCK_SESSION_H_
