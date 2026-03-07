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
firmware_dir="$HOME/Desktop/firmware"
source_file="$firmware_dir/$firmware_file"

if [[ ! -f "$source_file" ]]; then
	echo -e "${RED}Error:${NC} firmware file not found: $source_file"
	exit 1
fi

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
