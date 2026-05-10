#!/usr/bin/env python3
import json
import os
import urllib.parse
from datetime import datetime

USERS_FILE = os.path.join(os.path.dirname(__file__), '..', 'users.json')
LOG_FILE = os.path.join(os.path.dirname(__file__), '..', 'user_log.txt')

def load_json(path, default):
    if os.path.exists(path):
        with open(path, 'r') as f:
            return json.load(f)
    return default

def save_json(path, data):
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)

def redirect(location):
    print("Status: 302 Found")
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
if username in users:
    print("Content-Type: text/html\n")
    print("<h1>Error</h1><p>Account already exists with this username.</p>")
    exit()

users[username] = {
    'password': password,
    'created_at': datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC"),
    'last_login': None
}
save_json(USERS_FILE, users)

# Log the signup
with open(LOG_FILE, 'a') as log:
    log.write("SIGNUP: %s\n" % username)

# After signup, return to landing page.
redirect("/index.html")