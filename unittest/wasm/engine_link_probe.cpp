// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
// Link diagnostic only. This is not a JS API or an executable bootstrap test:
// the caller's native configuration types intentionally remain internal.
#include "observer/ob_server.h"
#include "observer/ob_server_options.h"
#include <emscripten/emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE int seekdb_wasm_link_bootstrap(
    const oceanbase::observer::ObServerOptions *options,
    const oceanbase::common::ObPLogWriterCfg *log_config)
{
  if (options == nullptr || log_config == nullptr) {
    return oceanbase::common::OB_INVALID_ARGUMENT;
  }
  auto &server = oceanbase::observer::ObServer::get_instance();
  int ret = server.init(*options, *log_config);
  if (ret == oceanbase::common::OB_SUCCESS) ret = server.start();
  if (ret == oceanbase::common::OB_SUCCESS) ret = server.wait();
  server.destroy();
  return ret;
}

extern "C" EMSCRIPTEN_KEEPALIVE void seekdb_wasm_link_request_stop()
{
  oceanbase::observer::ObServer::get_instance().set_stop();
}
