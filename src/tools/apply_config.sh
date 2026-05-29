#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
if [ "${RAZUMDOM_CONFIG_DIR+x}" = x ]; then
    CONFIG_DIR=$RAZUMDOM_CONFIG_DIR
elif [ -d "$ROOT_DIR/configs" ]; then
    CONFIG_DIR="$ROOT_DIR/configs"
else
    CONFIG_DIR="$SCRIPT_DIR/../../configs/upgrade"
fi
PORT=${RAZUMDOM_MODBUS_PORT:-/dev/ttyRS485-1}
SERVICE=${RAZUMDOM_SERIAL_SERVICE:-wb-mqtt-serial}
TOOL="$SCRIPT_DIR/modbus_configure.py"

usage() {
    cat <<EOF
Usage: $0 <101|103|104|105|106|107|all>

Write and verify the selected dimmer configuration over $PORT.
The script stops $SERVICE for exclusive RS-485 access and restores it
when it was active before the operation.
EOF
}

config_for_address() {
    case "$1" in
    101) printf '%s\n' "$CONFIG_DIR/ud8-ddm845r.yaml" ;;
    103) printf '%s\n' "$CONFIG_DIR/ud9-ddm845r.yaml" ;;
    104) printf '%s\n' "$CONFIG_DIR/ud10-ddm845r.yaml" ;;
    105) printf '%s\n' "$CONFIG_DIR/ud11-ddl84r.yaml" ;;
    106) printf '%s\n' "$CONFIG_DIR/ud12-ddl84r.yaml" ;;
    107) printf '%s\n' "$CONFIG_DIR/ud13-ddl84r.yaml" ;;
    *)
        printf 'Unknown dimmer address: %s\n' "$1" >&2
        usage >&2
        return 2
        ;;
    esac
}

apply_one() {
    address=$1
    config=$(config_for_address "$address")
    if [ ! -r "$config" ]; then
        printf 'Configuration file is not readable: %s\n' "$config" >&2
        return 1
    fi

    printf 'Applying Modbus %s from %s\n' "$address" "$config"
    python3 "$TOOL" write --port "$PORT" --slave "$address" --config "$config"
    python3 "$TOOL" verify --port "$PORT" --slave "$address" --config "$config"
}

if [ "$#" -ne 1 ]; then
    usage >&2
    exit 2
fi

case "$1" in
-h|--help)
    usage
    exit 0
    ;;
all|101|103|104|105|106|107) ;;
*)
    config_for_address "$1" >/dev/null
    exit 2
    ;;
esac

if [ ! -x "$TOOL" ] && [ ! -r "$TOOL" ]; then
    printf 'Configuration tool is not readable: %s\n' "$TOOL" >&2
    exit 1
fi

service_was_active=0
restore_service() {
    if [ "$service_was_active" -eq 1 ]; then
        service "$SERVICE" start
    fi
}
trap restore_service EXIT
trap 'exit 130' HUP INT TERM

if systemctl is-active --quiet "$SERVICE"; then
    service_was_active=1
    service "$SERVICE" stop
fi

if [ "$1" = all ]; then
    for address in 101 103 104 105 106 107; do
        apply_one "$address"
    done
else
    apply_one "$1"
fi

printf 'Configuration apply and verify completed.\n'
