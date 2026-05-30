#!/usr/bin/env python3
import json
import os
import uuid
import hashlib
import secrets
import urllib.parse
from datetime import datetime, timedelta
from http.cookies import SimpleCookie

from cgi_data_store import (
    session_key,
    cookie_name,
    SESSIONS_FILE,
    USERS_FILE,
    USER_LOG_FILE as LOG_FILE,
    load_json,
    save_json_atomic,
    append_log_line,
)

MAX_LOGIN_ATTEMPTS = 5
LOGIN_WINDOW_SECONDS = 300


def create_session(username):
    sid = uuid.uuid4().hex
    sessions = load_json(SESSIONS_FILE, {})
    sessions[session_key(sid)] = {
        'username': username,
        'created_at': datetime.utcnow().isoformat(),
        'expires_at': (datetime.utcnow() + timedelta(minutes=15)).isoformat(),
    }
    save_json_atomic(SESSIONS_FILE, sessions)
    return sid


def verify_password(stored_password, provided_password):
    try:
        iterations, salt, stored_hash = stored_password.split('$')
        iterations = int(iterations)
        key = hashlib.pbkdf2_hmac(
            'sha256',
            provided_password.encode('utf-8'),
            salt.encode('utf-8'),
            iterations,
            dklen=32
        )
        return secrets.compare_digest(key.hex(), stored_hash)
    except (ValueError, AttributeError):
        return False


def is_legacy_password(stored_password):
    parts = stored_password.split('$')
    if len(parts) != 3:
        return True
    try:
        int(parts[0])
        return False
    except ValueError:
        return True


def hash_password(password):
    salt = secrets.token_hex(32)
    key = hashlib.pbkdf2_hmac(
        'sha256',
        password.encode('utf-8'),
        salt.encode('utf-8'),
        600000,
        dklen=32
    )
    return f"600000${salt}${key.hex()}"


def error_response(title, message, status_code=None):
    print("Content-Type: text/html\n")
    print(f"""<!DOCTYPE html>
<html>
<head><title>{title}</title></head>
<body style="font-family:Arial;background:#f4f4f4;padding:20px;">
    <h1 style="color:#dc2626;">{title}</h1>
    <p style="color:#dc2626;">{message}</p>
    <p><a href="/login.html">Back to Login</a></p>
    <p><a href="/signup.html">Sign Up</a></p>
</body>
</html>""")
    exit()


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
    error_response("Error", "Username and password are required.")

users = load_json(USERS_FILE, {})

if username not in users:
    error_response("Access Denied", "You need to sign up before logging in.")

user = users[username]
stored_password = user.get('password', '')

if is_legacy_password(stored_password):
    password_valid = (stored_password == password)
else:
    password_valid = verify_password(stored_password, password)

if not password_valid:
    append_log_line(LOG_FILE, f"FAILED LOGIN: {username} from {os.environ.get('REMOTE_ADDR', 'unknown')} at {datetime.utcnow().isoformat()}")
    error_response("Login Failed", "Incorrect password.")

if is_legacy_password(stored_password):
    user['password'] = hash_password(password)
    append_log_line(LOG_FILE, f"PASSWORD MIGRATED: {username} from plain-text to PBKDF2 at {datetime.utcnow().isoformat()}")

user['last_login'] = datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC")
users[username] = user
save_json_atomic(USERS_FILE, users)

sid = create_session(username)
cookie = SimpleCookie()
cname = cookie_name()
cookie[cname] = sid
cookie[cname]['path'] = '/'
cookie[cname]['httponly'] = True
cookie[cname]['max-age'] = 900

append_log_line(LOG_FILE, f"LOGIN: {username} from {os.environ.get('REMOTE_ADDR', 'unknown')} at {datetime.utcnow().isoformat()}")
redirect("/cgi-bin/dashboard.py", cookie=cookie)
