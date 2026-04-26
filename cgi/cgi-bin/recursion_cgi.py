#!/usr/bin/env python3
import os
import socket
import time
import random
from urllib.parse import parse_qs, urlencode


def to_int(v, d):
    try:
        return int(v)
    except:
        return d


def fire_slow_request(host, port, path):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setblocking(False)

        try:
            s.connect((host, port))
        except BlockingIOError:
            pass

        # Partial HTTP request (incomplete on purpose)
        payload = (
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: stress-bomb\r\n"
        ) % (path, host)

        # Send only part → keep server waiting
        cut = random.randint(10, len(payload))
        s.send(payload[:cut].encode())

        # DO NOT close immediately → hold connection
        return s

    except Exception:
        return None


def main():
    query = parse_qs(os.environ.get("QUERY_STRING", ""))
    host_header = os.environ.get("HTTP_HOST", "127.0.0.1:8080")
    script = os.environ.get("SCRIPT_NAME", "/cgi-bin/recursion_cgi.py")

    if ":" in host_header:
        host, port = host_header.split(":")
        port = int(port)
    else:
        host = host_header
        port = 80

    depth = to_int(query.get("depth", ["0"])[0], 0)

    # 🔥 aggressive fanout
    fanout = to_int(query.get("fanout", ["5"])[0], 5)

    sockets = []

    for _ in range(fanout):
        next_qs = urlencode({
            "depth": depth + 1,
            "fanout": fanout
        })
        path = "%s?%s" % (script, next_qs)

        s = fire_slow_request(host, port, path)
        if s:
            sockets.append(s)

    # 🔥 HOLD connections open → triggers timeout handling
    time.sleep(random.uniform(0.5, 2.0))

    # optionally finish some requests randomly
    for s in sockets:
        try:
            if random.random() < 0.3:
                s.send(b"\r\n")  # complete headers randomly
            s.close()
        except:
            pass

    # 🔥 ALSO delay CGI response → test server patience
    time.sleep(random.uniform(0.5, 2.0))

    print("Content-Type: text/plain\r\n")
    print("CGI stress running")
    print(f"depth={depth} fanout={fanout}")