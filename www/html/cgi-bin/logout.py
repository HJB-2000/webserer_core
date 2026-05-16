#!/usr/bin/env python3
import os
from http.cookies import SimpleCookie

from cgi_data_store import SESSIONS_FILE, session_key, cookie_name, load_json, save_json_atomic


def destroy_session(sid):
    sessions = load_json(SESSIONS_FILE, {})
    key = session_key(sid)
    if key in sessions:
        del sessions[key]
        save_json_atomic(SESSIONS_FILE, sessions)


# Get session from cookie and delete it
cookie_str = os.environ.get('HTTP_COOKIE', '')
if cookie_str:
    cookie = SimpleCookie()
    cookie.load(cookie_str)
    sid_morsel = cookie.get(cookie_name())
    if sid_morsel:
        destroy_session(sid_morsel.value)

# Clear cookie
expired = SimpleCookie()
cname = cookie_name()
expired[cname] = ''
expired[cname]['path'] = '/'
expired[cname]['expires'] = 'Thu, 01 Jan 1970 00:00:00 GMT'
print(expired.output())
print("Status: 302 Found")
print("Location: /index.html")
print()