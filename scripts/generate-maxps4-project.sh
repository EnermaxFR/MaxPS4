#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-$PWD}"
APP="$ROOT/MaxPS4-iOS"
command -v xcodegen >/dev/null 2>&1 || brew install xcodegen
cd "$APP"
xcodegen generate --spec project.yml
xcodebuild -list -project MaxPS4.xcodeproj
