"""Real six-symbol C ABI smoke. Stdlib only; never substitutes an index backend."""
import argparse
from array import array
import ctypes as ct
import gc
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--output", type=Path, default=Path("smoke-result.json"))
    parser.add_argument("--n", type=int, default=2048)
    parser.add_argument("--dim", type=int, default=32)
    parser.add_argument("--nq", type=int, default=20)
    parser.add_argument("--topk", type=int, default=10)
    parser.add_argument("--minimum-recall", type=float, default=0.95)
    args = parser.parse_args()
    if platform.system() != "Linux" or ct.sizeof(ct.c_long) != 8 or ct.sizeof(ct.c_uint) != 4:
        raise SystemExit("Run in the official Linux LP64 mirror; this is not a Windows runtime test")
    if args.n < 256 or args.nq < 2 or args.topk < 1 or args.topk > args.n or args.dim < 1:
        raise SystemExit("Invalid smoke dimensions")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    library = ct.CDLL(str(args.library.resolve()))
    float_ptr = ct.POINTER(ct.c_float)
    uint_ptr = ct.POINTER(ct.c_uint)
    library.seekdb_cuvs_build.argtypes = [float_ptr, ct.c_long, ct.c_long]
    library.seekdb_cuvs_build.restype = ct.c_void_p
    library.seekdb_cuvs_search.argtypes = [ct.c_void_p, float_ptr, ct.c_long, ct.c_long, uint_ptr, float_ptr]
    library.seekdb_cuvs_search.restype = ct.c_int
    library.seekdb_cuvs_free.argtypes = [ct.c_void_p]
    library.seekdb_cuvs_free.restype = None
    library.seekdb_cuvs_cagra_knn.argtypes = [float_ptr, ct.c_long, ct.c_long, float_ptr, ct.c_long, ct.c_long, uint_ptr]
    library.seekdb_cuvs_cagra_knn.restype = ct.c_int
    library.seekdb_cuvs_serialize.argtypes = [ct.c_void_p, ct.c_char_p]
    library.seekdb_cuvs_serialize.restype = ct.c_int
    library.seekdb_cuvs_deserialize.argtypes = [ct.c_char_p, ct.c_long, ct.c_long]
    library.seekdb_cuvs_deserialize.restype = ct.c_void_p
    libc = ct.CDLL(None)
    libc.tmpfile.argtypes, libc.tmpfile.restype = [], ct.c_void_p
    libc.fileno.argtypes, libc.fileno.restype = [ct.c_void_p], ct.c_int
    libc.fseeko.argtypes, libc.fseeko.restype = [ct.c_void_p, ct.c_long, ct.c_int], ct.c_int
    libc.ftello.argtypes, libc.ftello.restype = [ct.c_void_p], ct.c_long
    libc.rewind.argtypes, libc.rewind.restype = [ct.c_void_p], None
    libc.fflush.argtypes, libc.fflush.restype = [ct.c_void_p], ct.c_int
    libc.fread.argtypes, libc.fread.restype = [ct.c_void_p, ct.c_size_t, ct.c_size_t, ct.c_void_p], ct.c_size_t
    libc.fwrite.argtypes, libc.fwrite.restype = [ct.c_void_p, ct.c_size_t, ct.c_size_t, ct.c_void_p], ct.c_size_t
    libc.fclose.argtypes, libc.fclose.restype = [ct.c_void_p], ct.c_int
    rng = random.Random(1729)
    base = array("f", (rng.uniform(-1.0, 1.0) for _ in range(args.n * args.dim)))
    queries = array("f", (rng.uniform(-1.0, 1.0) for _ in range(args.nq * args.dim)))
    queries[:args.dim] = base[:args.dim]
    queries[args.dim:2 * args.dim] = base[-args.dim:]
    exact = []
    exact_ranked = []
    for q in range(args.nq):
        vector = queries[q * args.dim:(q + 1) * args.dim]
        ranked = sorted((sum((float(base[i * args.dim + d]) - float(vector[d])) ** 2 for d in range(args.dim)), i)
                        for i in range(args.n))
        exact.append({i for _, i in ranked[:args.topk]})
        exact_ranked.append(ranked[:args.topk])
    base_buffer = (ct.c_float * len(base)).from_buffer_copy(base)
    query_buffer = (ct.c_float * len(queries)).from_buffer_copy(queries)
    count = args.nq * args.topk
    ids_type = ct.c_uint * count
    distances_type = ct.c_float * count
    handle = None
    restored = None
    timings = {}
    checks = []
    search_audits = []

    def progress(passed, error=None):
        partial = {"passed": passed, "error": error, "library": str(args.library.resolve()),
            "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
            "seed": 1729, "n": args.n, "dim": args.dim, "nq": args.nq, "topk": args.topk,
            "minimum_recall": args.minimum_recall, "base_sha256": hashlib.sha256(base.tobytes()).hexdigest(),
            "queries_sha256": hashlib.sha256(queries.tobytes()).hexdigest(), "timings": timings,
            "checks_passed": len(checks), "search_audits": search_audits,
            "configuration": {name: value for name, value in os.environ.items() if name.startswith("SEEKDB_HIPVS_")}}
        args.output.write_text(json.dumps(partial, ensure_ascii=False, indent=2), encoding="utf-8")

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        checks.append(message)

    def search(index, with_distances=True):
        ids = ids_type(*([0xFFFFFFFF] * count))
        distances = distances_type(*([float("nan")] * count))
        status = library.seekdb_cuvs_search(index, query_buffer, args.nq, args.topk, ids,
                                              distances if with_distances else None)
        audit = {"search_number": len(search_audits) + 1, "status": status,
                 "with_distances": with_distances, "per_query": []}
        search_audits.append(audit)
        for q in range(args.nq):
            row = list(ids[q * args.topk:(q + 1) * args.topk])
            measured = list(distances[q * args.topk:(q + 1) * args.topk]) if with_distances else None
            exact_for_returned = [sum((float(base[i * args.dim + d]) - float(queries[q * args.dim + d])) ** 2
                                     for d in range(args.dim)) if 0 <= i < args.n else None for i in row]
            audit["per_query"].append({"query": q, "predicted_ids": row,
                "predicted_distances": [x if math.isfinite(x) else None for x in measured] if measured else None,
                "exact_distances_for_predicted_ids": exact_for_returned,
                "gold_ids": [i for _, i in exact_ranked[q]], "gold_distances": [value for value, _ in exact_ranked[q]],
                "hit_count": len(set(row) & exact[q]), "recall": len(set(row) & exact[q]) / args.topk})
        progress(False)
        check(status == 0, "search returns C ABI success")
        hits = 0
        for q in range(args.nq):
            row = list(ids[q * args.topk:(q + 1) * args.topk])
            check(len(set(row)) == args.topk and all(0 <= i < args.n for i in row), "IDs are unique in-range row offsets")
            hits += len(set(row) & exact[q])
            if with_distances:
                measured = list(distances[q * args.topk:(q + 1) * args.topk])
                check(all(math.isfinite(x) for x in measured), "distances are finite")
                check(all(a <= b + 1e-5 for a, b in zip(measured, measured[1:])), "L2 distances are ordered")
                for i, observed in zip(row, measured):
                    target = sum((float(base[i * args.dim + d]) - float(queries[q * args.dim + d])) ** 2 for d in range(args.dim))
                    check(math.isclose(observed, target, rel_tol=5e-5, abs_tol=1e-4), "distance matches exact squared L2 for returned ID")
        recall = hits / count
        audit["recall"] = recall
        audit["layout_id_distance_checks_passed"] = True
        progress(False)
        print(json.dumps({"search_number": audit["search_number"], "recall": recall,
                          "required": args.minimum_recall, "layout_id_distance_checks_passed": True}), flush=True)
        check(recall >= args.minimum_recall, f"Recall@{args.topk}={recall:.6f} < required {args.minimum_recall}")
        return list(ids), list(distances) if with_distances else None, recall

    try:
        check(not library.seekdb_cuvs_build(base_buffer, 0, args.dim), "invalid zero-row build fails")
        check(library.seekdb_cuvs_search(None, query_buffer, args.nq, args.topk, ids_type(), None) != 0, "NULL-handle search fails")
        started = time.perf_counter()
        handle = library.seekdb_cuvs_build(base_buffer, args.n, args.dim)
        timings["build_synced_seconds"] = time.perf_counter() - started
        check(bool(handle), "GPU CAGRA -> full HNSW build returns handle")
        del base_buffer
        gc.collect()
        first_ids, first_distances, recall = search(handle)
        repeated_ids, _, repeated_recall = search(handle, with_distances=False)
        check(repeated_ids == first_ids, "search-many is stable and optional distances are supported")
        sentinel = ids_type(*([0xFFFFFFFF] * count))
        check(library.seekdb_cuvs_search(handle, query_buffer, args.nq, args.n + 1, sentinel, None) != 0,
              "topk larger than dataset fails")
        check(all(value == 0xFFFFFFFF for value in sentinel), "invalid search leaves caller output unchanged")
        with tempfile.TemporaryDirectory(prefix="hnsw-abi-", dir=args.output.parent) as directory:
            snapshot = Path(directory) / "snapshot.bin"
            started = time.perf_counter()
            check(library.seekdb_cuvs_serialize(handle, os.fsencode(snapshot)) == 0, "HNSW snapshot serializes")
            timings["serialize_seconds"] = time.perf_counter() - started
            payload = snapshot.read_bytes()
            check(payload[:8] == b"SKHNSW01" and len(payload) > 80, "snapshot is one self-contained HNSW envelope")
            check(not library.seekdb_cuvs_deserialize(os.fsencode(snapshot), args.n + 1, args.dim), "snapshot shape mismatch fails")
            damaged = Path(directory) / "damaged.bin"
            corrupted = bytearray(payload)
            corrupted[-1] ^= 1
            damaged.write_bytes(corrupted)
            check(not library.seekdb_cuvs_deserialize(os.fsencode(damaged), args.n, args.dim), "corrupted payload checksum fails")
            library.seekdb_cuvs_free(handle)
            handle = None
            started = time.perf_counter()
            restored = library.seekdb_cuvs_deserialize(os.fsencode(snapshot), args.n, args.dim)
            timings["deserialize_seconds"] = time.perf_counter() - started
            check(bool(restored), "HNSW snapshot restores after freeing original handle")
            restored_ids, restored_distances, restored_recall = search(restored)
            check(restored_ids == first_ids and restored_distances == first_distances, "restored HNSW has exactly equal search results")
        # Exact v5 caller contract: C tmpfile() -> /proc/self/fd/N. In the
        # deserialize direction the original FILE* stays at EOF after fflush.
        save_file = libc.tmpfile()
        load_file = libc.tmpfile()
        proc_restored = None
        try:
            check(bool(save_file) and bool(load_file), "libc tmpfile opens caller-owned streams")
            save_fd, load_fd = libc.fileno(save_file), libc.fileno(load_file)
            save_path, load_path = f"/proc/self/fd/{save_fd}".encode(), f"/proc/self/fd/{load_fd}".encode()
            check(library.seekdb_cuvs_serialize(restored, save_path) == 0, "v5 proc-FD snapshot serializes into existing caller tmpfile")
            check(libc.fileno(save_file) == save_fd, "serialize preserves caller descriptor ownership")
            check(libc.fseeko(save_file, 0, os.SEEK_END) == 0, "caller fseeko to snapshot end succeeds")
            byte_count = libc.ftello(save_file)
            check(byte_count > 80, "caller observes nonempty graph payload via its original FILE pointer")
            libc.rewind(save_file)
            buffer = ct.create_string_buffer(byte_count)
            check(libc.fread(buffer, 1, byte_count, save_file) == byte_count, "caller rewind and fread recover full snapshot")
            proc_payload = buffer.raw
            check(proc_payload[:8] == b"SKHNSW01", "proc-FD payload has HNSW envelope")
            check(library.seekdb_cuvs_serialize(restored, save_path) == 0, "proc-FD overwrite works while caller stream was at EOF")
            check(libc.fseeko(save_file, 0, os.SEEK_END) == 0 and libc.ftello(save_file) == byte_count,
                  "proc-FD overwrite truncates correctly rather than appending")
            check(libc.fwrite(buffer, 1, byte_count, load_file) == byte_count and libc.fflush(load_file) == 0,
                  "caller writes and fflushs snapshot into another C tmpfile")
            check(libc.ftello(load_file) == byte_count, "v5 load caller stays at EOF without rewind")
            proc_restored = library.seekdb_cuvs_deserialize(load_path, args.n, args.dim)
            check(bool(proc_restored), "v5 deserialize reopens proc-FD at zero despite caller FILE at EOF")
            check(libc.fileno(load_file) == load_fd and libc.ftello(load_file) == byte_count,
                  "deserialize preserves caller FD and original stream position")
            proc_ids, proc_distances, proc_recall = search(proc_restored)
            check(proc_ids == first_ids and proc_distances == first_distances, "proc-FD restored HNSW results equal original results")
        finally:
            if proc_restored:
                library.seekdb_cuvs_free(proc_restored)
            if load_file:
                libc.fclose(load_file)
            if save_file:
                libc.fclose(save_file)
        base_buffer = (ct.c_float * len(base)).from_buffer_copy(base)
        one_shot_ids = ids_type()
        check(library.seekdb_cuvs_cagra_knn(base_buffer, args.n, args.dim, query_buffer, args.nq, args.topk, one_shot_ids) == 0,
              "legacy one-shot symbol runs real HNSW bridge")
        one_shot_recall = sum(len(set(one_shot_ids[q * args.topk:(q + 1) * args.topk]) & exact[q]) for q in range(args.nq)) / count
        search_audits.append({"search_number": "one-shot", "recall": one_shot_recall,
                              "predicted_ids": list(one_shot_ids), "gold_ids": [[i for _, i in row] for row in exact_ranked]})
        check(one_shot_recall >= args.minimum_recall, "one-shot recall passes")
        receipt = {"passed": True, "library": str(args.library.resolve()), "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
            "platform": platform.platform(), "abi": {"long_bytes": ct.sizeof(ct.c_long), "uint_bytes": ct.sizeof(ct.c_uint), "symbols": 6},
            "seed": 1729, "n": args.n, "dim": args.dim, "nq": args.nq, "topk": args.topk, "recall": recall,
            "repeated_recall": repeated_recall, "restored_recall": restored_recall, "one_shot_recall": one_shot_recall,
            "proc_fd_recall": proc_recall, "v5_libc_tmpfile_roundtrip": True,
            "timings": timings, "search_audits": search_audits,
            "base_sha256": hashlib.sha256(base.tobytes()).hexdigest(), "queries_sha256": hashlib.sha256(queries.tobytes()).hexdigest(),
            "configuration": {name: value for name, value in os.environ.items() if name.startswith("SEEKDB_HIPVS_")},
            "checks_passed": len(checks), "scope": "bridge-only ABI/quality/persistence smoke; not a seekdb end-to-end build speed claim"}
        args.output.write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps({key: value for key, value in receipt.items() if key != "search_audits"}, ensure_ascii=False, indent=2))
    except Exception as error:
        progress(False, f"{type(error).__name__}: {error}")
        raise
    finally:
        if restored:
            library.seekdb_cuvs_free(restored)
        if handle:
            library.seekdb_cuvs_free(handle)


if __name__ == "__main__":
    main()
