#!/bin/bash
# One-shot: install SSH server on the Lubuntu live host + authorize the p8 key,
# so the operator can drive the terminal over LAN. Needs WiFi (apt).
set -e
echo "== installing openssh-server (needs internet) =="
sudo apt-get update
sudo apt-get install -y openssh-server
mkdir -p ~/.ssh
wget -qO- https://raw.githubusercontent.com/doublemint199/SpoofKPF/main/id.pub >> ~/.ssh/authorized_keys
chmod 700 ~/.ssh
chmod 600 ~/.ssh/authorized_keys
sudo systemctl enable --now ssh
echo "==================================="
echo " SSH READY"
echo " user : $(whoami)"
echo " IP   : $(hostname -I)"
echo "==================================="
