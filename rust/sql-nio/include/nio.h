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

#ifndef NIO_H
#define NIO_H

/*
 * C ABI for the Rust crate `seekdb_nio` — the reactor that replaces seekdb's
 * C++ ObSqlNioImpl. The C++ `ObSqlNio` class is a thin shim that delegates to
 * these functions; the Rust reactor calls back into the existing
 * `ObSqlSockHandler` through the callback vtable below.
 *
 * Hand-maintained; keep in sync with src/lib.rs.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Reactor nio_reactor;
typedef struct nio_connection_handle nio_connection_handle;

#define NIO_ABI_VERSION 26U

/* nio_start failure reasons, written through its out_err parameter. */
#define NIO_START_OK 0
#define NIO_START_EINVAL 1     /* null addr/cb, bad sizes, thread_count range */
#define NIO_START_EABI 2       /* abi_version != NIO_ABI_VERSION */
#define NIO_START_EADDR 3      /* unparseable listen address */
#define NIO_START_ECALLBACKS 4 /* missing callback fn or null ctx */
#define NIO_START_EIO 5        /* bind / register / thread-spawn failure */
#define NIO_START_ETLS 6       /* tls config given but cert/key/ca load failed */

/* TLS channel encryption. Paths are passed verbatim; Rust inherits the
 * process cwd, so seekdb's relative wallet PEM-path convention works as-is.
 * The strings must stay valid for the duration of the nio_start call.
 * A client certificate is verified when ca_file is set and the client
 * presents one, but never demanded, so certificate-less --ssl-mode=REQUIRED
 * clients still connect. The negotiated TLS metadata is exposed separately
 * by nio_get_tls_session_info; no OpenSSL SSL* crosses this ABI. */
#define NIO_TLS_MIN_NONE 0U
#define NIO_TLS_MIN_TLSV1 1U
#define NIO_TLS_MIN_TLSV1_1 2U
#define NIO_TLS_MIN_TLSV1_2 3U
#define NIO_TLS_MIN_TLSV1_3 4U

typedef struct {
  const char *ca_file;   /* nullable: no client-certificate verification */
  const char *cert_file; /* required: server certificate chain, PEM */
  const char *key_file;  /* required: server private key, PEM (PKCS#8/RSA) */
  uint8_t min_tls_version; /* NIO_TLS_MIN_*; TLS 1.2 is the lowest rustls supports */
  uint8_t reserved[7];
} nio_tls_config;

/* Borrowed Rust-normalized TLS state for the active request. The strings point
 * into the Rustls session and remain valid until the request is committed or
 * aborted; C++ must consume them synchronously. No SSL*, X509*, or DER object
 * crosses this ABI. cipher_name is the Rust-normalized SQL account cipher
 * name. */
typedef struct {
  const char *data;
  int64_t len;
} nio_tls_string_view;

typedef struct {
  uint8_t tls_active;
  uint8_t peer_cert_present;
  uint8_t peer_cert_verified;
  uint8_t peer_cert_info_valid;
  uint8_t reserved[4];
  nio_tls_string_view cipher_name;
  nio_tls_string_view peer_cert_common_name;
  nio_tls_string_view peer_cert_issuer;
  nio_tls_string_view peer_cert_subject;
} nio_tls_session_info;

/* The capability bits both sides consult — the single cross-language
 * vocabulary. Values are pinned against C++'s ObMySQLCapabilityFlags and
 * Rust's capability.rs by the two ABI-check translation units. Negotiation
 * itself is Rust-owned; C++ treats the login view's capabilities as
 * read-only. */
#define NIO_CLIENT_COMPRESS 0x00000020U
#define NIO_CLIENT_PROTOCOL_41 0x00000200U
#define NIO_CLIENT_SSL 0x00000800U
#define NIO_CLIENT_MULTI_STATEMENTS 0x00010000U
#define NIO_CLIENT_MULTI_RESULTS 0x00020000U
#define NIO_CLIENT_SESSION_TRACK 0x00800000U

typedef enum {
  NIO_FRAME_ERROR = -1,
  NIO_FRAME_OK = 0,
  NIO_FRAME_NEED_MORE = 1
} nio_frame_result;

typedef enum {
  NIO_PACKET_LOGIN = 1,
  NIO_PACKET_COMMAND = 2,
  NIO_PACKET_AUTH_SWITCH_RESPONSE = 3
} nio_packet_kind;

