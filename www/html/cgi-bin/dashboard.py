#!/usr/bin/env python3
import os
import html
from datetime import datetime
from http.cookies import SimpleCookie

from cgi_data_store import USERS_FILE, SESSIONS_FILE, session_key, cookie_name, load_json, save_json_atomic


def html_escape(text):
    """Escape HTML special characters to prevent XSS."""
    if text is None:
        return ""
    return html.escape(str(text), quote=True)

def get_session_user():
    """Validate session and return username if authenticated."""
    cookie_str = os.environ.get('HTTP_COOKIE', '')
    if not cookie_str:
        return None
    
    cookie = SimpleCookie()
    cookie.load(cookie_str)
    sid_morsel = cookie.get(cookie_name())
    if sid_morsel is None:
        return None
    
    sid = session_key(sid_morsel.value)
    sessions = load_json(SESSIONS_FILE, {})
    
    if sid not in sessions:
        return None
    
    session_data = sessions[sid]
    
    # Handle both old format (string) and new format (dict)
    if isinstance(session_data, dict):
        username = session_data.get('username')
        
        # Check session expiration
        expires_at = session_data.get('expires_at')
        if expires_at:
            try:
                expire_time = datetime.fromisoformat(expires_at)
                if datetime.utcnow() > expire_time:
                    del sessions[sid]
                    save_json_atomic(SESSIONS_FILE, sessions)
                    return None
            except (ValueError, TypeError):
                pass
        
        return username
    else:
        # Legacy format (plain username string)
        return session_data

def redirect(location):
    """Send HTTP 302 redirect response."""
    print("Status: 302 Found")
    print(f"Location: {location}")
    print()
    exit()

# ============================================================
# Main Request Handler
# ============================================================

username = get_session_user()
if username is None:
    redirect("/login.html")

users = load_json(USERS_FILE, {})
user = users.get(username)
if user is None:
    redirect("/login.html")

# Escape all user-controlled data before embedding in HTML
last_login = html_escape(user.get('last_login') or "First login")
safe_username = html_escape(username)

# Generate user list with proper escaping
users_list_items = []
for name in sorted(users.keys()):
    safe_name = html_escape(name)
    users_list_items.append(f"<li>{safe_name}</li>")
users_list = "\n".join(users_list_items)

print("Content-Type: text/html\n")
print(f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Dashboard - {safe_username}</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <div class="container">
        <h1>Welcome back, {safe_username}!</h1>
        <p>Last login: {last_login}</p>
        <div class="info-box">
            <h3>All users in platform</h3>
            <ul>{users_list}</ul>
        </div>
        <p><a href="/dashboard/index.html">Open Dashboard Pages</a></p>
        <p><a href="/cgi-bin/view_users.py">View All Users</a></p>
        <p><a href="/cgi-bin/logout.py">Log out</a></p>
    </div>
</body>
</html>""")