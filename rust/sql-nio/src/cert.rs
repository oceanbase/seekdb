// Copyright (c) 2025 OceanBase.
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

use x509_parser::prelude::parse_x509_certificate;

const MAX_NAME_BYTES: usize = 4096;

/// The account-policy view of an already rustls-verified peer certificate.
///
/// All fields are owned by the TLS session. The FFI only borrows these byte
/// slices while the session/request is alive; no certificate object or DER
/// buffer crosses into C++.
pub(crate) struct PeerCertificateInfo {
    pub(crate) common_name: Vec<u8>,
    pub(crate) issuer: Vec<u8>,
    pub(crate) subject: Vec<u8>,
    pub(crate) valid: bool,
}

impl PeerCertificateInfo {
    pub(crate) fn parse(der: &[u8]) -> Self {
        match parse_x509_certificate(der) {
            Ok((remaining, cert)) if remaining.is_empty() => {
                let issuer = format_name(cert.issuer());
                let subject = format_name(cert.subject());
                let common_name = cert
                    .subject()
                    .iter_common_name()
                    .next()
                    .and_then(|cn| cn.as_str().ok())
                    .map(str::as_bytes)
                    .map(Vec::from)
                    .unwrap_or_default();
                let valid = !issuer.is_empty() && !subject.is_empty();
                Self {
                    common_name,
                    issuer,
                    subject,
                    valid,
                }
            }
            _ => Self::invalid(),
        }
    }

    fn invalid() -> Self {
        Self {
            common_name: Vec::new(),
            issuer: Vec::new(),
            subject: Vec::new(),
            valid: false,
        }
    }
}

/// Use the slash-delimited name accepted by seekdb's SQL account fields, but
/// build it from the Rust X.509 representation rather than OpenSSL's
/// X509_NAME_oneline. x509-parser's display form uses `", "` between RDNs and
/// `" + "` for attributes in one RDN; those separators are normalized here.
fn format_name(name: &x509_parser::x509::X509Name<'_>) -> Vec<u8> {
    format_display_name(&name.to_string())
}

fn format_display_name(display: &str) -> Vec<u8> {
    if display.is_empty() || display.starts_with("<X509Error:") {
        return Vec::new();
    }
    let display = display.replace(" + ", "+");
    let mut formatted = Vec::with_capacity(display.len() + 1);
    formatted.push(b'/');
    let bytes = display.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b',' => {
                formatted.push(b'/');
                i += 1;
                if i < bytes.len() && bytes[i] == b' ' {
                    i += 1;
                }
            }
            byte => {
                formatted.push(byte);
                i += 1;
            }
        }
    }
    if formatted.len() > MAX_NAME_BYTES {
        Vec::new()
    } else {
        formatted
    }
}

#[cfg(test)]
mod tests {
    use super::{format_display_name, PeerCertificateInfo};

    #[test]
    fn rejects_truncated_certificate() {
        assert!(!PeerCertificateInfo::parse(&[0x30, 0x00]).valid);
    }

    #[test]
    fn formats_display_name_for_sql_account() {
        assert_eq!(format_display_name("CN=allowed2"), b"/CN=allowed2");
        assert_eq!(
            format_display_name("CN=allowed2, O=seekdb"),
            b"/CN=allowed2/O=seekdb"
        );
    }
}
