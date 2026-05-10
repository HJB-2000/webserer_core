#!/usr/bin/env python3
import json
import os
import uuid
import urllib.parse
from datetime import datetime
from http.cookies import SimpleCookie

USERS_FILE = os.path.join(os.path.dirname(__file__), '..', 'users.json')
SESSIONS_FILE = os.path.join(os.path.dirname(__file__), '..', 'sessions.json')

def load_json(path, default):
    if os.path.exists(path):
        with open(path, 'r') as f:
            return json.load(f)
    return default

def save_json(path, data):
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)

def create_session(username):
    sid = uuid.uuid4().hex
    sessions = load_json(SESSIONS_FILE, {})
    sessions[sid] = username
    save_json(SESSIONS_FILE, sessions)
    return sid

def set_cookie(sid):
    cookie = SimpleCookie()
    cookie['session_id'] = sid
    cookie['session_id']['path'] = '/'
    print(cookie.output())

def redirect(location, cookie=None):
    print("Status: 302 Found")
    if cookie:
        print(cookie.output())
    print(f"Location: {location}")
    print("Content-Type: text/html")
    print()
    exit()


def read_post_params():
    try:
        content_length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    except ValueError:
        content_length = 0
    raw = os.read(0, content_length).decode("utf-8", "replace") if content_length > 0 else ""
    return urllib.parse.parse_qs(raw)

params = read_post_params()
username = params.get('username', [''])[0].strip()
password = params.get('password', [''])[0]

if not username or not password:
    print("Content-Type: text/html\n")
    print("<h1>Error</h1><p>Username and password are required.</p>")
    exit()

users = load_json(USERS_FILE, {})
if username not in users:
    print("Content-Type: text/html\n")
    print("""<!DOCTYPE html>
<html>
<head><title>Access Denied</title></head>
<body style="font-family:Arial;background:#f4f4f4;padding:20px;">
    <h1 style="color:#dc2626;">Access Denied</h1>
    <p style="color:#dc2626;">You need to sign up before logging in.</p>
    <p><a href="/signup.html">Go to Sign Up</a></p>
</body>
</html>""")
    exit()

user = users[username]
if user.get('password') != password:
    print("Content-Type: text/html\n")
    print("<h1 style='color:#dc2626;'>Error</h1><p style='color:#dc2626;'>Incorrect password.</p>")
    exit()

# Successful login
# Successful login: update last_login timestamp and create session.
user['last_login'] = datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC")
users[username] = user
save_json(USERS_FILE, users)

sid = create_session(username)
cookie = SimpleCookie()
cookie['session_id'] = sid
cookie['session_id']['path'] = '/'

redirect("/cgi-bin/dashboard.py", cookie=cookie)