# #!/usr/bin/env python3
# import os
# import re
# import html
# from urllib.parse import parse_qs

# UPLOAD_DIR = "/home/fahd/webserv/www/html/uploads"


# def safe_filename(name):
#     name = os.path.basename(name)
#     return re.sub(r"[^A-Za-z0-9._-]", "_", name) or "upload.bin"


# def ensure_upload_dir():
#     if not os.path.isdir(UPLOAD_DIR):
#         os.makedirs(UPLOAD_DIR)


# def parse_content_length():
#     try:
#         return int(os.environ.get("CONTENT_LENGTH", "0") or "0")
#     except ValueError:
#         return 0


# def parse_multipart(content_type, body_bytes):
#     m = re.search(r"boundary=([^;]+)", content_type)
#     if not m:
#         return None, "Missing multipart boundary"

#     boundary = m.group(1).strip().strip('"')
#     boundary_bytes = ("--" + boundary).encode("utf-8")

#     parts = body_bytes.split(boundary_bytes)
#     for part in parts:
#         if not part or part in (b"--\r\n", b"--", b"\r\n"):
#             continue

#         # normalize edges
#         if part.startswith(b"\r\n"):
#             part = part[2:]
#         if part.endswith(b"\r\n"):
#             part = part[:-2]
#         if part.endswith(b"--"):
#             part = part[:-2]

#         sep = b"\r\n\r\n"
#         idx = part.find(sep)
#         if idx == -1:
#             continue

#         header_block = part[:idx].decode("utf-8", "replace")
#         payload = part[idx + len(sep):]

#         disp = None
#         for line in header_block.split("\r\n"):
#             if line.lower().startswith("content-disposition:"):
#                 disp = line
#                 break

#         if not disp:
#             continue

#         filename_match = re.search(r'filename="([^"]*)"', disp)
#         if not filename_match:
#             continue

#         original_name = filename_match.group(1)
#         if not original_name:
#             continue

#         filename = safe_filename(original_name)
#         target = os.path.join(UPLOAD_DIR, filename)

#         with open(target, "wb") as f:
#             f.write(payload)

#         return filename, None

#     return None, "No file field found in multipart body"


# def parse_urlencoded(body_bytes):
#     form = parse_qs(body_bytes.decode("utf-8", "replace"))
#     return form


# def respond(title, message, ok=True):
#     print("Content-Type: text/html\r\n")
#     color = "#16a34a" if ok else "#dc2626"
#     print("<!DOCTYPE html>")
#     print("<html><head><meta charset='UTF-8'><title>%s</title></head><body style='font-family:Arial;background:#0f172a;color:#e2e8f0;padding:24px'>" % html.escape(title))
#     print("<div style='max-width:700px;margin:0 auto;background:#1e293b;padding:20px;border-radius:10px;border-left:4px solid %s'>" % color)
#     print("<h2>%s</h2>" % html.escape(title))
#     print("<p>%s</p>" % message)
#     print("<p><a href='/index.html' style='color:#38bdf8'>← Back to index</a></p>")
#     print("</div></body></html>")


# def main():
#     ensure_upload_dir()

#     method = os.environ.get("REQUEST_METHOD", "GET").upper()
#     if method != "POST":
#         respond("Upload Error", "Only POST is allowed for uploads.", ok=False)
#         return

#     content_type = os.environ.get("CONTENT_TYPE", "")
#     length = parse_content_length()
#     body = os.read(0, length) if length > 0 else b""

#     if "multipart/form-data" in content_type:
#         filename, error = parse_multipart(content_type, body)
#         if error:
#             respond("Upload Failed", html.escape(error), ok=False)
#             return

#         respond(
#             "Upload Success",
#             "Saved file: <code>%s</code> into <code>/uploads/</code>." % html.escape(filename),
#             ok=True,
#         )
#         return

#     # fallback for x-www-form-urlencoded
#     form = parse_urlencoded(body)
#     message = "Received non-multipart POST with fields: <code>%s</code>" % html.escape(", ".join(sorted(form.keys())) or "(none)")
#     respond("Upload Handler", message, ok=True)


# if __name__ == "__main__":
#     main()


#!/usr/bin/env python3
import cgi
import os
import sys

# ============================================================
# Configuration - Use environment variable or relative path
# ============================================================
# Get upload directory from environment, or use relative path from script location
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_UPLOAD_DIR = os.path.join(SCRIPT_DIR, '..', 'uploads')

UPLOAD_DIR = os.environ.get('UPLOAD_DIR', DEFAULT_UPLOAD_DIR)

# Security: Resolve to absolute path and normalize
UPLOAD_DIR = os.path.realpath(os.path.abspath(UPLOAD_DIR))

# Maximum upload size in bytes (default: 10MB)
MAX_UPLOAD_SIZE = int(os.environ.get('MAX_UPLOAD_SIZE', 10 * 1024 * 1024))

# Allowed file extensions (empty = allow all)
ALLOWED_EXTENSIONS = os.environ.get('ALLOWED_EXTENSIONS', '').split(',') if os.environ.get('ALLOWED_EXTENSIONS') else []

# Chunk size for streaming writes (64KB)
CHUNK_SIZE = 64 * 1024


