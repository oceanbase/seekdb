"""Public result assertions, matching the judge semantics."""
import math

def same(actual, expected, abs_tol=1e-5, rel_tol=1e-5):
    if isinstance(expected, float):
        return (type(actual) in (int, float) and math.isfinite(actual)
                and math.isclose(actual, expected, abs_tol=abs_tol, rel_tol=rel_tol))
    if isinstance(expected, list):
        return (isinstance(actual, list) and len(actual) == len(expected)
                and all(same(a, b, abs_tol, rel_tol) for a, b in zip(actual, expected)))
    return type(actual) is type(expected) and actual == expected


def assert_response(response, expected, config):
    if "error" in expected:
        assert response.get("ok") is False, "statement should fail"
        code = response.get("error", {}).get("code")
        assert isinstance(code, str) and code, "missing error.code"
        required = config.get("error_codes", {}).get(expected["error"])
        if required is not None:
            assert code == required, f"error.code {code!r} != {required!r}"
        return
    assert response.get("ok") is True, f"statement failed: {response}"
    for key in ("affected_rows", "columns"):
        if key in expected:
            assert same(response.get(key), expected[key]), f"{key}: {response.get(key)!r} != {expected[key]!r}"
    if "rows" not in expected:
        return
    actual, wanted = response.get("rows"), expected["rows"]
    assert isinstance(actual, list), "missing rows"
    # Unordered queries use multiset comparison; duplicate rows must not disappear.
    if expected.get("unordered"):
        remaining = list(actual)
        for row in wanted:
            index = next((i for i, candidate in enumerate(remaining)
                          if same(candidate, row, config["abs_tol"], config["rel_tol"])), None)
            assert index is not None, f"missing row: {row!r}"
            remaining.pop(index)
        assert not remaining, f"unexpected rows: {remaining[:5]!r}"
    else:
        assert same(actual, wanted, config["abs_tol"], config["rel_tol"]), f"rows: {actual!r} != {wanted!r}"

