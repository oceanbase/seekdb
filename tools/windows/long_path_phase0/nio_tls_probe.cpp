// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real sql-nio SSLRequest over its discovered Windows named pipe.
#include "nio.h"
#include "path_context.h"
#include "path_fixture.h"
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace seekdb_phase0;
void check(bool ok, const char *stage) { if (!ok) throw std::runtime_error(stage); }
std::string utf8(const std::wstring &s) {
  std::string out; check(bool(path_to_utf8(s, out)), "encode UTF-8"); return out;
}
struct Handle {
  HANDLE h;
  explicit Handle(HANDLE v) : h(v) { check(h && h != INVALID_HANDLE_VALUE, "open Windows handle"); }
  ~Handle() { CloseHandle(h); }
  Handle(const Handle &) = delete;
};
template<class T, void (*Free)(T *)> using Owner = std::unique_ptr<T, decltype(Free)>;
using Key = Owner<EVP_PKEY, EVP_PKEY_free>;
using Cert = Owner<X509, X509_free>;
Key key() {
  Owner<EVP_PKEY_CTX, EVP_PKEY_CTX_free> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
  check(ctx && EVP_PKEY_keygen_init(ctx.get()) == 1 && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) == 1, "RSA setup");
  EVP_PKEY *p = nullptr; check(EVP_PKEY_keygen(ctx.get(), &p) == 1, "RSA generation"); return Key(p, EVP_PKEY_free);
}
Cert certificate(EVP_PKEY *key, X509 *issuer, EVP_PKEY *signer, bool ca, long serial) {
  Cert cert(X509_new(), X509_free); check(bool(cert), "X509 allocation");
  check(X509_set_version(cert.get(), 2) == 1 && ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial) == 1 &&
      X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60) && X509_gmtime_adj(X509_getm_notAfter(cert.get()), 86400) &&
      X509_set_pubkey(cert.get(), key) == 1, "X509 fields");
  X509_NAME *name = X509_get_subject_name(cert.get());
  check(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
      reinterpret_cast<const unsigned char *>(ca ? "seek533-test-ca" : "localhost"), -1, -1, 0) == 1 &&
      X509_set_issuer_name(cert.get(), issuer ? X509_get_subject_name(issuer) : name) == 1, "X509 names");
  X509V3_CTX ctx{}; X509V3_set_ctx(&ctx, issuer ? issuer : cert.get(), cert.get(), nullptr, nullptr, 0);
  auto extension = [&](int nid, const char *value) {
    Owner<X509_EXTENSION, X509_EXTENSION_free> ext(X509V3_EXT_conf_nid(nullptr, &ctx, nid, const_cast<char *>(value)), X509_EXTENSION_free);
    check(ext && X509_add_ext(cert.get(), ext.get(), -1) == 1, "X509 extension");
  };
  extension(NID_basic_constraints, ca ? "critical,CA:TRUE" : "critical,CA:FALSE");
  extension(NID_key_usage, ca ? "critical,keyCertSign,cRLSign" : "critical,digitalSignature,keyEncipherment");
  if (!ca) { extension(NID_ext_key_usage, "serverAuth,clientAuth"); extension(NID_subject_alt_name, "DNS:localhost"); }
  check(X509_sign(cert.get(), signer, EVP_sha256()) > 0, "X509 sign"); return cert;
}
std::string pem(X509 *cert, EVP_PKEY *key) {
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
  check(bio && (cert ? PEM_write_bio_X509(bio.get(), cert) : PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr)) == 1, "PEM serialize");
  char *p = nullptr; const long n = BIO_get_mem_data(bio.get(), &p); check(n > 0, "PEM contents"); return std::string(p, n);
}
void write_file(const std::wstring &path, const std::string &data) {
  Handle file(CreateFileW(extended_path(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  DWORD n = 0; check(WriteFile(file.h, data.data(), static_cast<DWORD>(data.size()), &n, nullptr) && n == data.size(), "write fixture");
}
struct State { std::atomic<int> login{0}, closed{0}, info{-1}; };
int connect_cb(void *, void *, int, int local, nio_greeting_info *g) {
  if (!local) return -1;
  g->sessid = 533; std::memset(g->scramble, 'a', sizeof(g->scramble));
  const char v[] = "seek533-tls"; std::memcpy(g->version, v, sizeof(v)-1); g->version_len = sizeof(v)-1; g->status_flags = 2; return 0;
}
int readable_cb(void *ctx, void *sess, char *, int64_t, uint64_t, int kind, const nio_mysql_command_view *, uint64_t generation) {
  auto *s = static_cast<State *>(ctx); nio_tls_session_info info{};
  if (kind == NIO_PACKET_LOGIN && nio_get_tls_session_info(sess, generation, &info) == 0 && info.tls_active && info.cipher_name.len > 0) {
    s->info.store(info.peer_cert_present ? (info.peer_cert_verified ? 2 : 1) : 0);
    ++s->login;
  }
  return -1;
}
void disconnect_cb(void *, void *) {}
void close_cb(void *ctx, void *, int) { ++static_cast<State *>(ctx)->closed; }
struct Reactor {
  nio_reactor *p;
  explicit Reactor(nio_reactor *v) : p(v) {}
  ~Reactor() { if (p) { nio_stop(p); nio_wait_destroy(p); } }
  Reactor(const Reactor &) = delete;
};
// Each operation has a deadline; drain cancellation before releasing its buffer.
// The wrapper also bounds the entire native process.
DWORD pipe_io(HANDLE pipe, bool writing, void *bytes, DWORD length) {
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); OVERLAPPED ov{}; ov.hEvent = event.h;
  DWORD count = 0; const BOOL ready = writing ? WriteFile(pipe, bytes, length, &count, &ov) : ReadFile(pipe, bytes, length, &count, &ov);
  if (!ready) {
    check(GetLastError() == ERROR_IO_PENDING, "pipe I/O initiation");
    if (WaitForSingleObject(event.h, 5000) != WAIT_OBJECT_0) {
      CancelIoEx(pipe, &ov); GetOverlappedResult(pipe, &ov, &count, TRUE);
      throw std::runtime_error("pipe I/O deadline");
    }
    check(GetOverlappedResult(pipe, &ov, &count, FALSE), "pipe I/O completion");
  }
  check(count > 0, "pipe EOF"); return count;
}
void transfer(HANDLE pipe, bool writing, char *p, size_t n) {
  while (n) { const DWORD done = pipe_io(pipe, writing, p, static_cast<DWORD>(n)); p += done; n -= done; }
}
void drain(SSL *ssl, HANDLE pipe) {
  char data[16384]; BIO *bio = SSL_get_wbio(ssl);
  while (BIO_ctrl_pending(bio)) { int n = BIO_read(bio, data, sizeof(data)); check(n > 0, "TLS BIO read"); transfer(pipe, true, data, n); }
}
std::wstring endpoint(const std::wstring &run) {
  Handle file(CreateFileW(extended_path(run + L"\\sql.pipe").c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  char data[128]; DWORD n = 0; check(ReadFile(file.h, data, sizeof(data), &n, nullptr) && n > 0 && n < sizeof(data), "discovery read");
  std::string bare(data, n); check(bare.find_first_not_of("0123456789-") == std::string::npos, "discovery format");
  return L"\\\\.\\pipe\\" + std::wstring(bare.begin(), bare.end());
}
void handshake(const std::wstring &run, X509 *ca, X509 *leaf, EVP_PKEY *key, bool client_cert, State &state, int version, int rejection) {
  Handle pipe(CreateFileW(endpoint(run).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
  char header[4]; transfer(pipe.h, false, header, 4);
  size_t n = static_cast<unsigned char>(header[0]) | (static_cast<unsigned char>(header[1]) << 8) | (static_cast<unsigned char>(header[2]) << 16);
  check(n > 16 && n < 4096 && header[3] == 0, "MySQL greeting header");
  std::vector<char> greeting(n); transfer(pipe.h, false, greeting.data(), n);
  check(greeting[0] == 10 && std::memcmp(greeting.data()+1, "seek533-tls", 11) == 0, "MySQL greeting identity");
  std::vector<char> request(36, 0); request[0] = 32; request[3] = 1;
  request[5] = 0x0a; // CLIENT_PROTOCOL_41 | CLIENT_SSL, little endian.
  request[12] = 45; transfer(pipe.h, true, request.data(), request.size());
  Owner<SSL_CTX, SSL_CTX_free> ctx(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
  check(ctx && X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx.get()), ca) == 1, "client trust");
  SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
  if (version) check(SSL_CTX_set_min_proto_version(ctx.get(), version)==1 && SSL_CTX_set_max_proto_version(ctx.get(), version)==1, "exact TLS version");
  struct Trace { int hello=0; int certificate=0; } trace;
  SSL_CTX_set_msg_callback(ctx.get(), [](int writing, int, int type, const void *data, size_t size, SSL *, void *arg) {
    if (!writing || type!=SSL3_RT_HANDSHAKE || size<4) return;
    auto *t=static_cast<Trace *>(arg); const auto *bytes=static_cast<const unsigned char *>(data);
    if (bytes[0]==SSL3_MT_CLIENT_HELLO) ++t->hello;
    // Empty Certificate is 7 bytes in TLS 1.2, 8 in TLS 1.3 (empty context).
    if (bytes[0]==SSL3_MT_CERTIFICATE && size>8) ++t->certificate;
  });
  SSL_CTX_set_msg_callback_arg(ctx.get(), &trace);
  if (client_cert) check(SSL_CTX_use_certificate(ctx.get(), leaf) == 1 && SSL_CTX_use_PrivateKey(ctx.get(), key) == 1, "client identity");
  Owner<SSL, SSL_free> ssl(SSL_new(ctx.get()), SSL_free); check(bool(ssl), "SSL allocation");
  BIO *in = BIO_new(BIO_s_mem()), *out = BIO_new(BIO_s_mem());
  if (!in || !out) { BIO_free(in); BIO_free(out); throw std::runtime_error("BIO allocation"); }
  BIO_set_mem_eof_return(in, -1); SSL_set_bio(ssl.get(), in, out);
  check(SSL_set1_host(ssl.get(), "localhost") == 1 && SSL_set_tlsext_host_name(ssl.get(), "localhost") == 1, "TLS host verification");
  SSL_set_connect_state(ssl.get()); const ULONGLONG deadline = GetTickCount64() + 10000;
  try {
  for (;;) {
    int result = SSL_do_handshake(ssl.get()); int error = result == 1 ? SSL_ERROR_NONE : SSL_get_error(ssl.get(), result);
    drain(ssl.get(), pipe.h); if (result == 1) break;
    check(GetTickCount64() < deadline && error == SSL_ERROR_WANT_READ, "TLS handshake failed");
    char data[16384]; DWORD got = pipe_io(pipe.h, false, data, sizeof(data)); check(BIO_write(in, data, got) == static_cast<int>(got), "TLS BIO feed");
  }
  check(SSL_get_verify_result(ssl.get()) == X509_V_OK, "TLS peer verify");
  if (version) check(SSL_version(ssl.get())==version, "negotiated TLS version");
  // Encrypted HandshakeResponse, sequence 2. The actual Rust parser dispatches
  // LOGIN only after TLS handshake/authentication and packet parsing complete.
  std::vector<char> login(4 + 32 + 5 + 1, 0); login[0] = static_cast<char>(login.size()-4); login[3] = 2;
  login[5] = 0x0a; login[12] = 45; std::memcpy(login.data()+36, "test", 4);
  check(SSL_write(ssl.get(), login.data(), static_cast<int>(login.size())) == login.size(), "encrypted MySQL login");
  drain(ssl.get(), pipe.h);
  // Keep the transport alive until the server consumes the encrypted login.
  for (int i=0; i<200 && state.login==0 && state.closed==0; ++i) Sleep(10);
  check(state.login==1, "server did not consume encrypted login");
  } catch (const std::runtime_error &) {
    if (!rejection) throw;
    // Observe a server-initiated close while our pipe is still open; merely
    // closing the client after an arbitrary error is not rejection evidence.
    for (int i=0; i<200 && state.closed==0; ++i) Sleep(10);
    check(state.closed==1 && state.login==0 && trace.hello>0, "server rejection evidence missing");
    if (rejection==1) check(trace.certificate>0 && SSL_get0_peer_certificate(ssl.get()) && SSL_get_verify_result(ssl.get())==X509_V_OK,
        "untrusted client certificate was not sent or server was not trusted");
    std::cout << "NIO_TLS_HANDSHAKE_REJECT_PASS kind=" << rejection << " client_version=" << version
        << " certificate_sent=" << trace.certificate << '\n';
    return;
  }
  check(rejection==0, "invalid TLS client unexpectedly reached LOGIN");
}
void connection_case(const std::wstring &run, const std::wstring &wallet, X509 *ca, X509 *leaf, EVP_PKEY *key, int mode, int client_version=0, uint8_t server_min=NIO_TLS_MIN_TLSV1_2, int rejection=0) {
  State state; nio_callbacks callbacks{&state, connect_cb, readable_cb, disconnect_cb, close_cb};
  std::string dir=utf8(run), ca_path=utf8(wallet+L"\\ca.pem"), cert_path=utf8(wallet+L"\\cert.pem"), key_path=utf8(wallet+L"\\key.pem");
  nio_tls_config tls{}; tls.ca_file=mode ? ca_path.c_str() : nullptr; tls.cert_file=cert_path.c_str(); tls.key_file=key_path.c_str(); tls.min_tls_version=server_min;
  int32_t error=-1;
  {
    Reactor reactor(nio_start_v27("127.0.0.1:0", NIO_ABI_VERSION, &callbacks, sizeof(callbacks), 64, 1, &tls, sizeof(tls), &error, 1, dir.data(), dir.size()));
    check(reactor.p && error==NIO_START_OK && nio_get_bound_tcp_port(reactor.p)==0, "NIO TLS start");
    dir.assign("overwritten"); ca_path.assign("overwritten"); cert_path.assign("overwritten"); key_path.assign("overwritten");
    handshake(run, ca, leaf, key, mode==2, state, client_version, rejection);
    if (rejection) check(state.login==0 && state.info==-1, "rejected connection exposed a login");
    else check(state.login==1 && state.info==(mode==2 ? 2 : 0), "server TLS login/session evidence");
  }
  check(state.closed==1, "TLS callback cleanup");
  check(std::filesystem::is_empty(extended_path(run)), "TLS discovery cleanup");
}
void rejected(const std::wstring &run, const std::wstring &wallet, const std::string &cert_pem, const std::string &key_pem, const std::string &mismatched_key) {
  State state; nio_callbacks callbacks{&state, connect_cb, readable_cb, disconnect_cb, close_cb};
  const std::string dir=utf8(run), ca=utf8(wallet+L"\\ca.pem"), cert=utf8(wallet+L"\\cert.pem"), key=utf8(wallet+L"\\key.pem"), missing=utf8(wallet+L"\\absent.pem"), invalid="C:\\\xc0\xaf";
  for (int variant=0; variant<8; ++variant) {
    write_file(wallet+L"\\cert.pem", variant==3 ? "" : variant==4 ? "invalid PEM\n" : cert_pem);
    write_file(wallet+L"\\key.pem", variant==5 ? "invalid PEM\n" : variant==7 ? mismatched_key : key_pem);
    nio_tls_config tls{}; tls.ca_file=variant==2 ? missing.c_str() : ca.c_str();
    tls.cert_file=variant==0 ? missing.c_str() : variant==6 ? invalid.c_str() : cert.c_str();
    tls.key_file=variant==1 ? missing.c_str() : key.c_str(); int32_t error=-1;
    Reactor reactor(nio_start_v27("127.0.0.1:0", NIO_ABI_VERSION, &callbacks, sizeof(callbacks), 64, 1, &tls, sizeof(tls), &error, 1, dir.data(), dir.size()));
    check(!reactor.p && error==NIO_START_ETLS && state.closed==0 && state.login==0 && !std::filesystem::exists(extended_path(run)), "ETLS rejection before resources");
    std::cout << "NIO_TLS_REJECT_PASS variant=" << variant << '\n';
  }
  write_file(wallet+L"\\cert.pem", cert_pem); write_file(wallet+L"\\key.pem", key_pem);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  try {
    check(argc==3, "usage: nio_tls_probe owned-empty-root policy"); const std::wstring root=argv[1];
    check(std::filesystem::is_empty(root), "root must be empty");
    PathContext before; check(bool(before.initialize(utf8(root))), "capture cwd");
    DWORD policy=0, size=sizeof(policy);
    check(RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"LongPathsEnabled", RRF_RT_REG_DWORD, nullptr, &policy, &size)==ERROR_SUCCESS && utf8(argv[2])==std::to_string(policy), "policy identity");
    const HRSRC resource=FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
    check(resource!=nullptr, "manifest resource");
    const HGLOBAL loaded=LoadResource(nullptr, resource); check(loaded!=nullptr, "manifest load");
    const auto *contents=static_cast<const char *>(LockResource(loaded)); check(contents!=nullptr, "manifest contents");
    const std::string manifest(contents, SizeofResource(nullptr, resource));
    check(manifest.find(">true</longPathAware>")!=std::string::npos, "longPathAware manifest");
    // Taking an imported function's address can identify an executable import
    // thunk. Query the loaded DLL modules themselves, as in the gRPC probe.
    for (const wchar_t *name : {L"libcrypto-3-x64.dll", L"libssl-3-x64.dll"}) {
      HMODULE module=GetModuleHandleW(name); check(module!=nullptr, "loaded TLS DLL");
      std::wstring path(512, L'\0');
      for (;;) { DWORD n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size())); check(n>0, "TLS module path");
        if(n<path.size()) {path.resize(n); break;} path.resize(path.size()*2); }
      std::cout << "NIO_TLS_MODULE=" << utf8(path) << '\n';
    }
    std::cout << "NIO_TLS_RUNTIME policy=" << policy << " openssl=" << OpenSSL_version(OPENSSL_VERSION) << '\n';
    auto ca_key=key(), leaf_key=key(); auto ca=certificate(ca_key.get(), nullptr, ca_key.get(), true, 1);
    auto leaf=certificate(leaf_key.get(), ca.get(), ca_key.get(), false, 2);
    const auto ca_pem=pem(ca.get(), nullptr), cert_pem=pem(leaf.get(), nullptr), key_pem=pem(nullptr, leaf_key.get());
    int cases=0;
    for (bool unicode : {false, true}) {
      for (size_t length : {root.size()+20, size_t(280), size_t(600), size_t(1200), size_t(2048), size_t(259-9), size_t(260-9), size_t(261-9), size_t(4096-9)}) {
        const auto path=directory_at_length(std::u16string(root.begin(),root.end()), length, unicode);
        const std::wstring wallet(path.begin(),path.end()); std::filesystem::create_directories(extended_path(wallet));
        write_file(wallet+L"\\ca.pem", ca_pem); write_file(wallet+L"\\cert.pem", cert_pem); write_file(wallet+L"\\key.pem", key_pem);
        for (int mode=0; mode<3; ++mode) connection_case(root+L"\\run", wallet, ca.get(), leaf.get(), leaf_key.get(), mode);
        std::cout << "NIO_TLS_PATH_PASS wallet_units=" << wallet.size() << " cert_units=" << wallet.size()+9 << " unicode=" << unicode << " modes=3\n"; ++cases;
      }
    }
    // Combine long discovery and long PEM paths; previous matrices exercised
    // those boundaries separately. Exact versions prove no silent negotiation.
    const auto long_base16=directory_at_length(std::u16string(root.begin(),root.end()), 2048, true);
    const std::wstring long_base(long_base16.begin(),long_base16.end());
    const std::wstring long_run=long_base+L"\\run";
    const auto long_wallet16=directory_at_length(std::u16string(root.begin(),root.end()), 4096-9, true);
    const std::wstring long_wallet(long_wallet16.begin(),long_wallet16.end());
    auto untrusted_key=key();
    auto untrusted_ca=certificate(untrusted_key.get(), nullptr, untrusted_key.get(), true, 3);
    auto untrusted_leaf=certificate(leaf_key.get(), untrusted_ca.get(), untrusted_key.get(), false, 4);
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
      for (int mode=0; mode<3; ++mode) connection_case(long_run, long_wallet, ca.get(), leaf.get(), leaf_key.get(), mode, version);
      connection_case(long_run, long_wallet, ca.get(), untrusted_leaf.get(), leaf_key.get(), 2, version, NIO_TLS_MIN_TLSV1_2, 1);
      std::cout << "NIO_TLS_COMBINED_PASS base_units=2048 cert_units=4096 version=" << version << " modes=3\n";
    }
    connection_case(long_run, long_wallet, ca.get(), leaf.get(), leaf_key.get(), 2, TLS1_3_VERSION, NIO_TLS_MIN_TLSV1_3);
    connection_case(long_run, long_wallet, ca.get(), leaf.get(), leaf_key.get(), 2, TLS1_2_VERSION, NIO_TLS_MIN_TLSV1_3, 2);
    std::cout << "NIO_TLS_MIN_VERSION_PASS\n";
    const std::wstring wallet=root+L"\\negative"; std::filesystem::create_directory(wallet); write_file(wallet+L"\\ca.pem", ca_pem);
    rejected(root+L"\\rejected-run", wallet, cert_pem, key_pem, pem(nullptr, ca_key.get()));
    for (const auto &entry : std::filesystem::directory_iterator(root)) std::filesystem::remove_all(extended_path(entry.path().wstring()));
    PathContext after; check(bool(after.initialize(utf8(root))) && after.original_cwd()==before.original_cwd() && std::filesystem::is_empty(root), "cwd/fixture cleanup");
    check(cases==18, "matrix count"); std::cout << "NIO_TLS_COMPONENT_PASS policy=" << policy << '\n'; return 0;
  } catch (const std::exception &e) { std::cerr << "NIO_TLS_FAILURE " << e.what() << '\n'; return 1; }
}
