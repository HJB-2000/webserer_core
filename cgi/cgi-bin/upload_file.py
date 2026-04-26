#!/usr/bin/env python3
import os
import re
import html
from urllib.parse import parse_qs

UPLOAD_DIR = "/home/fahd/webserv/www/html/uploads"


def safe_filename(name):
    name = os.path.basename(name)
    return re.sub(r"[^A-Za-z0-9._-]", "_", name) or "upload.bin"


def ensure_upload_dir():
    if not os.path.isdir(UPLOAD_DIR):
        os.makedirs(UPLOAD_DIR)


def parse_content_length():
    try:
        return int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    except ValueError:
        return 0


def parse_multipart(content_type, body_bytes):
    m = re.search(r"boundary=([^;]+)", content_type)
    if not m:
        return None, "Missing multipart boundary"

    boundary = m.group(1).strip().strip('"')
    boundary_bytes = ("--" + boundary).encode("utf-8")

    parts = body_bytes.split(boundary_bytes)
    for part in parts:
        if not part or part in (b"--\r\n", b"--", b"\r\n"):
            continue

        # normalize edges
        if part.startswith(b"\r\n"):
            part = part[2:]
        if part.endswith(b"\r\n"):
            part = part[:-2]
        if part.endswith(b"--"):
            part = part[:-2]

        sep = b"\r\n\r\n"
        idx = part.find(sep)
        if idx == -1:
            continue

        header_block = part[:idx].decode("utf-8", "replace")
        payload = part[idx + len(sep):]

        disp = None
        for line in header_block.split("\r\n"):
            if line.lower().startswith("content-disposition:"):
                disp = line
                break

        if not disp:
            continue

        filename_match = re.search(r'filename="([^"]*)"', disp)
        if not filename_match:
            continue

        original_name = filename_match.group(1)
        if not original_name:
            continue

        filename = safe_filename(original_name)
        target = os.path.join(UPLOAD_DIR, filename)

        with open(target, "wb") as f:
            f.write(payload)

        return filename, None

    return None, "No file field found in multipart body"


def parse_urlencoded(body_bytes):
    form = parse_qs(body_bytes.decode("utf-8", "replace"))
    return form


def respond(title, message, ok=True):
    print("Content-Type: text/html\r\n")
    color = "#16a34a" if ok else "#dc2626"
    print("<!DOCTYPE html>")
    print("<html><head><meta charset='UTF-8'><title>%s</title></head><body style='font-family:Arial;background:#0f172a;color:#e2e8f0;padding:24px'>" % html.escape(title))
    print("<div style='max-width:700px;margin:0 auto;background:#1e293b;padding:20px;border-radius:10px;border-left:4px solid %s'>" % color)
    print("<h2>%s</h2>" % html.escape(title))
    print("<p>%s</p>" % message)
    print("<p><a href='/index.html' style='color:#38bdf8'>← Back to index</a></p>")
    print("</div></body></html>")


def main():
    ensure_upload_dir()

    method = os.environ.get("REQUEST_METHOD", "GET").upper()
    if method != "POST":
        respond("Upload Error", "Only POST is allowed for uploads.", ok=False)
        return

    content_type = os.environ.get("CONTENT_TYPE", "")
    length = parse_content_length()
    body = os.read(0, length) if length > 0 else b""

    if "multipart/form-data" in content_type:
        filename, error = parse_multipart(content_type, body)
        if error:
            respond("Upload Failed", html.escape(error), ok=False)
            return

        respond(
            "Upload Success",
            "Saved file: <code>%s</code> into <code>/uploads/</code>." % html.escape(filename),
            ok=True,
        )
        return

    # fallback for x-www-form-urlencoded
    form = parse_urlencoded(body)
    message = "Received non-multipart POST with fields: <code>%s</code>" % html.escape(", ".join(sorted(form.keys())) or "(none)")
    respond("Upload Handler", message, ok=True)


if __name__ == "__main__":
    main()
