Anroot - Android Terminal App Documentation

<div align="center">

https://crossberry.vercel.app/logo.png

Run Anroot Linux on Your Android Device Without Root Access

https://img.shields.io/badge/Termux-0.118.2-blue.svg
https://img.shields.io/badge/License-MIT-green.svg
https://img.shields.io/badge/Telegram-Join-blue

Developed by Crossberry

</div>

---

📑 Table of Contents

· Overview
· Features
· Prerequisites
· Installation Guide
· Configuration
· Usage
· Troubleshooting
· Uninstallation
· Technical Details
· Support
· License

---

🎯 Overview

Anroot is a powerful Android application that allows you to run Anroot Linux on your Android device using Termux as the base terminal emulator. The app combines the flexibility of Termux with the penetration testing capabilities of Anroot Linux, all without requiring root access on your device .

What is Anroot?

Anroot is a customized Termux-based solution with:

· Package Name: com.anroot
· Base Technology: Termux + PRoot isolation
· Target OS: Anroot Linux (command-line interface)

---

✨ Features

Feature Description
No Root Required Runs entirely in userspace using PRoot technology
Full Anroot Tools Access to Anroot Linux's extensive penetration testing toolkit
Custom Package Name Uses com.anroot instead of standard com.termux
Isolated Environment Complete separation from host Android system
Lightweight Minimal resource overhead compared to VM solutions
Persistent Storage Your tools and data persist across sessions 

---

📋 Prerequisites

Hardware Requirements

· Android 7.0 or higher
· Minimum 4GB free storage space
· 2GB RAM (recommended)
· Stable internet connection (WiFi recommended for initial setup)

Software Requirements

· Termux (custom build with com.anroot package)
· Anroot APK (custom build)
· VNC Viewer (optional, for GUI)

---

🔧 Installation Guide

Step 1: Install Custom Termux

Download and install the custom Termux build with package name com.anroot:

```bash
# Download custom Termux build
wget https://github.com/termux/termux-app/releases/download/v0.118.2/termux-app_v0.118.2+github-debug_arm64-v8a.apk

# Install via ADB or directly on device
adb install termux-app_v0.118.2+github-debug_arm64-v8a.apk
```

Note: You need a Termux build modified with package name com.anroot. Use termux-generator to create your own custom build .

Step 2: Install Anroot App

Download and install the Anroot APK from Crossberry.

Step 3: Initial Termux Setup

Open Termux and run the following commands:

```bash
# Update package repositories
pkg update -y && pkg upgrade -y

# Install required packages
pkg install -y wget curl proot tar openssl-tool

# Grant storage permission
termux-setup-storage
```

Step 4: Install Anroot Linux

Run the automated installation script:

```bash
# Download and execute Anroot installer
wget https://raw.githubusercontent.com/AndronixApp/AndronixOrigin/master/Installer/Anroot/kali.sh -O kali.sh
chmod +x kali.sh
bash kali.sh
```

The script will download approximately 500MB-1GB of files and set up the Anroot environment .

Step 5: Launch Anroot

After installation completes:

```bash
# Start Anroot Linux
./start-kali.sh
```

You should see the Anroot Linux prompt:

```bash
root@localhost:~#
```

---

⚙️ Configuration

Initial Anroot Setup

Once inside Anroot, update the system:

```bash
# Update package lists
apt update

# Upgrade all packages
apt upgrade -y

# Install basic tools
apt install -y git wget curl nano vim
```

Installing Additional Tools

```bash
# Install metasploit framework
apt install -y metasploit-framework

# Install nmap
apt install -y nmap

# Install sqlmap
apt install -y sqlmap

# Install hydra
apt install -y hydra
```

Network Configuration

```bash
# Check network interfaces
ifconfig

# Test connectivity
ping -c 4 google.com
```

Persistent Storage

Your Anroot environment is stored in:

```
/data/data/com.anroot/files/home/kali-arm64/
```

All changes persist automatically across sessions .

---

🚀 Usage

Basic Commands

Command Action
./start-kali.sh Start Anroot Linux
exit Exit Anroot session
apt install <package> Install new tools
apt update Update package lists
apt upgrade Upgrade installed packages

