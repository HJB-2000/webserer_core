#!/usr/bin/env python3
import os
import json
import html
from datetime import datetime
from http.cookies import SimpleCookie

# Define data directory and sessions file directly
DATA_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', 'data'))
SESSIONS_FILE = os.path.join(DATA_DIR, 'sessions.json')

REDACT_KEYS = {"HTTP_COOKIE", "HTTP_AUTHORIZATION", "AUTHORIZATION"}
# REDACT_KEYS = set()


def get_session_user():
    cookie_str = os.environ.get('HTTP_COOKIE', '')
    if not cookie_str:
        return None

    cookie = SimpleCookie()
    cookie.load(cookie_str)
    # Use standard 'session_id' instead of cookie_name() wrapper
    sid_morsel = cookie.get('session_id')
    if sid_morsel is None:
        return None

    sid = sid_morsel.value

    # Load sessions natively using standard python open()
    sessions = {}
    if os.path.exists(SESSIONS_FILE):
        try:
            with open(SESSIONS_FILE, 'r') as f:
                sessions = json.load(f)
        except Exception:
            pass

    # No namespacing needed, check the sid directly
    if sid not in sessions:
        return None

    session_data = sessions[sid]
    username = session_data.get('username')
    expires_at = session_data.get('expires_at')

    if expires_at:
        try:
            expire_time = datetime.fromisoformat(expires_at)
            if datetime.utcnow() > expire_time:
                # Remove expired session directly and save
                del sessions[sid]
                with open(SESSIONS_FILE, 'w') as f:
                    json.dump(sessions, f, indent=2)
                return None
        except Exception:
            pass

    return username


def redirect(location):
    print("Status: 302 Found")
    print(f"Location: {location}\n")
    exit()


username = get_session_user()
if username is None:
    redirect("/login.html")

safe_username = html.escape(str(username), quote=True)

print("Status: 200 OK")
print("Content-Type: text/html\n")
print(f"<h1>CGI Environment Variables</h1><p>Logged in as: <strong>{safe_username}</strong></p><pre>")

# Print the environment variables normally for the subject requirement showcase
for key, value in sorted(os.environ.items()):
    safe_key = html.escape(str(key), quote=True)
    if key in REDACT_KEYS:
        safe_value = "[REDACTED]"
    else:
        safe_value = html.escape(str(value), quote=True)
    print(f"{safe_key}: {safe_value}")
print("</pre>")