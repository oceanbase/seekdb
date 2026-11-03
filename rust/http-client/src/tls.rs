// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Hostname-only verification for libcurl's Rustls backend.

use std::ffi::{c_char, c_int, c_uint, c_void, CStr};
use std::net::IpAddr;
use std::ptr;

use aws_lc_sys as crypto;
use rustls_ffi::{certificate::rustls_certificate_get_der, connection::rustls_connection};

// Public curl 8.17 APIs not exposed by curl-sys 0.4.84. Keep these declarations
// together so updating the pinned curl version has one ABI review point.
const CURLOPT_PREREQFUNCTION: curl_sys::CURLoption = 20312;
const CURLOPT_PREREQDATA: curl_sys::CURLoption = 10313;
const CURLINFO_TLS_SSL_PTR: curl_sys::CURLINFO = 0x400000 + 45;
const CURLSSLBACKEND_RUSTLS: c_int = 14;
const CURLUPART_URL: c_uint = 0;
const CURLUPART_SCHEME: c_uint = 1;
const CURLUPART_HOST: c_uint = 5;
const CURLU_PUNYCODE: c_uint = 1 << 12;

#[repr(C)]
struct TlsSessionInfo {
    backend: c_int,
    internals: *mut c_void,
}

enum CurlUrl {}

extern "C" {
    fn curl_url() -> *mut CurlUrl;
    fn curl_url_cleanup(url: *mut CurlUrl);
    fn curl_url_set(url: *mut CurlUrl, part: c_uint, value: *const c_char, flags: c_uint)
        -> c_uint;
    fn curl_url_get(
        url: *const CurlUrl,
        part: c_uint,
        value: *mut *mut c_char,
        flags: c_uint,
    ) -> c_uint;
}

pub(super) struct HostCheck {
    // The address refers only to our owning Easy2, which drives callbacks on
    // one thread at a time. It is never dereferenced outside that callback.
    // Keeping the address rather than a raw pointer preserves Collector: Send.
    easy: usize,
    failure: Option<&'static str>,
}

impl HostCheck {
    pub(super) fn new(easy: *mut curl_sys::CURL) -> Box<Self> {
        Box::new(Self {
            easy: easy as usize,
            failure: None,
        })
    }

    pub(super) fn failure(&self) -> Option<&'static str> {
        self.failure
    }
}

pub(super) fn install(easy: *mut curl_sys::CURL, state: *mut HostCheck) -> Result<(), curl::Error> {
    type Callback = extern "C" fn(*mut c_void, *mut c_char, *mut c_char, c_int, c_int) -> c_int;
    // SAFETY: the boxed state belongs to this easy handle's Collector and
    // outlives curl_easy_cleanup. Neither option transfers ownership to curl.
    unsafe {
        let code = curl_sys::curl_easy_setopt(easy, CURLOPT_PREREQDATA, state.cast::<c_void>());
        if code != curl_sys::CURLE_OK {
            return Err(curl::Error::new(code));
        }
        let code = curl_sys::curl_easy_setopt(easy, CURLOPT_PREREQFUNCTION, prereq as Callback);
        if code != curl_sys::CURLE_OK {
            return Err(curl::Error::new(code));
        }
    }
    Ok(())
}

extern "C" fn prereq(
    data: *mut c_void,
    _primary: *mut c_char,
    _local: *mut c_char,
    _primary_port: c_int,
    _local_port: c_int,
) -> c_int {
    if data.is_null() {
        return 1;
    }
    // SAFETY: install supplies a live Box owned by this easy handle. Curl
    // invokes prereq synchronously before sending each request, including
    // redirects and reused connections, with no concurrent access to state.
    let state = unsafe { &mut *data.cast::<HostCheck>() };
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: this is the owning handle's synchronous prereq callback.
        unsafe { verify(state.easy as *mut curl_sys::CURL) }
    }));
    state.failure = match result {
        Ok(Ok(())) => None,
        Ok(Err(reason)) => Some(reason),
        Err(_) => Some("TLS hostname verification failed unexpectedly"),
    };
    i32::from(state.failure.is_some())
}

