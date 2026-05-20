#!/usr/bin/env python3
"""
Quick diagnostic script for webserv cookies/sessions
"""

import subprocess
import re
import random

HOST = "127.0.0.1"
PORT = 8080
BASE_URL = f"http://{HOST}:{PORT}"

class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    MAGENTA = '\033[95m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

class HTTPResponse:
    def __init__(self, raw_response):
        self.raw = raw_response
        self.status_code = 0
        self.headers = {}
        self.body = ""
        self.cookies = {}
        self._parse()
    
    def _parse(self):
        if not self.raw:
            return
        
        parts = self.raw.split('\r\n\r\n', 1)
        if len(parts) != 2:
            parts = self.raw.split('\n\n', 1)
        
        if len(parts) != 2:
            return
        
        header_part, self.body = parts
        lines = header_part.split('\n')
        
        if not lines:
            return
        
        status_match = re.match(r'HTTP/\d\.\d\s+(\d{3})\s+(.*)', lines[0].strip())
        if status_match:
            self.status_code = int(status_match.group(1))
        
        for line in lines[1:]:
            line = line.strip()
            if ':' in line:
                key, value = line.split(':', 1)
                key = key.strip().lower()
                value = value.strip()
                self.headers[key] = value
                
                if key == 'set-cookie':
                    cookie_parts = value.split(';')[0].split('=', 1)
                    if len(cookie_parts) == 2:
                        self.cookies[cookie_parts[0].strip()] = cookie_parts[1].strip()

def curl(method, path, headers=None, data=None, cookies=None, follow_redirects=False, verbose=False):
    cmd = ['curl', '-s', '-i', '-X', method]
    
    if headers:
        for k, v in headers.items():
            cmd.extend(['-H', f'{k}: {v}'])
    
    if data:
        cmd.extend(['-d', data])
    
    if cookies:
        cookie_str = '; '.join([f'{k}={v}' for k, v in cookies.items()])
        cmd.extend(['-H', f'Cookie: {cookie_str}'])
    
    if follow_redirects:
        cmd.append('-L')
    
    if verbose:
        cmd.append('-v')
    
    cmd.append(f'{BASE_URL}{path}')
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        return HTTPResponse(result.stdout)
    except Exception as e:
        print(f"Curl error: {e}")
        return HTTPResponse("")

def print_section(title):
    print(f"\n{Colors.BOLD}{Colors.MAGENTA}{'='*60}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.MAGENTA}  {title}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.MAGENTA}{'='*60}{Colors.RESET}")

# ============================================
# MAIN DIAGNOSTICS
# ============================================
print(f"{Colors.BOLD}{Colors.CYAN}Webserv Session/Cookie Diagnostic{Colors.RESET}")
print(f"Target: {BASE_URL}")

# 1. Check server is up
print_section("1. SERVER CHECK")
r = curl("GET", "/")
print(f"GET / → Status: {r.status_code}")
if r.status_code == 0:
    print(f"{Colors.RED}Server not reachable! Start your webserv first.{Colors.RESET}")
    exit(1)
print(f"{Colors.GREEN}Server is running!{Colors.RESET}")

# 2. Check CGI is working
print_section("2. CGI CHECK")
r = curl("GET", "/cgi-bin/env.py")
print(f"GET /cgi-bin/env.py → Status: {r.status_code}")
print(f"Body preview: {r.body[:200]}")

# 3. Test signup
print_section("3. SIGNUP TEST")
username = f"diag_{random.randint(100, 999)}"
password = "test123"

print(f"Creating user: {username}")

# Without following redirect
r = curl("POST", "/cgi-bin/signup.py",
         data=f"username={username}&password={password}&email={username}@test.com",
         headers={'Content-Type': 'application/x-www-form-urlencoded'})
print(f"Signup status: {r.status_code}")
print(f"Location header: {r.headers.get('location', 'NONE')}")
print(f"All response headers:")
for k, v in sorted(r.headers.items()):
    print(f"  {k}: {v}")
print(f"Body (first 300 chars): {r.body[:300]}")

# With following redirect
r2 = curl("POST", "/cgi-bin/signup.py",
          data=f"username={username}&password={password}&email={username}@test.com",
          headers={'Content-Type': 'application/x-www-form-urlencoded'},
          follow_redirects=True)
print(f"\nSignup (follow redirect) status: {r2.status_code}")

# 4. Test login
print_section("4. LOGIN TEST")

r = curl("POST", "/cgi-bin/login.py",
         data=f"username={username}&password={password}",
         headers={'Content-Type': 'application/x-www-form-urlencoded'})

print(f"Login status: {r.status_code}")
print(f"All response headers:")
for k, v in sorted(r.headers.items()):
    print(f"  {k}: {v}")
print(f"Parsed cookies: {r.cookies}")
print(f"Body (first 300 chars): {r.body[:300]}")

# Check raw Set-Cookie header
set_cookie = r.headers.get('set-cookie', '')
print(f"\nRaw Set-Cookie header: '{set_cookie}'")

# Try parsing manually
if 'session_id' in set_cookie or 'session' in set_cookie.lower():
    print(f"{Colors.GREEN}Found session cookie!{Colors.RESET}")
else:
    print(f"{Colors.YELLOW}No session cookie found in response{Colors.RESET}")

# 5. Test session usage
if r.cookies:
    print_section("5. SESSION CHECK")
    
    # Use the cookies from login
    print(f"Using cookies: {r.cookies}")
    
    r_check = curl("GET", "/cgi-bin/check_session.py", cookies=r.cookies)
    print(f"check_session.py status: {r_check.status_code}")
    print(f"Body: {r_check.body[:500]}")
    
    r_dash = curl("GET", "/cgi-bin/dashboard.py", cookies=r.cookies)
    print(f"\ndashboard.py status: {r_dash.status_code}")
    print(f"Body (first 300 chars): {r_dash.body[:300]}")
else:
    print_section("5. SESSION CHECK - SKIPPED")
    print(f"{Colors.RED}No cookies to test session{Colors.RESET}")

# 6. Test C++ CGI output parser
print_section("6. CGI OUTPUT PARSER CHECK")

# Test with env.py to see raw CGI output
r = curl("GET", "/cgi-bin/env.py")
print(f"Status: {r.status_code}")
print(f"Content-Type: {r.headers.get('content-type', 'MISSING')}")
print(f"Content-Length: {r.headers.get('content-length', 'MISSING')}")
print(f"Connection: {r.headers.get('connection', 'MISSING')}")
print(f"Body size: {len(r.body)} bytes")

# Check for parser issues
if 'content-type' not in r.headers:
    print(f"{Colors.RED}Missing Content-Type header (parser issue?){Colors.RESET}")
if 'content-length' not in r.headers:
    print(f"{Colors.RED}Missing Content-Length header (parser issue?){Colors.RESET}")

# 7. Test static file with potential cookie
print_section("7. TEST WITH CUSTOM COOKIE")
r = curl("GET", "/", cookies={'test_cookie': 'hello'})
print(f"GET / with Cookie header:")
print(f"Status: {r.status_code}")
print(f"Request sent Cookie: test_cookie=hello")
print(f"Response Set-Cookie: {r.headers.get('set-cookie', 'NONE')}")

print(f"\n{Colors.BOLD}{Colors.CYAN}{'='*60}{Colors.RESET}")
print(f"{Colors.BOLD}{Colors.CYAN}DIAGNOSTIC COMPLETE{Colors.RESET}")
print(f"{Colors.BOLD}{Colors.CYAN}{'='*60}{Colors.RESET}")