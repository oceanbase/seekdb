#!/usr/bin/env python3
"""Real seekdb SQL build benchmark. No simulated GPU timings or fallback credit."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import sys
import time
from datetime import datetime, timezone
import uuid

import numpy as np

from sql_snapshot_integrity import KEY as SNAPSHOT_KEY, verify_sql_snapshot

IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]{0,63}\Z")
OPTION = re.compile(r"([a-z_][a-z0-9_]*)\s*=\s*([a-z_][a-z0-9_]*|[0-9]+(?:\.[0-9]+)?)\Z", re.I)
HARDWARE_FIELDS = ("cpu_model", "cpu_threads", "cpu_affinity", "gpu_model", "gpu_architecture",
                   "rocm_version", "hipvs_version", "seekdb_commit")
BUILD_LOG = re.compile(r'\[seekdb-hipvs-build\] backend=hipvs device_vendor=AMD '
                       r'gpu_name="([^"\r\n]+)" arch=(gfx[0-9A-Za-z_:]+) '
                       r'rows=([0-9]+) dim=([0-9]+) gpu_compute=true '
                       r'cpu_fallback=false export_completed=true')


def log_position(path: Path) -> tuple[int, int, int]:
    info = path.stat()
    return info.st_dev, info.st_ino, info.st_size


def evidence_from_log(path: Path, start: tuple[int, int, int], table: str, index: str,
                      rows: int, dim: int) -> dict:
    """Correlate one isolated SQL CREATE with its raw, newly appended GPU record.

    This mode requires a dedicated seekdb process without concurrent index DDL.
    Rotation, absent records and multiple records fail instead of guessing.
    """
    with path.open("rb") as source:
        info = os.fstat(source.fileno())
        if (info.st_dev, info.st_ino) != start[:2] or info.st_size < start[2]:
            raise RuntimeError("seekdb stderr rotated during timed index construction")
        length = info.st_size - start[2]
        if length > 16 * 1024 * 1024:
            raise RuntimeError("Unexpectedly large stderr interval; inspect server log")
        source.seek(start[2])
        raw = source.read(length)
    candidates = [line for line in raw.decode("utf-8", errors="replace").splitlines()
                  if "[seekdb-hipvs-build]" in line]
    if len(candidates) != 1:
        raise RuntimeError("Missing or ambiguous GPU execution record during SQL CREATE")
    match = BUILD_LOG.fullmatch(candidates[0])
    if not match or (int(match[3]), int(match[4])) != (rows, dim):
        raise RuntimeError("GPU execution record does not match the SQL input")
    return {"producer": "seekdb-hipvs-build", "backend": "hipvs", "device_vendor": "AMD",
            "gpu_compute": True, "cpu_fallback": False, "completed": True,
            "rows": rows, "dim": dim, "gpu_name": match[1], "gpu_architecture": match[2],
            "table_name": table, "index_name": index,
            "correlation": "single build in dedicated server stderr during SQL CREATE and first ANN query",
            "stderr_start": start[2], "stderr_end": start[2] + len(raw),
            "stderr_interval_sha256": hashlib.sha256(raw).hexdigest(), "raw_record": candidates[0]}


def identifier(value: str) -> str:
    if not IDENTIFIER.fullmatch(value):
        raise ValueError("Unsafe SQL identifier")
    return "`" + value + "`"


def parse_options(value: str) -> dict[str, str]:
    options = {}
    for item in value.split(","):
        match = OPTION.fullmatch(item.strip())
        if not match or match[1].lower() in options:
            raise ValueError("WITH options must be unique name=value pairs without SQL fragments")
        options[match[1].lower()] = match[2].lower()
    return options


def options_sql(options: dict[str, str]) -> str:
    # Always round-trip through the same strict grammar before SQL interpolation.
    value = ",".join(f"{key}={item}" for key, item in sorted(options.items()))
    parse_options(value)
    return value


def validate_comparable(cpu: dict, gpu: dict, k: int) -> None:
    for options in (cpu, gpu):
        if options.get("distance") != "l2" or options.get("type") != "hnsw":
            raise ValueError("This harness compares FP32 HNSW using L2 distance only")
        for key in ("m", "ef_construction", "ef_search"):
            if not re.fullmatch(r"[0-9]+", options.get(key, "")):
                raise ValueError(f"Explicit integer {key} is required")
        if not 5 <= int(options["m"]) <= 64:
            raise ValueError("m must be in [5,64]")
        if not int(options["m"]) <= int(options["ef_construction"]) <= 1000:
            raise ValueError("ef_construction must be between m and 1000")
        if not k <= int(options["ef_search"]) <= 160000:
            raise ValueError("ef_search must be between K and 160000")
    for key in ("distance", "type", "m", "ef_construction", "ef_search"):
        if cpu[key] != gpu[key]:
            raise ValueError(f"CPU/GPU {key} parameters must match")
    if cpu.get("lib") != "vsag":
        raise ValueError("CPU baseline must explicitly use lib=vsag")


def generate_data(rows: int, dim: int, queries: int, seed: int):
    if rows < 2 or not 1 <= dim <= 16384 or queries < 1:
        raise ValueError("rows>=2, queries>=1 and 1<=dim<=16384 required")
    rng = np.random.default_rng(seed)
    vectors = rng.standard_normal((rows, dim)).astype(np.float32)
    query = rng.standard_normal((queries, dim)).astype(np.float32)
    # Above 2**53, so accidental JSON float ID conversions are detectable.
    ids = (np.arange(rows, dtype=np.int64) * 104729) + (2**53 + 101)
    if ids[-1] > np.iinfo(np.int64).max:
        raise ValueError("Document IDs exceed signed BIGINT")
    rng.shuffle(ids)
    return vectors, query, ids


def fingerprint(vectors, queries, ids) -> str:
    digest = hashlib.sha256()
    for array, dtype in ((vectors, "<f4"), (queries, "<f4"), (ids, "<i8")):
        digest.update(np.asarray(array.shape, dtype="<i8").tobytes())
        digest.update(np.asarray(array, dtype=dtype).tobytes())
    return digest.hexdigest()


def exact_neighbors(vectors, queries, ids, k: int):
    if not 1 <= k <= len(vectors):
        raise ValueError("K must be between 1 and row count")
    if not np.isfinite(vectors).all() or not np.isfinite(queries).all():
        raise ValueError("All coordinates must be finite")
    if len(set(map(int, ids))) != len(vectors):
        raise ValueError("Document IDs must be unique")
    # Float64 accumulation on the exact stored FP32 values; bounded one-query memory.
    data64 = np.asarray(vectors, dtype=np.float64)
    result = []
    for query in queries:
        delta = data64 - np.asarray(query, dtype=np.float64)
        squared = np.einsum("ij,ij->i", delta, delta)
        order = np.lexsort((ids, squared))[:k]
        result.append([int(value) for value in ids[order]])
    return result


def assess_results(results, truth, allowed_ids, k: int) -> dict:
    if not truth or len(results) != len(truth):
        raise ValueError("Result/query count mismatch")
    allowed = allowed_ids if isinstance(allowed_ids, set) else set(map(int, allowed_ids))
    recalls = []
    for rows, expected in zip(results, truth):
        if len(rows) != k:
            raise ValueError("ANN returned an incomplete result set")
        actual = []
        previous = -math.inf
        for doc_id, distance in rows:
            if not isinstance(doc_id, (int, np.integer)) or isinstance(doc_id, bool):
                raise ValueError("ANN document ID is not an exact integer")
            if int(doc_id) not in allowed:
                raise ValueError("ANN returned a document ID absent from the input")
            distance = float(distance)
            if not math.isfinite(distance) or distance < 0:
                raise ValueError("ANN returned an invalid L2 distance")
            if distance + 1e-6 < previous:
                raise ValueError("ANN returned unsorted distances")
            previous = distance
            actual.append(int(doc_id))
        if len(set(actual)) != k:
            raise ValueError("ANN returned duplicate IDs")
        recalls.append(len(set(actual).intersection(expected)) / k)
    return {"recall_at_k": statistics.mean(recalls), "min_query_recall": min(recalls),
            "queries": len(truth), "valid_ids": True}


def read_evidence(path: Path | None) -> list[dict]:
    if path is None:
        return []
    records = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        item = json.loads(line)
        if not isinstance(item, dict):
            raise ValueError("Evidence must contain JSON objects, one per line")
        records.append(item)
    return records


def match_evidence(round_result: dict, records: list[dict], rows: int, dim: int):
    matches = [item for item in records if item.get("index_name") == round_result["index_name"]
               and item.get("table_name") == round_result["table_name"]]
    if len(matches) != 1:
        return None, "missing or ambiguous per-index GPU build evidence"
    item = matches[0]
    expected = {"producer": "seekdb-hipvs-build", "backend": "hipvs", "device_vendor": "AMD",
                "gpu_compute": True, "cpu_fallback": False, "completed": True,
                "rows": rows, "dim": dim}
    for key, value in expected.items():
        if type(item.get(key)) is not type(value) or item[key] != value:
            return None, f"GPU build evidence rejected: {key}"
    if not isinstance(item.get("gpu_name"), str) or not item["gpu_name"].strip():
        return None, "GPU build evidence missing gpu_name"
    # Only propagate known non-secret fields, never arbitrary log/environment contents.
    clean = {key: item[key] for key in expected}
    clean.update({key: item[key] for key in ("gpu_name", "table_name", "index_name")})
    return clean, None


def storage_integrity_error(item: dict, config: dict) -> str | None:
    """Require the actual SQL snapshot proof for this exact measured index."""
    proof = item.get("storage_integrity")
    if not isinstance(proof, dict):
        return "missing actual SQL snapshot integrity proof"
    try:
        m = int(item["with"]["m"])
    except (KeyError, TypeError, ValueError):
        return "missing measured index M"
    expected = {"method": "sql_snapshot_native_hnsw", "source_table": item["table_name"],
                "index_name": item["index_name"], "stored_count": config["rows"],
                "mapped_count": config["rows"], "dim": config["dim"], "m": m,
                "vectors_exact": True, "all_ids_present": True}
    for key, value in expected.items():
        if type(proof.get(key)) is not type(value) or proof[key] != value:
            return f"SQL snapshot proof mismatch: {key}"
    for key in ("snapshot_sha256", "mapping_sha256"):
        if not isinstance(proof.get(key), str) or not re.fullmatch(r"[0-9a-f]{64}", proof[key]):
            return f"SQL snapshot proof missing digest: {key}"
    for key in ("data_table_id", "snapshot_tablet_id", "snapshot_bytes"):
        if type(proof.get(key)) is not int or proof[key] <= 0:
            return f"SQL snapshot proof invalid metadata: {key}"
    expected_snapshot = f"__idx_{proof['data_table_id']}_{item['index_name']}_index_snapshot_data_table"
    expected_mapping = f"__idx_{proof['data_table_id']}_rowkey_vid_table"
    if proof.get("snapshot_table") != expected_snapshot or proof.get("mapping_table") != expected_mapping:
        return "SQL snapshot proof auxiliary tables do not belong to this measured index"
    if not isinstance(proof.get("chunk_keys"), list) or not proof["chunk_keys"]:
        return "SQL snapshot proof has no chunks"
    version = None
    for position, key in enumerate(proof["chunk_keys"]):
        match = SNAPSHOT_KEY.fullmatch(key) if isinstance(key, str) else None
        if not match or int(match[1]) != proof["snapshot_tablet_id"] or int(match[3]) != position:
            return "SQL snapshot proof chunk sequence/tablet mismatch"
        if version is None:
            version = int(match[2])
        if version <= 0 or int(match[2]) != version:
            return "SQL snapshot proof contains mixed or invalid versions"
    return None


def summarize(report: dict, records: list[dict]) -> dict:
    rounds = report["rounds"]
    cpu = [item for item in rounds if item["mode"] == "cpu"]
    gpu = [item for item in rounds if item["mode"] == "gpu"]
    summary = {"status": "INCOMPLETE", "speedup": None, "meets_target": False}
    summary["cpu_median_build_sec"] = statistics.median([item["build_sec"] for item in cpu]) if cpu else None
    summary["gpu_median_build_sec"] = statistics.median([item["build_sec"] for item in gpu]) if gpu else None
    if len(cpu) != report["config"]["repeats"] or (not report["config"]["cpu_only"] and len(gpu) != len(cpu)):
        return summary
    if any(not math.isfinite(item["build_sec"]) or item["build_sec"] <= 0 for item in rounds):
        raise ValueError("Build durations must be finite and positive")
    quality_failures = []
    for item in rounds:
        recall = item["quality"]["recall_at_k"]
        if not math.isfinite(recall) or not report["config"]["min_recall"] <= recall <= 1:
            quality_failures.append(f"{item['index_name']}: recall below threshold or invalid")
        if item.get("vector_plan_verified") is not True:
            quality_failures.append(f"{item['index_name']}: requested vector index plan is not verified")
        error = storage_integrity_error(item, report["config"])
        if error:
            quality_failures.append(f"{item['index_name']}: {error}")
    if quality_failures:
        summary["status"] = "QUALITY_OR_FUNCTION_FAILED"
        summary["reasons"] = quality_failures
        return summary
    if report["config"]["cpu_only"]:
        summary["status"] = "CPU_ONLY"
        return summary
    reasons = []
    for item in gpu:
        item["gpu_evidence"], error = match_evidence(item, records, report["config"]["rows"], report["config"]["dim"])
        if error:
            reasons.append(f"{item['index_name']}: {error}")
    missing_hardware = [field for field in HARDWARE_FIELDS if not report.get("server_environment", {}).get(field)]
    if reasons or missing_hardware:
        summary.update(status="UNVERIFIED", reasons=reasons, missing_server_environment=missing_hardware)
        return summary
    summary["speedup"] = summary["cpu_median_build_sec"] / summary["gpu_median_build_sec"]
    target = report["config"]["target_speedup"]
    if target is None:
        summary["status"] = "MEASURED_TARGET_UNSPECIFIED"
    else:
        summary["meets_target"] = summary["speedup"] >= target
        summary["status"] = "PASS" if summary["meets_target"] else "PERFORMANCE_TARGET_NOT_MET"
    return summary


def query_sql(table: str, index: str, k: int) -> str:
    if not isinstance(k, int) or k < 1:
        raise ValueError("Invalid query K")
    identifier(table)
    identifier(index)
    return (f"SELECT /*+ INDEX(t {index}) */ id, l2_distance(v, %s) AS distance "
            f"FROM {identifier(table)} t ORDER BY l2_distance(v, %s) APPROXIMATE LIMIT {k}")


def vector_json(value) -> str:
    return json.dumps(np.asarray(value, dtype=np.float32).tolist(), separators=(",", ":"), allow_nan=False)


def coverage_retry_settings(rows: int, configured_ef: int) -> tuple[int, int]:
    """Diagnostic breadth and result count for seekdb's min(ef,max(1000,10*K)) cap.

    A LIMIT 1 query cannot exercise ef > 1000 even if the session variable is
    larger. Widen only the completeness diagnostic, never the timed/recall query.
    """
    if rows < 1 or not 1 <= configured_ef <= 160000:
        raise ValueError("Invalid coverage diagnostic shape or search breadth")
    ef = min(160000, max(configured_ef, rows))
    k = min(rows, max(1, (ef + 9) // 10))
    return ef, k


def coverage_query_result(rows, expected_id: int, expected_count: int, allowed_ids: set[int]):
    """Validate a self-query completely and return its exact first ID/distance."""
    # Recall is not used here: this diagnostic's one truth item is the input
    # itself, but every returned row still needs the normal safety checks.
    assess_results([rows], [[expected_id]], allowed_ids, expected_count)
    actual_id, distance = int(rows[0][0]), float(rows[0][1])
    return actual_id == expected_id and abs(distance) < 1e-6, {
        "returned_id": actual_id, "distance": distance}


def first_query_when_ready(connection, cursor, sql, values, max_retries=10):
    """Bound the baseline's post-DDL snapshot visibility retries inside the timer."""
    failures = []
    while True:
        try:
            cursor.execute(sql, values)
            return cursor.fetchall(), failures
        except Exception as error:
            code = error.args[0] if error.args and isinstance(error.args[0], int) else None
            if code not in (4016, 4138) or len(failures) >= max_retries:
                raise
            failures.append(code)
            # Discard a read snapshot older than the freshly created LOB tablets.
            # Never reuse a failed result or skip the eventual ANN correctness checks.
            connection.rollback()
            time.sleep(0.1)


