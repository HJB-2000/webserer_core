#!/usr/bin/env python3
"""
Webserv Test Suite - FIXED for your server's behavior
"""

import subprocess
import sys
import re
import time
import random
from concurrent.futures import ThreadPoolExecutor, as_completed

HOST = "127.0.0.1"
PORT = 8080
BASE_URL = f"http://{HOST}:{PORT}"

class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    MAGENTA = '\033[95m'
    BOLD = '\033[1m'
    RESET = '\033[0m'

class HTTPResponse:
    def __init__(self, raw_response):
        self.raw = raw_response
        self.status_code = 0
        self.headers = {}
        self.body = ""
        self.cookies = {}
        self._parse()
    
    def _parse(self):
        if not self.raw: return
        parts = self.raw.split('\r\n\r\n', 1)
        if len(parts) != 2: parts = self.raw.split('\n\n', 1)
        if len(parts) != 2: return
        
        header_part, self.body = parts
        lines = header_part.split('\n')
        if not lines: return
        
        m = re.match(r'HTTP/\d\.\d\s+(\d{3})', lines[0])
        if m: self.status_code = int(m.group(1))
        
        for line in lines[1:]:
            if ':' in line:
                k, v = line.split(':', 1)
                k = k.strip().lower()
                v = v.strip()
                self.headers[k] = v
                if k == 'set-cookie':
                    cp = v.split(';')[0].split('=', 1)
                    if len(cp) == 2: self.cookies[cp[0].strip()] = cp[1].strip()

def curl(method, path, headers=None, data=None, cookies=None, follow=False):
    cmd = ['curl', '-s', '-i', '-X', method, '--max-time', '15']
    if headers:
        for k, v in headers.items(): cmd.extend(['-H', f'{k}: {v}'])
    if data: cmd.extend(['-d', data])
    if cookies: cmd.extend(['-H', f'Cookie: {"; ".join(f"{k}={v}" for k,v in cookies.items())}'])
    if follow: cmd.append('-L')
    cmd.append(f'{BASE_URL}{path}')
    
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
        return HTTPResponse(r.stdout)
    except:
        return HTTPResponse("")

class TestSuite:
    def __init__(self):
        self.passed = 0
        self.failed = 0
    
    def test(self, name, passed):
        if passed: self.passed += 1
        else: self.failed += 1
        print(f"  {'✓' if passed else '✗'} {name}")
        return passed
    
    def summary(self):
        print(f"\n{'='*50}")
        print(f"  Passed: {self.passed}  Failed: {self.failed}")
        print(f"{'='*50}")