/* What the finishing worker's request DID to the auth state, passed to
 * nio_prepare_commit. Rust owns the wire phase machine and computes the next
 * read state itself (Login/ChangeUserAuth + NIO_AUTH_OK -> Command;
 * Login/Command + NIO_AUTH_SWITCH_SENT -> auth-switch response; ordinary
 * command finishers pass NIO_AUTH_NO_CHANGE). This replaces the removed
 * nio_next_read enum, which transmitted the wire state itself and left the
 * phase machine dual-owned. */
typedef enum {
  NIO_AUTH_NO_CHANGE = 0,
  NIO_AUTH_OK = 1,
  NIO_AUTH_SWITCH_SENT = 2
} nio_auth_outcome;

/* Greeting inputs C++ fills during on_connect: sessid and the 20-byte
 * scramble exist as soon as the callback has constructed the session, and
 * version is the display string for HandshakeV10, and status_flags is the
 * server status word captured during connection setup. Replaces the former
 * out-of-header reverse-FFI symbol sm_conn_greeting_info. */
typedef struct {
  uint32_t sessid;
  uint8_t scramble[20];
  uint8_t version[64];
  int64_t version_len;
  uint16_t status_flags;
  uint8_t reserved[6];
} nio_greeting_info;

typedef struct {
  int32_t off;
  int32_t len;
} nio_login_field;

typedef struct {
  int32_t key_off;
  int32_t key_len;
  int32_t value_off;
  int32_t value_len;
} nio_login_attr;

/* Borrowed parser metadata; attrs remains valid until the request's
 * nio_commit_request, which owns releasing the login metadata (the former
 * nio_release_login export was removed with ABI 22). */
typedef struct {
  uint32_t capabilities;
  uint8_t charset;
  uint8_t reserved[3];
  nio_login_field username;
  nio_login_field auth_response;
  nio_login_field database;
  nio_login_field auth_plugin_name;
  int32_t attr_count;
  const nio_login_attr *attrs;
} nio_login_view;

#define NIO_MYSQL_COMMAND_LAYOUT_BYTES 1U
#define NIO_MYSQL_COMMAND_LAYOUT_FIELD_LIST 2U
#define NIO_MYSQL_COMMAND_LAYOUT_U32 3U
#define NIO_MYSQL_COMMAND_LAYOUT_U16 4U
#define NIO_MYSQL_COMMAND_LAYOUT_FETCH 5U
#define NIO_MYSQL_COMMAND_LAYOUT_LONG_DATA 6U
#define NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER 7U
#define NIO_MYSQL_COMMAND_LAYOUT_EXECUTE 8U
#define NIO_MYSQL_COMMAND_LAYOUT_EMPTY 9U
#define NIO_MYSQL_COMMAND_LAYOUT_U8 10U

#define NIO_MYSQL_CHANGE_USER_HAS_CHARSET (1U << 0)
#define NIO_MYSQL_CHANGE_USER_HAS_PLUGIN (1U << 1)
#define NIO_MYSQL_CHANGE_USER_HAS_ATTRS (1U << 2)
#define NIO_MYSQL_CHANGE_USER_SECURE_AUTH (1U << 3)

typedef struct {
  int32_t off;
  int32_t len;
} nio_mysql_command_field;

/* Pointer-free command metadata. Every field is an offset into the complete
 * logical body, including its command byte. The view itself is borrowed only
 * for the synchronous on_readable callback; C++ must copy it before returning.
 */
typedef struct {
  uint32_t command;
  uint32_t layout;
  int64_t scalar0;
  int64_t scalar1;
  nio_mysql_command_field fields[4];
  int64_t scalar2;
} nio_mysql_command_view;

#define NIO_MYSQL_EXECUTE_PARSE_OK 0
#define NIO_MYSQL_EXECUTE_PARSE_INVALID_ARGUMENT -1
#define NIO_MYSQL_EXECUTE_PARSE_MALFORMED -2
#define NIO_MYSQL_EXECUTE_PARSE_UNSUPPORTED -3
#define NIO_MYSQL_EXECUTE_PARSE_CAPACITY -4

