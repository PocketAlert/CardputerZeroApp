#!/bin/sh
# Sets the release version in app-builder.json. Merging the change into main
# publishes v<version> on GitHub and in the CardputerZero store.
#
#   scripts/bump-version.sh 1.1.0
#   scripts/bump-version.sh patch|minor|major
set -eu

cd "$(dirname "$0")/.."
current="$(jq -r .version app-builder.json)"
IFS=. read -r major minor patch <<EOV
$current
EOV

case "${1:-}" in
	patch) next="$major.$minor.$((patch + 1))" ;;
	minor) next="$major.$((minor + 1)).0" ;;
	major) next="$((major + 1)).0.0" ;;
	[0-9]*.[0-9]*.[0-9]*) next="$1" ;;
	*) echo "usage: $0 <x.y.z|patch|minor|major>  (current: $current)" >&2; exit 1 ;;
esac

if git rev-parse -q --verify "refs/tags/v$next" >/dev/null; then
	echo "v$next is already tagged" >&2
	exit 1
fi

tmp="$(mktemp)"
jq --arg v "$next" '.version = $v' app-builder.json > "$tmp"
cat "$tmp" > app-builder.json && rm -f "$tmp"
echo "version: $current -> $next"
echo "Next: git commit -am \"chore: release v$next\", then merge into main"