def main():
    ts = TestSuite()
    
    print(f"{Colors.BOLD}Webserv Test Suite (Fixed){Colors.RESET}")
    print(f"Target: {BASE_URL}\n")
    
    # ============================================
    # 1. STATIC FILES
    # ============================================
    print(f"{Colors.BOLD}1. Static Files{Colors.RESET}")
    
    r = curl("GET", "/")
    ts.test("GET / → 200", r.status_code == 200)
    
    r = curl("GET", "/style.css")
    ts.test("GET /style.css → 200", r.status_code == 200)
    
    r = curl("GET", "/login.html")
    ts.test("GET /login.html → 200", r.status_code == 200)
    
    r = curl("GET", "/dashboard/")
    ts.test("GET /dashboard/ → 200", r.status_code == 200)
    
    r = curl("GET", "/nonexistent.html")
    ts.test("GET /nonexistent → 404", r.status_code == 404)
    
    r = curl("GET", "/errors/404.html")
    ts.test("GET /errors/404.html → 200", r.status_code == 200)
    
    # ============================================
    # 2. CGI ENDPOINTS
    # ============================================
    print(f"\n{Colors.BOLD}2. CGI Endpoints{Colors.RESET}")
    
    r = curl("GET", "/cgi-bin/env.py")
    ts.test("GET /cgi-bin/env.py → 200", r.status_code == 200)
    ts.test("env.py is JSON", 'application/json' in r.headers.get('content-type', ''))
    
    r = curl("POST", "/cgi-bin/save_body.py", data="test=hello")
    ts.test("POST /cgi-bin/save_body.py", r.status_code in [200, 201])
    
    # ============================================
    # 3. SESSIONS & COOKIES
    # ============================================
    print(f"\n{Colors.BOLD}3. Sessions & Cookies{Colors.RESET}")
    
    username = f"test_{random.randint(1000, 9999)}"
    
    # Signup
    r = curl("POST", "/cgi-bin/signup.py",
             data=f"username={username}&password=test&email={username}@test.com",
             headers={'Content-Type': 'application/x-www-form-urlencoded'})
    ts.test(f"Signup → 302 redirect", r.status_code == 302)
    
    # Login (gets session cookie)
    r = curl("POST", "/cgi-bin/login.py",
             data=f"username={username}&password=test",
             headers={'Content-Type': 'application/x-www-form-urlencoded'})
    ts.test(f"Login → 302 redirect", r.status_code == 302)
    
    has_session = 'session_id' in r.cookies
    ts.test(f"Login sets session cookie", has_session)
    
    if has_session:
        sess = {'session_id': r.cookies['session_id']}
        
        r = curl("GET", "/cgi-bin/check_session.py", cookies=sess)
        ts.test(f"Session check returns username", username in r.body)
        
        r = curl("GET", "/cgi-bin/dashboard.py", cookies=sess)
        ts.test(f"Dashboard accessible with session", r.status_code == 200)
        ts.test(f"Dashboard shows username", username in r.body)
        
        r = curl("GET", "/cgi-bin/logout.py", cookies=sess, follow=True)
        ts.test(f"Logout successful", r.status_code in [200, 302])
    
    # ============================================
    # 4. UPLOAD
    # ============================================
    print(f"\n{Colors.BOLD}4. File Upload{Colors.RESET}")
    
    boundary = "----TestBoundary"
    upload_data = f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"test.txt\"\r\nContent-Type: text/plain\r\n\r\nHello stress test!\r\n--{boundary}--"
    
    r = curl("POST", "/cgi-bin/upload.py",
             data=upload_data,
             headers={'Content-Type': f'multipart/form-data; boundary={boundary}'})
    ts.test(f"File upload", r.status_code == 200)
    
    # ============================================
    # 5. CONCURRENT STRESS
    # ============================================
    print(f"\n{Colors.BOLD}5. Concurrent Stress Test{Colors.RESET}")
    
    def worker(i):
        try:
            r = curl("GET", "/")
            return r.status_code == 200
        except: return False
    
    print(f"  Running 50 concurrent requests...")
    start = time.time()
    with ThreadPoolExecutor(max_workers=50) as ex:
        results = [f.result() for f in [ex.submit(worker, i) for i in range(50)]]
    elapsed = time.time() - start
    rate = sum(results) / len(results) * 100
    
    ts.test(f"50 concurrent GET /", rate > 90)
    print(f"    Success: {rate:.0f}% in {elapsed:.2f}s")
    
    # ============================================
    # 6. RAPID FIRE
    # ============================================
    print(f"\n{Colors.BOLD}6. Rapid Fire Test{Colors.RESET}")
    
    start = time.time()
    rapid = [curl("GET", "/").status_code == 200 for _ in range(100)]
    elapsed = time.time() - start
    rate = sum(rapid) / len(rapid) * 100
    rps = 100 / elapsed if elapsed > 0 else 0
    
    ts.test(f"100 rapid requests", rate > 95)
    print(f"    Success: {rate:.0f}% in {elapsed:.2f}s ({rps:.0f} req/s)")
    
    # ============================================
    # 7. CONCURRENT CGI
    # ============================================
    print(f"\n{Colors.BOLD}7. Concurrent CGI Stress{Colors.RESET}")
    
    def cgi_worker(i):
        try:
            r = curl("GET", "/cgi-bin/env.py")
            return r.status_code == 200
        except: return False
    
    print(f"  Running 20 concurrent CGI requests...")
    start = time.time()
    with ThreadPoolExecutor(max_workers=20) as ex:
        results = [f.result() for f in [ex.submit(cgi_worker, i) for i in range(20)]]
    elapsed = time.time() - start
    rate = sum(results) / len(results) * 100
    
    ts.test(f"20 concurrent CGI", rate > 80)
    print(f"    Success: {rate:.0f}% in {elapsed:.2f}s")
    
    # ============================================
    # SUMMARY
    # ============================================
    ts.summary()
    return 0 if ts.failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())