#define NIO_MYSQL_EXECUTE_VALUE_NULL 1U
#define NIO_MYSQL_EXECUTE_VALUE_I64 2U
#define NIO_MYSQL_EXECUTE_VALUE_U64 3U
#define NIO_MYSQL_EXECUTE_VALUE_F32_BITS 4U
#define NIO_MYSQL_EXECUTE_VALUE_F64_BITS 5U
#define NIO_MYSQL_EXECUTE_VALUE_BYTES 6U
#define NIO_MYSQL_EXECUTE_VALUE_YEAR 7U
#define NIO_MYSQL_EXECUTE_VALUE_DATE 8U
#define NIO_MYSQL_EXECUTE_VALUE_DATETIME 9U
#define NIO_MYSQL_EXECUTE_VALUE_TIMESTAMP 10U
#define NIO_MYSQL_EXECUTE_VALUE_TIME 11U
#define NIO_MYSQL_EXECUTE_VALUE_LONG_DATA 12U

#define NIO_MYSQL_EXECUTE_PARAM_UNSIGNED (1U << 0)
#define NIO_MYSQL_EXECUTE_PARAM_NEGATIVE (1U << 1)

typedef struct {
  uint16_t mysql_type;
  uint8_t type_flags;
  uint8_t reserved;
} nio_mysql_execute_param_meta;

/* Caller-owned, pointer-free result for one standard COM_STMT_EXECUTE
 * parameter. value_off/value_len are relative to the parameter tail from the
 * command view, not to a Rust allocation. */
typedef struct {
  uint64_t value;
  int32_t value_off;
  int32_t value_len;
  uint32_t days;
  uint32_t microseconds;
  uint16_t year;
  uint16_t mysql_type;
  uint8_t type_flags;
  uint8_t kind;
  uint8_t flags;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t reserved[4];
} nio_mysql_execute_param;

typedef struct {
  uint8_t new_params_bound_flag;
  uint8_t reserved[7];
} nio_mysql_execute_parse_result;

/* Worker-stage, stateless parser for the parameter tail of COM_STMT_EXECUTE.
 * cached_meta is required only when new_params_bound_flag is not 1.
 * long_data[i] is 1 when C++ owns an existing COM_STMT_SEND_LONG_DATA piece;
 * such a parameter consumes no inline value. Outputs are unchanged on error.
 *
 * Routing contract: NIO_MYSQL_EXECUTE_PARSE_UNSUPPORTED is returned if and
 * only if a parameter's declared type is an OceanBase extension type
 * (160..=163, 200..=211, 215..=219 inline on the wire, plus 256..=258 which
 * can only arrive through cached_meta since the wire type field is one byte);
 * detection happens before any standard value byte is consumed, and the
 * caller must then route the ENTIRE tail to the legacy C++ decoder. Every
 * standard MySQL type parses here. */
int nio_parse_mysql_execute_params(
    const char *tail, int64_t tail_len, int64_t param_count,
    const nio_mysql_execute_param_meta *cached_meta, int64_t cached_count,
    const uint8_t *long_data, int64_t long_data_count,
    nio_mysql_execute_param *out_params, int64_t out_capacity,
    nio_mysql_execute_parse_result *out_result);

/* Call-scoped response input views. C++ owns every pointed-to byte. */
typedef struct {
  const char *data;
  int64_t len;
} nio_byte_view;

typedef struct nio_mysql_kv_view {
  nio_byte_view key;
  nio_byte_view value;
#ifdef __cplusplus
  /* OceanBase array containers instantiate a diagnostic printer for T. */
  int64_t to_string(char *, const int64_t) const { return 0; }
#endif
} nio_mysql_kv_view;

/* OK-packet serialization mode, chosen by C++ per packet:
 * - USE_STANDARD_SERIALIZE set: standard MySQL session-state-info layout —
 *   one type-0 (system-variable) track item per entry of system_vars;
 *   user_vars is IGNORED entirely in this mode.
 * - USE_STANDARD_SERIALIZE clear (legacy OceanBase mode): system_vars and
 *   user_vars are joined into a single type-0 blob, entries separated by the
 *   literal "__NULL" marker.
 * SCHEMA_CHANGED / STATE_CHANGED gate the type-1 (schema) and type-2
 * (state-changed) track items in both modes. */
