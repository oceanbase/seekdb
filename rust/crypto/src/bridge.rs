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

use crate::{CipherMode, DesKey, Error, HashAlgorithm, Result};

#[cxx::bridge(namespace = "oceanbase::crypto")]
pub(crate) mod ffi {
    enum Code {
        Ok,
        InvalidArgument,
        Allocation,
        Unsupported,
        InvalidCiphertext,
        Backend,
    }
    struct OperationResult {
        code: Code,
        size: usize,
        backend_code: u64,
    }
    struct CipherInfo {
        result: OperationResult,
        key_len: usize,
        iv_len: usize,
        padding: bool,
        aead: bool,
    }
    struct TimestampResult {
        result: OperationResult,
        unix_seconds: i64,
    }
    struct CalendarResult {
        result: OperationResult,
        year: i32,
        month: i32,
        day: i32,
        hour: i32,
        minute: i32,
        second: i32,
    }

    unsafe extern "C++" {
        include!("rust/crypto/include/sdk.h");
        fn sdk_digest(algorithm: u32, input: &[u8], output: &mut [u8]) -> OperationResult;
        fn sdk_hmac(algorithm: u32, key: &[u8], input: &[u8], output: &mut [u8])
            -> OperationResult;
        fn sdk_random(output: &mut [u8]) -> OperationResult;
        fn sdk_cipher_info(mode: u32) -> CipherInfo;
        fn sdk_encrypt(
            mode: u32,
            key: &[u8],
            iv: &[u8],
            aad: &[u8],
            input: &[u8],
            output: &mut [u8],
            tag: &mut [u8],
        ) -> OperationResult;
        fn sdk_decrypt(
            mode: u32,
            key: &[u8],
            iv: &[u8],
            aad: &[u8],
            input: &[u8],
            tag: &[u8],
            output: &mut [u8],
        ) -> OperationResult;
        fn sdk_des(
            password: &[u8],
            has_password: bool,
            buffer: &mut [u8],
            encrypt: bool,
        ) -> OperationResult;
        fn sdk_des_crypt(password: &[u8], salt: &[u8], output: &mut [u8]) -> OperationResult;
        fn sdk_certificate_expiry(pem: &[u8]) -> CalendarResult;
    }

    extern "Rust" {
        fn digest_size(algorithm: u32) -> OperationResult;
        fn digest_into(algorithm: u32, input: &[u8], output: &mut [u8]) -> OperationResult;
        fn hmac_into(
            algorithm: u32,
            key: &[u8],
            input: &[u8],
            output: &mut [u8],
        ) -> OperationResult;
        fn random_fill(output: &mut [u8]) -> OperationResult;
        fn cipher_info(mode: u32) -> CipherInfo;
        fn ciphertext_size(mode: u32, input_size: usize) -> OperationResult;
        fn encrypt_into(
            mode: u32,
            key: &[u8],
            iv: &[u8],
            aad: &[u8],
            input: &[u8],
            output: &mut [u8],
            tag: &mut [u8],
        ) -> OperationResult;
        fn decrypt_into(
            mode: u32,
            key: &[u8],
            iv: &[u8],
            aad: &[u8],
            input: &[u8],
            tag: &[u8],
            output: &mut [u8],
        ) -> OperationResult;
        fn is_des_encrypted(input: &[u8]) -> bool;
        fn des_encrypt_into(
            input: &[u8],
            password: &[u8],
            has_password: bool,
            key_number: u8,
            output: &mut [u8],
        ) -> OperationResult;
        fn des_decrypt_into(
            input: &[u8],
            password: &[u8],
            has_password: bool,
            output: &mut [u8],
        ) -> OperationResult;
        fn des_crypt_into(password: &[u8], salt: &[u8], output: &mut [u8]) -> OperationResult;
        fn certificate_expiry(pem: &[u8]) -> TimestampResult;
    }
}

fn operation(result: Result<usize>) -> ffi::OperationResult {
    match result {
        Ok(size) => ffi::OperationResult {
            code: ffi::Code::Ok,
            size,
            backend_code: 0,
        },
        Err(error) => {
            let (code, backend_code) = match error {
                Error::InvalidArgument => (ffi::Code::InvalidArgument, 0),
                Error::Allocation => (ffi::Code::Allocation, 0),
                Error::Unsupported => (ffi::Code::Unsupported, 0),
                Error::InvalidCiphertext { backend_code } => {
                    (ffi::Code::InvalidCiphertext, backend_code)
                }
                Error::Backend { backend_code } => (ffi::Code::Backend, backend_code),
            };
            ffi::OperationResult {
                code,
                size: 0,
                backend_code,
            }
        }
    }
}

