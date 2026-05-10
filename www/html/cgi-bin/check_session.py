#!/usr/bin/env python3
import json
import os
from http.cookies import SimpleCookie

SESSIONS_FILE = os.path.join(os.path.dirname(__file__), '..', 'sessions.json')

def load_json(path, default):
    if os.path.exists(path):
        with open(path, 'r') as f:
            return json.load(f)
    return default

cookie_str = os.environ.get('HTTP_COOKIE', '')
username = None

if cookie_str:
    cookie = SimpleCookie()
    cookie.load(cookie_str)
    sid_morsel = cookie.get('session_id')
    if sid_morsel:
        sid = sid_morsel.value
        sessions = load_json(SESSIONS_FILE, {})
        username = sessions.get(sid)

if username:
    print("Status: 200 OK")
    print("Content-Type: text/plain\n")
    print(username)
else:
    print("Status: 401 Unauthorized")
    print("Content-Type: text/plain\n")
    print("No valid session")
