#!/bin/sh
# Recover the internal Goodix 53x5 sensor after USB re-enumeration.
# This is run by udev/systemd, not from the driver hot path.

set -eu

dev="${1:-}"

normalize_dev() {
  dev="$(printf '%s' "$1" | tr / -)"

  case "$dev" in
    ""|*[!A-Za-z0-9_.:-]*)
      return 1
      ;;
  esac

  # If an interface name was passed accidentally, normalize it to the device name.
  dev="${dev%%:1.*}"
  return 0
}

unbind_cdc_acm() {
  bound=0

  for iface in "$dev:1.0" "$dev:1.1"; do
    if [ -e "/sys/bus/usb/drivers/cdc_acm/$iface" ]; then
      bound=1
      printf '%s\n' "$iface" > /sys/bus/usb/drivers/cdc_acm/unbind 2>/dev/null || true
    fi
  done

  return "$bound"
}

recover_dev() {
  if ! normalize_dev "$1"; then
    return 0
  fi

  echo "Recovering Goodix 53x5 USB device $dev"

  # Interface binding can trail device enumeration. Keep polling until the
  # device is present and cdc_acm has stayed unbound briefly, but cap the wait.
  stable=0
  i=0
  while [ "$i" -lt 100 ]; do
    if [ -d "/sys/bus/usb/devices/$dev" ]; then
      if unbind_cdc_acm; then
        stable=$((stable + 1))
        if [ "$stable" -ge 20 ]; then
          break
        fi
      else
        stable=0
      fi
    fi

    i=$((i + 1))
    sleep 0.05
  done

  echo "Leaving fprintd.service state unchanged"
}

if [ -n "$dev" ]; then
  recover_dev "$dev"
  exit 0
fi

# Resume ordering path: no udev instance was supplied, so wait for the internal
# sensor to re-enumerate and recover it before releasing fprintd.service.
i=0
while [ "$i" -lt 100 ]; do
  found=0

  for path in /sys/bus/usb/devices/*; do
    vendor=
    product=

    [ -r "$path/idVendor" ] || continue
    read -r vendor < "$path/idVendor" || continue
    [ "$vendor" = "27c6" ] || continue

    [ -r "$path/idProduct" ] || continue
    read -r product < "$path/idProduct" || continue
    case "$product" in
      5335|5385|5395)
        found=1
        recover_dev "${path##*/}"
        ;;
    esac
  done

  if [ "$found" -eq 1 ]; then
    exit 0
  fi

  i=$((i + 1))
  sleep 0.05
done

echo "No Goodix 53x5 USB device appeared during resume recovery"
