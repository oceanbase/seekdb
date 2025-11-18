import logging
import os

import dotenv
from pyobvector import VECTOR, FtsIndexParam, FtsParser, VectorIndex
from pyobvector.client.hybrid_search import HybridSearch
from sqlalchemy import VARCHAR, Column, Integer

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

dotenv.load_dotenv()

# OceanBase connection parameters
OCEANBASE_URI = os.getenv("OCEANBASE_URI")
OCEANBASE_USER = os.getenv("OCEANBASE_USER")
OCEANBASE_PASSWORD = os.getenv("OCEANBASE_PASSWORD")
OCEANBASE_DBNAME = os.getenv("OCEANBASE_DBNAME")


def get_or_create_client() -> HybridSearch:
    """Get or create OceanBase HybridSearch client."""
    return HybridSearch(
        uri=OCEANBASE_URI,
        user=OCEANBASE_USER,
        password=OCEANBASE_PASSWORD,
        db_name=OCEANBASE_DBNAME,
    )


if __name__ == "__main__":
    uri = os.getenv("OCEANBASE_URI")
    user = os.getenv("OCEANBASE_USER")
    password = os.getenv("OCEANBASE_PASSWORD")
    db_name = os.getenv("OCEANBASE_DBNAME")

    client = HybridSearch(
        uri=os.getenv("OCEANBASE_URI"),
        user=os.getenv("OCEANBASE_USER"),
        password=os.getenv("OCEANBASE_PASSWORD"),
        db_name=os.getenv("OCEANBASE_DBNAME"),
    )

    test_table_name = "hybrid_search_test"

    client.create_table(
        table_name=test_table_name,
        columns=[
            Column("id", Integer, primary_key=True, autoincrement=False),
            Column("source_id", VARCHAR(32)),
            Column("enabled", Integer),
            Column("vector", VECTOR(3)),
            Column("title", VARCHAR(255)),
            Column("content", VARCHAR(255)),
        ],
        indexes=[
            VectorIndex("vec_idx", "vector", params="distance=l2, type=hnsw, lib=vsag"),
        ],
        mysql_charset="utf8mb4",
        mysql_collate="utf8mb4_unicode_ci",
        mysql_organization="heap",
    )

    for col in ["title", "content"]:
        client.create_fts_idx_with_fts_index_param(
            table_name=test_table_name,
            fts_idx_param=FtsIndexParam(
                index_name=f"fts_idx_{col}",
                field_names=[col],
                parser_type=FtsParser.IK,
            ),
        )

    client.insert(
        table_name=test_table_name,
        data=[
            {
                "id": 1,
                "source_id": "3b767712b57211f09c170242ac130008",
                "enabled": 1,
                "vector": [1, 1, 1],
                "title": "企业版和社区版的功能差异",
                "content": "OceanBase 数据库提供企业版和社区版两种形态。",
            },
            {
                "id": 2,
                "vector": [1, 2, 3],
                "enabled": 1,
                "source_id": "3b791472b57211f09c170242ac130008",
                "title": "快速体验 OceanBase 社区版",
                "content": "本文根据使用场景详细介绍如何快速部署 OceanBase 数据库，旨在帮助您快速掌握并成功使用 OceanBase 数据库。",
            },
            {
                "id": 3,
                "source_id": "3b7af31eb57211f09c170242ac130008",
                "enabled": 1,
                "vector": [3, 2, 1],
                "title": "配置最佳实践",
                "content": "为了确保用户在各种业务场景下，能够基于 OceanBase 数据库获得比较好的性能，OceanBase 基于过往大量真实场景的调优经验总结了各类业务场景下一些核心配置项和变量的推荐配置。",
            },
            {
                "id": 4,
                "source_id": "3b7cb9ceb57211f09c170242ac130008",
                "enabled": 1,
                "vector": [2, 2, 2],
                "title": "OceanBase 实时分析能力白皮书",
                "content": "重点解读 OceanBase 实时分析能力的 8 大核心特性，以及在 HTAP 混合负载场景、实时数据分析场景，和 PL/SQL 批处理场景的应用实践与案例。",
            },
        ],
    )

    query = {
        "bool": {
            "must": [
                {
                    "query_string": {
                        "fields": ["title^10", "content"],
                        "type": "best_fields",
                        "query": '((数据)^0.5106318299637825 (迁移)^0.2651122588583924 (oceanbase)^0.22425591117782506 ("oceanbase 数据 迁移"~2)^1.5)',
                        "minimum_should_match": "30%",
                        "boost": 1,
                    }
                }
            ],
            "filter": [
                {
                    "terms": {
                        "source_id": [
                            "3b791472b57211f09c170242ac130008",
                            "3b7af31eb57211f09c170242ac130008",
                        ]
                    }
                },
                {"bool": {"must_not": [{"range": {"enabled": {"lt": 1}}}]}},
            ],
            "boost": 0.7,
        }
    }

    request = {
        "query": query,
        "knn": {
            "field": "vector",
            "k": 1024,
            "num_candidates": 1024,
            "query_vector": [1, 2, 3],
            "filter": query,
            "similarity": 0.2,
        },
        "from": 0,
        "size": 60,
    }

    res = client.search(index=test_table_name, body=request)
    assert isinstance(res, list)
    assert len(res) > 0

    client.drop_table_if_exist(table_name=test_table_name)
