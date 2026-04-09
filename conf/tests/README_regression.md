# Parser regression suite

This suite is designed to catch suspicious parser behavior quickly and point to likely bug areas.

## What it does

- Runs a set of **valid** configs that must parse successfully.
- Runs a set of **invalid** configs that must fail with expected parse errors.
- If a test behaves unexpectedly, it prints a **"suspicious area"** hint (where in your code to investigate first).

## Files

- `conf/tests/regression_manifest.txt` — test matrix (`id|config|expected_exit|expected_regex|bug_area|notes`)
- `conf/tests/run_parser_regression.sh` — automated runner
- `conf/confs/regression/` — config corpus

## Run

From repository root:

```bash
bash conf/tests/run_parser_regression.sh
```

## Fuzz crash-hunting (random malformed configs)

Use the lightweight fuzz runner to generate many random malformed configs and
feed them to the parser under ASan-enabled build.

```bash
bash conf/tests/run_parser_fuzz.sh
```

Optional parameters:

```bash
# 120 cases, fixed seed for reproducibility
bash conf/tests/run_parser_fuzz.sh 120 1337
```

### Fuzz output interpretation

- `ok parses`: random case happened to be valid and parsed.
- `parse errors`: parser rejected malformed input safely (expected behavior).
- `crashes`: ASan/segfault signatures detected (investigate immediately).
- `other statuses`: unusual exits that may indicate non-standard failure paths.

## Notes

- Valid test failing usually means directive storage/parsing regression.
- Invalid test passing usually means a missing validator or duplicate-detection break.
- The output includes tail logs from each failed case to speed up debugging.
