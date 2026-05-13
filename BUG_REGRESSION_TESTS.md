# Bug regression tests (Copilot / Devin / HJB review items)

Run the server from the repo root (adjust config path if needed):

```bash
./webserv fahd.conf
```

Default base URL used below: `http://127.0.0.1:8080`. Export once:

```bash
export BASE=http://127.0.0.1:8080
```

---

## 1. Sensitive JSON / logs not under document root (static leak)

**Bug:** `sessions.json`, `users.json`, `toydb.json`, `user_log.txt` under `www/html/` were served as static files.

**Setup:** CGI must write to `www/data/` only (`cgi_data_store.py`). There must be **no** copies of these names under `www/html/` (delete legacy files if present).

| # | Test | Command | Expected |
|---|------|---------|----------|
| 1a | No `sessions.json` at site root | `curl -sS -o /dev/null -w '%{http_code}' "$BASE/sessions.json"` | `404` |
| 1b | No `users.json` at site root | `curl -sS -o /dev/null -w '%{http_code}' "$BASE/users.json"` | `404` |
| 1c | No `user_log.txt` at site root | `curl -sS -o /dev/null -w '%{http_code}' "$BASE/user_log.txt"` | `404` |
| 1d | No `toydb.json` at site root | `curl -sS -o /dev/null -w '%{http_code}' "$BASE/toydb.json"` | `404` |

**Pass:** All return `404` and response body must **not** contain session IDs, password hashes, or log lines.

**Fail:** Any `200` with JSON/plain body containing session or user data → vulnerability still present (usually a leftover file under `www/html/`).

---

## 2. Data store actually used by CGI (`www/data/`)

| # | Test | Command | Expected |
|---|------|---------|----------|
| 2a | Data dir exists | `test -d www/data && echo ok` | prints `ok` |
| 2b | After signup/login, files under data | `ls www/data/*.json 2>/dev/null \| wc -l` | ≥ 1 when flows ran (sessions/users may appear) |

**Pass:** New sessions/users appear only under `www/data/` when exercising signup/login, not under `www/html/`.

---

## 3. Atomic JSON + locking (concurrency / crash safety)

**Bug:** Plain `open(path,'w')` could corrupt JSON on concurrent writers or crash mid-write.

**Manual:** Hard to assert with a single `curl`. Quick stress (optional):

```bash
# From repo root; may create many temp sessions — use a throwaway account
for i in $(seq 1 20); do curl -sS -X POST -d "username=u$i&password=password123456" "$BASE/cgi-bin/signup.py" -o /dev/null & done; wait
python3 -c "import json; json.load(open('www/data/users.json'))" && echo "JSON parses OK"
```

**Expected:** `JSON parses OK` (no `JSONDecodeError`). If signup rejects duplicates, vary usernames or run fewer parallel signups.

---

## 4. `bad_request.py` — invalid CGI output

| # | Test | Command | Expected |
|---|------|---------|----------|
| 4a | Script output is not valid headers | `curl -sS -o /dev/null -w '%{http_code}' "$BASE/cgi-bin/bad_request.py"` | Not `200` with a normal HTML body from the script; typically **`400`**, **`502`**, or similar error** from the server (implementation-dependent). |

**Pass:** Client does **not** receive a successful 200 carrying the script’s fake “header” line as real HTTP headers.

---

## 5. `safe_strtol` lives with declaration (`httpConfig.cpp`)

**Bug:** Definition was in the wrong translation unit.

**Build test:**

```bash
make clean && make -j4
```

**Expected:** Clean link; no duplicate symbol errors for `safe_strtol`.

---

## 6. RFC 3875 — no `HTTP_CONTENT_TYPE` / `HTTP_CONTENT_LENGTH` duplicates

**Bug:** `CONTENT_TYPE` / `CONTENT_LENGTH` must not be duplicated as `HTTP_CONTENT_*` for the same request.

**Procedure (manual / one-off):** Temporarily add to a throwaway CGI under `cgi-bin` (or use logging if your tree logs env):

```python
import os
for k in sorted(os.environ):
    if "CONTENT" in k:
        print(k + "=" + repr(os.environ[k][:80]))
```

POST with a body and `Content-Type: text/plain`:

```bash
curl -sS -X POST -H 'Content-Type: text/plain' -d 'hi' "$BASE/cgi-bin/<your-probe>.py"
```

**Expected:** Output includes `CONTENT_TYPE` and `CONTENT_LENGTH` (for non-empty body). It must **not** include both `CONTENT_TYPE` and `HTTP_CONTENT_TYPE` with the same client value (and same idea for length when body present).

---

