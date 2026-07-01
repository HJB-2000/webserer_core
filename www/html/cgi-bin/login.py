#!/usr/bin/env python3
import os
import json
import uuid
import hashlib
import secrets
import urllib.parse
from datetime import datetime, timedelta
from http.cookies import SimpleCookie

DATA_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', 'data'))
USERS_FILE = os.path.join(DATA_DIR, 'users.json')
SESSIONS_FILE = os.path.join(DATA_DIR, 'sessions.json')
SESSION_LIFE_TIME = timedelta(minutes=15)

def error_response(title, message):
    print("Content-Type: text/html\n")
    print(f"""<!DOCTYPE html>
<html>
<head><title>{title}</title></head>
<body style="font-family:Arial;background:#f4f4f4;padding:20px;">
    <h1 style="color:#dc2626;">{title}</h1>
    <p style="color:#dc2626;">{message}</p>
    <p><a href="/login.html">Back to Login</a></p>
</body>
</html>""")
    exit()

def redirect(location, cookie=None):
    print("Status: 302 Found")
    if cookie:
        print(cookie.output())
    print(f"Location: {location}")
    print("Content-Type: text/html\n")
    exit()

try:
    content_length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
except ValueError:
    content_length = 0
raw_post = os.read(0, content_length).decode("utf-8", "replace") if content_length > 0 else ""
params = urllib.parse.parse_qs(raw_post)

username = params.get('username', [''])[0].strip()
password = params.get('password', [''])[0]

if not username or not password:
    error_response("Error", "Username and password are required.")


users = {}
if os.path.exists(USERS_FILE):
    try:
        with open(USERS_FILE, 'r') as f:
            users = json.load(f)
    except Exception:
        pass

if username not in users:
    error_response("Access Denied", "Account does not exist.")

stored_password = users[username].get('password', '')
provided_hash = hashlib.sha256(password.encode('utf-8')).hexdigest()

if not secrets.compare_digest(stored_password, provided_hash):
    error_response("Login Failed", "Incorrect password.")

sessions = {}
if os.path.exists(SESSIONS_FILE):
    try:
        with open(SESSIONS_FILE, 'r') as f:
            sessions = json.load(f)
    except Exception:
        pass

sid = uuid.uuid4().hex
sessions[sid] = {
    'username': username,
    'expires_at': (datetime.utcnow() + SESSION_LIFE_TIME).isoformat(),
}

os.makedirs(os.path.dirname(os.path.abspath(SESSIONS_FILE)), exist_ok=True)
with open(SESSIONS_FILE, 'w') as f:
    json.dump(sessions, f, indent=2)

cookie = SimpleCookie()
cookie['session_id'] = sid
cookie['session_id']['path'] = '/'
cookie['session_id']['httponly'] = True
cookie['session_id']['max-age'] = 900

redirect("/dashboard/index.html", cookie=cookie)