#define NIO_MYSQL_OK_USE_STANDARD_SERIALIZE (1U << 0)
#define NIO_MYSQL_OK_SCHEMA_CHANGED (1U << 1)
#define NIO_MYSQL_OK_STATE_CHANGED (1U << 2)

typedef struct {
  uint64_t affected_rows;
  uint64_t last_insert_id;
  uint32_t capability_flags;
  uint32_t behavior_flags;
  uint16_t status_flags;
  uint16_t warnings;
  uint32_t reserved;
  nio_byte_view message;
  nio_byte_view changed_schema;
  const nio_mysql_kv_view *system_vars;
  int64_t system_var_count;
  const nio_mysql_kv_view *user_vars;
  int64_t user_var_count;
} nio_mysql_ok_view;

typedef struct nio_mysql_field_view {
  nio_byte_view schema;
  nio_byte_view table;
  nio_byte_view org_table;
  nio_byte_view name;
  nio_byte_view org_name;
  nio_byte_view type_owner;
  nio_byte_view type_name;
  uint32_t column_length;
  uint16_t charset;
  uint16_t flags;
  uint32_t field_type;
  uint32_t default_type;
  uint8_t decimals;
  uint8_t reserved[7];
#ifdef __cplusplus
  /* OceanBase array containers instantiate a diagnostic printer for T. */
  int64_t to_string(char *, const int64_t) const { return 0; }
#endif
} nio_mysql_field_view;

#define NIO_MYSQL_ROW_TEXT 0U
#define NIO_MYSQL_ROW_BINARY 1U

#define NIO_MYSQL_CELL_NULL 0U
#define NIO_MYSQL_CELL_LENENC_BYTES 1U
#define NIO_MYSQL_CELL_I8 2U
#define NIO_MYSQL_CELL_I16 3U
#define NIO_MYSQL_CELL_I32 4U
#define NIO_MYSQL_CELL_I64 5U
#define NIO_MYSQL_CELL_F32_BITS 6U
#define NIO_MYSQL_CELL_F64_BITS 7U
#define NIO_MYSQL_CELL_YEAR 8U
#define NIO_MYSQL_CELL_DATE 9U
#define NIO_MYSQL_CELL_DATETIME 10U
#define NIO_MYSQL_CELL_TIME 11U
#define NIO_MYSQL_CELL_BIT 12U
#define NIO_MYSQL_CELL_LEGACY_LENENC_NULL 13U

#define NIO_MYSQL_CELL_TIME_NEGATIVE (1U << 0)

typedef struct nio_mysql_cell_view {
  nio_byte_view bytes;
  uint64_t value;
  uint32_t days;
  uint32_t microseconds;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t kind;
  uint8_t flags;
  uint8_t bit_len;
  uint8_t reserved[6];
#ifdef __cplusplus
  /* OceanBase array containers instantiate a diagnostic printer for T. */
  int64_t to_string(char *, const int64_t) const { return 0; }
#endif
} nio_mysql_cell_view;

typedef struct {
  const nio_mysql_cell_view *cells;
  int64_t cell_count;
  uint32_t protocol;
  uint32_t reserved;
} nio_mysql_row_view;

/* All callbacks run on the io thread. ctx = ObSqlSockHandler*, kept alive by
 * C++ from nio_start until nio_wait_destroy returns; the entry points must be
 * safe to invoke concurrently from all io threads for distinct sessions.
 *
 * Cross-language publication contract: C++ session/connection state reachable
 * from the sess pointer is single-writer-per-in-flight-request. Plain
 * (non-atomic) fields that io-thread callbacks read (e.g. the connection
 * phase) may be written only by the thread currently owning the delivered
 * request, and only before it calls nio_prepare_commit/nio_commit_request —
 * commit's internal ordering is the sole publication edge. Kill/GC threads
 * must never write such fields. */
