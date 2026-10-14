# Copyright (c) 2025 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.

"""Community reports and global queries for the pinned GraphRAG integration.

Pass tables from to_tables(), communities from build_communities(), a GraphRAG
LLMCompletion-compatible model and its tokenizer. Model credentials, timeouts
and retries belong to the caller's provider; nothing is read from environment.
Inputs are caller-owned and must not be mutated during an operation.

Report generation includes complete community evidence or fails its token
budget; it never silently drops entities/edges. Global search uses GraphRAG's
normal report batching and scored map/reduce context selection, not exhaustive
fact enumeration. Returned provenance is the source snapshot, not proof that an
LLM's citations or factual claims are correct. No cross-store atomicity or
automatic refresh after subsequent database writes is promised.
"""

import hashlib
import json
import math
from dataclasses import dataclass

import pandas as pd
from graphrag.data_model.schemas import COMMUNITY_REPORTS_FINAL_COLUMNS
from graphrag.index.operations.finalize_community_reports import finalize_community_reports
from graphrag.index.operations.summarize_communities.community_reports_extractor import CommunityReportsExtractor
from graphrag.prompts.index.community_report import COMMUNITY_REPORT_PROMPT
from graphrag.prompts.query.global_search_reduce_system_prompt import NO_DATA_ANSWER
from graphrag.query.indexer_adapters import read_indexer_communities, read_indexer_reports
from graphrag.query.structured_search.global_search.community_context import GlobalCommunityContext
from graphrag.query.structured_search.global_search.search import GlobalSearch


@dataclass(frozen=True)
class ReportBundle:
    reports: pd.DataFrame
    snapshot_digest: str


def _digest(tables, communities):
    # Detect accidental reuse with changed attributes, another tenant or different
    # community membership. This is a consistency check, not a security signature.
    payload = {name: frame.to_json(orient="split", force_ascii=False)
               for name, frame in tables.items()}
    payload["communities"] = communities.to_json(orient="split", force_ascii=False)
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()


def _positive(value):
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError("token limits must be positive integers")


class _CheckedModel:
    """Keep upstream's caught provider/stream/JSON failures observable."""

    def __init__(self, model):
        self.model = model
        self.error = None

    async def completion_async(self, **kwargs):
        self.check()
        try:
            response = await self.model.completion_async(**kwargs)
            if kwargs.get("stream"):
                return self._stream(response)
            if kwargs.get("response_format_json_object"):
                parsed = json.loads(response.content)
                if not isinstance(parsed, dict) or not isinstance(parsed.get("points"), list):
                    raise ValueError("invalid global map response")
                for point in parsed["points"]:
                    score = point.get("score") if isinstance(point, dict) else None
                    if (not isinstance(point, dict) or not isinstance(point.get("description"), str)
                            or isinstance(score, bool) or not isinstance(score, (int, float))
                            or not math.isfinite(score) or not 0 <= score <= 100):
                        raise ValueError("invalid global map point")
            return response
        except Exception as error:
            self.error = error
            raise

    async def _stream(self, response):
        has_text = False
        try:
            async for chunk in response:
                has_text = has_text or bool(chunk.choices[0].delta.content)
                yield chunk
            if not has_text:
                raise ValueError("empty global reduce response")
        except Exception as error:
            self.error = error
            raise

    def check(self):
        if self.error is not None:
            raise RuntimeError("GraphRAG model call failed; no partial result returned") from self.error


