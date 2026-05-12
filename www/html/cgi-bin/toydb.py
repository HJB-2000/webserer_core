#!/usr/bin/env python3
import json
import os
import sys
import urllib.parse
from datetime import datetime
from http.cookies import SimpleCookie

from cgi_data_store import SESSIONS_FILE, TOYDB_FILE as DB_FILE, load_json, save_json_atomic


def get_session_user():
    cookie_str = os.environ.get('HTTP_COOKIE', '')
    if not cookie_str:
        return None
    cookie = SimpleCookie()
    cookie.load(cookie_str)
    sid_morsel = cookie.get('session_id')
    if sid_morsel is None:
        return None
    sid = sid_morsel.value
    sessions = load_json(SESSIONS_FILE, {})
    session_data = sessions.get(sid)
    if isinstance(session_data, dict):
        username = session_data.get('username')
        expires_at = session_data.get('expires_at')
        if expires_at:
            try:
                expire_time = datetime.fromisoformat(expires_at)
                if datetime.utcnow() > expire_time:
                    return None
            except (ValueError, TypeError):
                pass
        if isinstance(username, str) and username.strip():
            return username.strip()
        return None
    if isinstance(session_data, str) and session_data.strip():
        return session_data.strip()
    return None

def http_response(status_code, content_type, body):
    print(f"Status: {status_code}")
    print(f"Content-Type: {content_type}")
    print()
    print(body)

# ── Authenticate ─────────────────────────────────────────
username = get_session_user()
if username is None:
    http_response('401 Unauthorized', 'text/plain', 'Not logged in')
    exit()

# ── Read request method / params ─────────────────────────
method = os.environ.get('REQUEST_METHOD', 'GET')
query_string = os.environ.get('QUERY_STRING', '')
params = urllib.parse.parse_qs(query_string)
key = params.get('key', [None])[0]

# ── Load database safely with migration ──────────────────
db = load_json(DB_FILE, {})

# Ensure the user's data is a dict (migrate old plain values)
if username not in db or not isinstance(db.get(username), dict):
    db[username] = {}          # reset to empty private store

user_data = db[username]

# ── Routing ──────────────────────────────────────────────
if method == 'GET':
    if key:
        if key in user_data:
            http_response('200 OK', 'text/plain', user_data[key])
        else:
            http_response('404 Not Found', 'text/plain', 'Key not found')
    else:
        http_response('200 OK', 'application/json', json.dumps(user_data, indent=2))

elif method == 'POST':
    try:
        cl = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
    except ValueError:
        cl = 0
    raw = os.read(0, cl).decode('utf-8', 'replace') if cl > 0 else ''
    post_params = urllib.parse.parse_qs(raw)
    key = post_params.get('key', [None])[0]
    value = post_params.get('value', [''])[0]
    if not key:
        http_response('400 Bad Request', 'text/plain', 'Missing key')
    else:
        user_data[key] = value
        db[username] = user_data
        save_json_atomic(DB_FILE, db)
        http_response('201 Created', 'text/plain', f'Key "{key}" stored.')

elif method == 'DELETE':
    if not key:
        http_response('400 Bad Request', 'text/plain', 'Missing key')
    elif key not in user_data:
        http_response('404 Not Found', 'text/plain', 'Key not found')
    else:
        del user_data[key]
        db[username] = user_data
        save_json_atomic(DB_FILE, db)
        http_response('200 OK', 'text/plain', f'Key "{key}" deleted.')

else:
    http_response('405 Method Not Allowed', 'text/plain', 'Supported: GET, POST, DELETE')