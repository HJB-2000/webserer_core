#!/usr/bin/env python3
import os
import json
from http.cookies import SimpleCookie

DATA_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', 'data'))
SESSIONS_FILE = os.path.join(DATA_DIR, 'sessions.json')

cookie_str = os.environ.get('HTTP_COOKIE', '')
if cookie_str:
    cookie = SimpleCookie()
    cookie.load(cookie_str)
    sid_morsel = cookie.get('session_id')
    if sid_morsel:
        sid = sid_morsel.value
        
        sessions = {}
        if os.path.exists(SESSIONS_FILE):
            try:
                with open(SESSIONS_FILE, 'r') as f:
                    sessions = json.load(f)
            except Exception:
                pass
                
        if sid in sessions:
            del sessions[sid]
            with open(SESSIONS_FILE, 'w') as f:
                json.dump(sessions, f, indent=2)

expired = SimpleCookie()
expired['session_id'] = 'deleted'
expired['session_id']['expires'] = 'Thu, 01 Jan 1970 00:00:00 GMT'
expired['session_id']['path'] = '/'
expired['session_id']['max-age'] = 0

print("Status: 302 Found")
print("Content-Type: text/html")
print(expired.output())
print("Location: /index.html")
print()
