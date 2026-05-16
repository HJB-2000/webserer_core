#!/usr/bin/env python3
"""
Shared paths and JSON persistence for CGI demos.

Runtime data lives under www/data/ (outside www/html/) so it is not served
as static files. Writes use a lock file plus temp+rename for atomicity.

Session isolation across ports
-------------------------------
Browsers scope cookies by domain only, NOT by port. So localhost:8080 and
localhost:9090 share the same cookie jar -- a login on port 9090 overwrites
the session cookie that port 8080 set.

The fix: session IDs are stored in sessions.json under a namespaced key:
    "{port}:{sid}"   e.g.  "8080:a9e0d2f5..."  and  "9090:a9e0d2f5..."

The browser still holds a plain sid in its cookie. Every script that reads
or writes a session must call session_key(sid) to get the namespaced key.
SERVER_PORT is a mandatory CGI/1.1 variable (RFC 3875 s4.1.15) so it is
always available without any config changes.
"""
import fcntl
import json
import os
import tempfile

# Single shared data directory (www/html/cgi-bin -> www/data)
_DATA_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), '..', '..', 'data')
)

SESSIONS_FILE = os.path.join(_DATA_DIR, 'sessions.json')
USERS_FILE    = os.path.join(_DATA_DIR, 'users.json')
TOYDB_FILE    = os.path.join(_DATA_DIR, 'toydb.json')
USER_LOG_FILE = os.path.join(_DATA_DIR, 'user_log.txt')


def session_key(sid):
    """
    Return the namespaced session key for the current server port.

    Stored as  "<port>:<sid>"  so sessions from different ports never
    collide even though the browser sends the same cookie value to both.
    """
    port = os.environ.get('SERVER_PORT', 'default')
    return f"{port}:{sid}"


def cookie_name():
    """
    Return a port-specific cookie name, e.g. 'session_id_8080'.

    Browsers scope cookies by domain only, not port. Using a different
    cookie name per port means each server has its own independent slot
    in the browser's cookie jar and logins never overwrite each other.
    """
    port = os.environ.get('SERVER_PORT', 'default')
    return f"session_id_{port}"


def ensure_parent_dir(path):
    d = os.path.dirname(os.path.abspath(path))
    if d:
        os.makedirs(d, exist_ok=True)


def load_json(path, default):
    if not os.path.exists(path):
        return default
    ensure_parent_dir(path)
    lock_path = path + '.lock'
    with open(lock_path, 'a') as lockf:
        fcntl.flock(lockf.fileno(), fcntl.LOCK_SH)
        try:
            with open(path, 'r') as f:
                return json.load(f)
        finally:
            fcntl.flock(lockf.fileno(), fcntl.LOCK_UN)


def save_json_atomic(path, data):
    """Exclusive lock, write to temp in same dir, atomic replace."""
    ensure_parent_dir(path)
    lock_path = path + '.lock'
    with open(lock_path, 'a') as lockf:
        fcntl.flock(lockf.fileno(), fcntl.LOCK_EX)
        try:
            d = os.path.dirname(os.path.abspath(path)) or '.'
            fd, tmp = tempfile.mkstemp(prefix='.tmp_', suffix='.json', dir=d, text=True)
            try:
                with os.fdopen(fd, 'w') as wf:
                    json.dump(data, wf, indent=2)
                    wf.flush()
                    os.fsync(wf.fileno())
                os.replace(tmp, path)
            except Exception:
                try:
                    os.unlink(tmp)
                except OSError:
                    pass
                raise
        finally:
            fcntl.flock(lockf.fileno(), fcntl.LOCK_UN)


def append_log_line(path, line):
    """Append one line to a log file (locked, UTF-8)."""
    ensure_parent_dir(path)
    if not line.endswith('\n'):
        line += '\n'
    with open(path, 'a', encoding='utf-8') as f:
        fcntl.flock(f.fileno(), fcntl.LOCK_EX)
        try:
            f.write(line)
            f.flush()
            os.fsync(f.fileno())
        finally:
            fcntl.flock(f.fileno(), fcntl.LOCK_UN)