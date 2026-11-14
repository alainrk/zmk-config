#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

workflow="Build ZMK firmware"

usage() {
	echo -e "${YELLOW}Usage:${NC} $0 --left | --right [--main | --pr <number>]"
	echo
	echo "  --left, --right   which half to flash"
	echo "  --main            use the latest successful build on main"
	echo "  --pr <number>     use the latest successful build of that PR"
	echo
	echo "Without --main or --pr, the script asks you which build to use."
}

require_cmd() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo -e "${RED}Error:${NC} required command not found: $1"
		exit 1
	fi
}

side=""
source_kind=""
pr_number=""

while [[ $# -gt 0 ]]; do
	case "$1" in
	--left | --right)
		side="${1#--}"
		;;
	--main)
		source_kind="main"
		;;
	--pr)
		if [[ $# -lt 2 || ! "$2" =~ ^[0-9]+$ ]]; then
			echo -e "${RED}Error:${NC} --pr needs a PR number"
			usage
			exit 1
		fi
		source_kind="pr"
		pr_number="$2"
		shift
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		usage
		exit 1
		;;
	esac
	shift
done

if [[ -z "$side" ]]; then
	usage
	exit 1
fi

firmware_file="nice_view_disp-sofle_choc_pro_${side}-zmk.uf2"
volume_path="/Volumes/KEEBART"
firmware_dir="/tmp/zmk-firmware"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_file="$firmware_dir/$firmware_file"

require_cmd gh
require_cmd find
require_cmd unzip

cd "$script_dir"

# Ask which build to use when no --main or --pr flag was given.
if [[ -z "$source_kind" ]]; then
	echo -e "${BLUE}Fetching${NC} open PRs..."
	pr_lines="$(gh pr list --state open --json number,title,headRefName \
		--jq '.[] | "\(.number)\t\(.headRefName)\t\(.title)"')"

	echo
	echo "Which build do you want to flash?"
	echo "  0) latest build on main"
	pr_numbers=()
	i=1
	if [[ -n "$pr_lines" ]]; then
		while IFS=$'\t' read -r num branch title; do
			echo "  $i) PR #$num: $title ($branch)"
			pr_numbers+=("$num")
			i=$((i + 1))
		done <<<"$pr_lines"
	fi
	echo

	read -r -p "Choice [0]: " choice
	choice="${choice:-0}"
	if [[ ! "$choice" =~ ^[0-9]+$ || "$choice" -ge "$i" ]]; then
		echo -e "${RED}Error:${NC} invalid choice: $choice"
		exit 1
	fi

	if [[ "$choice" -eq 0 ]]; then
		source_kind="main"
	else
		source_kind="pr"
		pr_number="${pr_numbers[$((choice - 1))]}"
	fi
fi

# Work out which branch and commit to look for.
if [[ "$source_kind" == "main" ]]; then
	branch="main"
	expected_sha="$(gh api repos/{owner}/{repo}/commits/main --jq '.sha')"
	label="main"
else
	pr_info="$(gh pr view "$pr_number" --json headRefName,headRefOid,state \
		--jq '"\(.headRefName)\t\(.headRefOid)\t\(.state)"')"
	IFS=$'\t' read -r branch expected_sha pr_state <<<"$pr_info"
	label="PR #$pr_number ($branch)"
	if [[ "$pr_state" != "OPEN" ]]; then
		echo -e "${YELLOW}Warning:${NC} PR #$pr_number is $pr_state"
	fi
fi

echo -e "${BLUE}Looking for${NC} the latest successful build of $label..."
run_line="$(gh run list --workflow "$workflow" --branch "$branch" --status success --limit 1 \
	--json databaseId,headSha,createdAt \
	--jq '.[0] | select(. != null) | "\(.databaseId)\t\(.headSha)\t\(.createdAt)"')"

if [[ -z "$run_line" ]]; then
	echo -e "${RED}Error:${NC} no successful \"$workflow\" run found for $label"
	exit 1
fi
IFS=$'\t' read -r run_id run_sha run_date <<<"$run_line"

echo -e "${BLUE}Using${NC} run $run_id (commit ${run_sha:0:7}, built $run_date)"

# Warn if the newest commit has not been built (yet, or the build failed).
if [[ "$run_sha" != "$expected_sha" ]]; then
	echo -e "${YELLOW}Warning:${NC} the latest commit on $label is ${expected_sha:0:7}, but its build is not finished or did not pass."
	read -r -p "Flash the older build ${run_sha:0:7} anyway? [y/N]: " answer
	if [[ ! "$answer" =~ ^[Yy]$ ]]; then
		echo "Stopped."
		exit 1
	fi
fi

echo -e "${BLUE}Cleaning${NC} previous firmware folder to avoid conflicts..."
rm -rf "$firmware_dir"
mkdir -p "$firmware_dir"

download_dir="$firmware_dir/.artifact_download"
mkdir -p "$download_dir"

echo -e "${BLUE}Downloading${NC} artifact \"firmware\" from run $run_id..."
gh run download "$run_id" --name firmware --dir "$download_dir"

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
