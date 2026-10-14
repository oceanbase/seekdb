# Copyright (c) 2025 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.

"""Real GraphRAG report/map/reduce code, deterministic model responses only.

These test orchestration, provenance and failures, not model answer quality.
Run together with test_seekdb_snapshot.py using unittest discovery.
"""

import asyncio
import json
import unittest

from graphrag_llm.tokenizer import Tokenizer
from graphrag_llm.types import LLMCompletionChunk, LLMChoiceChunk, LLMChoiceDelta
from graphrag_llm.utils import create_completion_response

from seekdb_query import ReportBundle, generate_reports, global_query
from seekdb_snapshot import build_communities, to_tables
from test_seekdb_snapshot import fixture


class CharacterTokenizer(Tokenizer):
    def __init__(self):
        pass

    def encode(self, text):
        return list(map(ord, text))

    def decode(self, tokens):
        return "".join(map(chr, tokens))


class FixtureModel:
    def __init__(self, failure=None):
        self.calls = []
        self.failure = failure

    async def completion_async(self, **kwargs):
        self.calls.append(kwargs)
        if "response_format" in kwargs:
            if self.failure == "report":
                raise OSError("injected report failure")
            report = {"title": "Fixture community", "summary": "Fixture summary",
                      "rating": 3, "rating_explanation": "Fixture only",
                      "findings": [{"summary": "Finding", "explanation": "Known fixture evidence"}]}
            response = create_completion_response(json.dumps(report))
            response.formatted_response = kwargs["response_format"](**report)
            return response
        if kwargs.get("stream"):
            async def stream():
                yield LLMCompletionChunk(id="test", created=0, model="fixture", object="chat.completion.chunk",
                    choices=[LLMChoiceChunk(index=0, delta=LLMChoiceDelta(content="Fixture answer"))])
                if self.failure == "stream":
                    raise OSError("injected stream failure")
            return stream()
        if self.failure == "map":
            raise OSError("injected map failure")
        text = ('{"points": [{"description": "Fixture finding", "score": 80}]}'
                if self.failure != "json" else "invalid JSON")
        return create_completion_response(text)


class QueryTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.tables = to_tables(fixture(), workspace="tenant", namespace="graph")
        self.communities = build_communities(self.tables)
        self.tokenizer = CharacterTokenizer()

    async def reports(self, model=None):
        return await generate_reports(self.tables, self.communities, model=model or FixtureModel(),
                                      tokenizer=self.tokenizer, max_input_tokens=100000)

    async def test_reports_and_query(self):
        model = FixtureModel()
        bundle = await self.reports(model)
        self.assertEqual(len(bundle.reports), len(self.communities))
        self.assertEqual(bundle.reports.id.nunique(), len(self.communities))
        self.assertEqual(len(model.calls), len(self.communities))
        self.assertTrue(any('"title": "isolated"' in call["messages"] for call in model.calls))
        result = await global_query(self.tables, self.communities, bundle, "What are the themes?",
                                    model=model, tokenizer=self.tokenizer, max_context_tokens=160)
        self.assertEqual(result["answer"], "Fixture answer")
        self.assertGreater(result["details"].llm_calls_categories["map"], 1)
        self.assertEqual(result["details"].llm_calls_categories["reduce"], 1)
        self.assertEqual(result["provenance"].id.tolist(), self.tables["seekdb_provenance"].id.tolist())

    async def test_failures_do_not_return_partial_results(self):
        with self.assertRaises(RuntimeError):
            await self.reports(FixtureModel("report"))
        bundle = await self.reports()
        for failure in ("map", "json", "stream"):
            with self.subTest(failure=failure), self.assertRaises(RuntimeError):
                await global_query(self.tables, self.communities, bundle, "themes",
                                   model=FixtureModel(failure), tokenizer=self.tokenizer)

    async def test_snapshot_mismatch_missing_reports_and_budget(self):
        bundle = await self.reports()
        model = FixtureModel()
        with self.assertRaises(ValueError):
            await generate_reports(self.tables, self.communities, model=model,
                                   tokenizer=self.tokenizer, max_input_tokens=1)
        self.assertEqual(model.calls, [])
        with self.assertRaises(ValueError):
            await global_query(self.tables, self.communities,
                               ReportBundle(bundle.reports.iloc[:-1], bundle.snapshot_digest), "themes",
                               model=model, tokenizer=self.tokenizer)
        self.tables["entities"].loc[0, "description"] = "changed"
        with self.assertRaises(ValueError):
            await global_query(self.tables, self.communities, bundle, "themes",
                               model=model, tokenizer=self.tokenizer)
        self.assertEqual(model.calls, [])

    async def test_empty_and_cancellation(self):
        tables = to_tables({"nodes": [], "edges": []}, workspace="t", namespace="g")
        communities = build_communities(tables)
        model = FixtureModel()
        bundle = await generate_reports(tables, communities, model=model, tokenizer=self.tokenizer)
        result = await global_query(tables, communities, bundle, "themes", model=model, tokenizer=self.tokenizer)
        self.assertIsNone(result["details"])
        self.assertEqual(model.calls, [])
        entered, stopped = asyncio.Event(), asyncio.Event()
        class PausedModel:
            async def completion_async(self, **kwargs):
                entered.set()
                try:
                    await asyncio.Event().wait()
                finally:
                    stopped.set()
        task = asyncio.create_task(self.reports(PausedModel()))
        await asyncio.wait_for(entered.wait(), 5)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(stopped.is_set())
        entered.clear()
        stopped.clear()
        bundle = await self.reports()
        task = asyncio.create_task(global_query(self.tables, self.communities, bundle, "themes",
                                                model=PausedModel(), tokenizer=self.tokenizer,
                                                max_context_tokens=160))
        await asyncio.wait_for(entered.wait(), 5)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(stopped.is_set())


if __name__ == "__main__":
    unittest.main()
