# GPU HNSW build bridge

This optional bridge builds the core graph with AMD hipVS/cuVS CAGRA, synchronizes
the GPU, converts it into a full HNSW hierarchy, and persists a self-contained
HNSW graph and dataset. Queries use CPU HNSW. It does not claim GPU query speedup.

The seekdb adaptor buffers ordinary dense L2 HNSW input without duplicate CPU
graph construction, builds during DDL snapshot serialization, and persists the
bridge payload inside OBCUVS02. CPU VSAG remains the baseline and explicit
fallback. CUVS indexes with extra information are rejected; iterative filtered
queries retain VSAG state and finalization semantics.

The C ABI is build/search/free, serialize/deserialize and the historical one-shot
cagra_knn name. IDs are checked uint32 row offsets, translated to external int64
VIDs by seekdb. Snapshot SKHNSW01 includes dimensions, full-hierarchy parameters,
payload length and checksum. The caller's /proc/self/fd/N tmpfile is updated in
place and remains open. Regular paths use atomic replacement. Old CAGRA-only
snapshots are not accepted as full HNSW snapshots.

Build in the official AMD environment with its existing ROCm and hipVS:

```bash
bash tools/hipvs_bridge/build.sh /workspace/results/libseekdb_cuvs_bridge.so
export LD_LIBRARY_PATH=/opt/hipvs/lib:/opt/rocm/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export SEEKDB_HIPVS_HNSW_HIERARCHY=gpu
export SEEKDB_HIPVS_HNSW_M=16
export SEEKDB_HIPVS_GRAPH_DEGREE=32
export SEEKDB_HIPVS_INTERMEDIATE_DEGREE=64
export SEEKDB_HIPVS_EF_CONSTRUCTION=200
export SEEKDB_HIPVS_SEARCH_EF=512
export SEEKDB_HIPVS_CPU_THREADS=1
python3 tools/hipvs_bridge/smoke_ctypes.py /workspace/results/libseekdb_cuvs_bridge.so \
  --output /workspace/results/bridge-smoke.json
```

HIPVS_ROOT, ROCM_ROOT and HIPCC can select the installed toolchain. No dependency
installation occurs. The build rejects undefined symbols and verifies all six
C exports. Full GPU hierarchy is the default; an explicitly selected full CPU
hierarchy is allowed, while NONE and silent hierarchy fallback are rejected.

For seekdb, build this checkout normally, then set SEEKDB_CUVS_BRIDGE to the
absolute library path before starting the server. The adaptor dynamically loads
the bridge; the database binary has no mandatory GPU link dependency. A missing
or incomplete bridge explicitly logs CPU fallback. A fallback run is excluded
from GPU build performance results.

The smoke validates ABI, IDs, squared L2 distances, exact-GT recall, repeated
queries and single-file/FILE-descriptor snapshot round trips. It is not an
end-to-end seekdb build benchmark. See docs/amd/README.md and
tools/benchmark/amd_hnsw/README.md for the complete measurement boundary.

GPU configuration, actual CPU affinity/thread resources and every library hash
must be retained with the CPU/GPU comparison. The environment settings above are
reproduction defaults, not assertions about an unrecorded formal experiment.

Parameter contract: the preserved six-function ABI does not carry SQL build or
query parameters to a GPU handle. Explicitly synchronize the process M,
ef_construction and search-ef profile with SQL DDL and the effective session ef.
GPU query ef is saved in the handle/snapshot; changing session ef does not
dynamically reconfigure it. This backend does not claim arbitrary per-index or
per-query settings. Ordinary VSAG keeps its existing parameter path. See
docs/amd/PARAMETER_CONTRACT108.md for the measured profile and source audit.
