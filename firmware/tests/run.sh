#!/bin/bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
temporary_dir=$(mktemp -d)
trap 'rm -rf -- "$temporary_dir"' EXIT
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=undefined -g \
    optical_encoder_test.cpp -o "$temporary_dir/optical_encoder_test"
"$temporary_dir/optical_encoder_test"