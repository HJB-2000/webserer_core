#!/usr/bin/env python3
import json
import os
from http.cookies import SimpleCookie

USERS_FILE = os.path.join(os.path.dirname(__file__), '..', 'users.json')
SESSIONS_FILE = os.path.join(os.path.dirname(__file__), '..', 'sessions.json')

def load_json(path, default):
    if os.path.exists(path):
        with open(path, 'r') as f:
            return json.load(f)
    return default

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
    return sessions.get(sid)

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
    # Session exists but user data missing (should not happen)
    redirect("/login.html")

last_login = user.get('last_login') or "First login"
users_list = "".join(["<li>%s</li>" % name for name in sorted(users.keys())])

print("Content-Type: text/html\n")
print(f"""<!DOCTYPE html>
<html>
<head>
    <title>Dashboard</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <div class="container">
        <h1>Welcome back, {username}!</h1>
        <p>Last login: {last_login}</p>
        <div class="info-box">
            <h3>All users in platform</h3>
            <ul>{users_list}</ul>
        </div>
        <p><a href="/dashboard/index.html">Open Dashboard Pages</a></p>
        <p><a href="/cgi-bin/logout.py">Log out</a></p>
    </div>
</body>
</html>""")