# Source and dependency provenance

seekdb is Apache-2.0 licensed; the repository LICENSE remains unchanged.
The optional CUVS integration adapts these public author references:

- https://github.com/zihaomu/seekdb/tree/634888d9b037153fa143d2c913ba4b25503e1d62
- Earlier ABI: https://github.com/zihaomu/seekdb/tree/aadf910a6dae3b51ebc9f7cc61c3e3fe6bdd1a53
- DDL snapshot fix: e0bba2c3254f9862fe38fc2053c5146ccfe2cdc0

Original OceanBase copyright and Apache notices remain in adapted source files.
licenses/seekdb-author-v5-LICENSE.txt is copied from the pinned v5 repository;
provenance/sources-provenance.json records exact raw URLs and SHA256 values.
provenance/bridge-reference-downloads.json records the earlier downloads.

The new bridge calls installed hipVS/cuVS C APIs and DLPack tensor interfaces.
It does not vendor or redistribute GPU libraries, ROCm, cuVS headers or DLPack
headers. These dependencies are supplied by the official AMD environment and
remain governed by their own distribution terms. A public cuVS 25.06 license
copy is included at licenses/cuvs-25.06-LICENSE.txt; it is not asserted to replace
the installed hipVS package's license. API reference:
https://github.com/rapidsai/cuvs/tree/branch-25.06/cpp/include/cuvs/neighbors

The bridge implementation and experiment/validation scripts were developed for
this entry. The staged bridge source is byte-identical to the tested FD-fix
source (SHA256 864e7aa6fd12c0903108556d6b49fd683f105781b58bb0a8b0867f4719dfe220).
No reference Notebook query speedup is presented as this entry's construction
speedup. No third-party financial/PDF assets are included.
