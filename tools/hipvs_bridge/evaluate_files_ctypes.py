"""Evaluate the real input files against existing hash-bound GT; never recompute GT."""
from array import array
import argparse
import ctypes as ct
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import sys
import time


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_inputs(base_path, query_path, truth_directory):
    """Validate all rows, byte counts, hashes and full GT denominator."""
    truth_directory = Path(truth_directory)
    manifest_path = truth_directory / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("status") != "complete" or manifest.get("metric") != "l2" or manifest.get("ranking_distance") != "squared_l2":
        raise ValueError("Need complete explicit squared-L2 manifest")
    n, dimension, nq, k = [manifest[name] for name in ("base_count", "dimension", "query_count", "k")]
    if any(type(value) is not int or value <= 0 for value in (n, dimension, nq, k)) or k > n:
        raise ValueError("Invalid manifest dimensions")
    if manifest["recall_targets"] != nq * k:
        raise ValueError("GT denominator mismatch")
    if manifest["inputs"]["base"]["shape"] != [n, dimension] or manifest["inputs"]["query"]["shape"] != [nq, dimension]:
        raise ValueError("Manifest shape mismatch")
    arrays = []
    for path, name, count in ((Path(base_path), "base", n), (Path(query_path), "query", nq)):
        if path.stat().st_size != count * dimension * 4 or sha256(path) != manifest["inputs"][name]["sha256"]:
            raise ValueError(f"{name} file shape/hash differs from exact GT inputs")
        values = array("f")
        values.frombytes(path.read_bytes())
        if values.itemsize != 4:
            raise ValueError("Need IEEE754 float32 array representation")
        if sys.byteorder != "little":
            values.byteswap()
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f"{name} contains nonfinite values")
        arrays.append(values)
    truth = {}
    for filename in ("gold.json", "gold-distances.json"):
        path = truth_directory / filename
        artifact = manifest["artifacts"][filename]
        if path.stat().st_size != artifact["bytes"] or sha256(path) != artifact["sha256"]:
            raise ValueError(f"{filename} content/hash mismatch")
        truth[filename] = json.loads(path.read_text(encoding="utf-8"))
        if set(truth[filename]) != {str(q) for q in range(nq)}:
            raise ValueError(f"{filename} does not cover all {nq} queries")
    for query_id in range(nq):
        ids, distances = truth["gold.json"][str(query_id)], truth["gold-distances.json"][str(query_id)]
        if len(ids) != k or len(set(ids)) != k or any(type(i) is not int or not 0 <= i < n for i in ids):
            raise ValueError("GT has invalid row offsets or incomplete K")
        if len(distances) != k or any(not math.isfinite(value) for value in distances):
            raise ValueError("GT has invalid squared-L2 distances")
    return manifest, arrays[0], arrays[1], truth["gold.json"], truth["gold-distances.json"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("--query", required=True, type=Path)
    parser.add_argument("--truth-directory", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--minimum-recall", type=float, default=0.95)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    receipt = {"passed": False, "scope": "bridge quality only; no seekdb end-to-end speed claim", "error": None,
               "configuration": {name: value for name, value in os.environ.items() if name.startswith("SEEKDB_HIPVS_")}}
    handle = None

    def save():
        args.output.write_text(json.dumps(receipt, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")

    try:
        manifest, base, queries, gold, gold_distances = load_inputs(args.base, args.query, args.truth_directory)
        n, dimension, nq, k = [manifest[name] for name in ("base_count", "dimension", "query_count", "k")]
        receipt.update(n=n, dimension=dimension, query_count=nq, k=k, recall_targets=nq * k,
            base_sha256=sha256(args.base), query_sha256=sha256(args.query), gold_sha256=sha256(args.truth_directory / "gold.json"),
            manifest_sha256=sha256(args.truth_directory / "manifest.json"), library_sha256=sha256(args.library),
            gt_recomputed=False, input_hashes_match_gt=True, minimum_recall=args.minimum_recall)
        save()
        if platform.system() != "Linux" or ct.sizeof(ct.c_long) != 8 or ct.sizeof(ct.c_uint) != 4:
            raise ValueError("Run actual bridge in Linux LP64 mirror")
        library = ct.CDLL(str(args.library.resolve()))
        floats, uints = ct.POINTER(ct.c_float), ct.POINTER(ct.c_uint)
        library.seekdb_cuvs_build.argtypes, library.seekdb_cuvs_build.restype = [floats, ct.c_long, ct.c_long], ct.c_void_p
        library.seekdb_cuvs_search.argtypes, library.seekdb_cuvs_search.restype = [ct.c_void_p, floats, ct.c_long, ct.c_long, uints, floats], ct.c_int
        library.seekdb_cuvs_free.argtypes, library.seekdb_cuvs_free.restype = [ct.c_void_p], None
        base_buffer = (ct.c_float * len(base)).from_buffer(base)
        query_buffer = (ct.c_float * len(queries)).from_buffer(queries)
        ids = (ct.c_uint * (nq * k))(*([0xFFFFFFFF] * (nq * k)))
        distances = (ct.c_float * (nq * k))(*([float("nan")] * (nq * k)))
        started = time.perf_counter()
        handle = library.seekdb_cuvs_build(base_buffer, n, dimension)
        receipt["bridge_build_synced_seconds"] = time.perf_counter() - started
        if not handle:
            raise RuntimeError("seekdb_cuvs_build failed; preserve native stderr for stage/error")
        started = time.perf_counter()
        status = library.seekdb_cuvs_search(handle, query_buffer, nq, k, ids, distances)
        receipt["bridge_host_search_seconds"] = time.perf_counter() - started
        receipt["search_status"] = status
        per_query = []
        for q in range(nq):
            predicted = list(ids[q * k:(q + 1) * k])
            measured = list(distances[q * k:(q + 1) * k])
            expected_for_returned = [sum((float(base[i * dimension + d]) - float(queries[q * dimension + d])) ** 2
                                        for d in range(dimension)) if 0 <= i < n else None for i in predicted]
            valid_ids = len(set(predicted)) == k and all(0 <= i < n for i in predicted)
            valid_distances = all(math.isfinite(value) for value in measured)
            matching_distances = valid_ids and valid_distances and all(
                math.isclose(observed, exact, rel_tol=5e-5, abs_tol=1e-4) for observed, exact in zip(measured, expected_for_returned))
            hits = len(set(predicted) & set(gold[str(q)]))
            per_query.append({"query": q, "predicted_ids": predicted,
                "predicted_distances": [value if math.isfinite(value) else None for value in measured],
                "exact_distances_for_predicted_ids": expected_for_returned,
                "gold_ids": gold[str(q)], "gold_distances": gold_distances[str(q)], "hit_count": hits, "recall": hits / k,
                "ids_unique_and_in_range": valid_ids, "distances_match_returned_rows": matching_distances,
                "distances_ordered": valid_distances and all(a <= b + 1e-5 for a, b in zip(measured, measured[1:]))})
        hits = sum(row["hit_count"] for row in per_query)
        recall = hits / (nq * k)
        receipt.update(per_query=per_query, hits=hits, recall=recall,
            all_ids_valid=all(row["ids_unique_and_in_range"] for row in per_query),
            all_distances_match_returned_rows=all(row["distances_match_returned_rows"] for row in per_query),
            all_distances_ordered=all(row["distances_ordered"] for row in per_query),
            worst_query_recall=min(row["recall"] for row in per_query))
        save()
        if status != 0:
            raise RuntimeError(f"Native search status {status}")
        if not (receipt["all_ids_valid"] and receipt["all_distances_match_returned_rows"] and receipt["all_distances_ordered"]):
            raise AssertionError("ID, layout or distance checks failed; inspect per_query")
        if recall < args.minimum_recall:
            raise AssertionError(f"Recall@{k}={recall:.6f} < required {args.minimum_recall}")
        receipt["passed"] = True
        save()
        print(json.dumps({key: value for key, value in receipt.items() if key != "per_query"}, ensure_ascii=False, indent=2), flush=True)
    except Exception as error:
        receipt["error"] = f"{type(error).__name__}: {error}"
        save()
        print(json.dumps({key: value for key, value in receipt.items() if key != "per_query"}, ensure_ascii=False, indent=2), flush=True)
        raise
    finally:
        if handle:
            library.seekdb_cuvs_free(handle)


if __name__ == "__main__":
    main()
