#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Prints the next release version (x.y.z, no "v") from the git history. Semantic Versioning:
#
#   - No tag "vX.Y.Z" yet: the first release is the version in the VERSION file.
#   - Else the version of the newest tag plus one bump, from the commits since that tag:
#       major  a commit title like "feat!: ..." / "fix(scope)!: ...", or "BREAKING CHANGE:" in a body
#       minor  a commit title that starts with "feat:" or "feat(scope):"
#       patch  everything else (every merge to main is a release)
#   - While the major version is 0, a breaking change bumps the minor version and a feature
#     bumps the patch version, so a 0.x line never jumps to 1.0.0 by accident. Release 1.0.0
#     by hand: put 1.0.0 in VERSION and tag it (see CONTRIBUTING.md).
#
# Run it from anywhere inside the repository. Needs the tags (git fetch --tags).
set -euo pipefail

root=$(git rev-parse --show-toplevel)
last=$(git -C "$root" tag --list 'v[0-9]*.[0-9]*.[0-9]*' --sort=-v:refname | head -n 1)

if [ -z "$last" ]; then
    tr -d '[:space:]' < "$root/VERSION"
    echo
    exit 0
fi

version=${last#v}
major=${version%%.*}; rest=${version#*.}; minor=${rest%%.*}; patch=${rest#*.}

# Patterns are in variables because bash does not parse "(" and ")" in [[ =~ ]] reliably.
breaking_title='^[a-z]+(\([^)]*\))?!:'
feature_title='^feat(\([^)]*\))?:'

bump=patch
while IFS= read -r -d '' commit; do
    title=${commit%%$'\n'*}
    if [[ $title =~ $breaking_title ]] || [[ $commit == *$'\n'BREAKING\ CHANGE:* ]] || [[ $commit == *$'\n'BREAKING-CHANGE:* ]]; then
        bump=major
        break
    elif [[ $title =~ $feature_title ]]; then
        bump=minor
    fi
done < <(git -C "$root" log --format='%B%x00' "$last..HEAD")

# A 0.x version moves one step down: breaking -> minor, everything else -> patch.
if [ "$major" -eq 0 ]; then
    case $bump in major) bump=minor ;; minor) bump=patch ;; esac
fi

case $bump in
    major) echo "$((major + 1)).0.0" ;;
    minor) echo "$major.$((minor + 1)).0" ;;
    patch) echo "$major.$minor.$((patch + 1))" ;;
esac
