#!/usr/bin/env python3
import os
import json
from datetime import datetime
from http.cookies import SimpleCookie

DATA_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', 'data'))
SESSIONS_FILE = os.path.join(DATA_DIR, 'sessions.json')

cookie_str = os.environ.get('HTTP_COOKIE', '')
if not cookie_str:
    print("Status: 401 Unauthorized\nContent-Type: text/plain\n\nNot authenticated")
    exit()

cookie = SimpleCookie()
cookie.load(cookie_str)
sid_morsel = cookie.get('session_id')

if sid_morsel is None:
    print("Status: 401 Unauthorized\nContent-Type: text/plain\n\nNot authenticated")
    exit()

sid = sid_morsel.value

sessions = {}
if os.path.exists(SESSIONS_FILE):
    try:
        with open(SESSIONS_FILE, 'r') as f:
            sessions = json.load(f)
    except Exception:
        pass

if sid not in sessions:
    print("Status: 401 Unauthorized\nContent-Type: text/plain\n\nSession expired")
    exit()

session_data = sessions[sid]
expires_at = session_data.get('expires_at')

is_expired = True
if expires_at:
    try:
        is_expired = datetime.utcnow() > datetime.fromisoformat(expires_at)
    except Exception:
        pass

if is_expired:
    print("Status: 401 Unauthorized\nContent-Type: text/plain\n\nSession expired")
    exit()

print("Content-Type: text/plain\n")
print(session_data.get('username', 'Unknown'))