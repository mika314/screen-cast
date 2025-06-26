#!/usr/bin/env bash
set -euo pipefail

# 1) Make sure we have a device attached over USB
if ! adb devices | grep -qE '^[[:alnum:]]+\s+device$'; then
  echo "⚠️  No device found. Please connect via USB and try again."
  exit 1
fi

# 2) Grab the device's WLAN IP (adjust interface if yours isn’t wlan0)
device_ip=$(adb shell ip addr show wlan0 \
  | awk '/inet / {print $2}' \
  | cut -d/ -f1)

if [[ -z "$device_ip" ]]; then
  echo "⚠️  Could not determine device IP. Is Wi-Fi enabled on the device?"
  exit 1
fi

echo "✔️  Device IP is $device_ip"

# 3) Switch ADB to TCP mode on port 5555
adb tcpip 5555

# 4) Prompt before unplugging USB
read -p "🔌  Please disconnect the USB cable, then press Enter to continue…"

# 5) Connect over Wi-Fi
if adb connect "${device_ip}:5555" | grep -q 'connected'; then
  echo "✔️  Connected over Wi-Fi"
else
  echo "⚠️  Failed to connect to ${device_ip}:5555"
  exit 1
fi

# 6) Reverse local port (optional, for local web-server debugging on 8090)
adb reverse tcp:8090 tcp:8090

# 7) Launch your screen-cast binary (in the same dir as this script)
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$script_dir/screen-cast"
