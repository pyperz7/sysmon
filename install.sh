#!/bin/bash

# Get the directory of the script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SERVICE_NAME="sysmon"
EXEC_PATH="$SCRIPT_DIR/sysmon"
SOURCE_PATH="$SCRIPT_DIR/server.cpp"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
WWW_DIR="$SCRIPT_DIR/www"
HTML_FILE="$SCRIPT_DIR/index.html"
CA_FILE="$SCRIPT_DIR/ca.pem"
CERT_FILE="$SCRIPT_DIR/server.crt"
KEY_FILE="$SCRIPT_DIR/server.key"
ARGS="0.0.0.0 8443 $CERT_FILE $KEY_FILE"

# ✅ Dynamically get the current user and group
USER=$(whoami)
GROUP=$(id -gn)

echo "Installing dependencies..."
sudo apt update && sudo apt install -y g++ make cmake libboost-all-dev libssl-dev openssl

# ✅ Generate SSL certificates if missing
generate_ssl() {
    echo "Generating self-signed SSL certificates..."
    openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
        -keyout "$KEY_FILE" -out "$CERT_FILE" -subj "/CN=localhost"
    openssl req -new -key "$KEY_FILE" -out "$SCRIPT_DIR/server.csr" -subj "/CN=localhost"
    openssl x509 -req -days 365 -in "$SCRIPT_DIR/server.csr" -signkey "$KEY_FILE" -out "$CERT_FILE"
    cp "$CERT_FILE" "$CA_FILE"  # Use server.crt as CA file
    rm "$SCRIPT_DIR/server.csr"
}

if [[ ! -f "$CERT_FILE" || ! -f "$KEY_FILE" || ! -f "$CA_FILE" ]]; then
    generate_ssl
else
    echo "SSL certificates already exist. Skipping generation."
fi

# ✅ Ensure WWW directory exists and copy HTML file
echo "Setting up HTML files..."
mkdir -p "$WWW_DIR"
cp "$HTML_FILE" "$WWW_DIR/"

echo "Compiling server..."
g++ -std=c++17 -o "$EXEC_PATH" "$SOURCE_PATH" -lboost_system -lboost_thread -lssl -lcrypto -pthread
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed."
    exit 1
fi

# ✅ Create systemd service file dynamically
echo "Creating systemd service file at $SERVICE_FILE..."
sudo bash -c "cat > $SERVICE_FILE" <<EOF
[Unit]
Description=System Monitor Service
After=network.target

[Service]
ExecStart=$EXEC_PATH $ARGS
WorkingDirectory=$SCRIPT_DIR
Restart=always
User=$USER
Group=$GROUP
Environment=PATH=/usr/bin:/bin
LimitNOFILE=4096

[Install]
WantedBy=multi-user.target
EOF

# ✅ Set correct permissions
echo "Setting correct permissions..."
sudo chmod 644 "$SERVICE_FILE"
sudo chmod 600 "$KEY_FILE" "$CERT_FILE" "$CA_FILE"

# ✅ Reload systemd
echo "Reloading systemd..."
sudo systemctl daemon-reload

# ✅ Enable and start the service
echo "Enabling $SERVICE_NAME service..."
sudo systemctl enable "$SERVICE_NAME"
echo "Starting $SERVICE_NAME service..."
sudo systemctl start "$SERVICE_NAME"

# ✅ Check service status
echo "Checking service status..."
sudo systemctl status "$SERVICE_NAME" --no-pager
echo "Installation complete. $SERVICE_NAME is now running."
