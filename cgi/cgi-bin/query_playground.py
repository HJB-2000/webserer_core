#!/usr/bin/python3
import os
from urllib.parse import parse_qs
from html import escape

query = os.environ.get("QUERY_STRING", "")
params = parse_qs(query, keep_blank_values=True)
name = params.get("name", ["Guest"])[0]
mode = params.get("mode", ["normal"])[0]

palette = {
    "normal": "#2d6cdf",
    "fire": "#e85d04",
    "mint": "#00a896",
    "violet": "#7b2cbf",
}
accent = palette.get(mode, "#2d6cdf")

print("Content-Type: text/html")
print("Status: 200 OK")
print()
print("<!doctype html>")
print("<html><head><meta charset='utf-8'><title>Query Playground</title></head>")
print("<body style='font-family:Arial,sans-serif;background:#0f172a;color:#e2e8f0;padding:24px;'>")
print("<h1 style='color:{}'>🎯 Query Playground</h1>".format(escape(accent)))
print("<p>Hello, <b>{}</b>! CGI parsed your query string successfully.</p>".format(escape(name)))
print("<p>Selected mode: <b>{}</b></p>".format(escape(mode)))
print("<h3>Raw QUERY_STRING</h3>")
print("<pre>{}</pre>".format(escape(query)))
print("<h3>Parsed parameters</h3><ul>")
if params:
    for k in sorted(params.keys()):
        print("<li><b>{}</b> = {}</li>".format(escape(k), escape(', '.join(params[k]))))
else:
    print("<li>No parameters provided</li>")
print("</ul>")
print("</body></html>")
