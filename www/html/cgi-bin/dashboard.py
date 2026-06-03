#!/usr/bin/env python3
import os
import html
from datetime import datetime
from http.cookies import SimpleCookie

from cgi_data_store import USERS_FILE, SESSIONS_FILE, session_key, cookie_name, load_json, save_json_atomic


def html_escape(text):
    if text is None:
        return ""
    return html.escape(str(text), quote=True)


def get_session_user():
    cookie_str = os.environ.get('HTTP_COOKIE', '')
    if not cookie_str:
        return None
    cookie = SimpleCookie()
    cookie.load(cookie_str)
    sid_morsel = cookie.get(cookie_name())
    if sid_morsel is None:
        return None
    sid = session_key(sid_morsel.value)
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
                    save_json_atomic(SESSIONS_FILE, sessions)
                    return None
            except (ValueError, TypeError):
                pass
        return username
    else:
        return session_data


def redirect(location):
    print("Status: 302 Found")
    print(f"Location: {location}")
    print()
    exit()


username = get_session_user()
if username is None:
    redirect("/login.html")

users = load_json(USERS_FILE, {})
user = users.get(username)
if user is None:
    redirect("/login.html")

last_login = html_escape(user.get('last_login') or "First login")
safe_username = html_escape(username)

users_list_items = []
for name in sorted(users.keys()):
    users_list_items.append(f"<li>{html_escape(name)}</li>")
users_list = "\n".join(users_list_items)

print("Content-Type: text/html\n")
print(f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Dashboard - {safe_username}</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <div class="container">
        <h1>Welcome back, {safe_username}!</h1>
        <p>Last login: {last_login}</p>
        <div class="info-box">
            <h3>All registered users</h3>
            <ul>{users_list}</ul>
        </div>
        <div class="nav-links">
            <a href="/dashboard/index.html">KV Store &amp; Error Tests</a>
            <a href="/index.html">Home</a>
            <a href="/cgi-bin/logout.py">Log Out</a>
        </div>
    </div>
</body>
</html>""")
