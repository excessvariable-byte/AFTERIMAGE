#!/usr/bin/env python3
"""Convenience launcher for the AFTERIMAGE Memory Web Server."""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WEB_SERVER = ROOT / 'web' / 'server.py'

if not WEB_SERVER.exists():
    sys.exit(f"Error: Could not find web/server.py in {ROOT}")

os.execv(sys.executable, [sys.executable, str(WEB_SERVER)] + sys.argv[1:])
