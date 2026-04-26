#!/usr/bin/env python3
import os

print("Content-Type: text/html\r\n")

log_file = "/home/fahd/webserv/www/html/user_log.txt"

print("""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Users Database</title>
    <style>
        body { font-family: Arial, sans-serif; background: #0f172a; color: #fff; padding: 20px; }
        .container { max-width: 700px; margin: auto; background: #1e293b; padding: 20px; border-radius: 8px; }
        h1 { color: #38bdf8; text-align: center; }
        table { width: 100%; border-collapse: collapse; margin-top: 20px; }
        th, td { padding: 10px; border-bottom: 1px solid #334155; text-align: left; vertical-align: middle; }
        th { background: #334155; }
        .delete-btn { background: #ef4444; color: white; border: none; padding: 5px 10px; border-radius: 4px; cursor: pointer; }
        .delete-btn:hover { background: #dc2626; }
        .back-btn { display: inline-block; margin-top: 20px; color: #fff; background: #f59e0b; padding: 10px 15px; border-radius: 5px; text-decoration: none; }
        .back-btn:hover { background: #d97706; }
        .status { margin-top: 10px; padding: 8px; border-radius: 4px; display: inline-block; }
        .success { background: #22c55e; }
        .error { background: #ef4444; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Current Users</h1>
        <div id="statusMsg"></div>
        <table>
            <thead>
                <tr>
                    <th>Time</th>
                    <th>Username</th>
                    <th>Action</th>
                </tr>
            </thead>
            <tbody id="userTableBody">
""")

def generate_table_rows():
    rows = ""
    if os.path.exists(log_file):
        with open(log_file, "r") as f:
            lines = f.readlines()
            if not lines:
                rows = '<tr><td colspan="3" style="text-align:center;">No users yet.</td></tr>'
            else:
                for line in reversed(lines):
                    if "LOGIN SUCCESS" in line:
                        line = line.strip()
                        start = line.find('[')
                        end = line.find(']')
                        if start != -1 and end != -1:
                            time_part = line[start+1:end]
                            suffix = line[end+1:].strip()
                            if suffix.startswith("LOGIN SUCCESS: "):
                                user_part = suffix[len("LOGIN SUCCESS: "):]
                            else:
                                user_part = "Unknown"
                        else:
                            parts = line.split("LOGIN SUCCESS: ")
                            if len(parts) == 2:
                                time_part = parts[0].strip("[] ")
                                user_part = parts[1]
                            else:
                                time_part = "Unknown"
                                user_part = "Unknown"
                        # Do NOT HTML-escape here – data attributes will be read as raw text.
                        # But ensure no quotes inside the values break the attribute.
                        # Replace double quotes with &quot; for safety.
                        time_part_attr = time_part.replace('"', '&quot;')
                        user_part_attr = user_part.replace('"', '&quot;')
                        rows += f"""
                        <tr data-username="{user_part_attr}" data-timestamp="{time_part_attr}">
                            <td>{time_part}</td>
                            <td>{user_part}</td>
                            <td><button class="delete-btn" onclick="deleteUser(this)">Delete</button></td>
                        </tr>
                        """
    else:
        rows = '<tr><td colspan="3" style="text-align:center;">No users yet.</td></tr>'
    return rows

print(generate_table_rows())

print("""            </tbody>
        </table>
        <a href="/index.html" class="back-btn">Go Back</a>
    </div>
    <script>
        function deleteUser(btn) {
            const row = btn.closest('tr');
            const username = row.getAttribute('data-username');
            const timestamp = row.getAttribute('data-timestamp');
            const statusDiv = document.getElementById('statusMsg');
            
            fetch('/cgi-bin/delete_user.py', {
                method: 'DELETE',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded'
                },
                body: `username=${encodeURIComponent(username)}&timestamp=${encodeURIComponent(timestamp)}`
            })
            .then(response => {
                if (response.ok) {
                    statusDiv.innerHTML = '<div class="status success">User deleted successfully.</div>';
                    row.remove();
                    const tbody = document.getElementById('userTableBody');
                    if (tbody.children.length === 0) {
                        tbody.innerHTML = '<tr><td colspan="3" style="text-align:center;">No users yet.</td></tr>';
                    }
                } else {
                    return response.text().then(text => {
                        statusDiv.innerHTML = `<div class="status error">Delete failed: ${text}</div>`;
                    });
                }
            })
            .catch(err => {
                statusDiv.innerHTML = `<div class="status error">Error: ${err.message}</div>`;
            });
        }
    </script>
</body>
</html>
""")