struct Url(*mut CurlUrl);
impl Drop for Url {
    fn drop(&mut self) {
        // SAFETY: this owns the nonnull CURLU returned by curl_url.
        unsafe { curl_url_cleanup(self.0) }
    }
}

struct CurlString(*mut c_char);
impl Drop for CurlString {
    fn drop(&mut self) {
        // SAFETY: curl_url_get allocates this string with curl's allocator.
        unsafe { curl_sys::curl_free(self.0.cast()) }
    }
}

impl Url {
    unsafe fn part(&self, part: c_uint, flags: c_uint) -> Result<CurlString, &'static str> {
        let mut value = ptr::null_mut();
        let code = curl_url_get(self.0, part, &mut value, flags);
        // Some conversion errors leave an allocated output string.
        let value = CurlString(value);
        if code != 0 || value.0.is_null() {
            return Err("TLS target URL component is unavailable");
        }
        Ok(value)
    }
}

unsafe fn verify(easy: *mut curl_sys::CURL) -> Result<(), &'static str> {
    let mut effective: *mut c_char = ptr::null_mut();
    if curl_sys::curl_easy_getinfo(easy, curl_sys::CURLINFO_EFFECTIVE_URL, &mut effective)
        != curl_sys::CURLE_OK
        || effective.is_null()
    {
        return Err("TLS effective target URL is unavailable");
    }
    let url = Url(curl_url());
    if url.0.is_null() {
        return Err("TLS target URL allocation failed");
    }
    if curl_url_set(url.0, CURLUPART_URL, effective, 0) != 0 {
        return Err("TLS target URL could not be parsed");
    }
    let scheme = url.part(CURLUPART_SCHEME, 0)?;
    if CStr::from_ptr(scheme.0)
        .to_bytes()
        .eq_ignore_ascii_case(b"http")
    {
        return Ok(());
    }
    if !CStr::from_ptr(scheme.0)
        .to_bytes()
        .eq_ignore_ascii_case(b"https")
    {
        return Err("TLS target URL has an unexpected scheme");
    }
    let host = url.part(CURLUPART_HOST, CURLU_PUNYCODE)?;
    let host = CStr::from_ptr(host.0)
        .to_str()
        .map_err(|_| "TLS hostname is not UTF-8")?;
    // CURLU normalizes encoded hosts, IDNA, and IPv6 scope IDs using the same
    // parser as the transfer. HOST excludes ZONEID but retains IPv6 brackets.
    let host = host
        .strip_prefix('[')
        .and_then(|s| s.strip_suffix(']'))
        .unwrap_or(host);
    let mut session: *mut TlsSessionInfo = ptr::null_mut();
    if curl_sys::curl_easy_getinfo(easy, CURLINFO_TLS_SSL_PTR, &mut session) != curl_sys::CURLE_OK
        || session.is_null()
    {
        return Err("TLS origin connection is unavailable");
    }
    // Curl queries only origin TLS for this info, including through an HTTPS
    // proxy. The pinned Rustls backend returns its public rustls_connection.
    if (*session).backend != CURLSSLBACKEND_RUSTLS || (*session).internals.is_null() {
        return Err("TLS hostname verification requires an active Rustls origin connection");
    }
    let cert =
        rustls_connection::rustls_connection_get_peer_certificate((*session).internals.cast(), 0);
    let mut der = ptr::null();
    let mut len = 0;
    if cert.is_null()
        || rustls_certificate_get_der(cert, &mut der, &mut len) != rustls_ffi::rustls_result::Ok
        || der.is_null()
        || len == 0
    {
        return Err("TLS origin certificate is unavailable");
    }
    // The borrowed DER is valid until the next mutating Rustls call. All
    // parsing and verification finish here before control returns to curl.
    let length = len
        .try_into()
        .map_err(|_| "TLS origin certificate is too large")?;
    let certificate = Certificate(crypto::d2i_X509(ptr::null_mut(), &mut der, length));
    if certificate.0.is_null() {
        return Err("TLS origin certificate could not be parsed");
    }
    if certificate.matches(host) {
        Ok(())
    } else {
        Err("TLS certificate hostname does not match the target host")
    }
}

