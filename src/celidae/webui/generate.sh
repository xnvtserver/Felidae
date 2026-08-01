#!/usr/bin/env bash
set -euo pipefail
if ! command -v node >/dev/null 2>&1; then
    echo "Node.js is required to regenerate the Celidae visualizer assets."
    echo "Install it from https://nodejs.org, then re-run this script."
    exit 1
fi
node "$(dirname "$0")/generate-template.js"
