# Fix for oceanbase/seekdb#1538

Rework the arithmetic identity simplification in `ObTransformSimplifyExpr` so
that redundant `+ 0` / `- 0` / `* 1` predicates are eliminated even when the
plan cache auto-parameterizes the integer literals (question marks). The
equivalent filter now folds to the same expression as the original one, so it
gets the same range scan and estimated rows.

OB-P:eHAYenC-McAlwVjaYBCwlg
