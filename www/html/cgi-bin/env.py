#!/usr/bin/env python3
import html
import json
import os
from datetime import datetime
from http.cookies import SimpleCookie

SESSIONS_FILE = os.path.join(os.path.dirname(__file__), '..', 'sessions.json')
REDACT_KEYS = {"HTTP_COOKIE", "HTTP_AUTHORIZATION", "AUTHORIZATION"}


def load_json(path, default):
    if os.path.exists(path):
        with open(path, 'r') as f:
            return json.load(f)
    return default


def save_json(path, data):
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)


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
    if sid not in sessions:
        return None

    session_data = sessions[sid]
    if isinstance(session_data, dict):
        username = session_data.get('username')
        expires_at = session_data.get('expires_at')
        if expires_at:
            try:
                expire_time = datetime.fromisoformat(expires_at)
                if datetime.utcnow() > expire_time:
                    del sessions[sid]
                    save_json(SESSIONS_FILE, sessions)
                    return None
            except (ValueError, TypeError):
                pass
        return username
    return session_data


def redirect(location):
    print("Status: 302 Found")
    print(f"Location: {location}")
    print()
    raise SystemExit


username = get_session_user()
if username is None:
    redirect("/login.html")

safe_username = html.escape(str(username), quote=True)

print("Status: 200 OK")
print("Content-Type: text/html\n")
print(f"<h1>CGI Environment Variables</h1><p>Logged in as: <strong>{safe_username}</strong></p><pre>")
for key, value in sorted(os.environ.items()):
    safe_key = html.escape(str(key), quote=True)
    if key in REDACT_KEYS:
        safe_value = "[REDACTED]"
    else:
        safe_value = html.escape(str(value), quote=True)
    print(f"{safe_key}: {safe_value}")
print("</pre>")
