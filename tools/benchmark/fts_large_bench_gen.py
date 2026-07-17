#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成大规模全文索引压测数据（批量 INSERT SQL）。"""

import argparse
import random
import sys

TITLES_CN = [
    "数据库性能优化实践",
    "全文索引分词器热路径分析",
    "OceanBase 稳定性的工程经验",
    "倒排索引与检索系统设计",
    "中文分词 ik 词典加载机制",
    "搜索引擎召回率调优笔记",
    "分布式存储 compaction 策略",
    "SQL 执行计划与索引选择",
    "日志系统内存池化设计",
    "停用词过滤的全局单例实现",
]

TITLES_EN = [
    "Full-text search indexing benchmark",
    "Tokenization hot path optimization",
    "Database audit and security review",
    "Inverted index build performance",
    "English tokenizer beng parser test",
    "Query latency under heavy load",
    "Document retrieval relevance scoring",
    "Bulk insert throughput measurement",
]

CONTENT_CN = [
    "全文索引的分词器热路径优化包括解析器实例复用、停用词全局单例、内存池化数据结构 ObFastSegmentArray 等技术。",
    "在大量文档索引场景下，ik 分词需要频繁查词典和判断停用词，CPU 开销主要集中在分词热路径。",
    "OceanBase 是一款非常稳定的数据库，支持 ik、beng、space、ngram 等多种内置分词器。",
    "倒排索引是全文检索的核心数据结构，分词质量直接影响召回率、准确率和查询延迟。",
    "中华人民共和国人民大会堂是著名地标，本文用于测试中文 smart 与 max_word 分词差异。",
    "性能优化需要结合 profile 数据定位热点，reuse_parser 和 metadata_alloc 分离是常见手段。",
]

CONTENT_EN = [
    "The quick brown fox jumps over the lazy dog. Full-text search requires efficient tokenization.",
    "Completed the annual security audit. No major vulnerabilities were found in the system.",
    "Database performance optimization focuses on index build, query planning and IO reduction.",
    "Bulk loading documents triggers repeated tokenizer execution on the indexing hot path.",
    "English beng parser tokenization is used as a baseline for cross-parser comparison.",
]

CATEGORIES = list(range(1, 11))


def esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace("'", "''")


def make_row(i: int) -> tuple:
    cat = CATEGORIES[i % len(CATEGORIES)]
    title_en = f"{TITLES_EN[i % len(TITLES_EN)]} #{i}"
    body_en = f"{CONTENT_EN[i % len(CONTENT_EN)]} row={i}. Benchmark document for beng parser."
    if i % 3 == 0:
        title = f"{TITLES_EN[i % len(TITLES_EN)]} #{i}"
        body = f"{CONTENT_EN[i % len(CONTENT_EN)]} row={i}. " + CONTENT_CN[i % len(CONTENT_CN)]
    elif i % 3 == 1:
        title = f"{TITLES_CN[i % len(TITLES_CN)]} #{i}"
        body = f"{CONTENT_CN[i % len(CONTENT_CN)]} 文档编号={i}。" + CONTENT_EN[i % len(CONTENT_EN)]
    else:
        title = f"{TITLES_CN[i % len(TITLES_CN)]} / {TITLES_EN[i % len(TITLES_EN)]} #{i}"
        body = (
            f"{CONTENT_CN[i % len(CONTENT_CN)]} "
            f"{CONTENT_EN[i % len(CONTENT_EN)]} "
            f"混合文档 row={i}，用于压测 MATCH 与 TOKENIZE。"
        )
    return cat, title, body, title_en, body_en


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate FTS large bench INSERT SQL")
    parser.add_argument("--rows", type=int, default=20000, help="total rows to insert")
    parser.add_argument("--batch", type=int, default=500, help="rows per INSERT statement")
    parser.add_argument("--seed", type=int, default=42, help="random seed for reproducibility")
    args = parser.parse_args()

    if args.rows <= 0 or args.batch <= 0:
        print("rows and batch must be positive", file=sys.stderr)
        return 1

    random.seed(args.seed)
    print("USE fts_large_bench;")
    print("START TRANSACTION;")

    batch_vals = []
    for i in range(args.rows):
        cat, title, body, title_en, body_en = make_row(i)
        batch_vals.append(
            f"({cat}, '{esc(title)}', '{esc(body)}', "
            f"'{esc(title_en)}', '{esc(body_en)}')"
        )
        if len(batch_vals) >= args.batch:
            print("INSERT INTO docs (category, title, content, title_en, content_en) VALUES")
            print(",\n".join(batch_vals) + ";")
            batch_vals = []

    if batch_vals:
        print("INSERT INTO docs (category, title, content, title_en, content_en) VALUES")
        print(",\n".join(batch_vals) + ";")

    print("COMMIT;")
    print(f"SELECT COUNT(*) AS loaded_rows FROM docs;")
    return 0


if __name__ == "__main__":
    sys.exit(main())
