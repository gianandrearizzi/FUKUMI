#!/bin/sh
# Fetches the Mutable Instruments code (MIT) that the engine builds on, at the tested commit.
set -e
cd "$(dirname "$0")/.."
COMMIT=f4cfa7697095a28d292bff758beb51deb49b5a86
if [ ! -d vendor/eurorack/.git ]; then
  mkdir -p vendor
  git clone --filter=blob:none https://github.com/pichenettes/eurorack.git vendor/eurorack
fi
git -C vendor/eurorack checkout -q $COMMIT
echo "vendor/eurorack a $COMMIT"
