#!/bin/bash

# Get the directory of the script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SERVICE_NAME="sysmon"
EXEC_PATH="$SCRIPT_DIR/sysmon"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
CA_FILE="$SCRIPT_DIR/ca.pem"
CERT_FILE="$SCRIPT_DIR/server.crt"
KEY_FILE="$SCRIPT_DIR/server.key"

echo "Stopping $SERVICE_NAME service..."
sudo systemctl stop $SERVICE_NAME 2>/dev/null

echo "Disabling $SERVICE_NAME service..."
sudo systemctl disable $SERVICE_NAME 2>/dev/null

if [ -f "$SERVICE_FILE" ]; then
    echo "Removing service file: $SERVICE_FILE..."
    sudo rm "$SERVICE_FILE"
else
    echo "Service file not found: $SERVICE_FILE"
fi

if [ -f "$EXEC_PATH" ]; then
    echo "Removing compiled binary: $EXEC_PATH..."
    sudo rm "$EXEC_PATH"
else
    echo "Executable not found: $EXEC_PATH"
fi

if [[ -f "$CA_FILE" || -f "$CERT_FILE" || -f "$KEY_FILE" ]]; then
    echo "Removing SSL certificates..."
    sudo rm -f "$CA_FILE" "$CERT_FILE" "$KEY_FILE"
else
    echo "No SSL certificates found to remove."
fi

echo "Reloading systemd daemon..."
sudo systemctl daemon-reload

echo "Checking if $SERVICE_NAME is still active..."
if systemctl is-active --quiet $SERVICE_NAME; then
    echo "Error: $SERVICE_NAME is still running!"
else
    echo "$SERVICE_NAME successfully uninstalled."
fi

echo "✅ Uninstallation complete. 'www/' and 'index.html' were preserved."