## 7. `check_session.py` — `expires_at` enforced

**Setup:** In `www/data/sessions.json`, set one session dict to include `"expires_at"` in the **past** (ISO format from `datetime.utcnow().isoformat()`), and keep a valid `session_id` cookie pointing at that sid (or call with cookie matching that sid).

| # | Test | Command | Expected |
|---|------|---------|----------|
| 7a | Expired session rejected | `curl -sS -b 'session_id=<expired_sid>' "$BASE/cgi-bin/check_session.py"` | Status line or body indicates **not** logged in (e.g. `401` / “Session expired” / “Not authenticated” per script). |

**Pass:** Expired entries are not treated as valid sessions (no username echo for expired dict).

---

## 8. `toydb.py` — session expiry on read path

Same idea: session with past `expires_at` in `www/data/sessions.json`, cookie for that sid.

| # | Test | Command | Expected |
|---|------|---------|----------|
| 8a | Toy DB rejects expired session | `curl -sS -b 'session_id=<expired_sid>' "$BASE/cgi-bin/toydb.py"` | `401` / “Not logged in” (not 200 with data). |

---

## 9. `login.py` — new sessions carry `expires_at`

After a normal login, inspect `www/data/sessions.json` (never commit secrets):

```bash
python3 -c "import json; d=json.load(open('www/data/sessions.json')); print(list(d.values())[-1])"
```

**Expected:** Latest session object is a `dict` containing `expires_at` (and `username`).

---

## 10. `conf/parsing.cpp` — no silent bump of tiny `client_max_body_size` in `validate_final_config`

**Note:** There may still be an **http**-block `< 20000` default elsewhere in `parsing.cpp`; this test targets “small explicit limits are not silently replaced inside `validate_final_config` for server/location.”

| # | Test | Command | Expected |
|---|------|---------|----------|
| 10a | Config with small limit loads | Use a test `.conf` with e.g. `client_max_body_size 1024;` at server and location, start `./webserv test.conf` | Server starts (or fails with an **explicit** parse error — not silently rewritten to 1MB without you knowing). |
| 10b | Upload over limit | `curl -sS -o /dev/null -w '%{http_code}' -X POST --data-binary @/dev/zero -H 'Transfer-Encoding: chunked' ...` (or fixed body > 1024 bytes to a POST location) | **`413`** when body exceeds configured limit. |

(Adjust URL to a `POST`-allowed upload route in your test config.)

---

## 11. Makefile hygiene

```bash
grep -n 'location_parser.cpp' Makefile | cat -A
```

**Expected:** Line ending has **no** trailing space after `conf/location_parser.cpp`.

---

## 12. `CgiHandler.hpp` — no dead private CGI helpers

```bash
grep -E '_trim|_toLower|_toStrInt|_toStrSize' cgi/CgiHandler.hpp || true
```

**Expected:** No matches (declarations removed).

---

## 13. `src/EventLoop.cpp` — teammate-owned (BUG-C7 + status code)

Document only; **do not fail your PR** if these still match “before teammate fix.”

| # | Test | Command | Expected **after teammate fix** |
|---|------|---------|----------------------------------|
| 13a | CGI stdout buffer respects **location** `client_max_body_size` | `curl -sS -o /dev/null -w '%{http_code}' "$BASE/cgi-bin/big.py"` (script outputs > server default but &lt; location limit if config is 30M vs 10M server) | **`200`** when output fits **location** limit. |
| 13b | Oversized **CGI output** | Same `big.py` when output exceeds buffer | **`502`** Bad Gateway (not **413**). |

**Current / interim:** May return **`413`** on buffer overflow and still use `conn->writeBuffer().maxSize()` for `CgiJob` — track in `bugs.md` under teammate.

---

## Quick checklist (copy-paste)

```bash
export BASE=http://127.0.0.1:8080
curl -sS -o /dev/null -w 'sessions.json %{http_code}\n' "$BASE/sessions.json"
curl -sS -o /dev/null -w 'users.json    %{http_code}\n' "$BASE/users.json"
curl -sS -o /dev/null -w 'user_log.txt  %{http_code}\n' "$BASE/user_log.txt"
curl -sS -o /dev/null -w 'toydb.json    %{http_code}\n' "$BASE/toydb.json"
curl -sS -o /dev/null -w 'bad_request   %{http_code}\n' "$BASE/cgi-bin/bad_request.py"
make -j4
grep -E '_trim|_toLower' cgi/CgiHandler.hpp && echo FAIL || echo CgiHandler.hpp OK
```

All static leak lines should print **`404`** (once legacy `www/html/` copies are removed).
