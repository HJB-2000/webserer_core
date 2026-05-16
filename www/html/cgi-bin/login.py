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

# ============================================================
# Security Configuration
# ============================================================
MAX_LOGIN_ATTEMPTS = 5
LOGIN_WINDOW_SECONDS = 300  # 5 minutes


# def create_session(username):
#     """
#     Create a new session for the given username.
    
#     Returns the session ID (32-char hex string).
#     """
#     sid = uuid.uuid4().hex
#     sessions = load_json(SESSIONS_FILE, {})
#     sessions[sid] = {
#         'username': username,
#         'created_at': datetime.utcnow().isoformat(),
#         'expires_at': (datetime.utcnow() + timedelta(days=7)).isoformat(),
#     }
#     save_json_atomic(SESSIONS_FILE, sessions)
#     return sid

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
    """
    Verify a password against its stored hash using constant-time comparison.
    
    Stored format: iterations$salt$hash
    
    Args:
        stored_password: The hashed password from users.json
        provided_password: The plain-text password from the login form
    
    Returns:
        bool: True if the password matches, False otherwise
    """
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
        
        # Constant-time comparison prevents timing attacks
        return secrets.compare_digest(key.hex(), stored_hash)
    except (ValueError, AttributeError):
        # If stored_password is not in the expected format (e.g., legacy plain-text)
        return False


def is_legacy_password(stored_password):
    """
    Check if the stored password is in the old plain-text format.
    
    Returns True if it doesn't match the hash format (iterations$salt$hash).
    """
    parts = stored_password.split('$')
    if len(parts) != 3:
        return True
    try:
        int(parts[0])
        return False
    except ValueError:
        return True


def hash_password(password):
    """
    Hash a password using PBKDF2-SHA256 with a random salt.
    
    Returns: iterations$salt$hash (all hex-encoded where applicable)
    """
    salt = secrets.token_hex(32)
    key = hashlib.pbkdf2_hmac(
        'sha256',
        password.encode('utf-8'),
        salt.encode('utf-8'),
        600000,  # iterations
        dklen=32
    )
    return f"600000${salt}${key.hex()}"


def error_response(title, message, status_code=None):
    """Send an error page response."""
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
    """Send HTTP 302 redirect response."""
    print("Status: 302 Found")
    if cookie:
        print(cookie.output())
    print(f"Location: {location}")
    print("Content-Type: text/html")
    print()
    exit()


def read_post_params():
    """Read and parse POST form data from stdin."""
    try:
        content_length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    except ValueError:
        content_length = 0
    raw = os.read(0, content_length).decode("utf-8", "replace") if content_length > 0 else ""
    return urllib.parse.parse_qs(raw)


# ============================================================
# Main Request Handler
# ============================================================

params = read_post_params()
username = params.get('username', [''])[0].strip()
password = params.get('password', [''])[0]

# Validate presence
if not username or not password:
    error_response("Error", "Username and password are required.")

# Load users
users = load_json(USERS_FILE, {})

# Check if user exists
if username not in users:
    error_response("Access Denied", "You need to sign up before logging in.")

user = users[username]
stored_password = user.get('password', '')

# ============================================================
# PASSWORD VERIFICATION (UPDATED)
# ============================================================

# Check if this is a legacy plain-text password that needs migration
if is_legacy_password(stored_password):
    # Legacy comparison (will be migrated on successful login)
    password_valid = (stored_password == password)
else:
    # Secure comparison using PBKDF2 hash
    password_valid = verify_password(stored_password, password)

if not password_valid:
    # Log failed attempt
    append_log_line(LOG_FILE, f"FAILED LOGIN: {username} from {os.environ.get('REMOTE_ADDR', 'unknown')} at {datetime.utcnow().isoformat()}")
    
    error_response("Login Failed", "Incorrect password.")

# ============================================================
# SUCCESSFUL LOGIN
# ============================================================

# Migrate legacy plain-text passwords to hashed format
if is_legacy_password(stored_password):
    user['password'] = hash_password(password)
    append_log_line(LOG_FILE, f"PASSWORD MIGRATED: {username} from plain-text to PBKDF2 at {datetime.utcnow().isoformat()}")

# Update last_login timestamp
user['last_login'] = datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC")
users[username] = user
save_json_atomic(USERS_FILE, users)

# Create session
sid = create_session(username)
cookie = SimpleCookie()
cname = cookie_name()
cookie[cname] = sid
cookie[cname]['path'] = '/'
cookie[cname]['httponly'] = True  # Prevent JavaScript access
#  tell the browser to keep the cookie for 15 minutes
cookie[cname]['max-age'] = 900

# Log successful login
append_log_line(LOG_FILE, f"LOGIN: {username} from {os.environ.get('REMOTE_ADDR', 'unknown')} at {datetime.utcnow().isoformat()}")

# Redirect to dashboard
redirect("/cgi-bin/dashboard.py", cookie=cookie)