def run_round(connection, report, mode, number, vectors, queries, ids, truth, options):
    cfg = report["config"]
    tag = report["run_id"]
    table = f"ahb_{tag}_{mode}_{number}"
    index = f"ahb_idx_{tag}_{mode}_{number}"
    created = False
    cursor = connection.cursor()
    try:
        # The session variable overrides the value stored in WITH(...).
        cursor.execute("SET SESSION ob_hnsw_ef_search=%s", (int(options["ef_search"]),))
        cursor.execute("SELECT @@session.ob_hnsw_ef_search")
        if int(cursor.fetchone()[0]) != int(options["ef_search"]):
            raise RuntimeError("Effective HNSW search breadth differs from the requested parameters")
        report["active_operation"] = f"{mode}/{number}: create raw table"
        cursor.execute(f"CREATE TABLE {identifier(table)} (id BIGINT PRIMARY KEY, v VECTOR({cfg['dim']}) NOT NULL)")
        created = True
        started = time.perf_counter()
        report["active_operation"] = f"{mode}/{number}: raw load"
        for begin in range(0, len(vectors), cfg["batch"]):
            batch = [(int(ids[i]), vector_json(vectors[i])) for i in range(begin, min(begin + cfg["batch"], len(vectors)))]
            cursor.executemany(f"INSERT INTO {identifier(table)} (id,v) VALUES (%s,%s)", batch)
        connection.commit()
        load_sec = time.perf_counter() - started
        report["active_operation"] = f"{mode}/{number}: verify raw IDs"
        cursor.execute(f"SELECT id FROM {identifier(table)} ORDER BY id")
        if [int(row[0]) for row in cursor.fetchall()] != sorted(map(int, ids)):
            raise RuntimeError("Raw input row/ID verification failed")
        cursor.execute(f"SHOW INDEX FROM {identifier(table)}")
        if any(row[2] != "PRIMARY" for row in cursor.fetchall()):
            raise RuntimeError("Unexpected pre-existing index before timed build")
        sql = query_sql(table, index, cfg["k"])
        first = vector_json(queries[0])
        log_path = Path(cfg["server_stderr"]) if cfg.get("server_stderr") else None
        log_start = log_position(log_path) if mode == "gpu" and log_path else None
        # Start before DDL: scans, GPU transfers, synchronization, graph conversion and
        # the first usable ANN query are all inside this interval. No standalone kernel timing.
        started = time.perf_counter()
        report["active_operation"] = f"{mode}/{number}: create vector index"
        cursor.execute(f"CREATE VECTOR INDEX {identifier(index)} ON {identifier(table)}(v) WITH ({options_sql(options)})")
        ddl_sec = time.perf_counter() - started
        report["active_operation"] = f"{mode}/{number}: first ANN query"
        first_rows, readiness_retries = first_query_when_ready(connection, cursor, sql, (first, first))
        assess_results([first_rows], truth[:1], ids, cfg["k"])
        build_sec = time.perf_counter() - started
        evidence = evidence_from_log(log_path, log_start, table, index, cfg["rows"], cfg["dim"]) if log_start else None
        report["active_operation"] = f"{mode}/{number}: explain vector index"
        cursor.execute("EXPLAIN " + sql, (first, first))
        plan = "\n".join(str(row[0]) for row in cursor.fetchall())
        vector_plan = "VECTOR INDEX" in plan.upper() and index.lower() in plan.lower()
        if not vector_plan:
            raise RuntimeError("EXPLAIN does not prove the requested vector index is used")
        query_started = time.perf_counter()
        report["active_operation"] = f"{mode}/{number}: recall queries"
        results = []
        for query in queries:
            parameter = vector_json(query)
            cursor.execute(sql, (parameter, parameter))
            results.append(cursor.fetchall())
        query_sec = time.perf_counter() - query_started
        quality = assess_results(results, truth, ids, cfg["k"])
        # Probe every input vector as an ANN reachability diagnostic. Approximate
        # misses alone cannot establish omitted storage; inspect SQL snapshots below.
        coverage_started = time.perf_counter()
        report["active_operation"] = f"{mode}/{number}: all-vector self ANN diagnostic"
        # Self ANN reachability is distinct from Recall@K at the
        # configured search breadth. Every input is still queried in both modes.
        coverage_ef, coverage_k = coverage_retry_settings(cfg["rows"], int(options["ef_search"]))
        found = 0
        missed = []
        missed_examples = []
        coverage_plan = None
        allowed_ids = set(map(int, ids))
        self_sql = query_sql(table, index, 1)
        for position, (vector, doc_id) in enumerate(zip(vectors, ids), 1):
            parameter = vector_json(vector)
            cursor.execute(self_sql, (parameter, parameter))
            own = cursor.fetchall()
            matched, detail = coverage_query_result(own, int(doc_id), 1, allowed_ids)
            if matched:
                found += 1
            else:
                missed.append((parameter, int(doc_id)))
                if len(missed_examples) < 20:
                    missed_examples.append({"expected_id": int(doc_id), "initial": detail})
            if position % 1000 == 0:
                print(f"{mode}/{number}: checked {position}/{len(ids)} input IDs", flush=True)
        if missed:
            cursor.execute("SET SESSION ob_hnsw_ef_search=%s", (coverage_ef,))
            cursor.execute("SELECT @@session.ob_hnsw_ef_search")
            if int(cursor.fetchone()[0]) != coverage_ef:
                raise RuntimeError("Coverage diagnostic session search breadth did not take effect")
            retry_sql = query_sql(table, index, coverage_k)
            parameter = missed[0][0]
            cursor.execute("EXPLAIN " + retry_sql, (parameter, parameter))
            coverage_plan = "\n".join(str(row[0]) for row in cursor.fetchall())
            if "VECTOR INDEX" not in coverage_plan.upper() or index.lower() not in coverage_plan.lower():
                raise RuntimeError("Coverage retry does not use the requested vector index")
            for position, (parameter, doc_id) in enumerate(missed):
                cursor.execute(retry_sql, (parameter, parameter))
                own = cursor.fetchall()
                matched, detail = coverage_query_result(own, doc_id, coverage_k, allowed_ids)
                if position < len(missed_examples):
                    missed_examples[position]["retry"] = detail
                    missed_examples[position]["retry_matched"] = matched
                if matched:
                    found += 1
        result = {"mode": mode, "round": number, "table_name": table, "index_name": index,
                  "raw_load_sec": load_sec, "ddl_sec": ddl_sec, "build_sec": build_sec,
                  "readiness_retry_codes": readiness_retries,
                  "query_ef_search": int(options["ef_search"]), "coverage_ef_search": coverage_ef,
                  "coverage_retry_count": len(missed),
                  "coverage_initial_effective_ef_search": min(int(options["ef_search"]), 1000),
                  "coverage_retry_limit": coverage_k,
                  "coverage_retry_effective_ef_search": min(coverage_ef, max(1000, 10 * coverage_k)),
                  "coverage_missed_examples": missed_examples, "coverage_query_plan": coverage_plan,
                  "query_sec": query_sec, "coverage_sec": time.perf_counter() - coverage_started,
                  "coverage_found": found, "quality": quality, "vector_plan_verified": vector_plan,
                  "query_plan": plan, "with": options}
        if evidence:
            result["raw_gpu_evidence"] = evidence
        # Preserve ANN diagnostics even when direct snapshot verification fails.
        report["rounds"].append(result)
        report["active_operation"] = f"{mode}/{number}: SQL snapshot integrity"
        storage_started = time.perf_counter()
        try:
            result["storage_integrity"] = verify_sql_snapshot(
                connection, table, index, vectors, ids, int(options["m"]))
        finally:
            result["storage_verification_sec"] = time.perf_counter() - storage_started
        report["active_operation"] = None
        print(json.dumps({"mode": mode, "round": number, "index_name": index, "build_sec": build_sec,
                          "recall_at_k": quality["recall_at_k"], "coverage_found": found,
                          "stored_count": result["storage_integrity"]["stored_count"]}), flush=True)
    finally:
        try:
            if created:
                # Only this process's successful CREATE is ever eligible for deletion.
                cursor.execute(f"DROP TABLE {identifier(table)}")
        finally:
            cursor.close()


