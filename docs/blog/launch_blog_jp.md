# その Benchmark で Agent のベクトルデータベースを選ぶのは間違いだ

**Agent の実際の負荷は streaming workload です——ほとんどのベクトルデータベースはそのために設計されていません。**

もしあなたが Agent 用のベクトルデータベースを選んでいるなら、おそらく ann-benchmarks や各社が公式に出している性能比較を参考にしているでしょう。それらのテストが実行しているのはこのような負荷です：まずデータを一括インポートし、インデックスを構築してから、読み取り専用のクエリを実行する。

**これは Agent の負荷ではありません。**

Agent の実際の負荷はこうです：

```python
for step in agent.run():
    memory.write(step.observation)        # 継続的に書き込み
    relevant = memory.search(step.query)  # ミリ秒後に検索
```

書き込みと検索が同時に発生し、間隔はミリ秒単位で、しかも並行して行われます。この負荷パターンには名前があります——streaming workload です。[VectorDBBench](https://github.com/zilliztech/VectorDBBench) はこのために StreamingPerformanceCase を設計しました：固定レートで継続的に書き込み + 並行クエリ、本番環境の Agent とまったく同じです。

VectorDBBench は Zilliz（Milvus を開発している会社）がメンテナンスしているサードパーティのオープンソース benchmark フレームワークです。私たちはこれを使って 6 つの主要なベクトルデータベースをテストしました。

---

## 見落とされている指標：並行処理時に P99 はどれだけ悪化するのか？

テスト条件：Cohere 10M データセット（768 次元）、16 vCPU / 64 GiB、統一 HNSW インデックスパラメータ（M=16 / ef_construction=256 / ef_search=200）、継続的に 500 行/秒で書き込み。

![seekdb streaming benchmark: 6 vector databases compared](../../images/benchmark_full.svg)

多くの人は benchmark を見るとき QPS とシリアル遅延だけを見ます。しかし Agent は本番環境でシングルスレッドで動いているわけではありません。**実際に SLA を決めるのは並行時の P99——そして並行度が上がったときに何倍に膨れ上がるかです。**

グラフの「P99 Jitter」グループを見てください：

- **ES：10.3 倍**——シリアル P99 はわずか 5.2ms（seekdb より速い）ですが、並行処理を開始した途端 53.6ms に跳ね上がります
- **Milvus：9.7 倍**——シリアル 15.9ms、並行時は一気に 153.6ms まで急上昇
- **seekdb：1.1 倍**——19.7ms から 21.7ms へ、ほぼ変化なし

これはパラメータチューニングの問題ではありません——アーキテクチャの問題です。次のセクションで詳しく説明します。

> テストスクリプトと設定の全文：[github.com/oceanbase/vdb-streambench](https://github.com/oceanbase/vdb-streambench)、他の製品の追加 PR も歓迎します。

---

## なぜ streaming 負荷で P99 が爆発するのか

Milvus、ES、Qdrant はそれぞれが得意とするシナリオ（一括インポート + 読み取り専用クエリ）では優れたパフォーマンスを発揮します——もともとそのシナリオのために設計されているからです。しかし streaming 書き込みは構造的な問題を露呈させます：新しい segment が次々と生成されるのです。クエリ時には N 個の segment それぞれに対して fanout して knn を実行し、結果をマージする必要があります。シングルスレッドであればなんとか制御可能ですが、**並行度が上がると、N 個の segment × M 個のクエリスレッドが CPU 上で互いに競合し、P99 が急上昇します。**

**ほとんどのベクトルデータベースでは、streaming 書き込みに伴いインデックスの segment 数が膨張し、並行クエリの競合が悪化し続けます。seekdb のインデックス数は固定です（常に 2 つだけ）ので、この問題は発生しません。**

具体的には、seekdb v1.3.0 は streaming 負荷のために 2 つのメカニズムを設計しました：

**第一に、書き込みパスはインデックスに触れません。** トランザクションのコミット後は redo log を書くだけで即座に返します。独立した Change Stream パイプラインがバックグラウンドで非同期に redo log を消費し、ベクトルをメモリ上の delta HNSW インデックスに書き込みます。書き込みとインデックス構築は物理的に完全に分離されており——書き込みがインデックス構築によってブロックされることはありません。

**第二に、クエリパスは常に 2 つのインデックスだけを参照します。** seekdb は delta HNSW（増分層、新しい書き込みを受け付ける）と snapshot HNSW（メインストレージ層）を維持しており、LSM-Tree の階層構造に似た考え方です。クエリ時には 2 つのインデックスそれぞれに knn search を実行して結果をマージします——どれだけデータを書き込んでも、インデックス数は膨張せず、並行クエリの競合は発生しません。

私たち自身がこの問題を経験しました。グラフの seekdb v1.2.0 のグループ——69 QPS、並行 P99 410ms——これがアーキテクチャを書き直す前の成績です。旧バージョンの書き込みパスはインデックスを同期的に構築しており、上述した従来のアーキテクチャと同じ問題を抱えていました。書き直し後、同じ製品で QPS は 22 倍に向上、P99 遅延は 1/19 に低減——すべてこの 2 つのメカニズムによるものです。

---

## Agent に必要なのは速さだけではない——「やり直し」の仕組みも必要

パフォーマンスについては説明しました。しかし Agent を開発したことがある方なら、もう一つの課題をご存知でしょう：Agent は試行錯誤的にデータを変更する必要があります（memory の書き換え、実験の実行、テーブルの破壊的変更など）。**安全なサンドボックスとロールバックの仕組みが必要です。**

ほとんどのベクトルデータベースにはこの概念がありません。seekdb はカーネルレベルで Copy-on-Write を実装しています：

```sql
-- サブ秒のスナップショット、データのコピーなし
FORK DATABASE agent_state TO sandbox_42;

-- Agent はサンドボックス内で自由に操作
USE sandbox_42;
INSERT INTO memory (embedding, content) VALUES ('[0.1,...]', 'new observation');

-- 試行成功 → メインラインにマージ
MERGE TABLE sandbox_42.memory INTO agent_state.memory STRATEGY THEIRS;

-- 試行失敗 → 破棄、メインラインは影響なし
DROP DATABASE sandbox_42;
```

これはカーネルレベルの COW であり、アプリケーション層の snapshot/restore ではありません。fork は秒単位で完了し、データのコピーは行わず、各サンドボックスは完全に書き込み可能なデータベースです（テーブル構造、ベクトルインデックス、自動インクリメントカラムすべてが正常に動作します）。3 つのコンフリクト戦略（`FAIL` / `THEIRS` / `OURS`）により、Agent の変更をどこまで信頼するかを精密に制御できます。`FORK DATABASE` と `FORK TABLE` の 2 つの粒度をサポートしています。

---

## 1 つの SQL でハイブリッド検索を完結

Agent の検索は通常、純粋なベクトル類似度だけではありません。著者やタイムレンジでフィルタリングしつつ、全文検索も組み合わせたい場合があるでしょう。seekdb では、これが 1 つの SQL で実現できます：

```sql
SELECT id, title, l2_distance(emb, '[0.12,0.34,...]') AS dist
FROM docs
WHERE MATCH(content) AGAINST ('quarterly report')
  AND author_id = 42
  AND created_at > '2026-01-01'
ORDER BY dist APPROXIMATE LIMIT 10;
```

ベクトル + 全文検索 + スカラーフィルタリングが同じ実行計画内でプッシュダウンされ、クライアント側で複数のクエリ結果を組み合わせる必要はありません。完全な MySQL プロトコル互換で、LangChain / LlamaIndex / Dify / 任意の MySQL クライアントからそのまま接続できます。

---

## 30 秒で試してみる

```bash
pip install -U pyseekdb
```

```python
import pyseekdb

client = pyseekdb.Client(path="./agent_state.db")
memory = client.get_or_create_collection(name="episodic")

# 第1ラウンド：Agent の観察を書き込み
memory.upsert(
    ids=["1", "2", "3"],
    documents=[
        "user prefers dark mode",
        "user speaks English and Chinese",
        "user timezone is UTC+8",
    ],
)
memory.refresh_index()

results = memory.query(query_texts="ui preferences?", n_results=1)
print(results["documents"])
# -> [['user prefers dark mode']]

# 第2ラウンド：新しい観察を書き込み、インデックスをリフレッシュ後すぐに検索可能
memory.upsert(ids=["4"], documents=["user saw pricing page 3 times today"])
memory.refresh_index()

results = memory.query(query_texts="purchase intent signals", n_results=1)
print(results["documents"])
# -> [['user saw pricing page 3 times today']]
```

サーバー不要、スキーマ不要、組み込みモードでプロセス内で動作します。書き込みは非同期インデックスパイプラインを通ります（サーバーモードと同じアーキテクチャ）。即座にクエリする必要がある場合は `refresh_index()` を一度呼び出してインデックスの準備を確認してください。サーバーモードや分散モードへの切り替えは接続パラメータを 1 行変更するだけです。[Cloud でインストール不要のお試し](https://d0.seekdb.ai)もできます（登録不要、7 日間無料、curl 一発）。

---

## seekdb について

seekdb は完全にオープンソース（Apache 2.0）で、[OceanBase](https://jp.oceanbase.com/) チームが開発しています。すでに OceanBase をお使いかもしれません——Alipay、Taobao、DiDi、Xiaomi などの本番環境で稼働しています。seekdb は同じストレージエンジンと SQL エグゼキューターを継承し、Agent シナリオにおけるベクトル + リレーショナルのハイブリッド負荷に特化しています——オープンソース化以来 2,500 以上の GitHub star を獲得し、LangChain / LlamaIndex / Dify / Coze などの主要フレームワークとも統合済みです。

---

Agent 用のデータベースを選定中であれば——30 秒で上記のデモを実行してみてください。

**⭐ [github.com/oceanbase/seekdb](https://github.com/oceanbase/seekdb)** — star をいただけると、より多くの方にこのプロジェクトを知っていただけますし、私たちの開発へのモチベーションにもなります。

問題が発生した場合や Agent シナリオについて議論したい場合：[GitHub Issues](https://github.com/oceanbase/seekdb/issues) · [GitHub Discussions](https://github.com/oceanbase/seekdb/discussions)