async def generate_reports(tables, communities, *, model, tokenizer,
                           max_input_tokens=16000, max_report_length=2000):
    """Generate every community's report using GraphRAG's structured extractor.

    Calls are sequential. Oversized complete input raises before that call;
    earlier successful calls may still have incurred cost, but no partial bundle
    is returned. The provider must implement cancellation for its own I/O.
    """
    _positive(max_input_tokens)
    _positive(max_report_length)
    digest = _digest(tables, communities)
    guard = _CheckedModel(model)
    extractor = CommunityReportsExtractor(guard, COMMUNITY_REPORT_PROMPT, max_report_length)
    reports = []
    for community in communities.itertuples():
        entities = tables["entities"][tables["entities"].id.isin(community.entity_ids)]
        edges = tables["relationships"][tables["relationships"].id.isin(community.relationship_ids)]
        ids = set(community.entity_ids) | set(community.relationship_ids)
        provenance = tables["seekdb_provenance"][tables["seekdb_provenance"].id.isin(ids)]
        if len(entities) != len(community.entity_ids) or len(edges) != len(community.relationship_ids):
            raise ValueError("community references missing snapshot records")
        context = json.dumps({"Entities": entities.to_dict("records"),
                              "Relationships": edges.to_dict("records"),
                              "Source records": provenance.to_dict("records")}, ensure_ascii=False)
        prompt = COMMUNITY_REPORT_PROMPT.format(input_text=context, max_report_length=max_report_length)
        if len(tokenizer.encode(prompt)) > max_input_tokens:
            raise ValueError("complete community evidence exceeds report input budget")
        result = await extractor(context)
        guard.check()
        report = result.structured_output
        if (report is None or not report.title.strip() or not report.summary.strip()
                or not math.isfinite(report.rating) or not 0 <= report.rating <= 10):
            raise ValueError("invalid or missing community report")
        reports.append({"community": community.community, "level": community.level,
                        "title": report.title, "summary": report.summary, "rank": report.rating,
                        "rating_explanation": report.rating_explanation,
                        "findings": [item.model_dump() for item in report.findings],
                        "full_content": result.output, "full_content_json": report.model_dump_json()})
    frame = (finalize_community_reports(pd.DataFrame(reports), communities) if reports
             else pd.DataFrame(columns=COMMUNITY_REPORTS_FINAL_COLUMNS))
    # Identical model text must not alias two communities or tenant snapshots.
    frame["id"] = [hashlib.sha256(f"{digest}:{community}".encode()).hexdigest()
                   for community in frame.community]
    if digest != _digest(tables, communities):
        raise ValueError("snapshot changed during report generation")
    return ReportBundle(frame, digest)


async def global_query(tables, communities, bundle, query, *, model, tokenizer,
                       max_context_tokens=8000, max_data_tokens=8000):
    """Query all root communities with GraphRAG's real map/reduce implementation.

    Returns answer, detailed GraphRAG context, and snapshot provenance for
    tracing report -> community -> entity/edge -> stored source/version.
    Root level intentionally covers every entity once, including isolates.
    """
    _positive(max_context_tokens)
    _positive(max_data_tokens)
    if not isinstance(query, str) or not query.strip():
        raise ValueError("query must be nonempty text")
    if bundle.snapshot_digest != _digest(tables, communities):
        raise ValueError("reports do not belong to this snapshot")
    if (len(bundle.reports) != len(communities)
            or set(bundle.reports.community) != set(communities.community)):
        raise ValueError("incomplete community report bundle")
    if communities.empty:
        return {"answer": NO_DATA_ANSWER, "details": None,
                "provenance": tables["seekdb_provenance"].copy()}
    reports = read_indexer_reports(bundle.reports, communities, community_level=0)
    guard = _CheckedModel(model)
    context = GlobalCommunityContext(reports, read_indexer_communities(communities, bundle.reports),
                                     tokenizer=tokenizer)
    search = GlobalSearch(
        guard, context, tokenizer=tokenizer, concurrent_coroutines=1,
        # In 3.3.0 json_mode=True duplicates response_format_json_object at the
        # map call. The map implementation itself already sets that flag.
        json_mode=False, allow_general_knowledge=False, max_data_tokens=max_data_tokens,
        context_builder_params={"max_context_tokens": max_context_tokens,
                                "include_community_weight": False, "shuffle_data": False})
    result = await search.search(query)
    guard.check()
    if bundle.snapshot_digest != _digest(tables, communities):
        raise ValueError("snapshot changed during global query")
    return {"answer": result.response, "details": result,
            "provenance": tables["seekdb_provenance"].copy()}
