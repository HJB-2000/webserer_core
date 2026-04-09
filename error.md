# Error Log

Bugs found during Phase 1 integration review (2026-04-04).
Separated by owner.  Each entry has: file, line, symptom, fix.

---

## Teammate — conf/ (ServerConfig / CGI)

These must be fixed in the `conf/` files before `main.cpp` can be wired
to use the real config parser.

---

### BUG-T1 — Redirect location never fires (wrong default config)

**File:** [conf/locationConfig.cpp](conf/locationConfig.cpp#L278)
**Lines:** 278–283 (`set_default_conf(int num)`, branch `num == 2`)

**Symptom:**
The `/old-page` location is supposed to redirect to `/new-page` with 301.
`_return_code` and `_return_value` are set correctly, but `_redirect_enabled`
is never set to `true`.  `getRedirectEnabled()` returns `false`, so
`ResponseHandler` skips the redirect branch entirely — the location serves
as a regular static-file location instead.

**Root cause:**
`set_default_conf` manually assigns `_return_code` / `_return_value` but
does not call `setReturnRedirection()`, which is the only place that sets
`_redirect_enabled = true`.

**Fix (teammate):**
In `set_default_conf(2)`, add after `_return_value = "/new-page";`:
```cpp
_redirect_enabled = true;
```

---

### BUG-T2 — Semicolons embedded in default error-page paths

**Files:**
- [conf/serverConfig.cpp](conf/serverConfig.cpp#L258) — `Server::set_default_conf()`
- [conf/locationConfig.cpp](conf/locationConfig.cpp#L243) — all four `set_default_conf` branches

**Lines:** serverConfig.cpp:258–259 ; locationConfig.cpp:243–244, 263–264, 282–283, 302–303

**Symptom:**
Default error-page paths are stored as `"./errors/400.html;"` (with a
trailing semicolon).  When `ResponseHandler::_loadErrorPage()` tries to
`open("./errors/400.html;", O_RDONLY)`, the syscall fails because `;` is
part of the filename string.  The server silently falls back to the built-in
HTML — custom error pages never load.

**Root cause:**
The semicolons from the config-file syntax were accidentally included in the
string literal (copy-paste from a `.conf` line).

**Fix (teammate):**
Remove the trailing `;` from every error-page path literal in
`set_default_conf`. Example:
```cpp
// wrong
_error_page[400] = "./errors/400.html;";
// correct
_error_page[400] = "./errors/400.html";
```

---

### BUG-T3 — `using namespace std` in headers (style / safety)

**Files:**
- [conf/serverConfig.hpp](conf/serverConfig.hpp#L11)
- [conf/locationConfig.hpp](conf/locationConfig.hpp#L11)
- [conf/httpConfig.hpp](conf/httpConfig.hpp#L10)
- [conf/eventsConfig.hpp](conf/eventsConfig.hpp#L5)
- [conf/LexerConfig.hpp](conf/LexerConfig.hpp#L6)

**Symptom:**
Any translation unit that includes one of these headers silently imports the
entire `std` namespace into the global scope.  This can shadow identifiers,
cause subtle name-resolution bugs, and breaks strict coding conventions.

**Fix (teammate):**
Remove `using namespace std;` from all headers.
Replace unqualified `string`, `vector`, `map` uses in the headers with
`std::string`, `std::vector`, `std::map`.

---

### BUG-T4 — `I_Want()` and `check_dup_path_locations()` declared but not defined

**Files:**
- [conf/parserConf.hpp](conf/parserConf.hpp#L35) — `void I_Want();`
- [conf/serverConfig.hpp](conf/serverConfig.hpp#L45) — `void check_dup_path_locations();`

**Symptom:**
Both functions are declared in the class but have no implementation in any
`.cpp` file.  Currently harmless (nobody calls them), but any future call
will produce a linker error.

**Fix (teammate):**
Either implement both functions, or remove their declarations.

---

## Me — src/ (core + response)

---

### BUG-M1 — `main.cpp` still uses hardcoded TMP_HOST / TMP_PORT stub

**File:** [src/main.cpp](src/main.cpp#L123)
**Lines:** 123–145

**Symptom:**
The server always binds to `0.0.0.0:8080` regardless of what the config
file says.  The real `ParserConf` / `Server` classes are now compiled in but
not yet called.  Running `./webserv myconfig.conf` silently ignores the file.

**Blocked by:** BUG-T1 and BUG-T2 in teammate's code must be fixed first,
so that `set_default_conf` (the no-argv fallback path) is reliable.

**Fix (me) — after teammate fixes their bugs:**
Replace the stub block in `main()` with:
```cpp
// parse config
// tokenize file → call ParserConf → get vector<Server>
// for each server → make_listener() → loop.addServerSocket()
```
Full wiring spec is in the Phase 5 section of `change_tracker.md`.

---

### BUG-M2 — HttpParser still uses TMP_CLIENT_MAX_BODY_SIZE macro

**File:** [src/HttpParser.cpp](src/HttpParser.cpp)
**Symptom:**
Body-size limit is fixed at 1 MB compile-time regardless of the per-server
`client_max_body_size` directive parsed from the config.

**Fix (me):**
Pass `conn->config()->getMaxBody()` into `HttpParser::feed()` or read it
from the connection inside `_parseBody()` instead of using the macro.

---
