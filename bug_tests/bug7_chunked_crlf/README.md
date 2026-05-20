# BUG 7 — Chunked Body Trailing CRLF Consumed Without Validation

## Location
`src/HttpParser.cpp` — `HttpParser::_parseChunked()`

## Root Cause

```cpp
// Step 1: consume trailing CRLF
if (req._chunk_trailing) {
    if (buf.size() < 2) return;   // waits for 2 bytes
    buf.consume(2);               // ← blindly discards ANY 2 bytes
    req._chunk_trailing = false;
```

The RFC 7230 §4.1 chunked format is:
```
<hex-size>\r\n
<data bytes>
\r\n                ← this is the "trailing CRLF" for the chunk
<hex-size>\r\n
...
0\r\n
\r\n
```

The parser checks that 2 bytes are present, but never verifies they are `\r\n`.
Any 2-byte sequence after chunk data is silently consumed as if it were a valid
trailing CRLF. This means:

- A client sending `\r\x00`, `\n\n`, `AB`, or any other 2-byte garbage after
  chunk data will have the garbage accepted and the request parsed as valid.
- Downstream proxies or WAFs that validate the CRLF strictly may reject the
  same request — creating a desync opportunity (HTTP request smuggling vector).
- CGI scripts that process the body receive it as if it were well-formed.

## How to Trigger

`trigger.py` sends four malformed chunked requests:
1. Trailing `\r\x00` instead of `\r\n`
2. Trailing `\n\n` instead of `\r\n`
3. Trailing `\x00\x00` (null bytes)
4. Trailing `AB` (printable garbage)

If the server responds **200 OK** (or any success status) to any of these,
the bug is confirmed — the server accepted a protocol-violating request.

A correct server must respond **400 Bad Request** to all four.

## Expected Result (bug present)
Server returns `200 OK` or `201 Created` for malformed trailing bytes.

## Expected Result (bug fixed)
Server returns `400 Bad Request` for all malformed trailing bytes.

## Fix

```cpp
if (req._chunk_trailing) {
    if (buf.size() < 2) return;

    // Validate the 2 bytes are actually \r\n
    if (buf.data()[0] != '\r' || buf.data()[1] != '\n') {
        req.parse_state = PSTATE_ERROR;
        req.error_code  = 400;
        return;
    }

    buf.consume(2);
    req._chunk_trailing = false;
    ...
}
```

## Manual Reproduction with netcat

```bash
# Start the server
./webserv conf/confs/replit.conf &

# Send a chunked POST with wrong trailing bytes (\r\0 instead of \r\n)
printf 'POST /uploads/ HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nhello\r\x00 0\r\n\r\n' \
    | nc 127.0.0.1 5000

# A correct server returns 400. A buggy server returns 201.
```
