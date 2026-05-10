#!/usr/bin/env python3
import cgi
import os
import sys

UPLOAD_DIR = '/home/fahd/www/html/uploads'

# Ensure upload directory exists and is writable
os.makedirs(UPLOAD_DIR, exist_ok=True)

print("Content-Type: text/html")
print()

try:
    form = cgi.FieldStorage()
    
    if 'file' not in form:
        print("<h2>No file uploaded</h2>")
        print("<p>Please select a file to upload.</p>")
        sys.exit(0)
    
    file_item = form['file']
    
    if not file_item.filename:
        print("<h2>No file selected</h2>")
        print("<p>Please choose a file.</p>")
        sys.exit(0)
    
    # Get the filename
    filename = os.path.basename(file_item.filename)
    filepath = os.path.join(UPLOAD_DIR, filename)
    
    # Save the file
    with open(filepath, 'wb') as f:
        f.write(file_item.file.read())
    
    file_size = os.path.getsize(filepath)
    
    print(f"<h2>✅ Upload successful!</h2>")
    print(f"<p>File: <strong>{filename}</strong></p>")
    print(f"<p>Size: {file_size} bytes</p>")
    print(f"<p>Location: {filepath}</p>")
    print(f"<p><a href='/uploads/{filename}'>View file</a></p>")
    
except Exception as e:
    print(f"<h2>❌ Upload failed</h2>")
    print(f"<p>Error: {str(e)}</p>")
