# #!/usr/bin/env python3
# import json
# import os
# import urllib.parse
# from datetime import datetime

# USERS_FILE = os.path.join(os.path.dirname(__file__), '..', 'users.json')
# LOG_FILE = os.path.join(os.path.dirname(__file__), '..', 'user_log.txt')

# def load_json(path, default):
#     if os.path.exists(path):
#         with open(path, 'r') as f:
#             return json.load(f)
#     return default

# def save_json(path, data):
#     with open(path, 'w') as f:
#         json.dump(data, f, indent=2)

# def redirect(location):
#     print("Status: 302 Found")
#     print(f"Location: {location}")
#     print("Content-Type: text/html")
#     print()
#     exit()


# def read_post_params():
#     try:
#         content_length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
#     except ValueError:
#         content_length = 0
#     raw = os.read(0, content_length).decode("utf-8", "replace") if content_length > 0 else ""
#     return urllib.parse.parse_qs(raw)

# params = read_post_params()
# username = params.get('username', [''])[0].strip()
# password = params.get('password', [''])[0]

# if not username or not password:
#     print("Content-Type: text/html\n")
#     print("<h1>Error</h1><p>Username and password are required.</p>")
#     exit()

# users = load_json(USERS_FILE, {})
# if username in users:
#     print("Content-Type: text/html\n")
#     print("<h1>Error</h1><p>Account already exists with this username.</p>")
#     exit()

# users[username] = {
#     'password': password,
#     'created_at': datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC"),
#     'last_login': None
# }
# save_json(USERS_FILE, users)

# # Log the signup
# with open(LOG_FILE, 'a') as log:
#     log.write("SIGNUP: %s\n" % username)

# # After signup, return to landing page.
# redirect("/index.html")



#!/usr/bin/env python3
import json
import os
import re
import hashlib
import secrets
import urllib.parse
from datetime import datetime

USERS_FILE = os.path.join(os.path.dirname(__file__), '..', 'users.json')
LOG_FILE = os.path.join(os.path.dirname(__file__), '..', 'user_log.txt')

# ============================================================
# Security Configuration
# ============================================================
MIN_USERNAME_LENGTH = 3
MAX_USERNAME_LENGTH = 32
MIN_PASSWORD_LENGTH = 8
MAX_PASSWORD_LENGTH = 128
USERNAME_REGEX = re.compile(r'^[a-zA-Z0-9._-]+$')
HASH_ITERATIONS = 600000  # PBKDF2 iterations (OWASP 2023 recommendation)


def load_json(path, default):
    """Load and parse a JSON file. Returns default if file doesn't exist."""
    if os.path.exists(path):
        with open(path, 'r') as f:
            return json.load(f)
    return default


def save_json(path, data):
    """Save data to a JSON file with pretty-print formatting."""
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)


def redirect(location):
    """Send HTTP 302 redirect response."""
    print("Status: 302 Found")
    print(f"Location: {location}")
    print("Content-Type: text/html")
    print()
    exit()


def error_response(title, message):
    """Send an error page response."""
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
    """Read and parse POST form data from stdin."""
    try:
        content_length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    except ValueError:
        content_length = 0
    raw = os.read(0, content_length).decode("utf-8", "replace") if content_length > 0 else ""
    return urllib.parse.parse_qs(raw)


def hash_password(password):
    """
    Hash a password using PBKDF2-SHA256 with a random salt.
    
    Returns a string in the format: iterations$salt$hash
    where salt and hash are hex-encoded.
    """
    salt = secrets.token_hex(32)  # 256-bit random salt
    key = hashlib.pbkdf2_hmac(
        'sha256',
        password.encode('utf-8'),
        salt.encode('utf-8'),
        HASH_ITERATIONS,
        dklen=32  # 256-bit derived key
    )
    return f"{HASH_ITERATIONS}${salt}${key.hex()}"


def verify_password(stored_password, provided_password):
    """
    Verify a password against its stored hash.
    
    Uses constant-time comparison to prevent timing attacks.
    Stored format: iterations$salt$hash
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
        
        # Constant-time comparison
        return secrets.compare_digest(key.hex(), stored_hash)
    except (ValueError, AttributeError):
        return False


def validate_username(username):
    """
    Validate username format and length.
    
    Returns (is_valid, error_message)
    """
    if len(username) < MIN_USERNAME_LENGTH:
        return False, f"Username must be at least {MIN_USERNAME_LENGTH} characters."
    if len(username) > MAX_USERNAME_LENGTH:
        return False, f"Username must be at most {MAX_USERNAME_LENGTH} characters."
    if not USERNAME_REGEX.match(username):
        return False, "Username can only contain letters, numbers, dots, underscores, and hyphens."
    return True, None


def validate_password(password):
    """
    Validate password strength.
    
    Returns (is_valid, error_message)
    """
    if len(password) < MIN_PASSWORD_LENGTH:
        return False, f"Password must be at least {MIN_PASSWORD_LENGTH} characters."
    if len(password) > MAX_PASSWORD_LENGTH:
        return False, f"Password must be at most {MAX_PASSWORD_LENGTH} characters."
    
    # Optional: enforce complexity requirements
    # Uncomment to require at least one digit and one letter
    # if not re.search(r'[a-zA-Z]', password) or not re.search(r'[0-9]', password):
    #     return False, "Password must contain at least one letter and one number."
    
    return True, None


# ============================================================
# Main Request Handler
# ============================================================

params = read_post_params()
username = params.get('username', [''])[0].strip()
password = params.get('password', [''])[0]

# Validate presence
if not username or not password:
    error_response("Error", "Username and password are required.")

# Validate username format
username_valid, username_error = validate_username(username)
if not username_valid:
    error_response("Invalid Username", username_error)

# Validate password strength
password_valid, password_error = validate_password(password)
if not password_valid:
    error_response("Weak Password", password_error)

# Check for existing user
users = load_json(USERS_FILE, {})
if username in users:
    error_response("Account Exists", "An account already exists with this username.")

# Hash password and create user
hashed_password = hash_password(password)
users[username] = {
    'password': hashed_password,
    'created_at': datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC"),
    'last_login': None
}
save_json(USERS_FILE, users)

# Log the signup (never log passwords!)
with open(LOG_FILE, 'a') as log:
    log.write(f"SIGNUP: {username} at {datetime.utcnow().isoformat()}\n")

# Redirect to landing page
redirect("/index.html")