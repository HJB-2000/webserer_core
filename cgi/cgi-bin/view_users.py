# #!/usr/bin/env python3
# import os

# print("Content-Type: text/html\r\n")

# log_file = "/home/fahd/webserv/www/html/user_log.txt"

# print("""<!DOCTYPE html>
# <html lang="en">
# <head>
#     <meta charset="UTF-8">
#     <title>Users Database</title>
#     <style>
#         body { font-family: Arial, sans-serif; background: #0f172a; color: #fff; padding: 20px; }
#         .container { max-width: 700px; margin: auto; background: #1e293b; padding: 20px; border-radius: 8px; }
#         h1 { color: #38bdf8; text-align: center; }
#         table { width: 100%; border-collapse: collapse; margin-top: 20px; }
#         th, td { padding: 10px; border-bottom: 1px solid #334155; text-align: left; vertical-align: middle; }
#         th { background: #334155; }
#         .delete-btn { background: #ef4444; color: white; border: none; padding: 5px 10px; border-radius: 4px; cursor: pointer; }
#         .delete-btn:hover { background: #dc2626; }
#         .back-btn { display: inline-block; margin-top: 20px; color: #fff; background: #f59e0b; padding: 10px 15px; border-radius: 5px; text-decoration: none; }
#         .back-btn:hover { background: #d97706; }
#         .status { margin-top: 10px; padding: 8px; border-radius: 4px; display: inline-block; }
#         .success { background: #22c55e; }
#         .error { background: #ef4444; }
#     </style>
# </head>
# <body>
#     <div class="container">
#         <h1>Current Users</h1>
#         <div id="statusMsg"></div>
#         <table>
#             <thead>
#                 <tr>
#                     <th>Time</th>
#                     <th>Username</th>
#                     <th>Action</th>
#                 </tr>
#             </thead>
#             <tbody id="userTableBody">
# """)

# def generate_table_rows():
#     rows = ""
#     if os.path.exists(log_file):
#         with open(log_file, "r") as f:
#             lines = f.readlines()
#             if not lines:
#                 rows = '<tr><td colspan="3" style="text-align:center;">No users yet.</td></tr>'
#             else:
#                 for line in reversed(lines):
#                     if "LOGIN SUCCESS" in line:
#                         line = line.strip()
#                         start = line.find('[')
#                         end = line.find(']')
#                         if start != -1 and end != -1:
#                             time_part = line[start+1:end]
#                             suffix = line[end+1:].strip()
#                             if suffix.startswith("LOGIN SUCCESS: "):
#                                 user_part = suffix[len("LOGIN SUCCESS: "):]
#                             else:
#                                 user_part = "Unknown"
#                         else:
#                             parts = line.split("LOGIN SUCCESS: ")
#                             if len(parts) == 2:
#                                 time_part = parts[0].strip("[] ")
#                                 user_part = parts[1]
#                             else:
#                                 time_part = "Unknown"
#                                 user_part = "Unknown"
#                         # Do NOT HTML-escape here – data attributes will be read as raw text.
#                         # But ensure no quotes inside the values break the attribute.
#                         # Replace double quotes with &quot; for safety.
#                         time_part_attr = time_part.replace('"', '&quot;')
#                         user_part_attr = user_part.replace('"', '&quot;')
#                         rows += f"""
#                         <tr data-username="{user_part_attr}" data-timestamp="{time_part_attr}">
#                             <td>{time_part}</td>
#                             <td>{user_part}</td>
#                             <td><button class="delete-btn" onclick="deleteUser(this)">Delete</button></td>
#                         </tr>
#                         """
#     else:
#         rows = '<tr><td colspan="3" style="text-align:center;">No users yet.</td></tr>'
#     return rows

# print(generate_table_rows())

# print("""            </tbody>
#         </table>
#         <a href="/index.html" class="back-btn">Go Back</a>
#     </div>
#     <script>
#         function deleteUser(btn) {
#             const row = btn.closest('tr');
#             const username = row.getAttribute('data-username');
#             const timestamp = row.getAttribute('data-timestamp');
#             const statusDiv = document.getElementById('statusMsg');
            
#             fetch('/cgi-bin/delete_user.py', {
#                 method: 'DELETE',
#                 headers: {
#                     'Content-Type': 'application/x-www-form-urlencoded'
#                 },
#                 body: `username=${encodeURIComponent(username)}&timestamp=${encodeURIComponent(timestamp)}`
#             })
#             .then(response => {
#                 if (response.ok) {
#                     statusDiv.innerHTML = '<div class="status success">User deleted successfully.</div>';
#                     row.remove();
#                     const tbody = document.getElementById('userTableBody');
#                     if (tbody.children.length === 0) {
#                         tbody.innerHTML = '<tr><td colspan="3" style="text-align:center;">No users yet.</td></tr>';
#                     }
#                 } else {
#                     return response.text().then(text => {
#                         statusDiv.innerHTML = `<div class="status error">Delete failed: ${text}</div>`;
#                     });
#                 }
#             })
#             .catch(err => {
#                 statusDiv.innerHTML = `<div class="status error">Error: ${err.message}</div>`;
#             });
#         }
#     </script>
# </body>
# </html>
# """)


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
    """Validate session and return username if authenticated."""
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
                    save_json(SESSIONS_FILE, sessions)
                    return None
            except (ValueError, TypeError):
                pass
        
        return username
    else:
        return session_data


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