#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Linux ]]; then
    echo "This script is for Linux only." >&2
    exit 1
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
archive="$script_dir/dist/Wireless3DPrinter.zip"
cura_dir="${HOME:?HOME must be set}/.local/share/cura"

if [[ ! -f "$archive" ]]; then
    echo "Plugin archive not found: $archive. Build it first." >&2
    exit 1
fi
if [[ ! -d "$cura_dir" ]]; then
    echo "Cura user directory not found: $cura_dir" >&2
    exit 1
fi

versions=()
for directory in "$cura_dir"/*; do
    [[ -d "$directory" ]] || continue
    version=${directory##*/}
    if [[ "$version" =~ ^[0-9]+(\.[0-9]+)*$ ]]; then
        versions+=("$version")
    fi
done
if ((${#versions[@]} == 0)); then
    echo "No Cura version directory found in $cura_dir." >&2
    exit 1
fi
version=$(printf '%s\n' "${versions[@]}" | sort -V | tail -n 1)
plugins="$cura_dir/$version/plugins"
mkdir -p -- "$plugins"
stage=$(mktemp -d "$plugins/.wireless-3d-printer.XXXXXXXX")
target="$plugins/Wireless3DPrinter"
backup="$stage/previous"
restore=0
cleanup() {
    if ((restore)); then
        if [[ ! -e "$target" && ! -L "$target" ]]; then
            mv -- "$backup" "$target" || {
                echo "Could not restore the previous plugin; it remains at $backup." >&2
                return
            }
        else
            echo "Could not restore the previous plugin; it remains at $backup." >&2
            return
        fi
    fi
    rm -rf -- "$stage"
}
trap cleanup EXIT

unzip -q "$archive" -d "$stage/new"
if [[ ! -f "$stage/new/Wireless3DPrinter/plugin.json" ]]; then
    echo "Invalid plugin archive: Wireless3DPrinter/plugin.json is missing." >&2
    exit 1
fi
if [[ -e "$target" || -L "$target" ]]; then
    mv -- "$target" "$backup"
    restore=1
fi
mv -- "$stage/new/Wireless3DPrinter" "$target"
restore=0
echo "Successfully installed Wireless3DPrinter for Cura $version in $plugins. Restart Cura."
