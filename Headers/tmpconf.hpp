// ============================================================
//  tmpconf.hpp
//  Temporary configuration macros.
//
//  PURPOSE
//  ───────
//  ServerConfig.hpp is owned by a teammate (Phase 1).
//  Until that file arrives every hardcoded config value lives
//  here as a macro so the rest of the implementation can proceed.
//
//  REPLACEMENT RULE (per macro group below)
//  ─────────────────────────────────────────
//  When ServerConfig is integrated, replace each macro usage
//  with the corresponding field access shown in the comment.
//  Then remove the macro (and eventually this whole file).
//
//  DO NOT add logic here — values only.
// ============================================================
#ifndef TMPCONF_HPP
#define TMPCONF_HPP

// ── Server socket ────────────────────────────────────────────
// Replace with: config.host   config.port
#define TMP_HOST            "0.0.0.0"
#define TMP_PORT            8080

// ── Connection limits ────────────────────────────────────────
// Replace with: config->client_max_body_size
#define TMP_CLIENT_MAX_BODY_SIZE    (1 * 1024 * 1024)   // 1 MB

// Replace with: config->timeout_seconds
#define TMP_TIMEOUT_SECONDS         60

// ── HttpParser hard limits (Plan.md §3) ──────────────────────
// These come from the HTTP spec — not from ServerConfig.
// They stay as macros even after ServerConfig integration.
#define TMP_MAX_URI_LENGTH          8192    // → 414 if exceeded
#define TMP_MAX_HEADER_LINE         8192    // → 431 if exceeded
#define TMP_MAX_HEADER_COUNT        100     // → 431 if exceeded

// ── Static file serving ──────────────────────────────────────
// Replace with: config->root   config->index
#define TMP_ROOT                    "/var/www/html"
#define TMP_INDEX                   "index.html"

// Replace with: location->autoindex
#define TMP_AUTOINDEX               0       // 0 = off → 403 on missing index

// ── Error pages ──────────────────────────────────────────────
// Replace with: config->error_pages[code]
// Empty string means use the built-in hardcoded HTML fallback.
#define TMP_ERROR_PAGE_400          ""
#define TMP_ERROR_PAGE_403          ""
#define TMP_ERROR_PAGE_404          ""
#define TMP_ERROR_PAGE_405          ""
#define TMP_ERROR_PAGE_413          ""
#define TMP_ERROR_PAGE_500          ""
#define TMP_ERROR_PAGE_504          ""

// ── Allowed HTTP methods ─────────────────────────────────────
// Replace with: location->allowed_methods  (std::vector<std::string>)
// Stub: all three methods allowed everywhere.
#define TMP_ALLOW_GET               1
#define TMP_ALLOW_POST              1
#define TMP_ALLOW_DELETE            1

// ── CGI ──────────────────────────────────────────────────────
// Replace with: location->cgi_extension   location->cgi_path
#define TMP_CGI_EXTENSION           ""      // empty = CGI disabled
#define TMP_CGI_PATH                ""
#define TMP_CGI_TIMEOUT_SECONDS     10      // → SIGKILL + 504 if exceeded

// ── File upload ──────────────────────────────────────────────
// Replace with: location->upload_path
#define TMP_UPLOAD_PATH             ""      // empty = upload disabled

// ── Redirect ─────────────────────────────────────────────────
// Replace with: location->redirect_enabled  location->redirect_code  location->redirect_url
#define TMP_REDIRECT_ENABLED        0
#define TMP_REDIRECT_CODE           301
#define TMP_REDIRECT_URL            ""

#endif // TMPCONF_HPP
