# AFTERIMAGE local interface

`server.py` is a dependency-free Python 3 HTTP adapter for the existing C++ CLI.
It binds to `127.0.0.1` only. `index.html`, `style.css`, and `app.js` form the
responsive browser UI. Start the C++ build first, then run `python3 web/server.py`
from the project root and visit `http://127.0.0.1:8765`.

The adapter accepts only small JSON requests, validates that index paths are
existing directories, uses subprocess argument arrays (not shell interpolation),
and applies operation timeouts. Do not change the bind address to `0.0.0.0`
without adding authentication and a proper security review.
