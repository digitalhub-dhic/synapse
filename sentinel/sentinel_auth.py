"""Local teacher-console authentication helpers (standard library only)."""

import base64
import hashlib
import hmac
import json
import os
from pathlib import Path

ITERATIONS_MIN = 100_000
ITERATIONS_MAX = 2_000_000
PASSWORD_BYTES = 32
SESSION_SECRET_MIN_BYTES = 32


def get_security_dir(local_app_data=None):
    """Return the per-user security directory used by the Sentinel console."""
    root = local_app_data or os.environ.get("LOCALAPPDATA")
    if not root:
        root = str(Path.home() / "AppData" / "Local")
    return Path(root) / "SynapseSentinel" / "security"


def load_security_files(security_dir=None):
    """Load the cookie-signing key and password verifier; fail closed on errors."""
    directory = Path(security_dir) if security_dir is not None else get_security_dir()
    key_text = (directory / "web_session.key").read_text(encoding="ascii").strip()
    session_key = base64.b64decode(key_text, validate=True)
    if len(session_key) < SESSION_SECRET_MIN_BYTES:
        raise ValueError("The Sentinel session key is too short")

    credential = json.loads((directory / "console_credential.json").read_text(encoding="utf-8"))
    if credential.get("version") != 1 or credential.get("algorithm") != "PBKDF2-HMAC-SHA256":
        raise ValueError("Unsupported Sentinel credential format")
    iterations = credential.get("iterations")
    if not isinstance(iterations, int) or not ITERATIONS_MIN <= iterations <= ITERATIONS_MAX:
        raise ValueError("Invalid Sentinel credential iteration count")
    salt = base64.b64decode(credential["salt"], validate=True)
    expected = base64.b64decode(credential["hash"], validate=True)
    if len(salt) < 16 or len(expected) != PASSWORD_BYTES:
        raise ValueError("Invalid Sentinel credential material")
    return session_key, credential


def verify_console_password(candidate, credential):
    """Verify a teacher password using the per-user PBKDF2 credential."""
    if not isinstance(candidate, str) or not candidate or len(candidate) > 1024:
        return False
    try:
        salt = base64.b64decode(credential["salt"], validate=True)
        expected = base64.b64decode(credential["hash"], validate=True)
        iterations = credential["iterations"]
        if len(salt) < 16 or len(expected) != PASSWORD_BYTES:
            return False
        if not isinstance(iterations, int) or not ITERATIONS_MIN <= iterations <= ITERATIONS_MAX:
            return False
        actual = hashlib.pbkdf2_hmac("sha256", candidate.encode("utf-8"), salt, iterations, dklen=PASSWORD_BYTES)
        return hmac.compare_digest(actual, expected)
    except (KeyError, TypeError, ValueError, UnicodeError):
        return False
