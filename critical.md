CRITICAL (crash / UB / security)
return directive: out-of-bounds read on 1 value. location_parser.cpp:131-134 — location.setReturnRedirection(atoi(values[0].c_str()), values[1]) accesses values[1] with no size check. Config return 301; → UB (segfault / garbage). Also atoi is used without is_valid_number, so return abc xyz; silently gives code 0.

Server::operator= forgets _index_Files. serverConfig.cpp:84-98 — every other member is copied, but the index-files vector is not. Any serverA = serverB leaves stale / default indexes. Copy-ctor handles it correctly, which hides the bug until a std::vector reallocation or explicit assignment triggers it.

is_valid_number("") returns true. parserConf.cpp:67-75 — empty loop ⇒ "valid". This propagates:
is_valid_host accepts 1..2.3, 1.2.3., .1.2.3, 1.2.. as valid IPv4 (empty octet → is_valid_number("") → atoi("") → 0 → passes 0–255).
Any validator that gates on is_valid_number before atoi can be fooled by an empty token (today only tokens from storing_in_vec are non-empty, but this is a footgun waiting to break).


HIGH
Location matchLocation uses raw prefix match — no boundary. serverConfig.cpp:29-48 — path.find(loc_path) == 0 matches /apidoc against /api. Correct nginx semantics require the next char to be / or end-of-string (unless loc_path ends in /). Causes silently wrong routing.
Ordering dependency: location inherits from Server state captured at the moment the location block is read. locationConfig.cpp:32-48 Combined with server_parser.cpp:271-283 — if the user writes location / before root/index in the server block, the Location ctor copies empty strings, and validate_locations_of_server then rejects the config with "Missing required directive: root". Config that is semantically valid in nginx is rejected here purely due to directive order.

remove_comments treats # anywhere as comment start. parsing.cpp:5-27 — there is no quoting and no "only at token start" check, so root /var/www#x; silently truncates the rest of the line, producing confusing downstream errors. Nginx only treats # as a comment when it starts a token.
MEDIUM
isspace / isdigit / isalpha called on raw char (signed). e.g. parsing.cpp:36-39 and parserConf.cpp:69-72. Per C++98, passing a value not representable as unsigned char (or EOF) is UB. Any non-ASCII byte in the config triggers it. Fix: cast to (unsigned char).
client_max_body_size cap inconsistent across contexts.
http level caps at MAX_SERVER_LIMIT_s = 1 GiB parserConf.cpp:84-92
server level caps at MAX_SERVER_LIMIT = 1 GiB (duplicate constant, different name) server_parser.cpp:208-214
location level has no cap at all location_parser.cpp:106-112. So location / { client_max_body_size 1T; } silently succeeds (up to LLONG_MAX/mult). Subject says 1 GiB is the mandated max.
timeout accepts 0 but final validation rejects it. Setter range is 0 ≤ tmp ≤ 3600 server_parser.cpp:215-226 but parsServer then throws Missing required directive: timeout if get_timeout_seconds() <= 0 server_parser.cpp:308-309. So timeout 0; is effectively a "missing directive" error, which is misleading. Pick one: either forbid 0 in the setter or allow 0 in the validator.
Location paths that collide with grammar keywords fail. location_parser.cpp:10-16 — expects TYPE_VALUE as the path, but Lexer::identify classifies words like server, http, events, listen, root, index, return, etc. as TYPE_CONTEXT/TYPE_DIRECTIVE. So location server { … } or location return { … } is rejected at lex time even though it's a legitimate path token.
cgi_path/cgi_pass and upload_store/upload_path treated as aliases but deduped as distinct keys. location_parser.cpp:94-105 combined with location_parser.cpp:28-33 — a user can write both keys in the same block; the second silently overwrites the first with no duplicate-directive error.
parse_cl_mx_bd_sz accepts 0 silently; nginx semantics for 0 is "unlimited", but here 0 means "reject all bodies". Minor semantic trap; at least document it.
parsServer requires server_name. server_parser.cpp:304-305 — subject does not require server_name. This forces users to always provide it even when only one server listens on a port. Restrictive / diverges from nginx.
LOW / informational
allowed_methods GET GET GET; is accepted — location_parser.cpp:77-86 no dedupe; trivially bloats _allowed_methods.
root a b c; and index a b c; at server level — extra values are silently ignored for root (only values[0] used) server_parser.cpp:195-203. Should error out on extra tokens for scalar directives.
autoindex on off; accepts only values[0] — no check for extra values location_parser.cpp:87-93.
atoi for listen port / error codes has no overflow handling. Very large numeric strings pass is_valid_number but atoi overflow is UB. Use strtol with ERANGE checks like you already do in parse_cl_mx_bd_sz.
matchServer doesn't strip :port from the Host header. serverConfig.cpp:7-27 — exact string compare means Host: example.com:8080 never matches a server_name example.com;. Fallback to "first on port" masks the bug.
is_valid_host only accepts numeric IPv4. localhost and other hostnames aren't allowed as listen host.
_grammar is a std::map<std::string, t_token_type> rebuilt every time init_grammar is called. LexerConfig.cpp:58-90 If ever called twice (e.g. in tests), inserts are fine but there's no idempotence guard. Also has a commented-out duplicate allowed_methods line — dead noise.
ParserConf(bool default_conf) is dead code — nothing calls it; API_conf uses ServerConfig::set_default_conf directly.
report_parse_error always throws std::runtime_error(msg) — but the messages passed are often prefixes like "Syntax Error: directive " with the real detail in the where arg. The exception's what() ends up useless; only stderr has the info. Consider threading where into the exception message.
The top three (the return OOB, the missing _index_Files in operator=, and is_valid_number("") → IPv4 validator accepts malformed addresses) are the ones I'd fix first. Want me to open a PR with fixes, or leave this as analysis only?