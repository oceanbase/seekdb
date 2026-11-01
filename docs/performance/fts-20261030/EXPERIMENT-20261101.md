# Unpublished tokenizer experiment

Control: submitted commit `6aeef682f433e93baaee21986e06525d82442f7a` (revision21). The user reports 27.5965/50 for that submission. This experiment is **not a replacement submission and does not establish full marks**.

Changes are confined to `ob_fts_stop_word.cpp`: use `set_or_update` for one word-map lookup on insertion and repetition, preserving the initial count and last equivalent spelling; use an immutable, fixed-size ASCII-letter stopword table generated from the existing list, with original charset-aware GENERAL_CI lookup for every unsupported input. No input-text, token-result or query-result cache is added.

Both observer builds passed. Ten mysqltest suites passed, including the new stopword regression whose expected output was recorded with the unchanged revision21 executable. Eight concurrent tokenizer clients, 100 same-index relevance scores, the score-free limited-count plan, and the four counts 8001/11000/7332/20 all passed. The normalized SHA256 of the modified compiled source is `a991af89a8e8c29e1e39f3acf3c894a3ceb8f5182720cd9976a88e654b7d7e44`, matching the Windows file. Existing parser and index semantics were retained. The local server rejects latin1/gbk charset names, so no coverage of those charsets is claimed.

## Measurements and decision

ABBA SQL BENCHMARK diagnostic calls (100000 calls/sample, three samples/pass) showed about 11% IK and 14% English reductions for revision22's one-lookup change alone. Revision23, including the ASCII table, showed about 4.6% IK and 29.6% English reductions versus revision21. Raw measurements are in `revision22-micro.json` and `revision23-micro.json`; these measure internal calls, not the official SQL-loop metric.

Two complete benchmark runs per binary, in ABBA order with the original 20000-row/500-batch/3000-tokenize/200-query/3-sample/30-warmup/SKIP_LOAD=0 settings, did **not** show an overall improvement. Averaging each metric first gives build -5.39%, tokenization -2.45%, query -9.78%, and equal-category mean -5.87% relative reduction. SELECT1 was also 24.19% slower during candidate runs, although this change does not modify that path. This is evidence of substantial measurement confounding; it is not a reason to discard the negative full measurements or substitute microbenchmark gains for a contest score. All raw reports and the calculation are archived here under the revision23 names.

At the time of this experiment, the candidate was kept local and the submission branch stayed at 6aeef. It is superseded by the later validated follow-up documented in LATEST.md; the negative results here remain part of the record.

## New profile for follow-up

VM files `revision23-query.data`, `revision23-query-leaf.txt` and `revision23-query-inclusive.txt` capture the real English count query. Startup/replay work is substantial: transaction log replay contributes about 21.7% inclusive of sampled cycles, while the foreground worker contributes about 43.4%. This profile cannot be read as foreground-only percentages. The foreground still spends time in string-diff decoding, posting iteration, merging and result projection. Before more tuning, isolate the foreground worker or wait for replay to settle in diagnostic measurements; retain the unchanged official benchmark for final validation. Do not claim that VM replay overhead explains the platform's 27-point score.