typedef struct {
  void *ctx;
  // greeting must be fully written before returning 0; Rust builds and sends
  // HandshakeV10 from it immediately after this callback.
  int (*on_connect)(void *ctx, void *sess, int fd, int is_unix,
                    nio_greeting_info *greeting);
  // Rust frames and classifies a packet; C++ builds only the matching legacy
  // ObMySQLRawPacket view, verifies ObMP's phase, and delivers the request.
  // body is a writable generation-scoped lease. It contains one additional
  // NUL sentinel at body[body_len] and remains valid until both the matching
  // nio_commit_request and this on_readable callback have released it.
  // wire_bytes is the exact transport input consumed to produce this logical
  // packet. For compression, one outer frame may contain several logical
  // packets: the first reports the frame and later buffered packets may report
  // zero, while the sum remains exact.
  // command_view is non-NULL only for NIO_PACKET_COMMAND.
  int (*on_readable)(void *ctx, void *sess, char *body, int64_t body_len,
                     uint64_t wire_bytes, int packet_kind,
                     const nio_mysql_command_view *command_view,
                     uint64_t generation);
  // Transport teardown notification. Runs exactly once and may precede
  // on_close while a tenant worker still owns the in-flight request.
  void (*on_disconnect)(void *ctx, void *sess);
  void (*on_close)(void *ctx, void *sess, int err);
} nio_callbacks;

/* Lifecycle. session_size = sizeof(ObSqlSockSession). tls is NULL (with
 * tls_size 0) to disable TLS, or a nio_tls_config
 * (with its sizeof) to advertise CLIENT_SSL and serve TLS; the config is
 * validated before any thread is spawned or port bound, and a bad certificate
 * fails startup with NIO_START_ETLS rather than silently serving cleartext.
 * out_err is null or receives the NIO_START_* reason on a NULL return
 * (NIO_START_OK on success), so an ABI mismatch is distinguishable from a
 * busy port. disable_tcp must be 0 or 1; when it is 1 only the local
 * Unix-socket / named-pipe transport is started. It is appended after the
 * ABI-23 arguments so stale callers still pass out_err in the safe slot. */
nio_reactor *nio_start(const char *addr, uint32_t abi_version,
                       const nio_callbacks *cb, size_t callbacks_size,
                       size_t session_size, size_t thread_count,
                       const nio_tls_config *tls, size_t tls_size,
                       int32_t *out_err, int32_t disable_tcp);
/* Return the actual bound TCP port, including the ephemeral port selected for
 * an address ending in :0. Returns 0 for NULL or a TCP-disabled reactor. */
uint32_t nio_get_bound_tcp_port(const nio_reactor *reactor);
void nio_stop(nio_reactor *reactor);
void nio_wait_destroy(nio_reactor *reactor);
int nio_update_tcp_keepalive_params(nio_reactor *reactor, int32_t enabled,
                                    uint32_t idle, uint32_t interval,
                                    uint32_t count);

/* Acquire one connection-scoped response handle from a live session. The
 * returned opaque handle owns the Rust connection until
 * nio_connection_handle_release(). C++ must release every non-null handle
 * exactly once and before the session storage is reused. Append, flush, and
 * prepare calls must not overlap release. Commit must enter with a live handle;
 * after its entry clone, the lifecycle may run on_close/release while the rest
 * of commit is still executing. A null handle is never usable and may be
 * passed to release as a no-op. */
nio_connection_handle *nio_connection_handle_acquire(void *sess);
void nio_connection_handle_release(nio_connection_handle *connection_handle);

/* Produce the private, versioned packed-row blob consumed only by the
 * generation-scoped packed-row writer below. */
int nio_encode_mysql_packed_row_blob(char *buffer, int64_t buffer_len,
                                     const nio_mysql_row_view *row_view,
                                     int64_t *blob_len);

/* Generation-scoped semantic response writer. connection_handle is the live
 * connection-scoped opaque handle returned by nio_connection_handle_acquire.
 * These calls append directly to Rust-owned storage and advance the Rust-owned
 * inner MySQL sequence. C++ retains only the semantic packet/view for the
 * duration of each call.
 *
 * Blocking contract: once the staged batch crosses Rust's flush threshold, an
 * append publishes it and BLOCKS the calling worker until the client drains
 * the socket; nio_response_flush with is_final == 0 blocks the same way. There
 * is no overall deadline — the unblock paths are client progress, a transport
 * error, or an explicit kill (nio_shutdown / nio_set_shutdown), so kill is the
 * designated way to free a worker pinned by a stalled client. is_final == 1
 * only stages state and never blocks; it is the sole response call legal from
 * inside a callback. */
int nio_response_append_result_header(nio_connection_handle *connection_handle,
                                      uint64_t generation,
                                      uint64_t field_count,
                                      int64_t *framed_len);
