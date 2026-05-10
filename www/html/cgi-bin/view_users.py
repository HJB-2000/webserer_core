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
    raise SystemExit


username = get_session_user()
if username is None:
    redirect("/login.html")

users = load_json(USERS_FILE, {})

rows = "".join(
    f"<tr><td>{name}</td><td>{data.get('created_at','-')}</td><td>{data.get('last_login','-')}</td></tr>"
    for name, data in sorted(users.items())
)

print("Content-Type: text/html\n")
print(f"""<!DOCTYPE html>
<html lang=\"en\">
<head>
    <meta charset=\"UTF-8\">
    <title>Users</title>
    <link rel=\"stylesheet\" href=\"/style.css\">
</head>
<body>
    <div class=\"container\">
        <h1>Registered Users</h1>
        <table style=\"width:100%; border-collapse: collapse;\">
            <thead>
                <tr>
                    <th style=\"text-align:left; padding:8px; border-bottom:1px solid #334155;\">Username</th>
                    <th style=\"text-align:left; padding:8px; border-bottom:1px solid #334155;\">Created</th>
                    <th style=\"text-align:left; padding:8px; border-bottom:1px solid #334155;\">Last Login</th>
                </tr>
            </thead>
            <tbody>
                {rows or '<tr><td colspan="3" style="padding:8px;">No users yet</td></tr>'}
            </tbody>
        </table>
        <p style=\"margin-top:20px;\"><a href=\"/dashboard/index.html\">← Back to Dashboard</a></p>
    </div>
</body>
</html>""")
