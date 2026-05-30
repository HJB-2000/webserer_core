#!/usr/bin/env python3
import json
import os
import re
import hashlib
import secrets
import urllib.parse
from datetime import datetime

from cgi_data_store import (
    USERS_FILE,
    USER_LOG_FILE as LOG_FILE,
    load_json,
    save_json_atomic,
    append_log_line,
)

MIN_USERNAME_LENGTH = 3
MAX_USERNAME_LENGTH = 32
MIN_PASSWORD_LENGTH = 8
MAX_PASSWORD_LENGTH = 128
USERNAME_REGEX = re.compile(r'^[a-zA-Z0-9._-]+$')
HASH_ITERATIONS = 600000


def redirect(location):
    print("Status: 302 Found")
    print(f"Location: {location}")
    print("Content-Type: text/html")
    print()
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


def read_post_params():
    try:
        content_length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    except ValueError:
        content_length = 0
    raw = os.read(0, content_length).decode("utf-8", "replace") if content_length > 0 else ""
    return urllib.parse.parse_qs(raw)


def hash_password(password):
    salt = secrets.token_hex(32)
    key = hashlib.pbkdf2_hmac(
        'sha256',
        password.encode('utf-8'),
        salt.encode('utf-8'),
        HASH_ITERATIONS,
        dklen=32
    )
    return f"{HASH_ITERATIONS}${salt}${key.hex()}"


def validate_username(username):
    if len(username) < MIN_USERNAME_LENGTH:
        return False, f"Username must be at least {MIN_USERNAME_LENGTH} characters."
    if len(username) > MAX_USERNAME_LENGTH:
        return False, f"Username must be at most {MAX_USERNAME_LENGTH} characters."
    if not USERNAME_REGEX.match(username):
        return False, "Username can only contain letters, numbers, dots, underscores, and hyphens."
    return True, None


def validate_password(password):
    if len(password) < MIN_PASSWORD_LENGTH:
        return False, f"Password must be at least {MIN_PASSWORD_LENGTH} characters."
    if len(password) > MAX_PASSWORD_LENGTH:
        return False, f"Password must be at most {MAX_PASSWORD_LENGTH} characters."
    return True, None


params = read_post_params()
username = params.get('username', [''])[0].strip()
password = params.get('password', [''])[0]

if not username or not password:
    error_response("Error", "Username and password are required.")

username_valid, username_error = validate_username(username)
if not username_valid:
    error_response("Invalid Username", username_error)

password_valid, password_error = validate_password(password)
if not password_valid:
    error_response("Weak Password", password_error)

users = load_json(USERS_FILE, {})
if username in users:
    error_response("Account Exists", "An account already exists with this username.")

hashed_password = hash_password(password)
users[username] = {
    'password': hashed_password,
    'created_at': datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC"),
    'last_login': None
}
save_json_atomic(USERS_FILE, users)
append_log_line(LOG_FILE, f"SIGNUP: {username} at {datetime.utcnow().isoformat()}")
redirect("/index.html")
