# CGI session and demo data

## Data directory

JSON files and logs for the login/signup/toydb demos live under `www/data/`, **outside** the HTML document root (`www/html/`). That way they are not served as static files.

If you still have old copies under `www/html/` (`users.json`, `sessions.json`, etc.), copy them into `www/data/` once if you need the data, then **delete the originals under `www/html/`**. Leaving them in the document root allows anyone to `GET /sessions.json` and steal sessions.

Manual regression checks: see `BUG_REGRESSION_TESTS.md` in the repository root.

Paths are defined in `cgi_data_store.py` in this folder (`www/html/cgi-bin/`).

## Persistence

- `save_json_atomic()` writes via a temporary file and `os.replace()` under an `flock(2)` lock, so concurrent CGI processes are less likely to corrupt JSON.
- `load_json()` takes a shared lock while reading when the file exists.

## Session format

Sessions in `sessions.json` may be a legacy plain string (username) or a dict with:

- `username` — logged-in user
- `created_at` — ISO timestamp
- `expires_at` — optional ISO time; `login.py` sets this (7 days). Scripts that treat a user as logged in should treat an expired `expires_at` as logged out.

## Related server fixes

- CGI environment variables omit `HTTP_CONTENT_TYPE` / `HTTP_CONTENT_LENGTH` when the dedicated `CONTENT_TYPE` / `CONTENT_LENGTH` variables are used (RFC 3875).
- CGI stdout is capped using the **location** `client_max_body_size`; if that limit is exceeded the server responds with **502** (bad gateway), not 413.