int nio_response_append_eof(nio_connection_handle *connection_handle,
                            uint64_t generation,
                            uint8_t field_count, uint16_t warnings,
                            uint16_t status_flags, int64_t *framed_len);
/* Append result header (unless COM_FIELD_LIST), every field, and metadata EOF
 * transactionally under one connection lock. packet_count reports semantic
 * packets for the existing MYSQL_PACKET_OUT statistic. */
int nio_response_append_resultset_metadata(
    nio_connection_handle *connection_handle, uint64_t generation,
    int include_result_header, const nio_mysql_field_view *fields,
    int64_t field_count, uint8_t eof_field_count, uint16_t warnings,
    uint16_t status_flags, int64_t *packet_count, int64_t *framed_len);
int nio_response_append_prepare_ok(nio_connection_handle *connection_handle,
                                   uint64_t generation,
                                   uint32_t statement_id, uint16_t column_count,
                                   uint16_t parameter_count, uint16_t warnings,
                                   int64_t *framed_len);
int nio_response_append_error(nio_connection_handle *connection_handle,
                              uint64_t generation,
                              uint16_t error_code, const char *sql_state,
                              int64_t sql_state_len, const char *message,
                              int64_t message_len, int64_t *framed_len);
int nio_response_append_auth_switch(nio_connection_handle *connection_handle,
                                    uint64_t generation,
                                    const char *plugin_name,
                                    int64_t plugin_name_len,
                                    const char *scramble, int64_t scramble_len,
                                    int64_t *framed_len);
int nio_response_append_local_infile(nio_connection_handle *connection_handle,
                                     uint64_t generation,
                                     const char *filename, int64_t filename_len,
                                     int64_t *framed_len);
int nio_response_append_string(nio_connection_handle *connection_handle,
                               uint64_t generation,
                               const char *value, int64_t value_len,
                               int64_t *framed_len);
int nio_response_append_ok(nio_connection_handle *connection_handle,
                           uint64_t generation,
                           const nio_mysql_ok_view *ok_view,
                           int64_t *framed_len);
int nio_response_append_field(nio_connection_handle *connection_handle,
                              uint64_t generation,
                              const nio_mysql_field_view *field_view,
                              int64_t *framed_len);
int nio_response_append_row(nio_connection_handle *connection_handle,
                            uint64_t generation,
                            const nio_mysql_row_view *row_view,
                            int64_t *framed_len);
int nio_response_append_packed_row_blob(nio_connection_handle *connection_handle,
                                        uint64_t generation,
                                        const char *blob, int64_t blob_len,
                                        int64_t *framed_len);
int nio_response_flush(nio_connection_handle *connection_handle,
                       uint64_t generation,
                       int is_final);

/* Generation-scoped finalization uses the same opaque connection handle.
 * auth_outcome is a nio_auth_outcome value; see its definition. */
int nio_prepare_commit(nio_connection_handle *connection_handle,
                       uint64_t generation,
                       int auth_outcome);
void nio_commit_request(nio_connection_handle *connection_handle,
                        uint64_t generation);

/* Per-connection (void* sess) — the remaining ObSqlNio method surface. */
int nio_set_shutdown(void *sess, uint64_t generation);
void nio_shutdown(void *sess);
void nio_bind_sql_session(void *sess);
int nio_release_sql_session(void *sess);
/* Blocking generation-scoped read; timeout_us < 0 waits indefinitely.
 * Returns 1 packet, 0 timeout/interrupt, -1 error. The former read_handle
 * token carried no information beyond the generation, so mid-request reads
 * are addressed by (sess, generation) directly. */
int nio_wait_one_packet(void *sess, uint64_t generation, int64_t timeout_us,
                        char **body, int64_t *body_len, uint64_t *packet_lease);
/* generation == 0 atomically targets the currently active request. */
int nio_interrupt_read(void *sess, uint64_t generation);
int nio_release_read_packet(void *sess, uint64_t generation,
                            uint64_t packet_lease);
int nio_get_login_view(void *sess, uint64_t generation, nio_login_view *out);
/* The returned certificate pointer is borrowed from the active Rustls
 * connection; it must not be retained after the current request finishes. */
int nio_get_tls_session_info(void *sess, uint64_t generation,
                             nio_tls_session_info *out);

#ifdef __cplusplus
}
#endif

#endif /* NIO_H */
