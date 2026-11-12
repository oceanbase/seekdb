"""Deploy a seekDB instance via pylibseekdb and capture the telemetry content.id.

The competition's "deploy seekdb" basic-practice task asks for the telemetry
identity reported by a running instance.  seekDB writes a telemetry payload to
the instance directory; this script starts a real instance, exercises it, and
prints every telemetry identity it can find.
"""
import json
import os
import sys
import glob
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
DB_DIR = os.path.join(HERE, "seekdb_instance")

import pylibseekdb as seekdb


def find_telemetry_files(root):
    """Return candidate telemetry payload files under the instance directory."""
    hits = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            low = name.lower()
            if "telemetry" in low or "identity" in low:
                hits.append(os.path.join(dirpath, name))
    return hits


def dump_jsonish(path):
    """Print a file's content, parsing JSON when possible."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            raw = fh.read()
    except Exception as exc:
        print("    read error:", exc)
        return
    if not raw.strip():
        print("    (empty)")
        return
    try:
        obj = json.loads(raw)
        print("    JSON:", json.dumps(obj, ensure_ascii=False, indent=2)[:2000])
    except Exception:
        print("    RAW:", raw[:1500].replace("\n", " "))


def main():
    os.makedirs(DB_DIR, exist_ok=True)
    print("db_dir:", DB_DIR)

    print("== opening seekdb instance ==")
    instance = seekdb.open(db_dir=DB_DIR)
    print("opened:", instance)

    opts = instance.connection_options()
    print("connection_options:", opts)

    conn = instance.connect(database="test", autocommit=True)
    cur = conn.cursor()
    cur.execute("SELECT 1")
    print("SELECT 1 ->", cur.fetchall())
    cur.execute("SELECT VERSION()")
    print("VERSION  ->", cur.fetchall())
    cur.execute("SHOW DATABASES")
    print("DATABASES->", [r[0] for r in cur.fetchall()])

    # Create a small vector table so the instance is a real, working deployment.
    cur.execute("""
        CREATE TABLE IF NOT EXISTS articles (
            id        INT PRIMARY KEY,
            title     TEXT,
            embedding VECTOR(4),
            VECTOR INDEX idx_vec (embedding)
                WITH (DISTANCE=l2, TYPE=hnsw, LIB=vsag)
        ) ORGANIZATION = HEAP
    """)
    cur.execute("INSERT INTO articles VALUES (1, 'Hello seekdb', '[0.1, 0.2, 0.3, 0.4]')")
    cur.execute("""
        SELECT id, title, l2_distance(embedding, '[0.1, 0.2, 0.3, 0.4]') AS dist
        FROM articles ORDER BY dist APPROXIMATE LIMIT 5
    """)
    print("vector search ->", cur.fetchall())

    cur.close()
    conn.close()

    print("== telemetry files ==")
    files = find_telemetry_files(DB_DIR)
    for path in files:
        print("  ", path)
        dump_jsonish(path)

    print("== any file mentioning content.id ==")
    for dirpath, _dirs, filenames in os.walk(DB_DIR):
        for name in filenames:
            path = os.path.join(dirpath, name)
            try:
                if os.path.getsize(path) > 8 * 1024 * 1024:
                    continue
                with open(path, "rb") as fh:
                    blob = fh.read()
            except Exception:
                continue
            for needle in (b'"content"', b"content.id", b"telemetry"):
                if needle in blob:
                    print("  HIT", needle, "in", path)
                    break

    instance.close()
    print("done")


if __name__ == "__main__":
    main()
