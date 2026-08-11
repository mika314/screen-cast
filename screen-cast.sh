#!/usr/bin/env bash
set -euo pipefail

# Prefer a USB-attached device; fall back to any authorized device.
usb_serial=$(adb devices -l | awk '/\sdevice\s/ && /usb:/ {print $1; exit}')
serial=${usb_serial:-$(adb devices | awk '/\sdevice$/ {print $1; exit}')}

if [[ -z "$serial" ]]; then
  echo "⚠️  No device found. Please connect via USB and try again."
  exit 1
fi

echo "✔️  Using device $serial"
adb=(adb -s "$serial")

echo
echo "Connection mode:"
echo "  1) Tethered (USB)"
echo "  2) Wi-Fi"
read -r -p "Choose [1/2]: " mode

case "$mode" in
  1|"")
    echo "✔️  Using tethered mode"
    ;;
  2)
    # Drop any leftover Wi-Fi adb sessions so commands don't hit "more than one device"
    while read -r other; do
      [[ -z "$other" || "$other" == "$serial" ]] && continue
      if [[ "$other" == *:* ]]; then
        echo "✔️  Disconnecting stale adb session $other"
        adb disconnect "$other" >/dev/null || true
      fi
    done < <(adb devices | awk '/\sdevice$/ {print $1}')

    # Grab the device's WLAN IP (adjust interface if yours isn’t wlan0)
    device_ip=$("${adb[@]}" shell ip addr show wlan0 \
      | awk '/inet / {print $2}' \
      | cut -d/ -f1 \
      | tr -d '\r')

    if [[ -z "$device_ip" ]]; then
      echo "⚠️  Could not determine device IP. Is Wi-Fi enabled on the device?"
      exit 1
    fi

    echo "✔️  Device IP is $device_ip"

    # Switch ADB to TCP mode on port 5555
    "${adb[@]}" tcpip 5555

    # Prompt before unplugging USB
    read -r -p "🔌  Please disconnect the USB cable, then press Enter to continue…"

    # Connect over Wi-Fi
    if adb connect "${device_ip}:5555" | grep -q 'connected'; then
      echo "✔️  Connected over Wi-Fi"
    else
      echo "⚠️  Failed to connect to ${device_ip}:5555"
      exit 1
    fi

    serial="${device_ip}:5555"
    adb=(adb -s "$serial")
    ;;
  *)
    echo "⚠️  Invalid choice. Enter 1 for tethered or 2 for Wi-Fi."
    exit 1
    ;;
esac

# Reverse local port (optional, for local web-server debugging on 8090)
"${adb[@]}" reverse tcp:8090 tcp:8090

echo "✔️  Open http://localhost:8090 on the Quest"

# Launch your screen-cast binary (in the same dir as this script)
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$script_dir/screen-cast"