def secure_filename(filename):
    """
    Sanitize a filename to prevent path traversal attacks.
    
    - Strips directory components
    - Removes null bytes
    - Keeps only safe characters
    """
    if not filename:
        return None
    
    # Remove path components
    filename = os.path.basename(filename)
    
    # Remove null bytes (path traversal attempt)
    filename = filename.replace('\x00', '')
    
    # Remove leading dots (hidden files)
    while filename.startswith('.'):
        filename = filename[1:]
    
    # Reject empty filenames after sanitization
    if not filename:
        return None
    
    # Only allow alphanumeric, dots, dashes, underscores
    import re
    safe_name = re.sub(r'[^a-zA-Z0-9._-]', '_', filename)
    
    return safe_name


def check_extension(filename):
    """Check if file extension is allowed."""
    if not ALLOWED_EXTENSIONS:
        return True
    
    ext = os.path.splitext(filename)[1].lower()
    allowed = [e.lower().strip() for e in ALLOWED_EXTENSIONS if e.strip()]
    
    return ext in allowed


def error_response(title, message):
    """Send an HTML error response."""
    print("Content-Type: text/html\n")
    print(f"""<!DOCTYPE html>
<html>
<head><title>{title}</title></head>
<body style="font-family:Arial;background:#f4f4f4;padding:20px;">
    <h1 style="color:#dc2626;">{title}</h1>
    <p>{message}</p>
    <p><a href="/upload.html">Back to Upload</a></p>
</body>
</html>""")
    sys.exit(0)


# ============================================================
# Ensure upload directory exists with secure permissions
# ============================================================
try:
    os.makedirs(UPLOAD_DIR, mode=0o755, exist_ok=True)
except OSError as e:
    print("Content-Type: text/html\n")
    print(f"<h2>Server Configuration Error</h2><p>Cannot create upload directory: {e}</p>")
    sys.exit(0)

print("Content-Type: text/html")
print()

try:
    # Check content length before processing
    content_length = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
    if content_length > MAX_UPLOAD_SIZE:
        error_response("File Too Large", 
                      f"<p>Upload size ({content_length} bytes) exceeds maximum ({MAX_UPLOAD_SIZE} bytes).</p>")
    
    form = cgi.FieldStorage()
    
    if 'file' not in form:
        error_response("No File", "<p>Please select a file to upload.</p>")
    
    file_item = form['file']
    
    if not file_item.filename:
        error_response("No File", "<p>Please choose a file.</p>")
    
    # Sanitize filename
    original_filename = file_item.filename
    filename = secure_filename(original_filename)
    
    if filename is None:
        error_response("Invalid Filename", 
                      f"<p>The filename '{original_filename}' is not allowed.</p>")
    
    # Check extension
    if not check_extension(filename):
        allowed_str = ", ".join(ALLOWED_EXTENSIONS)
        error_response("Invalid File Type", 
                      f"<p>File extension not allowed. Allowed: {allowed_str}</p>")
    
    # Build safe filepath
    filepath = os.path.join(UPLOAD_DIR, filename)
    
    # Verify the final path is still within upload directory (symlink protection)
    real_filepath = os.path.realpath(filepath)
    if not real_filepath.startswith(os.path.realpath(UPLOAD_DIR)):
        error_response("Security Error", "<p>Invalid file path detected.</p>")
    
    # Handle filename collisions
    base, ext = os.path.splitext(filename)
    counter = 1
    while os.path.exists(filepath):
        filename = f"{base}_{counter}{ext}"
        filepath = os.path.join(UPLOAD_DIR, filename)
        counter += 1
        if counter > 1000:  # Safety limit
            error_response("Error", "<p>Too many files with the same name.</p>")
    
    # ============================================================
    # Stream file to disk in chunks (prevents memory exhaustion)
    # ============================================================
    bytes_written = 0
    with open(filepath, 'wb') as f:
        while True:
            chunk = file_item.file.read(CHUNK_SIZE)
            if not chunk:
                break
            
            bytes_written += len(chunk)
            
            # Enforce size limit during streaming
            if bytes_written > MAX_UPLOAD_SIZE:
                f.close()
                os.unlink(filepath)  # Delete partial file
                error_response("File Too Large",
                             f"<p>File exceeds maximum size of {MAX_UPLOAD_SIZE} bytes.</p>")
            
            f.write(chunk)
    
    file_size = bytes_written
    
    # Success response
    safe_original = original_filename.replace('<', '&lt;').replace('>', '&gt;')
    safe_filename_display = filename.replace('<', '&lt;').replace('>', '&gt;')
    
    print(f"""<!DOCTYPE html>
<html>
<head><title>Upload Successful</title></head>
<body style="font-family:Arial;background:#f4f4f4;padding:20px;">
    <h2 style="color:#16a34a;">Upload successful!</h2>
    <p><strong>Original filename:</strong> {safe_original}</p>
    <p><strong>Saved as:</strong> {safe_filename_display}</p>
    <p><strong>Size:</strong> {file_size:,} bytes</p>
    <p><a href="/uploads/{safe_filename_display}">View file</a></p>
    <p><a href="/upload.html">Upload another file</a></p>
</body>
</html>""")
    
except Exception as e:
    print(f"""<!DOCTYPE html>
<html>
<head><title>Upload Failed</title></head>
<body style="font-family:Arial;background:#f4f4f4;padding:20px;">
    <h2 style="color:#dc2626;">Upload failed</h2>
    <p>Error: {str(e)}</p>
    <p><a href="/upload.html">Try again</a></p>
</body>
</html>""")