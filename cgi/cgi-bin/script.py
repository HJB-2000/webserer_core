#!/usr/bin/env python3
import sys
import urllib.parse
from datetime import datetime
import random

# Required HTTP headers for CGI
print("Content-Type: text/html\r\n")

try:
    # Get the form data
    body = sys.stdin.read()
    form = urllib.parse.parse_qs(body)
    
    username = form.get("username", [None])[0]
    password = form.get("password", [None])[0]
    
    if not username or not password:
        print("<html><body><h2>Error: Missing credentials!</h2><a href='/login.html'>Go back</a></body></html>")
        sys.exit(0)
    
    # SIMPLER STORAGE: Instead of a complex database, let's just log logins to a simple text file
    log_file = "/home/fahd/webserv/www/html/user_log.txt"
    with open(log_file, "a") as f:
        f.write(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] LOGIN SUCCESS: {username}\n")
    
    # INNOVATION: Generate a beautiful, personalized Dashboard page dynamically
    # We use a public API to generate a cute robot avatar based on their username
    avatar_url = f"https://api.dicebear.com/7.x/bottts/svg?seed={urllib.parse.quote(username)}"
    rank = random.choice(["Beginner", "Pro", "Expert", "Master", "Admin"])
    
    html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <meta http-equiv="refresh" content="3;url=/index.html" />
    <title>Welcome {username}</title>
    <style>
        body {{
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #1e293b 0%, #0f172a 100%);
            color: #fff;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
        }}
        .dashboard {{
            background: rgba(255, 255, 255, 0.1);
            backdrop-filter: blur(10px);
            padding: 40px;
            border-radius: 15px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.3);
            text-align: center;
            border: 1px solid rgba(255,255,255,0.2);
            width: 350px;
            animation: fadeIn 0.5s ease-out;
        }}
        @keyframes fadeIn {{ from {{ opacity: 0; transform: translateY(20px); }} to {{ opacity: 1; transform: translateY(0); }} }}
        .avatar {{
            width: 120px;
            height: 120px;
            background-color: #fff;
            border-radius: 50%;
            margin: 0 auto 20px auto;
            padding: 10px;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.4);
        }}
        img {{ width: 100%; height: 100%; border-radius: 50%; }}
        h1 {{ margin: 0; color: #38bdf8; font-size: 28px; }}
        p {{ color: #cbd5e1; font-size: 16px; margin-bottom: 30px; }}
        .stats {{
            display: flex;
            justify-content: space-between;
            background: rgba(0,0,0,0.2);
            padding: 15px;
            border-radius: 10px;
            margin-bottom: 25px;
        }}
        .stat div:first-child {{ font-size: 12px; color: #94a3b8; text-transform: uppercase; }}
        .stat div:last-child {{ font-size: 18px; font-weight: bold; color: #facc15; }}
        .btn {{
            display: inline-block;
            padding: 12px 24px;
            background: #ef4444;
            color: white;
            text-decoration: none;
            border-radius: 8px;
            font-weight: bold;
            transition: background 0.3s, transform 0.2s;
        }}
        .btn:hover {{ background: #dc2626; transform: scale(1.05); }}
    </style>
</head>
<body>
    <div class="dashboard">
        <div class="avatar">
            <img src="{avatar_url}" alt="Avatar">
        </div>
        <h1>Welcome, {username}!</h1>
        <p>You have successfully breached the mainframe.</p>
        
        <div class="stats">
            <div class="stat">
                <div>Assigned Rank</div>
                <div>{rank}</div>
            </div>
            <div class="stat">
                <div>Status</div>
                <div>Online 🟢</div>
            </div>
        </div>
        
        <a href="/index.html" class="btn">Logout / Home</a>
    </div>
</body>
</html>"""
    
    print(html_content)

except Exception as e:
    print("<html><body><h2>Internal Server Error</h2>")
    print(f"<p>{str(e)}</p></body></html>")
