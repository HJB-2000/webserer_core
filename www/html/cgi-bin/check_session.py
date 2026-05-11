#!/usr/bin/env python3
import json
import os
from datetime import datetime
from http.cookies import SimpleCookie

SESSIONS_FILE = os.path.join(os.path.dirname(__file__), '..', 'sessions.json')


def load_json(path, default):
    if os.path.exists(path):
        with open(path, 'r') as f:
            return json.load(f)
    return default


def save_json(path, data):
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)


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
sid_morsel = cookie.get('session_id')

if sid_morsel is None:
    print("Status: 401 Unauthorized")
    print("Content-Type: text/plain")
    print()
    print("Not authenticated")
    exit()

sid = sid_morsel.value
sessions = load_json(SESSIONS_FILE, {})

if sid not in sessions:
    print("Status: 401 Unauthorized")
    print("Content-Type: text/plain")
    print()
    print("Session expired")
    exit()

session_data = sessions[sid]

# Handle both old format (plain string) and new format (dict with 'username' key)
if isinstance(session_data, dict):
    username = session_data.get('username', 'Unknown')
else:
    username = str(session_data)

# Return just the username as plain text (not JSON)
print("Content-Type: text/plain")
print()
print(username)