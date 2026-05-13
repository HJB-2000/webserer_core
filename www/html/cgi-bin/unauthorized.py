#!/usr/bin/env python3
print("Status: 401 Unauthorized")
print("WWW-Authenticate: Basic realm=\"Webserv\"")
print("Content-Type: text/html\n")
print("<html><body><h1>401 Unauthorized</h1><p>Authentication required</p></body></html>")
