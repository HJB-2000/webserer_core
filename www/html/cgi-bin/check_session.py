#!/usr/bin/env python3
import os
from datetime import datetime
from http.cookies import SimpleCookie

from cgi_data_store import SESSIONS_FILE, session_key, cookie_name, load_json


def session_expired(session_data):
    if not isinstance(session_data, dict):
        return False
    expires_at = session_data.get('expires_at')
    if not expires_at:
        return False
    try:
        expire_time = datetime.fromisoformat(expires_at)
        return datetime.utcnow() > expire_time
    except (ValueError, TypeError):
        return False


# Get session cookie
cookie_str = os.environ.get('HTTP_COOKIE', '')
if not cookie_str:
    print("Status: 401 Unauthorized")
    print("Content-Type: text/plain")
    print()
    print("Not authenticated")
    exit()

cookie = SimpleCookie()
cookie.load(cookie_str)
sid_morsel = cookie.get(cookie_name())

if sid_morsel is None:
    print("Status: 401 Unauthorized")
    print("Content-Type: text/plain")
    print()
    print("Not authenticated")
    exit()

sid = sid_morsel.value
sessions = load_json(SESSIONS_FILE, {})
sid = session_key(sid)

if sid not in sessions:
    print("Status: 401 Unauthorized")
    print("Content-Type: text/plain")
    print()
    print("Session expired")
    exit()

session_data = sessions[sid]

if session_expired(session_data):
    print("Status: 401 Unauthorized")
    print("Content-Type: text/plain")
    print()
    print("Session expired")
    exit()

# Handle both old format (plain string) and new format (dict with 'username' key)
if isinstance(session_data, dict):
    username = session_data.get('username', 'Unknown')
else:
    username = str(session_data)

# Return just the username as plain text (not JSON)
print("Content-Type: text/plain")
print()
print(username)