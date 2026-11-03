/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_EXTENSION_STATEMENT_DIAGNOSTICS_H_
#define SEEKDB_SQL_EXTENSION_STATEMENT_DIAGNOSTICS_H_

#include "lib/oblog/ob_warning_buffer.h"

namespace oceanbase { namespace sql {

// Synchronous, host-only statement scope. Routine resolvers collect persistent
// error_info from the current warning buffer, so each script statement needs an
// empty buffer even when the outer command has accumulated earlier diagnostics.
// Nested scopes merge outwards; the parent pointer is restored on all exits.
class ExtensionStatementDiagnostics final
{
public:
  explicit ExtensionStatementDiagnostics(int &status)
      : parent_(common::ob_get_tsi_warning_buffer()), status_(status)
  {
    common::ob_setup_tsi_warning_buffer(&local_);
  }
  ~ExtensionStatementDiagnostics()
  {
    common::ob_setup_tsi_warning_buffer(parent_);
    if (parent_ != nullptr) {
      const int merged = parent_->append_warnings(local_);
      if (status_ == common::OB_SUCCESS && merged != common::OB_SUCCESS) status_ = merged;
      if (local_.get_err_code() != common::OB_MAX_ERROR_CODE) {
        parent_->set_error(local_.get_err_msg(), local_.get_err_code());
        parent_->set_error_line_column(local_.get_error_line(), local_.get_error_column());
        parent_->set_sql_state(local_.get_sql_state());
      }
    }
  }
  ExtensionStatementDiagnostics(const ExtensionStatementDiagnostics &) = delete;
  ExtensionStatementDiagnostics &operator=(const ExtensionStatementDiagnostics &) = delete;

private:
  common::ObWarningBuffer local_;
  common::ObWarningBuffer *parent_;
  int &status_;
};

} }
#endif
