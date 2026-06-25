#!/usr/bin/env python3
import os
import json
import hashlib
import urllib.parse
from datetime import datetime

DATA_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', 'data'))
USERS_FILE = os.path.join(DATA_DIR, 'users.json')

MIN_USERNAME_LENGTH = 3
MAX_USERNAME_LENGTH = 32
MIN_PASSWORD_LENGTH = 8

def redirect(location):
    print("Status: 302 Found")
    print(f"Location: {location}")
    print("Content-Type: text/html\n")
    exit()

def error_response(title, message):
    print("Content-Type: text/html\n")
    print(f"""<!DOCTYPE html>
<html>
<head><title>{title}</title></head>
<body style="font-family:Arial;background:#f4f4f4;padding:20px;">
    <h1 style="color:#dc2626;">{title}</h1>
    <p style="color:#dc2626;">{message}</p>
    <p><a href="/signup.html">Back to Sign Up</a></p>
</body>
</html>""")
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

if not (MIN_USERNAME_LENGTH <= len(username) <= MAX_USERNAME_LENGTH):
    error_response("Invalid Username", "Username length invalid.")

if len(password) < MIN_PASSWORD_LENGTH:
    error_response("Weak Password", "Password too short.")

users = {}
if os.path.exists(USERS_FILE):
    try:
        with open(USERS_FILE, 'r') as f:
            users = json.load(f)
    except Exception:
        users = {}

if username in users:
    error_response("Account Exists", "An account already exists with this username.")

users[username] = {
    'password': hashlib.sha256(password.encode('utf-8')).hexdigest(),
    'created_at': datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC")
}

os.makedirs(os.path.dirname(os.path.abspath(USERS_FILE)), exist_ok=True)
with open(USERS_FILE, 'w') as f:
    json.dump(users, f, indent=2)

redirect("/login.html")