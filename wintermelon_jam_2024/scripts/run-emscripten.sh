#!/bin/bash

if [[ -z $(which emrun) ]]; then
  echo "No emrun detected.  Is Emscripten installed and in your PATH?"
  echo "Remember to follow the Emscripten SDK instructions."
  exit 1
fi

if [ ! -e ../build-wasm/index.html ]; then
  echo "Could not find built index.html file. Run build-emscripten.sh first."
  exit 1
fi

cd ../build-wasm && emrun index.html