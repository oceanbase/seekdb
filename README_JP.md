<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://mdn.alipayobjects.com/huamei_ytl0i7/afts/img/A*pKqtRILxGioAAAAAQLAAAAgAejCYAQ/original" width="420">
  <source media="(prefers-color-scheme: light)" srcset="https://mdn.alipayobjects.com/huamei_ytl0i7/afts/img/A*6BO4Q6D78GQAAAAAQFAAAAgAejCYAQ/original" width="420">
  <img alt="seekdb logo" src="images/logo.svg" width="420">
</picture>

# **Write · Search · Fork — AI エージェントのためのステートストア**

<p>
    <a href="https://github.com/oceanbase/seekdb/stargazers">
        <img alt="GitHub Stars" src="https://img.shields.io/github/stars/oceanbase/seekdb?style=flat-square&logo=github&color=yellow" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/releases">
        <img alt="Latest Release" src="https://img.shields.io/github/v/release/oceanbase/seekdb?style=flat-square&color=blue" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/commits">
        <img alt="Commit Activity" src="https://img.shields.io/github/commit-activity/m/oceanbase/seekdb?style=flat-square&color=green" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/graphs/contributors">
        <img alt="Contributors" src="https://img.shields.io/github/contributors/oceanbase/seekdb?style=flat-square&color=orange" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/issues">
        <img alt="Issues" src="https://img.shields.io/github/issues/oceanbase/seekdb?style=flat-square" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/blob/HEAD/LICENSE">
        <img alt="License" src="https://img.shields.io/badge/License-Apache_2.0-blue.svg?style=flat-square" />
    </a>
    <a href="https://pepy.tech/projects/pyseekdb">
        <img alt="Downloads" src="https://static.pepy.tech/badge/pyseekdb" />
    </a>
    <a href="https://discord.gg/74cF8vbNEs">
        <img alt="Join Discord" src="https://img.shields.io/badge/Discord-Join%20Chat-5865F2?logo=discord&style=flat-square" />
    </a>
    <a href="https://docs.seekdb.ai/">
        <img alt="Documentation" src="https://img.shields.io/badge/Docs-seekdb.ai-4285F4?style=flat-square&logo=read-the-docs&logoColor=white" />
    </a>
    <a href="https://deepwiki.com/oceanbase/seekdb">
        <img alt="Ask DeepWiki" src="https://deepwiki.com/badge.svg" />
    </a>
    <a href="https://www.linkedin.com/company/oceanbase" target="_blank" rel="noopener noreferrer">
        <img src="https://custom-icon-badges.demolab.com/badge/LinkedIn-0A66C2?logo=linkedin-white&logoColor=fff" alt="follow on LinkedIn">
    </a>
    <a href="https://www.youtube.com/@OceanBaseDB">
        <img alt="YouTube" src="https://img.shields.io/badge/YouTube-red?logo=youtube">
    </a>
</p>

MySQL 互換 · 組み込み / サーバー両対応 · ベクトル + 全文ハイブリッド検索 · COW サンドボックス

⚡ ストリーミング書き込み + 検索で 1,523 QPS（10× Milvus、3× Elasticsearch）<br>
🌿 FORK/MERGE サンドボックスでエージェントが安全に探索・自由にロールバック<br>
🔍 ベクトル + 全文 + スカラーを 1 本の SQL で完結<br>
🐬 完全な ACID、MySQL プロトコル、LangChain / LlamaIndex / Dify にネイティブ対応

[English](README.md) | [中文版](README_CN.md) | **日本語**