def save_report(report: dict, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    summary = report["summary"]
    lines = ["# AMD hipVS 端到端构建实测", "", f"状态：**{summary['status']}**", "",
             "计时范围：CREATE VECTOR INDEX 开始至首条指定索引 ANN 查询完成；数据生成、原始导入及质量检查另计。", "",
             "|路径|轮次|导入秒|DDL秒|端到端构建秒|Recall@K|自身ANN命中（诊断）|快照精确存储校验|", "|---|---:|---:|---:|---:|---:|---:|---|"]
    for row in report["rounds"]:
        storage = (f"{row['storage_integrity']['stored_count']}/{report['config']['rows']}，FP32/ID一致"
                   if storage_integrity_error(row, report["config"]) is None else "未验证/失败")
        lines.append(f"|{row['mode']}|{row['round']}|{row['raw_load_sec']:.6f}|{row['ddl_sec']:.6f}|{row['build_sec']:.6f}|{row['quality']['recall_at_k']:.6f}|{row['coverage_found']}/{report['config']['rows']}|{storage}|")
    lines += ["", f"CPU中位数：{summary.get('cpu_median_build_sec')} 秒。",
              f"GPU中位数：{summary.get('gpu_median_build_sec')} 秒。",
              f"有效构建加速比：{summary.get('speedup') if summary.get('speedup') is not None else '未验证，不给出加速成绩'}。",
              f"配置的质量门槛：{report['config']['min_recall']}；配置的性能目标：{report['config']['target_speedup']}。",
              "目标值由操作者按统一评测配置提供，本工具不声称其即为赛事已公布标准。",
              "自身ANN未命中作为可达性诊断保留；完整性要求实际SQL索引快照包含全部输入ID及逐字节一致的FP32向量，正式Recall门槛仍独立生效。",
              "", "GPU路径证明必须来自对应seekdb构建过程的原始日志/分析证据；同名库、GPU查询耗时或CPU回退不能代替。",
              "完整配置、每轮原始记录、数据指纹、服务器环境及校验原因见同名JSON。"]
    if report.get("failure"):
        lines += ["", "运行发生错误；报告仅保留已经完成的轮次，不视为完成评测。", report["failure"]]
    if summary.get("reasons") or summary.get("missing_server_environment"):
        lines += ["", "未验证原因：", json.dumps({key: summary.get(key) for key in ("reasons", "missing_server_environment")}, ensure_ascii=False)]
    destination.with_suffix(".md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rows", type=int, default=10000)
    parser.add_argument("--dim", type=int, default=128)
    parser.add_argument("--queries", type=int, default=200)
    parser.add_argument("--k", type=int, default=10)
    parser.add_argument("--seed", type=int, default=20261031)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--batch", type=int, default=500)
    parser.add_argument("--timeout-sec", type=int, default=3600)
    parser.add_argument("--cpu-with", default="distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=200,ef_search=200")
    parser.add_argument("--gpu-with", help="Explicit implemented GPU WITH parameters; never guessed by this harness")
    parser.add_argument("--cpu-only", action="store_true", help="Baseline smoke/measurement only; never GPU acceptance")
    parser.add_argument("--min-recall", type=float, required=True, help="Use the published evaluation threshold")
    parser.add_argument("--target-speedup", type=float, help="Use the published target, otherwise target remains unspecified")
    parser.add_argument("--evidence-jsonl", type=Path)
    parser.add_argument("--environment-json", type=Path)
    parser.add_argument("--server-stderr", type=Path,
                        help="Dedicated seekdb stderr file; no concurrent index builds permitted")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.repeats < 3 or not 1 <= args.k <= args.rows or args.queries < 1 or args.batch < 1 or args.timeout_sec < 1:
        parser.error("At least 3 repeats, rows>=K>=1 and positive queries, batch, timeout required")
    if not 0 < args.min_recall <= 1 or (args.target_speedup is not None and (not math.isfinite(args.target_speedup) or args.target_speedup <= 0)):
        parser.error("Recall must be in (0,1]; target speedup must be finite and positive")
    if not args.cpu_only and not args.gpu_with:
        parser.error("--gpu-with is required unless --cpu-only is selected")
    return args


def main(argv=None):
    args = arguments(argv)
    cpu = parse_options(args.cpu_with)
    gpu = parse_options(args.gpu_with) if args.gpu_with else dict(cpu)
    validate_comparable(cpu, gpu, args.k)
    database = os.environ.get("AMD_BENCH_DATABASE", "test")
    identifier(database)
    server_environment = {}
    if args.environment_json:
        source = json.loads(args.environment_json.read_text(encoding="utf-8"))
        server_environment = {field: source[field] for field in HARDWARE_FIELDS if field in source}
    config = {key: getattr(args, key) for key in ("rows", "dim", "queries", "k", "seed", "repeats", "batch", "min_recall", "target_speedup", "timeout_sec", "cpu_only")}
    config["server_stderr"] = str(args.server_stderr) if args.server_stderr else None
    report = {"schema_version": 1, "started_utc": datetime.now(timezone.utc).isoformat(),
              "run_id": uuid.uuid4().hex[:16], "config": config, "cpu_with": cpu, "gpu_with": gpu,
              "driver_environment": {"python": platform.python_version(), "numpy": np.__version__, "os": platform.platform()},
              "server_environment": server_environment, "rounds": [], "summary": {"status": "INCOMPLETE"}}
    connection = None
    try:
        started = time.perf_counter()
        vectors, queries, ids = generate_data(args.rows, args.dim, args.queries, args.seed)
        report["data_generation_sec"] = time.perf_counter() - started
        report["dataset_sha256"] = fingerprint(vectors, queries, ids)
        started = time.perf_counter()
        truth = exact_neighbors(vectors, queries, ids, args.k)
        report["ground_truth_sec"] = time.perf_counter() - started
        import pymysql
        connection = pymysql.connect(host=os.environ.get("AMD_BENCH_HOST", "127.0.0.1"),
            port=int(os.environ.get("AMD_BENCH_PORT", "2881")), user=os.environ.get("AMD_BENCH_USER", "root"),
            password=os.environ.get("AMD_BENCH_PASSWORD", ""), database=database, autocommit=False,
            charset="utf8mb4", connect_timeout=15, read_timeout=args.timeout_sec + 30, write_timeout=args.timeout_sec + 30)
        with connection.cursor() as cursor:
            cursor.execute("SET SESSION ob_query_timeout=%s", (args.timeout_sec * 1000000,))
            cursor.execute("SET SESSION ob_trx_timeout=%s", (args.timeout_sec * 1000000,))
            cursor.execute("SELECT VERSION()")
            report["server_version"] = cursor.fetchone()[0]
        for number in range(1, args.repeats + 1):
            order = ("cpu",) if args.cpu_only else (("cpu", "gpu") if number % 2 else ("gpu", "cpu"))
            for mode in order:
                run_round(connection, report, mode, number, vectors, queries, ids, truth, cpu if mode == "cpu" else gpu)
                report["summary"] = summarize(report, [])
                save_report(report, args.output)
        records = read_evidence(args.evidence_jsonl)
        records += [row["raw_gpu_evidence"] for row in report["rounds"] if "raw_gpu_evidence" in row]
        report["summary"] = summarize(report, records)
        if args.evidence_jsonl:
            report["evidence_sha256"] = hashlib.sha256(args.evidence_jsonl.read_bytes()).hexdigest()
    except Exception as error:
        # Connection errors may include account/host information; report type+numeric code
        # instead of echoing credentials, arbitrary SQL, or complete driver exceptions.
        code = error.args[0] if error.args and isinstance(error.args[0], int) else None
        report["failure"] = f"{type(error).__name__}; error_code={code}; completed_rounds={len(report['rounds'])}"
        report["summary"] = {"status": "ERROR", "speedup": None, "meets_target": False}
        print(report["failure"], file=sys.stderr)
    finally:
        if connection:
            connection.close()
        save_report(report, args.output)
    print(json.dumps(report["summary"], ensure_ascii=False))
    return 0 if report["summary"]["status"] in ("PASS", "CPU_ONLY", "MEASURED_TARGET_UNSPECIFIED") else 2


if __name__ == "__main__":
    raise SystemExit(main())
