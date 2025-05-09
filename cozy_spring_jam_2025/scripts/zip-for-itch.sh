#!/bin/bash

# Places zip in build directory.

if [ ! -e ../build-wasm/index.html ]; then
  echo "Could not find built index.html file. Run build-emscripten.sh first."
  exit 1
fi

set -eu

unset workdir
onexit() {
  if [ -n ${workdir-} ]; then
    rm -rf "$workdir"
  fi
}
trap onexit EXIT

workdir=$(mktemp --tmpdir -d gitzip.XXXXXX)

cp -a ../build-wasm/index.html ../build-wasm/index.data ../build-wasm/index.js ../build-wasm/index.wasm "$workdir"

pushd "$workdir"
git init
git config --local user.email "zip@example.com"
git config --local user.name "zip"
git add .
git commit -m "commit for zip"
popd

git archive --format=zip -o ../build-wasm/itch.zip --remote="$workdir" HEAD