struct Certificate(*mut crypto::X509);
impl Drop for Certificate {
    fn drop(&mut self) {
        // SAFETY: this owns the d2i_X509 allocation, including null on error.
        unsafe { crypto::X509_free(self.0) }
    }
}

impl Certificate {
    unsafe fn matches(&self, host: &str) -> bool {
        let ip = host.parse::<IpAddr>().ok();
        let names = crypto::X509_get_ext_d2i(
            self.0,
            crypto::NID_subject_alt_name,
            ptr::null_mut(),
            ptr::null_mut(),
        )
        .cast::<crypto::GENERAL_NAMES>();
        let mut san_present = false;
        let mut matched = false;
        if !names.is_null() {
            for i in 0..crypto::OPENSSL_sk_num(names.cast()) {
                let name = crypto::OPENSSL_sk_value(names.cast(), i).cast::<crypto::GENERAL_NAME>();
                if name.is_null() {
                    continue;
                }
                if (*name).type_ == crypto::GEN_DNS {
                    san_present = true;
                    if ip.is_none() {
                        matched |= host_matches(asn1_bytes((*name).d.dNSName), host);
                    }
                } else if (*name).type_ == crypto::GEN_IPADD {
                    san_present = true;
                    let bytes = asn1_bytes((*name).d.iPAddress);
                    matched |= match ip {
                        Some(IpAddr::V4(ip)) => bytes == ip.octets(),
                        Some(IpAddr::V6(ip)) => bytes == ip.octets(),
                        None => false,
                    };
                }
            }
            crypto::GENERAL_NAMES_free(names);
        }
        // Legacy curl suppresses CN fallback for either DNS or IP SANs.
        if san_present {
            return matched;
        }
        let subject = crypto::X509_get_subject_name(self.0);
        if subject.is_null() {
            return false;
        }
        let mut last = -1;
        loop {
            let index = crypto::X509_NAME_get_index_by_NID(subject, crypto::NID_commonName, last);
            if index < 0 {
                break;
            }
            last = index;
        }
        if last < 0 {
            return false;
        }
        let value = crypto::X509_NAME_ENTRY_get_data(crypto::X509_NAME_get_entry(subject, last));
        if value.is_null() {
            return false;
        }
        let mut utf8 = ptr::null_mut();
        let len = crypto::ASN1_STRING_to_UTF8(&mut utf8, value);
        let matched = len > 0
            && !utf8.is_null()
            && host_matches(std::slice::from_raw_parts(utf8, len as usize), host);
        crypto::OPENSSL_free(utf8.cast());
        matched
    }
}

// The caller keeps the owning X509/GENERAL_NAMES alive while using the slice.
unsafe fn asn1_bytes<'a>(value: *const crypto::ASN1_STRING) -> &'a [u8] {
    if value.is_null() {
        return &[];
    }
    let len = crypto::ASN1_STRING_length(value);
    let data = crypto::ASN1_STRING_get0_data(value);
    if len <= 0 || data.is_null() {
        return &[];
    }
    std::slice::from_raw_parts(data, len as usize)
}

fn host_matches(pattern: &[u8], host: &str) -> bool {
    if pattern.is_empty() || pattern.contains(&0) || host.is_empty() {
        return false;
    }
    let pattern = pattern.strip_suffix(b".").unwrap_or(pattern);
    let host = host.strip_suffix('.').unwrap_or(host).as_bytes();
    if !pattern.starts_with(b"*.") {
        return pattern.eq_ignore_ascii_case(host);
    }
    if std::str::from_utf8(host)
        .ok()
        .and_then(|s| s.parse::<IpAddr>().ok())
        .is_some()
    {
        return false;
    }
    if pattern.iter().filter(|byte| **byte == b'.').count() < 2 {
        return pattern.eq_ignore_ascii_case(host);
    }
    host.iter()
        .position(|byte| *byte == b'.')
        .is_some_and(|index| host[index..].eq_ignore_ascii_case(&pattern[1..]))
}
