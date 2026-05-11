#!/usr/bin/env python3
import json
import os
import html
from datetime import datetime, timedelta
from http.cookies import SimpleCookie

USERS_FILE = os.path.join(os.path.dirname(__file__), '..', 'users.json')
SESSIONS_FILE = os.path.join(os.path.dirname(__file__), '..', 'sessions.json')


def load_json(path, default):
    if os.path.exists(path):
        with open(path, 'r') as f:
            return json.load(f)
    return default


def save_json(path, data):
    """Save data to a JSON file."""
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)


def html_escape(text):
    """Escape HTML special characters to prevent XSS."""
    if text is None:
        return ""
    return html.escape(str(text), quote=True)


def get_session_user():
    """
    Validate session and return username if authenticated.
    
    Returns:
        str: The username if session is valid and not expired.
        None: If no valid session exists.
    """
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
    
    if sid not in sessions:
        return None
    
    session_data = sessions[sid]
    
    # Handle both old format (plain string) and new format (dict)
    if isinstance(session_data, dict):
        username = session_data.get('username')
        
        # Verify username is present and is a non-empty string
        if not username or not isinstance(username, str) or not username.strip():
            return None
        
        # Check session expiration
        expires_at = session_data.get('expires_at')
        if expires_at:
            try:
                expire_time = datetime.fromisoformat(expires_at)
                if datetime.utcnow() > expire_time:
                    del sessions[sid]
                    save_json(SESSIONS_FILE, sessions)
                    return None
            except (ValueError, TypeError):
                pass
        
        return username.strip()
    
    elif isinstance(session_data, str):
        # Legacy format: sessions[sid] is a plain username string
        if not session_data.strip():
            return None
        return session_data.strip()
    
    else:
        # Unknown format — reject
        return None


def redirect(location):
    """Send HTTP 302 redirect response."""
    print("Status: 302 Found")
    print(f"Location: {location}")
    print()
    raise SystemExit


# ============================================================
# Main Request Handler
# ============================================================

username = get_session_user()
if username is None:
    redirect("/login.html")

users = load_json(USERS_FILE, {})

# Build table rows with HTML-escaped values
rows_parts = []
for name, data in sorted(users.items()):
    safe_name = html_escape(name)
    safe_created = html_escape(data.get('created_at', '-'))
    safe_last_login = html_escape(data.get('last_login', '-'))
    rows_parts.append(
        f"<tr><td>{safe_name}</td><td>{safe_created}</td><td>{safe_last_login}</td></tr>"
    )
rows = "".join(rows_parts)

safe_username = html_escape(username)

print("Content-Type: text/html\n")
print(f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Users - {safe_username}</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <div class="container">
        <h1>Registered Users</h1>
        <p>Logged in as: <strong>{safe_username}</strong></p>
        <table style="width:100%; border-collapse: collapse;">
            <thead>
                <tr>
                    <th style="text-align:left; padding:8px; border-bottom:1px solid #334155;">Username</th>
                    <th style="text-align:left; padding:8px; border-bottom:1px solid #334155;">Created</th>
                    <th style="text-align:left; padding:8px; border-bottom:1px solid #334155;">Last Login</th>
                </tr>
            </thead>
            <tbody>
                {rows or '<tr><td colspan="3" style="padding:8px;">No users yet</td></tr>'}
            </tbody>
        </table>
        <p style="margin-top:20px;"><a href="/cgi-bin/dashboard.py">Back to Dashboard</a></p>
    </div>
</body>
</html>""")