fn digest_size(algorithm: u32) -> ffi::OperationResult {
    operation(HashAlgorithm::try_from(algorithm).map(HashAlgorithm::digest_size))
}

fn digest_into(algorithm: u32, input: &[u8], output: &mut [u8]) -> ffi::OperationResult {
    operation(
        HashAlgorithm::try_from(algorithm)
            .and_then(|algorithm| crate::digest_into(algorithm, input, output)),
    )
}

fn hmac_into(algorithm: u32, key: &[u8], input: &[u8], output: &mut [u8]) -> ffi::OperationResult {
    operation(
        HashAlgorithm::try_from(algorithm)
            .and_then(|algorithm| crate::hmac_into(algorithm, key, input, output)),
    )
}

fn random_fill(output: &mut [u8]) -> ffi::OperationResult {
    let size = output.len();
    operation(crate::random_fill(output).map(|_| size))
}

fn cipher_info(mode: u32) -> ffi::CipherInfo {
    let mode = if mode == 19 { 28 } else { mode };
    match CipherMode::try_from(mode).and_then(crate::cipher_info) {
        Ok(info) => ffi::CipherInfo {
            result: operation(Ok(0)),
            key_len: info.key_len,
            iv_len: info.iv_len,
            padding: info.padding,
            aead: info.aead,
        },
        Err(error) => ffi::CipherInfo {
            result: operation(Err(error)),
            key_len: 0,
            iv_len: 0,
            padding: false,
            aead: false,
        },
    }
}

fn ciphertext_size(mode: u32, input_size: usize) -> ffi::OperationResult {
    operation(CipherMode::try_from(mode).and_then(|mode| crate::ciphertext_size(mode, input_size)))
}

fn encrypt_into(
    mode: u32,
    key: &[u8],
    iv: &[u8],
    aad: &[u8],
    input: &[u8],
    output: &mut [u8],
    tag: &mut [u8],
) -> ffi::OperationResult {
    operation(
        CipherMode::try_from(mode)
            .and_then(|mode| crate::encrypt_folded_into(mode, key, iv, aad, input, output, tag)),
    )
}

fn decrypt_into(
    mode: u32,
    key: &[u8],
    iv: &[u8],
    aad: &[u8],
    input: &[u8],
    tag: &[u8],
    output: &mut [u8],
) -> ffi::OperationResult {
    let result = CipherMode::try_from(mode)
        .and_then(|mode| crate::decrypt_folded_into(mode, key, iv, aad, input, tag, output));
    if result.is_err() {
        output.fill(0);
    }
    operation(result)
}

fn is_des_encrypted(input: &[u8]) -> bool {
    crate::is_des_encrypted(input)
}

fn des_encrypt_into(
    input: &[u8],
    password: &[u8],
    has_password: bool,
    key_number: u8,
    output: &mut [u8],
) -> ffi::OperationResult {
    let key = if has_password {
        DesKey::Password(password)
    } else {
        DesKey::Default { key_number }
    };
    operation(crate::des_encrypt_into(input, key, output))
}

fn des_decrypt_into(
    input: &[u8],
    password: &[u8],
    has_password: bool,
    output: &mut [u8],
) -> ffi::OperationResult {
    let key = if has_password {
        DesKey::Password(password)
    } else {
        DesKey::Default {
            key_number: input.first().copied().unwrap_or(0) & 127,
        }
    };
    operation(crate::des_decrypt_into(input, key, output))
}

fn des_crypt_into(password: &[u8], salt: &[u8], output: &mut [u8]) -> ffi::OperationResult {
    operation(crate::des_crypt_into(password, salt, output))
}

fn certificate_expiry(pem: &[u8]) -> ffi::TimestampResult {
    match crate::certificate_expiry(pem) {
        Ok(unix_seconds) => ffi::TimestampResult {
            result: operation(Ok(0)),
            unix_seconds,
        },
        Err(error) => ffi::TimestampResult {
            result: operation(Err(error)),
            unix_seconds: 0,
        },
    }
}