Running as Root (Optional)

If your device is rooted, you can run Termux with superuser privileges:

```bash
# Install tsu (Termux Superuser)
pkg install tsu

# Launch Termux with root
tsu

# Start Anroot as root
./start-kali.sh
```

Note: Some tools require root privileges to function properly .

Working with Files

```bash
# Access shared storage
cd /sdcard

# Copy files to Anroot environment
cp /sdcard/target.zip ~/

# Extract files
unzip target.zip
```

---

🔍 Troubleshooting

Issue 1: "Permission denied" when running scripts

Solution: Make scripts executable

```bash
chmod +x scriptname.sh
```

Issue 2: PulseAudio Errors

If you see Autospawn lock errors when starting Anroot:

Solution: Edit start-kali.sh and modify the pulseaudio line 

```bash
# Open the file
nano start-kali.sh

# Change this line:
pulseaudio --start --system

# To this:
pulseaudio=" --start --system"
```

Issue 3: Repository Connection Failed

Solution: Change Termux repositories

```bash
termux-change-repo
# Select a different mirror
pkg update
```

Issue 4: Out of Memory Errors

Solution: Increase swap or close background apps

```bash
# Check available memory
free -h

# Close unnecessary apps on your phone
```

Issue 5: Slow Performance

Optimization tips:

· Close background apps
· Use CLI only (avoid GUI)
· Install lighter alternatives for heavy tools
· Consider using a more powerful device 

---

🗑️ Uninstallation

Complete Removal

To completely remove Anroot from your device:

```bash
# Inside Termux, run uninstall script
wget https://raw.githubusercontent.com/AndronixApp/AndronixOrigin/master/Uninstall/Anroot/UNI-kali.sh
chmod +x UNI-kali.sh
bash UNI-kali.sh

# Remove Termux app
# Settings → Apps → Anroot Termux → Uninstall

# Remove Anroot helper app
# Settings → Apps → Anroot → Uninstall
```

Clean Up Storage

```bash
# Delete remaining files manually
rm -rf /data/data/com.anroot/files/home/kali-arm64/
rm -rf ~/kali.sh ~/start-kali.sh
```

---

🛠 Technical Details

Architecture

```
┌─────────────────────────────────────┐
│         Android OS                   │
│  ┌─────────────────────────────┐    │
│  │     Termux (com.anroot)        │    │
│  │  ┌───────────────────────┐  │    │
│  │  │    PRoot Layer         │  │    │
│  │  │  ┌─────────────────┐  │  │    │
│  │  │  │  Anroot Linux FS   │  │  │    │
│  │  │  └─────────────────┘  │  │    │
│  │  └───────────────────────┘  │    │
│  └─────────────────────────────┘    │
└─────────────────────────────────────┘
```

Key Components

· Termux: Terminal emulator and Linux environment 
· PRoot: User-space chroot implementation
· Anroot RootFS: Anroot Linux base filesystem
· Custom Package: Modified com.anroot identifier 

File Locations

Path Purpose
/data/data/com.anroot/files/home/ User home directory
/data/data/com.anroot/files/usr/ Termux system files
~/kali-arm64/ Anroot Linux installation
~/start-kali.sh Anroot startup script

---

💬 Support

Community Channels

· Telegram Group: https://t.me/crossberry369
· Developer Website: Crossberry
· GitHub Issues: Termux Issues

Common Resources

· Termux Wiki
· Anroot Linux Docs
· PRoot Documentation

Reporting Issues

When reporting issues, please include:

1. Android version
2. Device model
3. Termux version (pkg list-installed)
4. Error messages/logs
5. Steps to reproduce

---

📜 License

This project is based on open-source software:

· Termux: GPLv3 with additional terms
· Anroot Linux: Various open-source licenses
· Anroot Helper: MIT License

---

⚠️ Disclaimer

Anroot is intended for educational and security testing purposes only. Users are responsible for complying with all applicable laws and regulations. The developers assume no liability for misuse of this software.

---

<div align="center">

Made with ❤️ by Crossberry

⬆ Back to Top

</div>
