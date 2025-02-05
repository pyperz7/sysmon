#!/bin/bash

SERVICE_NAME="sysmon"
EXEC_PATH="/home/$USER/projects/new_sysmon/sysmon"
SOURCE_PATH="/home/$USER/projects/new_sysmon/server.cpp"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
WWW_DIR="/home/$USER/projects/new_sysmon/www"
HTML_FILE="index.html"
ARGS="0.0.0.0 8443 server.crt server.key"

# ✅ Dynamically get the current user and group
USER=$(whoami)
GROUP=$(id -gn)

echo "Installing dependencies..."
sudo apt update && sudo apt install -y g++ make cmake libboost-all-dev libssl-dev

echo "Compiling server..."
g++ -std=c++17 -o $EXEC_PATH $SOURCE_PATH -lboost_system -lboost_thread -lssl -lcrypto -pthread
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed."
    exit 1
fi

# ✅ Ensure WWW directory exists and copy HTML file
echo "Setting up HTML files..."
mkdir -p $WWW_DIR
cp $HTML_FILE $WWW_DIR/

# ✅ Create systemd service file dynamically
echo "Creating systemd service file at $SERVICE_FILE..."
sudo bash -c "cat > $SERVICE_FILE" <<EOF
[Unit]
Description=System Monitor Service
After=network.target

[Service]
ExecStart=$EXEC_PATH $ARGS
WorkingDirectory=$(dirname $EXEC_PATH)
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
sudo chmod 644 $SERVICE_FILE

# ✅ Reload systemd
echo "Reloading systemd..."
sudo systemctl daemon-reload

# ✅ Enable and start the service
echo "Enabling $SERVICE_NAME service..."
sudo systemctl enable $SERVICE_NAME
echo "Starting $SERVICE_NAME service..."
sudo systemctl start $SERVICE_NAME

# ✅ Check service status
echo "Checking service status..."
sudo systemctl status $SERVICE_NAME --no-pager
echo "Installation complete. $SERVICE_NAME is now running."
