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

use crate::{
    certificate_expiry, cipher_info, ciphertext_size, decrypt_folded_into, decrypt_into,
    des_ciphertext_size, des_crypt_into, des_decrypt_into, des_encrypt_into, digest_into,
    encrypt_folded_into, encrypt_into, hmac_into, is_des_encrypted, random_fill, CipherInfo,
    CipherMode, DesKey, Error, HashAlgorithm,
};

fn hex(value: &str) -> Vec<u8> {
    assert_eq!(value.len() % 2, 0);
    value
        .as_bytes()
        .as_chunks::<2>()
        .0
        .iter()
        .map(|pair| {
            let text = std::str::from_utf8(pair).unwrap();
            u8::from_str_radix(text, 16).unwrap()
        })
        .collect()
}

#[test]
fn digest_known_answers_and_output_bounds() {
    // Published "abc" vectors from RFC 1320/1321, FIPS 180-4 and GB/T 32905.
    let cases = [
        (HashAlgorithm::Md4, "a448017aaf21d8525fc10ae87aa6729d"),
        (HashAlgorithm::Md5, "900150983cd24fb0d6963f7d28e17f72"),
        (HashAlgorithm::Sha1, "a9993e364706816aba3e25717850c26c9cd0d89d"),
        (HashAlgorithm::Sha224, "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7"),
        (HashAlgorithm::Sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
        (HashAlgorithm::Sha384, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"),
        (HashAlgorithm::Sha512, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"),
        (HashAlgorithm::Sm3, "66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0"),
    ];
    for (algorithm, expected) in cases {
        let expected = hex(expected);
        let mut output = [0xa5; 80];
        let size = digest_into(algorithm, b"abc", &mut output).unwrap();
        assert_eq!(size, expected.len(), "{algorithm:?}");
        assert_eq!(&output[..size], expected, "{algorithm:?}");
        assert!(output[size..].iter().all(|byte| *byte == 0xa5));

        let mut short = vec![0xa5; size - 1];
        assert_eq!(
            digest_into(algorithm, b"abc", &mut short),
            Err(Error::InvalidArgument)
        );
        assert!(short.iter().all(|byte| *byte == 0xa5));
    }
    let mut output = [0; 32];
    digest_into(HashAlgorithm::Sha256, b"", &mut output).unwrap();
    assert_eq!(
        output.as_slice(),
        hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
    );
}

#[test]
fn hmac_sha256_rfc4231() {
    let mut output = [0xa5; 40];
    let size = hmac_into(HashAlgorithm::Sha256, &[0x0b; 20], b"Hi There", &mut output).unwrap();
    assert_eq!(size, 32);
    assert_eq!(
        &output[..size],
        hex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7")
    );
    assert_eq!(&output[size..], &[0xa5; 8]);
    assert_eq!(
        hmac_into(HashAlgorithm::Sha256, b"key", b"data", &mut output[..31]),
        Err(Error::InvalidArgument)
    );
}

#[test]
fn aes128_block_modes_match_nist_and_apply_padding() {
    // NIST SP 800-38A F.1.1 and F.2.1, first plaintext block.
    let key = hex("2b7e151628aed2a6abf7158809cf4f3c");
    let iv = hex("000102030405060708090a0b0c0d0e0f");
    let plaintext = hex("6bc1bee22e409f96e93d7e117393172a");
    for (mode, expected, iv) in [
        (
            CipherMode::Aes128Ecb,
            "3ad77bb40d7a3660a89ecaf32466ef97",
            &[][..],
        ),
        (
            CipherMode::Aes128Cbc,
            "7649abac8119b246cee98e9b12e9197d",
            iv.as_slice(),
        ),
    ] {
        assert_eq!(ciphertext_size(mode, plaintext.len()), Ok(32));
        let mut encrypted = [0xa5; 40];
        let size = encrypt_into(mode, &key, iv, &[], &plaintext, &mut encrypted, &mut []).unwrap();
        assert_eq!(size, 32);
        assert_eq!(&encrypted[..16], hex(expected));
        assert_eq!(&encrypted[size..], &[0xa5; 8]);
        let mut recovered = [0xa5; 40];
        let size =
            decrypt_into(mode, &key, iv, &[], &encrypted[..size], &[], &mut recovered).unwrap();
        assert_eq!(size, plaintext.len());
        assert_eq!(&recovered[..size], plaintext);

        let mut empty = [0; 16];
        assert_eq!(
            encrypt_into(mode, &key, iv, &[], &[], &mut empty, &mut []),
            Ok(16)
        );
        assert_eq!(
            decrypt_into(mode, &key, iv, &[], &empty, &[], &mut recovered),
            Ok(0)
        );
    }
}

#[test]
fn aes128_gcm_known_answer_and_authentication_failure() {
    // NIST SP 800-38D: zero key, 96-bit zero IV and one zero plaintext block.
    let key = [0; 16];
    let iv = [0; 12];
    let plaintext = [0; 16];
    let mut encrypted = [0xa5; 16];
    let mut tag = [0; 16];
    assert_eq!(
        encrypt_into(
            CipherMode::Aes128Gcm,
            &key,
            &iv,
            &[],
            &plaintext,
            &mut encrypted,
            &mut tag
        ),
        Ok(16)
    );
    assert_eq!(
        encrypted.as_slice(),
        hex("0388dace60b6a392f328c2b971b2fe78")
    );
    assert_eq!(tag.as_slice(), hex("ab6e47d42cec13bdf53a67b21257bddf"));
    let mut recovered = [0xa5; 24];
    assert_eq!(
        decrypt_into(
            CipherMode::Aes128Gcm,
            &key,
            &iv,
            &[],
            &encrypted,
            &tag,
            &mut recovered
        ),
        Ok(16)
    );
    assert_eq!(&recovered[..16], &plaintext);
    assert_eq!(&recovered[16..], &[0xa5; 8]);

    tag[0] ^= 1;
    recovered.fill(0xa5);
    assert!(matches!(
        decrypt_into(
            CipherMode::Aes128Gcm,
            &key,
            &iv,
            &[],
            &encrypted,
            &tag,
            &mut recovered
        ),
        Err(Error::InvalidCiphertext { .. })
    ));
    assert_eq!(recovered, [0; 24]);
}

#[test]
fn native_gcm_accepts_arbitrary_aad_and_authenticates_it() {
    let mut encrypted = [0; 9];
    let mut tag = [0; 16];
    assert_eq!(
        encrypt_into(
            CipherMode::Aes128Gcm,
            &[7; 16],
            &[3; 12],
            b"context",
            b"plaintext",
            &mut encrypted,
            &mut tag
        ),
        Ok(9)
    );
    let mut recovered = [0xa5; 9];
    assert_eq!(
        decrypt_into(
            CipherMode::Aes128Gcm,
            &[7; 16],
            &[3; 12],
            b"context",
            &encrypted,
            &tag,
            &mut recovered
        ),
        Ok(9)
    );
    assert_eq!(&recovered, b"plaintext");
    assert!(matches!(
        decrypt_into(
            CipherMode::Aes128Gcm,
            &[7; 16],
            &[3; 12],
            b"changed",
            &encrypted,
            &tag,
            &mut recovered
        ),
        Err(Error::InvalidCiphertext { .. })
    ));
    assert_eq!(recovered, [0; 9]);
}

#[test]
fn folded_password_agrees_with_independently_computed_exact_key() {
    // The two complete 16-byte groups XOR together, then "extra" XORs the first five bytes.
    let password = b"0123456789abcdefABCDEFGHIJKLMNOPextra";
    let key = hex("140b05051073717f71732a2e2e2a2a36");
    let plaintext = b"binary\0plaintext";
    let mut exact = [0; 32];
    let mut folded = [0; 32];
    let mode = CipherMode::Aes128Cbc;
    let iv = [4; 16];
    let size = encrypt_into(mode, &key, &iv, &[], plaintext, &mut exact, &mut []).unwrap();
    assert_eq!(
        encrypt_folded_into(mode, password, &iv, &[], plaintext, &mut folded, &mut []),
        Ok(size)
    );
    assert_eq!(folded, exact);
    let mut recovered = [0xa5; 32];
    assert_eq!(
        decrypt_folded_into(
            mode,
            password,
            &iv,
            &[],
            &folded[..size],
            &[],
            &mut recovered
        ),
        Ok(plaintext.len())
    );
    assert_eq!(&recovered[..plaintext.len()], plaintext);

    assert_eq!(
        encrypt_folded_into(
            CipherMode::Aes128Gcm,
            password,
            &[0; 12],
            b"short",
            &[],
            &mut [],
            &mut [0; 16]
        ),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        decrypt_folded_into(
            CipherMode::Aes128Gcm,
            password,
            &[0; 12],
            b"short",
            &[],
            &[0; 16],
            &mut recovered
        ),
        Err(Error::InvalidArgument)
    );
    assert_eq!(recovered, [0; 32]);
}

#[test]
fn cipher_validation_rejects_bad_lengths_and_clears_failed_decryption() {
    assert_eq!(
        cipher_info(CipherMode::Aes128Cbc),
        Ok(CipherInfo {
            key_len: 16,
            iv_len: 16,
            padding: true,
            aead: false
        })
    );
    assert_eq!(
        cipher_info(CipherMode::Aes128Gcm),
        Ok(CipherInfo {
            key_len: 16,
            iv_len: 12,
            padding: false,
            aead: true
        })
    );
    assert_eq!(
        ciphertext_size(CipherMode::Aes128Cbc, usize::MAX),
        Err(Error::InvalidArgument)
    );
    let mut output = [0xa5; 32];
    assert_eq!(
        encrypt_into(
            CipherMode::Aes128Cbc,
            &[0; 15],
            &[0; 16],
            &[],
            b"x",
            &mut output,
            &mut []
        ),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        encrypt_into(
            CipherMode::Aes128Cbc,
            &[0; 16],
            &[0; 15],
            &[],
            b"x",
            &mut output,
            &mut []
        ),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        encrypt_into(
            CipherMode::Aes128Cbc,
            &[0; 16],
            &[0; 16],
            &[],
            b"x",
            &mut output[..15],
            &mut []
        ),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        encrypt_into(
            CipherMode::Aes128Gcm,
            &[0; 16],
            &[0; 12],
            &[],
            &[],
            &mut [],
            &mut [0; 15]
        ),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        decrypt_into(
            CipherMode::Aes128Cbc,
            &[0; 15],
            &[0; 16],
            &[],
            &[0; 16],
            &[],
            &mut output
        ),
        Err(Error::InvalidArgument)
    );
    assert_eq!(output, [0; 32]);
    output.fill(0xa5);
    assert!(matches!(
        decrypt_into(
            CipherMode::Aes128Cbc,
            &[0; 16],
            &[0; 16],
            &[],
            &[0; 15],
            &[],
            &mut output
        ),
        Err(Error::InvalidCiphertext { .. })
    ));
    assert_eq!(output, [0; 32]);
}

#[test]
fn des_password_ciphertext_matches_independent_openssl_fixture() {
    // openssl enc -des-ede3-cbc -nopad, with a zero IV and MD5 EVP_BytesToKey key.
    // Plaintext framing is "hello\0DES", six '*' bytes and a final 0x07 padding count.
    let plaintext = b"hello\0DES";
    let key = DesKey::Password(b"native\0password");
    let mut encrypted = [0xa5; 25];
    assert_eq!(des_encrypt_into(plaintext, key, &mut encrypted), Ok(17));
    assert_eq!(&encrypted[..17], hex("ffcef97f9639f9cd97e1c0ffdf294d48a0"));
    assert_eq!(&encrypted[17..], &[0xa5; 8]);
    assert!(is_des_encrypted(&encrypted[..17]));
    let mut recovered = [0xa5; 24];
    assert_eq!(
        des_decrypt_into(&encrypted[..17], key, &mut recovered),
        Ok(plaintext.len())
    );
    assert_eq!(&recovered[..plaintext.len()], plaintext);

    // CBC changes in the previous block can force the final padding count from 7 to 9.
    encrypted[8] ^= 7 ^ 9;
    recovered.fill(0xa5);
    assert!(matches!(
        des_decrypt_into(&encrypted[..17], key, &mut recovered),
        Err(Error::InvalidCiphertext { .. })
    ));
    assert_eq!(recovered, [0; 24]);
}

#[test]
fn des_default_schedule_key_numbers_and_framing() {
    let plaintext = b"hello\0DES";
    let mut encrypted = [0; 17];
    for key_number in 0..=9 {
        let key = DesKey::Default { key_number };
        assert_eq!(des_encrypt_into(plaintext, key, &mut encrypted), Ok(17));
        assert_eq!(encrypted[0], 0x80 | key_number);
        // Independently encrypted with openssl enc, using a 24-byte all-zero DES key.
        assert_eq!(&encrypted[1..], hex("a7d90fe74f177d800a640fa0eeb39e46"));
        let mut recovered = [0; 16];
        assert_eq!(
            des_decrypt_into(&encrypted, key, &mut recovered),
            Ok(plaintext.len())
        );
        assert_eq!(&recovered[..plaintext.len()], plaintext);
    }
    assert_eq!(
        des_encrypt_into(
            plaintext,
            DesKey::Default { key_number: 10 },
            &mut encrypted
        ),
        Err(Error::InvalidArgument)
    );
    assert_eq!(des_encrypt_into(b"", DesKey::Password(b""), &mut []), Ok(0));
    assert_eq!(des_decrypt_into(b"", DesKey::Password(b""), &mut []), Ok(0));
    assert_eq!(des_ciphertext_size(0), Ok(0));
    assert_eq!(des_ciphertext_size(8), Ok(17));
    assert_eq!(des_ciphertext_size(usize::MAX), Err(Error::InvalidArgument));

    for plaintext in [&b"ordinary\0text"[..], &[0x80; 8][..], &[0x7f; 9][..]] {
        assert!(!is_des_encrypted(plaintext));
        let mut output = [0xa5; 32];
        assert_eq!(
            des_decrypt_into(plaintext, DesKey::Password(b"unused"), &mut output),
            Ok(plaintext.len())
        );
        assert_eq!(&output[..plaintext.len()], plaintext);
        assert!(output[plaintext.len()..].iter().all(|byte| *byte == 0xa5));
        assert_eq!(
            des_decrypt_into(plaintext, DesKey::Password(b"unused"), &mut output[..1]),
            Err(Error::InvalidArgument)
        );
        assert_eq!(output[0], 0);
    }
}

#[test]
fn des_crypt_known_answer_and_c_string_password_rules() {
    let mut output = [0xa5; 14];
    assert_eq!(des_crypt_into(b"password", b"ab", &mut output), Ok(13));
    assert_eq!(&output[..13], b"abJnggxhB/yWI");
    assert_eq!(output[13], 0xa5);
    let mut truncated = [0; 13];
    assert_eq!(
        des_crypt_into(b"password ignored", b"abignored", &mut truncated),
        Ok(13)
    );
    assert_eq!(&truncated, &output[..13]);
    let mut with_nul = [0; 13];
    des_crypt_into(b"pass\0word", b"ab", &mut with_nul).unwrap();
    des_crypt_into(b"pass", b"ab", &mut truncated).unwrap();
    assert_eq!(with_nul, truncated);
    assert_eq!(des_crypt_into(b"", b"", &mut []), Ok(0));
    assert_eq!(
        des_crypt_into(b"password", b"a", &mut output),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        des_crypt_into(b"password", b"a!", &mut output),
        Err(Error::InvalidArgument)
    );
    assert_eq!(
        des_crypt_into(b"password", b"ab", &mut output[..12]),
        Err(Error::InvalidArgument)
    );
}

#[test]
fn random_fill_respects_buffer_bounds() {
    random_fill(&mut []).unwrap();
    let mut first = [0xa5; 66];
    let mut second = [0xa5; 66];
    random_fill(&mut first[1..65]).unwrap();
    random_fill(&mut second[1..65]).unwrap();
    assert_eq!((first[0], first[65]), (0xa5, 0xa5));
    assert_eq!((second[0], second[65]), (0xa5, 0xa5));
    assert_ne!(&first[1..65], &second[1..65]);
}

#[test]
fn certificate_expiry_uses_utc_and_supports_generalized_time() {
    // Public synthetic certificate from the native HTTP client's existing test fixtures.
    // openssl x509 -noout -enddate: Jan 1 00:00:00 2120 GMT (ASN.1 GeneralizedTime).
    let certificate = include_bytes!("testdata/expiry-2120.pem");
    assert_eq!(certificate_expiry(certificate), Ok(4_733_510_400));
    let mut chain = certificate.to_vec();
    chain.extend_from_slice(b"\ninvalid trailing certificate\n");
    assert_eq!(certificate_expiry(&chain), Ok(4_733_510_400));
    assert_eq!(certificate_expiry(b""), Err(Error::InvalidArgument));
    assert!(matches!(
        certificate_expiry(b"not a certificate"),
        Err(Error::Backend { .. })
    ));
}
