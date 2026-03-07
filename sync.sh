#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

usage() {
	echo -e "${YELLOW}Usage:${NC} $0 --left | --right"
}

require_cmd() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo -e "${RED}Error:${NC} required command not found: $1"
		exit 1
	fi
}

if [[ $# -ne 1 ]]; then
	usage
	exit 1
fi

case "$1" in
--left)
	firmware_file="nice_view_disp-sofle_choc_pro_left-zmk.uf2"
	;;
--right)
	firmware_file="nice_view_disp-sofle_choc_pro_right-zmk.uf2"
	;;
*)
	usage
	exit 1
	;;
esac

volume_path="/Volumes/KEEBART"
firmware_dir="/tmp/zmk-firmware"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_file="$firmware_dir/$firmware_file"

require_cmd gh
require_cmd find
require_cmd unzip

echo -e "${BLUE}Cleaning${NC} previous firmware folder to avoid conflicts..."
rm -rf "$firmware_dir"
mkdir -p "$firmware_dir"

download_dir="$firmware_dir/.artifact_download"
mkdir -p "$download_dir"

echo -e "${BLUE}Fetching${NC} latest run from workflow \"Build ZMK firmware\"..."
run_id="$(cd "$script_dir" && gh run list --workflow "Build ZMK firmware" --limit 1 --json databaseId --jq '.[0].databaseId')"

if [[ -z "$run_id" || "$run_id" == "null" ]]; then
	echo -e "${RED}Error:${NC} no runs found for workflow \"Build ZMK firmware\""
	exit 1
fi

echo -e "${BLUE}Downloading${NC} artifact \"firmware\" from run $run_id..."
cd "$script_dir" && gh run download "$run_id" --name firmware --dir "$download_dir"

zip_file="$(find "$download_dir" -type f -name '*.zip' -print -quit)"
if [[ -n "$zip_file" ]]; then
	echo -e "${BLUE}Unzipping${NC} $(basename "$zip_file")..."
	unzip -o "$zip_file" -d "$firmware_dir" >/dev/null
fi

downloaded_file="$(find "$firmware_dir" -type f -name "$firmware_file" -print -quit)"
if [[ -z "$downloaded_file" ]]; then
	echo -e "${RED}Error:${NC} $firmware_file was not found in downloaded artifact"
	exit 1
fi

cp -f "$downloaded_file" "$source_file"
echo -e "${GREEN}Firmware ready:${NC} $source_file"

echo -e "${BLUE}Waiting${NC} for $volume_path to be connected..."
while [[ ! -d "$volume_path" ]]; do
	sleep 1
done

echo -e "${BLUE}Volume detected.${NC}"
echo -e "${BLUE}Pausing${NC} 3 seconds to allow permissions to settle..."
sleep 3
echo -e "${BLUE}Copying${NC} $firmware_file..."
cp -f "$source_file" "$volume_path/"

echo -e "${GREEN}Success:${NC} copied $firmware_file to $volume_path"
