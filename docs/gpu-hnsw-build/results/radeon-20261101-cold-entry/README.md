# Fresh minimal-image acceptance, 2026-11-01

Compiled and packaged source: `4855acac7314df7d43f405b9c73c04ee4d40d562`. All 9,485 tracked file hashes remained unchanged. The release after this source adds only reports and evidence.

`final-provenance.json` identifies the binary, both actual CPack TGZs, source manifest and compiler hashes. `full-evidence.tgz` contains the complete build, package and entry logs, driver sources, original audit failures and retained server logs. Package binaries are archived separately in the local delivery bundle.

The image had no rpm2cpio/cpio. All 29 real pinned RPMs were downloaded and extracted by the candidate's Python/system-libarchive fallback; no system packages were installed. All 28 Linux regressions passed.

The complete GPU build used previously verified official Rust archives and 123 hash-verified locked Cargo vendor packages offline. A separate private, initially empty Rust/Cargo directory downloaded its toolchain from the official source. This cold download took 3682.31 seconds: the one-hour audit wrapper timed out, but its existing rustup child continued and exited 0. No replacement installer or preseeded downloads were used. The original failed `cold-rust-status.json` is preserved beside `cold-rust-completion-status.json`; installed cargo/rustc match the complete-build compiler byte for byte. A reuse-only helper check then returned 0.

The real contest entry passed init and GPU configure in both TGZ and default Release profiles on an independent identical checkout. It deliberately stopped at main C++ make with guard exit 42; this is separate from the complete TGZ C++ build.

Actual CPack archives were extracted, checked with ldd and run without LD_LIBRARY_PATH/LD_PRELOAD. All 27 SQL checks, six fresh CPU/GPU builds, DML/filtering and hidden-GPU restart passed. CPU/GPU medians: 24.943233504 / 2.994012695 seconds, ratio 8.331037990. GPU Recall@10: 1.000/0.999/1.000; restart: 1.000 for 100 queries.

Log rotation retained the third timed GPU build and the later DML rebuild. Benchmark JSON captured all three timed GPU success events before rotation, each with gpu=true fallback=false; the first two entire old raw log files are unavailable. `gpu-log-proof.json` states this explicitly. No performance run was repeated to replace evidence.

These are local acceptance results. Official task 47237054 returned 0/20 without usable error text or cases; its cause remains unconfirmed. RPM/DEB installation hooks and Bazel GPU builds are outside this TGZ validation.

The standalone text logs and version output have trailing whitespace normalized for Git; all original byte outputs are retained inside full-evidence.tgz.