[30 秒で試す](#30-秒で試す) · [クイックスタート](#クイックスタート) · [なぜ seekdb か](#なぜ-ai-エージェントに-seekdb-なのか) · [エコシステム](#エコシステムと統合) · [開発](#開発)

<sub>seekdb が役立つと思ったら、<a href="https://github.com/oceanbase/seekdb/stargazers">star</a> をお願いします — より多くの人にプロジェクトを届けられます。</sub>

---

</div>

## ⚡ パフォーマンス概要

<div align="center">
  <img src="images/benchmark.svg" alt="seekdb ベンチマーク：Milvus の 10.7 倍、Elasticsearch の 3.2 倍の QPS" width="720" />
</div>

> 📖 [ローンチブログを読む →](docs/blog/launch_blog_jp.md) · 🔁 [ベンチマークを再現する →](https://github.com/oceanbase/vdb-streambench)

---

<a id="30-秒で試す"></a>

## ⏱️ 30 秒で試す

<div align="center">
  <img src="images/demo.gif" alt="seekdb 30 秒デモ" width="720" />
</div>

```bash
pip install -U pyseekdb   # pyseekdb は seekdb の Python SDK です
```

> 📋 [demo.py ソースを見る →](images/demo.py)

サーバー不要、スキーマ定義不要、エンベディング設定不要。組み込みモードならプロセス内でそのまま動作し、たった 1 行でサーバーモードや OceanBase 分散モードに切り替えられます。[その他の例 →](#その他の例)

---

<a id="なぜ-ai-エージェントに-seekdb-なのか"></a>

## ✨ なぜ AI エージェントに seekdb なのか？

### 🔥 ストリーミング書き込み + 並行検索、P99 スパイクなし

エージェントのワークロードは「絶え間ない書き込み + ミリ秒単位の読み取り」が本質です。seekdb の**非同期インデックスパイプライン（Change Stream）**が DML とインデックス構築を分離し、**2 レベル HNSW**（インクリメンタル + スナップショット）によって、書き込み直後のベクトルが即座に検索可能になります。

<div align="center">
  <img src="images/architecture.svg" alt="seekdb 非同期インデックスパイプラインアーキテクチャ" width="720" />
</div>

書き込みパスはコミット後、インデックス構築を*待たずに*即座にリターンします。Change Stream パイプラインが redo ログを非同期で消費し、デルタ HNSW を更新。クエリはデルタインデックスとスナップショットインデックスの両方にきめ細かい読み取りロックでアクセスします — **これが高並行性でも P99 が安定し続ける理由です。**

> **実測結果：1,523 QPS、並行 P99 はわずか 21.7 ms — Milvus の 10.7 倍の QPS を達成。並行数上昇時の P99 ジッターは 1.1 倍に留まります（同一ワークロードで ES / Milvus は約 10 倍）。**

<sub>ソース: [`src/observer/change_stream/`](src/observer/change_stream/) · [`src/observer/vector_index/`](src/observer/vector_index/)</sub>

### 🌿 コピーオンライトサンドボックスでエージェントが自由に探索

`FORK DATABASE` でデータベース全体を数秒でスナップショット — データコピーはゼロ。エージェントはサンドボックス内で自由に実験（書き込み、クエリ、テーブル破壊すら可能）し、`MERGE TABLE` で成果をメインラインに反映するか、`DROP DATABASE` で丸ごと破棄。アプリケーション層の保存/復元ではなく、カーネルレベルの COW で実現しています。

```sql
-- 数秒でスナップショット、データコピーなし
FORK DATABASE agent_state TO agent_sandbox_42;

-- エージェントがサンドボックスで自由に読み書き...
USE agent_sandbox_42;
INSERT INTO memory (session_id, embedding, content) VALUES (...);

-- 成果をメインラインに反映（戦略：FAIL / THEIRS / OURS）
MERGE TABLE agent_sandbox_42.memory INTO agent_state.memory STRATEGY THEIRS;
-- ...または破棄：
DROP DATABASE agent_sandbox_42;
```

<sub>ソース: [`tools/deploy/mysql_test/test_suite/fork_table/`](tools/deploy/mysql_test/test_suite/fork_table/)</sub>

### 🔍 1 つの SQL でハイブリッド検索

ベクトル + 全文 + スカラーフィルターを単一の実行プランに統合。クライアント側での N+1 マージも、結果を貼り合わせるグルーコードも一切不要です。

```sql
SELECT id, title, l2_distance(emb, '[0.12,0.34,...]') AS dist
FROM docs
WHERE MATCH(content) AGAINST('quarterly report')
  AND author_id = 42
  AND created_at > '2026-01-01'
ORDER BY dist APPROXIMATE LIMIT 10;
```

### 🐬 MySQL 互換、ACID、組み込み可能

実績ある OceanBase SQL エンジン上に構築。組み込みライブラリ、単一ノードサーバー、または OceanBase 分散クラスターとして動作します。完全な ACID、リアルタイム書き込み、MySQL エコシステムをそのまま活用できます。

---

<a id="クイックスタート"></a>

## 🎬 クイックスタート

### インストール

プラットフォームを選択：

<details open>
<summary><b>☁️ クラウド（インストール不要）</b></summary>

curl 一発で稼働中のデータベースが手に入ります — サインアップ不要、クレジットカード不要。

```bash
curl -X POST https://d0.seekdb.ai/api/v1/instances
```

7 日間無料。[詳細はこちら →](https://d0.seekdb.ai)

</details>

<details open>
<summary><b>🐍 Python（AI/ML におすすめ）</b></summary>

```bash
pip install -U pyseekdb
```

</details>

<details>
<summary><b>🐳 Docker（簡易テスト）</b></summary>

```bash
docker run -d \
  --name seekdb \
  -p 2881:2881 \
  -p 2886:2886 \
  -v ./data:/var/lib/oceanbase \
  oceanbase/seekdb:latest
```
この Docker イメージの詳細は[ドキュメント](https://github.com/oceanbase/docker-images/blob/main/seekdb/README.md)を参照してください。

</details>

<details>
<summary><b>📦 バイナリ（スタンドアロン）</b></summary>

```bash
# Linux（ワンライナーインストール、sudo が必要な場合あり）
curl -fsSL https://obportal.s3.ap-southeast-1.amazonaws.com/download-center/opensource/seekdb/seekdb_install.sh | bash

# macOS（Homebrew）
brew tap oceanbase/seekdb
brew install seekdb
```

DEB/RPM オフラインインストールと設定の詳細は[デプロイメントドキュメント](https://docs.seekdb.ai/seekdb/deploy-by-systemd/)を参照してください。

</details>

<a id="その他の例"></a>

### 📝 その他の例

完全な Python SDK ガイド — 接続モード、エンベディング関数、メタデータフィルター — は [pyseekdb ユーザーガイド](https://github.com/oceanbase/pyseekdb)を参照してください。

<details open>
<summary><b>🤖 エージェントメモリパターン（継続的書込 + 即時検索）</b></summary>

エージェントの典型的なループ：観察を書き込み、ミリ秒後に関連コンテキストを検索、これを繰り返します。seekdb の非同期インデックスパイプラインが、持続的な並行負荷の下でも読み書き双方の高速性を維持します。

```python
import pyseekdb

client = pyseekdb.Client(path="./agent_state.db")
memory = client.get_or_create_collection(name="episodic")

for step in agent.run():
    # 観察結果を永続化
    memory.upsert(ids=[step.id], documents=[step.observation])

    # 関連コンテキストを検索 — 書き込みの数ミリ秒後に、
    # インクリメンタル HNSW が即座に応答（バックグラウンドリビルド待ち不要）
    relevant = memory.query(query_texts=step.next_query, n_results=5)

    agent.act(relevant)
```

</details>

<details>
<summary><b>🔍 組み込みデータベースの確認（SQL CLI）</b></summary>
pyseekdb アプリケーションの実行中に、同じデータベースディレクトリへ
SQL CLI を接続してデータを確認できます：

```bash
python3 tools/seekdb-cli --data-dir ./agent_state.db

seekdb> SHOW TABLES;
seekdb> SELECT * FROM episodic LIMIT 10;
```

ワンショット実行とバッチ出力も利用できます：

```bash
# 単一ステートメントを実行して終了
python3 tools/seekdb-cli -d ./agent_state.db -e "SELECT count(*) FROM episodic;"

# タブ区切り出力
python3 tools/seekdb-cli -d ./agent_state.db --batch -e "SHOW TABLES;"
```

サーバーモードでは TCP で接続します：

```bash
python3 tools/seekdb-cli -h 127.0.0.1 -P 2881
```

MySQL プロトコル対応のクライアントは、同じローカルソケットに接続
できます。たとえば公式の `mysql` CLI は次のように使います：

```bash
mysql -S agent_state.db/run/sql.sock -u root
```

この CLI は Python 標準ライブラリのみを使用します（pymysql などの
クライアント依存なし）。組み込みデータベースのローカルソケット
（`<data-dir>/run/sql.sock`）経由で MySQL ワイヤプロトコルを直接話し、
SQL を実行します。パスワードはデフォルトで `$SEEKDB_PASSWORD` を
参照し、未設定なら空文字列になります（pyseekdb の組み込みモードと同じ）。
ローカルソケットは Linux と macOS でサポートされます。Windows の
組み込みモードは名前付きパイプ（`run/sql.pipe`）を使用しており、
現時点では未対応です。

</details>

<details>
<summary><b>🗄️ SQL — スキーマ + ハイブリッド検索</b></summary>

```sql
-- ベクトル列、全文インデックス、HNSW ベクトルインデックスを持つテーブル
CREATE TABLE articles (
  id        INT PRIMARY KEY,
  title     TEXT,
  content   TEXT,
  embedding VECTOR(384),
  FULLTEXT INDEX idx_fts (content) WITH PARSER ik,
  VECTOR   INDEX idx_vec (embedding) WITH (DISTANCE=l2, TYPE=hnsw, LIB=vsag)
) ORGANIZATION = HEAP;

-- ハイブリッド検索：ベクトル類似度 + 全文マッチを 1 つのクエリで
SELECT id, title,
       l2_distance(embedding, '[0.12, 0.34, ...]') AS dist
FROM articles
WHERE MATCH(content) AGAINST('quarterly report')
ORDER BY dist APPROXIMATE
LIMIT 10;
```

Python 開発者は SQLAlchemy や任意の MySQL ドライバーでアクセスできます。

</details>


## 📚 ユースケース

<details open>
<summary><b>🎯 エージェント AI — メモリ、サンドボックス、ステート</b></summary>

エージェントには、絶え間ないメモリ書き込み、ミリ秒レベルの検索、探索のためのブランチング、問題発生時の即時ロールバックに対応できるステートストアが必要です。seekdb はまさにそのために作られました：

- **ストリーミング対応** — 書き込んだ瞬間から検索可能
- **COW サンドボックス** — `FORK DATABASE` で安全に実験、`MERGE` で採用、`DROP` で即ロールバック
- **ハイブリッド検索** — ベクトル + 全文 + リレーショナルを 1 本の SQL で
- **MySQL プロトコル** — LangChain、LlamaIndex、Dify がそのまま動作

パーソナルアシスタント · エンタープライズ自動化 · バーティカルエージェント · エージェントプラットフォーム

</details>

<details>
<summary><b>🧩 その他のユースケース</b></summary>

seekdb のハイブリッド検索 + マルチモデルエンジンは、従来型の AI ワークロードにも最適です：

- **📖 RAG とナレッジ検索** — ベクトル + 全文 + スカラーフィルター、多段階アクセス制御対応。*エンタープライズ QA、カスタマーサポート、業界インサイト、個人ナレッジベース。*
- **🔍 セマンティック検索** — エンベディングベースのテキスト、画像、マルチモーダル検索。*商品検索、テキストから画像、画像から商品。*
- **💻 AI 支援コーディング** — セマンティックコード検索、マルチプロジェクト分離、タイムトラベルクエリ。IDE プラグインやコードエージェント向け。*ローカル IDE、Web IDE、デザインからコードへ。*
- **⬆️ エンタープライズアプリケーションインテリジェンス** — レガシーシステム向けの MySQL 互換 AI レイヤー、行列ハイブリッドストレージ対応。*ドキュメントインテリジェンス、ビジネスインサイト、金融システム。*
- **📱 オンデバイス＆エッジ AI** — リソース制約のあるデバイス向けの組み込み/マイクロサーバーモード。*車載システム、AI 教育、コンパニオンロボット、ヘルスケアデバイス。*

</details>

---

<a id="エコシステムと統合"></a>

## 🌟 エコシステムと統合

<div align="center">

<p>
    <a href="https://github.com/langchain-ai/langchain/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/LangChain-✅-00A67E?style=flat-square&logo=langchain" alt="LangChain" />
    </a>
    <a href="https://github.com/run-llama/llama_index/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/LlamaIndex-✅-00A67E?style=flat-square&logo=llama" alt="LlamaIndex" />
    </a>
    <a href="https://github.com/langgenius/dify/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/Dify-✅-00A67E?style=flat-square&logo=dify" alt="Dify" />
    </a>
    <a href="https://github.com/langchain-ai/langchain/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/LangGraph-✅-00A67E?style=flat-square&logo=langgraph" alt="LangGraph" />
    </a>
    <a href="https://github.com/coze-dev/coze-studio/pulls?q=is%3Apr+oceanbase+is%3Aclosed">
        <img src="https://img.shields.io/badge/Coze-✅-00A67E?style=flat-square&logo=coze" alt="Coze" />
    </a>
    <a href="https://huggingface.co">
        <img src="https://img.shields.io/badge/HuggingFace-✅-00A67E?style=flat-square&logo=huggingface" alt="HuggingFace" />
    </a>
</p>

<sub>+ Camel-AI · DB-GPT · FastGPT · Firecrawl · Spring-AI-Alibaba · Cloudflare Workers AI · Jina AI · Ragas · Instructor · Baseten — 完全なリストは[ユーザーガイド](https://docs.seekdb.ai/seekdb/seekdb-overview)を参照してください。</sub>

</div>

---

## 🌐 次のステップとコミュニティ

- 📖 **[ドキュメントを読む →](https://docs.seekdb.ai/)** — クイックスタート、API リファレンス、統合ガイド
- 📝 **[ローンチブログ →](docs/blog/launch_blog_jp.md)** — Milvus の 10.7 倍の QPS を実現したアーキテクチャ
- 🐛 **[Issue を開く →](https://github.com/oceanbase/seekdb/issues)** — バグ報告、機能リクエスト
- 🤝 **[コントリビュート →](CONTRIBUTING.md)** — エージェント時代のステートストアを一緒に作ろう

<div align="center">

<p>
    <a href="https://discord.gg/74cF8vbNEs">
        <img src="https://img.shields.io/badge/Discord-Join%20Chat-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/discussions">
        <img src="https://img.shields.io/badge/GitHub%20Discussion-181717?style=for-the-badge&logo=github&logoColor=white" alt="GitHub Discussion" />
    </a>
    <a href="https://ask.oceanbase.com/">
        <img src="https://img.shields.io/badge/Forum-中国語コミュニティ-FF6900?style=for-the-badge" alt="Forum" />
    </a>
</p>

</div>

---

<a id="開発"></a>

## 🛠️ 開発

### ソースからビルド

ビルド前に、お使いの OS に必要なツールチェーンと依存関係をインストールしてください。詳細は[ツールチェーンのインストール](docs/developer-guide/en/toolchain.md)を参照してください。

```bash
# リポジトリをクローン
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
./build.sh release --init --make
mkdir -p ~/seekdb/bin
cp build_release/src/observer/seekdb ~/seekdb/bin
cd ~/seekdb
./bin/seekdb
```

この例では作業ディレクトリとして $HOME/seekdb を使用しています。テストには新しい空のディレクトリを使用してください。詳細は[開発者ガイド](docs/developer-guide/en/README.md)を参照してください。

### コントリビュート

コントリビューション大歓迎です！ まずは[コントリビューションガイド](CONTRIBUTING.md)をご覧ください。

<a href="https://github.com/oceanbase/seekdb/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=oceanbase/seekdb&max=400" alt="Contributors" />
</a>

---

## 📈 Star の推移

<a href="https://star-history.com/#oceanbase/seekdb&Date">
  <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=oceanbase/seekdb&type=Date" width="720" />
</a>

seekdb が役立つなら、**star で他の人にも届けましょう。** ⭐

---

## 📄 ライセンス

seekdb は [OceanBase](https://en.oceanbase.com/) チームが開発 — Alipay、Taobao、DiDi、Xiaomi などの本番環境で稼働しているデータベースエンジンと同一基盤です。[Apache License, Version 2.0](LICENSE) に基づく完全なオープンソースプロジェクトです。
