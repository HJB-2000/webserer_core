#!/usr/bin/env python3
import html
import os
import sys
import re

# ============================================================
# Configuration
# ============================================================
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
UPLOAD_DIR = os.path.join(SCRIPT_DIR, '..', 'uploads')
os.makedirs(UPLOAD_DIR, exist_ok=True)

MAX_UPLOAD_SIZE = 30 * 1024 * 1024  # 30MB


def parse_multipart():
    """Simple multipart form-data parser."""
    content_type = os.environ.get('CONTENT_TYPE', '')
    try:
        content_length = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
    except ValueError:
        content_length = 0
    
    if content_length == 0:
        return None, None, b''
    
    # Read body
    body = sys.stdin.buffer.read(content_length)
    
    # Extract boundary (supports quoted values and ignores trailing params)
    boundary_match = re.search(r'boundary=(?:"([^"]+)"|([^;]+))', content_type)
    if not boundary_match:
        return None, None, body

    boundary = (boundary_match.group(1) or boundary_match.group(2)).strip().encode()
    
    # Split by boundary
    parts = body.split(b'--' + boundary)
    
    for part in parts:
        if b'Content-Disposition' not in part:
            continue
        
        # Extract filename
        filename_match = re.search(rb'filename="([^"]*)"', part)
        if not filename_match:
            continue
        
        filename = filename_match.group(1).decode('utf-8', 'replace')
        
        # Find header/body separator
        sep = part.find(b'\r\n\r\n')
        if sep == -1:
            continue
        
        file_content = part[sep + 4:]
        
        # Remove trailing \r\n before next boundary or end
        if file_content.endswith(b'\r\n'):
            file_content = file_content[:-2]
        
        return filename, file_content, body
    
    return None, None, body


def secure_filename(filename):
    """Sanitize filename."""
    filename = os.path.basename(filename)
    filename = re.sub(r'[^a-zA-Z0-9._-]', '_', filename)
    if not filename or filename.startswith('.'):
        filename = 'uploaded_file'
    return filename


# ============================================================
# Main
# ============================================================

print("Content-Type: text/html\r\n\r\n")

try:
    filename, content, raw_body = parse_multipart()
    
    if filename is None or content is None:
        print("<h2>No file uploaded</h2>")
        print("<p>Please select a file.</p>")
        sys.exit(0)
    
    if len(content) > MAX_UPLOAD_SIZE:
        print("<h2>File too large</h2>")
        print(f"<p>Size: {len(content)} bytes (max: {MAX_UPLOAD_SIZE})</p>")
        sys.exit(0)
    
    safe_name = secure_filename(filename)
    
    # Handle duplicates
    filepath = os.path.join(UPLOAD_DIR, safe_name)
    base, ext = os.path.splitext(safe_name)
    counter = 1
    while os.path.exists(filepath):
        filepath = os.path.join(UPLOAD_DIR, f"{base}_{counter}{ext}")
        counter += 1
    
    with open(filepath, 'wb') as f:
        f.write(content)
    
    safe_filename = html.escape(filename, quote=True)
    safe_saved = html.escape(os.path.basename(filepath), quote=True)

    print(f"""<!DOCTYPE html>
<html>
<head><title>Upload OK</title></head>
<body style="font-family:Arial;padding:20px;">
    <h2 style="color:green;">Upload Successful!</h2>
    <p><strong>File:</strong> {safe_filename}</p>
    <p><strong>Saved as:</strong> {safe_saved}</p>
    <p><strong>Size:</strong> {len(content):,} bytes</p>
    <p><a href="/uploads/">View uploads</a></p>
</body>
</html>""")

except Exception as e:
    print(f"<h2>Error</h2><p>{html.escape(str(e))}</p>")