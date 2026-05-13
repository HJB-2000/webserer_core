#!/usr/bin/env python3
import os
from http.cookies import SimpleCookie

from cgi_data_store import SESSIONS_FILE, load_json, save_json_atomic


def destroy_session(sid):
    sessions = load_json(SESSIONS_FILE, {})
    if sid in sessions:
        del sessions[sid]
        save_json_atomic(SESSIONS_FILE, sessions)


# Get session from cookie and delete it
cookie_str = os.environ.get('HTTP_COOKIE', '')
if cookie_str:
    cookie = SimpleCookie()
    cookie.load(cookie_str)
    sid_morsel = cookie.get('session_id')
    if sid_morsel:
        destroy_session(sid_morsel.value)

# Clear cookie
expired = SimpleCookie()
expired['session_id'] = ''
expired['session_id']['path'] = '/'
expired['session_id']['expires'] = 'Thu, 01 Jan 1970 00:00:00 GMT'
print(expired.output())
print("Status: 302 Found")
print("Location: /index.html")
print()
