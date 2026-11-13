#!/bin/bash
cd "$(dirname "$0")"
if [ ! -x "build/afterimage" ]; then
  echo "AFTERIMAGE engine is not built yet."
  echo "Building now with CMake..."
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
  if [ $? -ne 0 ]; then
    echo "Build failed. Press Enter to close..."
    read -r
    exit 1
  fi
fi

echo "=========================================================="
echo " AFTERIMAGE — Human Memory Retrieval System (v2.0)"
echo "=========================================================="
echo "Starting local server at http://127.0.0.1:8765 ..."

# Automatically launch browser on macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
  (sleep 1 && open "http://127.0.0.1:8765") &
fi

python3 web/server.py
