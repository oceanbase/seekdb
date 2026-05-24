# seekdb/local Homebrew tap

Vendored `thrift@0.22` formula for macOS libseekdb CI. GitHub Actions `macos-14` runners often lack `thrift@0.22` in Homebrew core (only unversioned `thrift` 0.23+), which caused darwin zip drift vs local dev machines.

Installed by `install-macos-brew-deps.sh` via `brew install --formula homebrew-local/Formula/thrift@0.22.rb` (no `brew tap` — GHA cannot git-clone a non-repo path).
