#!/usr/bin/env python3
"""
Patch Anroot bootstrap zips to:
1. Replace com.termux with com.anroot in all files (text and ELF binaries)
2. Inject custom Anroot scripts and configurations

Usage: python3 patch-bootstrap.py <zip_file> [zip_file ...]
"""

import os
import sys
import zipfile

OLD_PREFIX = "com.termux"
NEW_PREFIX = "com.anroot"
OLD_PATH = "/data/data/com.termux"
NEW_PATH = "/data/data/com.anroot"


# ============================================================================
# dpkg-wrap v2 - Properly rewrites com.termux paths in .deb packages
# Uses dpkg-deb -R to extract, renames directories, dpkg-deb -b to rebuild
# Does NOT use the 'file' command (not in bootstrap)
# ============================================================================
DPKG_WRAP = r'''#!/data/data/com.anroot/files/usr/bin/sh
# dpkg-wrap v2 - Rewrite com.termux→com.anroot paths in .deb packages
# Installed as $PREFIX/bin/dpkg, original binary saved as $PREFIX/bin/dpkg.bin

export PREFIX=/data/data/com.anroot/files/usr
DPKG_BIN="$PREFIX/bin/dpkg.bin"

if [ ! -x "$DPKG_BIN" ]; then
    if [ -x "$PREFIX/bin/dpkg.orig" ]; then
        DPKG_BIN="$PREFIX/bin/dpkg.orig"
    else
        echo "dpkg-wrap: ERROR: No dpkg binary found!" >&2
        exit 1
    fi
fi

# Separate .deb files from other args
DEB_FILES=""
OTHER_ARGS=""
for arg in "$@"; do
    case "$arg" in
        *.deb)
            if [ -f "$arg" ]; then
                DEB_FILES="$DEB_FILES $arg"
            else
                OTHER_ARGS="$OTHER_ARGS $arg"
            fi
            ;;
        *)
            OTHER_ARGS="$OTHER_ARGS $arg"
            ;;
    esac
done

# No .deb files → pass through to real dpkg
if [ -z "$DEB_FILES" ]; then
    exec "$DPKG_BIN" "$@"
fi

# Process each .deb file
RESULT=0
for deb in $DEB_FILES; do
    # Quick check: does this .deb contain com.termux paths in the tar listing?
    NEEDS_REWRITE=0
    if dpkg-deb --fsys-tarfile "$deb" 2>/dev/null | tar -tf - 2>/dev/null | grep -q "com\.termux"; then
        NEEDS_REWRITE=1
    fi

    if [ $NEEDS_REWRITE -eq 0 ]; then
        # No rewriting needed, install directly
        "$DPKG_BIN" $OTHER_ARGS "$deb"
        RESULT=$?
        continue
    fi

    # Need to rewrite paths in the .deb
    WRAP_TMP=$(mktemp -d "$PREFIX/tmp/dpkg-wrap.XXXXXX")
    if [ -z "$WRAP_TMP" ] || [ ! -d "$WRAP_TMP" ]; then
        "$DPKG_BIN" $OTHER_ARGS "$deb"
        RESULT=$?
        continue
    fi

    EXTRACT="$WRAP_TMP/extract"
    mkdir -p "$EXTRACT"

    # Extract the .deb (control + data) using dpkg-deb -R
    if ! dpkg-deb -R "$deb" "$EXTRACT" 2>/dev/null; then
        rm -rf "$WRAP_TMP"
        "$DPKG_BIN" $OTHER_ARGS "$deb"
        RESULT=$?
        continue
    fi

    # Step 1: Rename com.termux directories → com.anroot in extracted data
    # The data is under $EXTRACT/data/data/com.termux/...
    if [ -d "$EXTRACT/data/data/com.termux" ]; then
        mkdir -p "$EXTRACT/data/data/com.anroot"
        for item in "$EXTRACT/data/data/com.termux"/*; do
            [ -e "$item" ] && mv -f "$item" "$EXTRACT/data/data/com.anroot/" 2>/dev/null
        done
        rm -rf "$EXTRACT/data/data/com.termux"
    fi

    # Step 2: Rewrite paths in DEBIAN control files
    if [ -d "$EXTRACT/DEBIAN" ]; then
        for ctrl_file in "$EXTRACT/DEBIAN"/*; do
            if [ -f "$ctrl_file" ]; then
                sed -i 's|com\.termux|com.anroot|g;s|/data/data/com\.termux|/data/data/com.anroot|g' "$ctrl_file" 2>/dev/null
            fi
        done
    fi

    # Step 3: Rewrite paths in known text file types in the data
    # Only process files that are likely text (avoid corrupting binaries)
    find "$EXTRACT" -path "$EXTRACT/DEBIAN" -prune -o -type f \( \
        -name "*.sh" -o -name "*.list" -o -name "*.md5sums" -o \
        -name "*.conffiles" -o -name "*.config" -o -name "*.templates" -o \
        -name "*.postinst" -o -name "*.preinst" -o -name "*.postrm" -o \
        -name "*.prerm" -o -name "*.pc" -o -name "*.la" -o -name "*.h" -o \
        -name "*.cmake" -o -name "*.py" -o -name "*.pl" -o -name "*.rb" -o \
        -name "*.conf" -o -name "*.cfg" -o -name "*.txt" -o \
        -name "*.properties" -o -name "*.xml" -o -name "*.json" \
    \) -print 2>/dev/null | while read f; do
        sed -i 's|com\.termux|com.anroot|g;s|/data/data/com\.termux|/data/data/com.anroot|g' "$f" 2>/dev/null
    done

    # Step 4: Fix symlinks pointing to com.termux paths
    find "$EXTRACT" -type l 2>/dev/null | while read link; do
        target=$(readlink "$link" 2>/dev/null)
        if echo "$target" | grep -q "com\.termux"; then
            new_target=$(echo "$target" | sed 's|com\.termux|com.anroot|g')
            rm -f "$link" 2>/dev/null
            ln -sf "$new_target" "$link" 2>/dev/null
        fi
    done

    # Step 5: Rebuild the .deb
    NEW_DEB="$WRAP_TMP/modified.deb"
    if dpkg-deb -b "$EXTRACT" "$NEW_DEB" 2>/dev/null; then
        "$DPKG_BIN" $OTHER_ARGS "$NEW_DEB"
        RESULT=$?
    else
        # Rebuild failed, try installing original as last resort
        "$DPKG_BIN" $OTHER_ARGS "$deb"
        RESULT=$?
    fi

    rm -rf "$WRAP_TMP"
done

exit $RESULT
'''


# ============================================================================
# First-boot setup script
# ============================================================================
ANROOT_FIRST_BOOT = r'''#!/data/data/com.anroot/files/usr/bin/sh
# anroot-first-boot v4 - First boot setup for Anroot
# Installs proot-distro, Debian, and configures auto-login

export PREFIX=/data/data/com.anroot/files/usr
export HOME=/data/data/com.anroot/files/home
export PATH=$PREFIX/bin:$PATH
export TMPDIR=$PREFIX/tmp
export LD_LIBRARY_PATH=$PREFIX/lib

LOG="[Anroot Setup]"
echo "$LOG === Anroot First Boot Setup Started ==="
echo "$LOG Date: $(date)"
echo "$LOG Prefix: $PREFIX"

# --- Step 1: Setup dpkg wrapper ---
echo "$LOG Setting up dpkg wrapper..."
if [ -f "$PREFIX/bin/dpkg-wrap" ] && [ -f "$PREFIX/bin/dpkg" ]; then
    # Save original dpkg binary as dpkg.bin
    if [ ! -f "$PREFIX/bin/dpkg.bin" ]; then
        cp "$PREFIX/bin/dpkg" "$PREFIX/bin/dpkg.bin"
        chmod 700 "$PREFIX/bin/dpkg.bin"
    fi
    # Replace dpkg with our wrapper
    cp "$PREFIX/bin/dpkg-wrap" "$PREFIX/bin/dpkg"
    chmod 700 "$PREFIX/bin/dpkg"
    echo "$LOG dpkg wrapper installed successfully."
else
    echo "$LOG WARNING: dpkg-wrap not found, skipping wrapper setup"
fi

# --- Step 2: Setup Anroot welcome banner ---
echo "$LOG Setting up welcome banner..."
cat > "$PREFIX/etc/motd" << 'MOTDEOF'
  ___                         ____            _
 / _ \ _ __   ___ _ __ __ _  |  _ \ ___  ___| |_ ___
| |_| | '_ \ / _ \ '__/ _` | | |_) / _ \/ __| __/ __|
|  _  | |_) |  __/ | | (_| | |  _ <  __/\__ \ |_\__ \
|_| |_| .__/ \___|_|  \__,_| |_| \_\___||___/\__|___/
      |_|
  Anroot - Linux on Android

  Website: https://crossberry.vercel.app
  GitHub:  https://github.com/grand369grand-lgtm/anroot
MOTDEOF
chmod 644 "$PREFIX/etc/motd"

# --- Step 3: Setup auto-login to Debian ---
echo "$LOG Setting up auto-login to Debian..."
mkdir -p "$PREFIX/etc/profile.d"
cat > "$PREFIX/etc/profile.d/anroot-autologin.sh" << 'AUTOEOF'
#!/data/data/com.anroot/files/usr/bin/sh
# Auto-login to Debian proot on every shell start

# Skip if already inside Debian
if [ -n "$ANROOT_DEBIAN_ACTIVE" ]; then
    return 0
fi

# Check if first-boot setup is still running
if [ -f "$PREFIX/ANROOT_FIRST_BOOT" ]; then
    echo ""
    echo "=== Anroot First-Time Setup ==="
    echo "Setup is still running in the background..."
    echo "Please wait for it to complete. This may take a few minutes."
    echo ""
    # Wait for first-boot to finish (max 10 minutes)
    WAITED=0
    while [ -f "$PREFIX/ANROOT_FIRST_BOOT" ] && [ $WAITED -lt 600 ]; do
        sleep 5
        WAITED=$((WAITED + 5))
        echo -n "."
    done
    echo ""
fi

# Check if Debian is installed
DEBIAN_ROOTFS="$PREFIX/var/proot-distro/installed-rootfs/debian"
if [ -d "$DEBIAN_ROOTFS" ]; then
    export ANROOT_DEBIAN_ACTIVE=1

    # Build bind mounts for storage access
    STORAGE_BINDS=""
    if [ -d "$HOME/storage" ]; then
        STORAGE_BINDS="--bind $HOME/storage:/root/storage"
    fi
    if [ -d "/sdcard" ]; then
        STORAGE_BINDS="$STORAGE_BINDS --bind /sdcard:/root/sdcard"
    fi

    # Clear screen and enter Debian
    clear
    exec proot-distro login debian $STORAGE_BINDS
else
    # Debian not installed - offer to run setup
    echo ""
    echo "=== Welcome to Anroot ==="
    echo "Debian is not installed yet."
    echo "Run 'anroot-first-boot' to set up Debian."
    echo ""
fi
AUTOEOF
chmod 700 "$PREFIX/etc/profile.d/anroot-autologin.sh"

# --- Step 4: Create marker that first-boot is running ---
touch "$PREFIX/ANROOT_FIRST_BOOT"

# --- Step 5: Install proot-distro using apt (NOT pkg) ---
echo "$LOG Installing proot-distro..."
echo "$LOG Running apt update..."
apt update 2>&1 | tail -3

echo "$LOG Running apt install proot-distro..."
apt install -y proot-distro 2>&1 | tail -5

# Verify proot-distro is available
if ! command -v proot-distro >/dev/null 2>&1; then
    echo "$LOG ERROR: proot-distro installation failed!"
    echo "$LOG You can try manually later: apt update && apt install proot-distro"
    rm -f "$PREFIX/ANROOT_FIRST_BOOT"
    exit 1
fi
echo "$LOG proot-distro installed successfully."

# --- Step 6: Install Debian ---
echo "$LOG Installing Debian (this may take several minutes)..."
proot-distro install debian 2>&1 | tail -10

# Verify Debian installation
DEBIAN_ROOTFS="$PREFIX/var/proot-distro/installed-rootfs/debian"
if [ ! -d "$DEBIAN_ROOTFS" ]; then
    echo "$LOG ERROR: Debian rootfs not found after installation!"
    echo "$LOG Try manually: proot-distro install debian"
    rm -f "$PREFIX/ANROOT_FIRST_BOOT"
    exit 1
fi
echo "$LOG Debian installed successfully."

# --- Step 7: Install essential packages inside Debian ---
echo "$LOG Installing essential packages inside Debian..."
proot-distro login debian -- apt update 2>&1 | tail -3
proot-distro login debian -- apt install -y sudo ncurses-term nano curl wget 2>&1 | tail -5

# --- Step 8: Install Anroot custom commands inside Debian ---
echo "$LOG Installing Anroot commands inside Debian..."
mkdir -p "$DEBIAN_ROOTFS/usr/local/bin"

# anroot-setup-storage
cat > "$DEBIAN_ROOTFS/usr/local/bin/anroot-setup-storage" << 'CMDEOF'
#!/bin/sh
echo "=== Anroot Storage Setup ==="
echo "Storage is automatically available at:"
echo "  /root/storage  - Anroot storage symlinks"
echo "  /root/sdcard   - External storage (/sdcard)"
echo ""
echo "If storage is not accessible, exit Debian (type 'exit'),"
echo "then run 'termux-setup-storage' from the Anroot shell."
CMDEOF
chmod 755 "$DEBIAN_ROOTFS/usr/local/bin/anroot-setup-storage"

# anroot-info
cat > "$DEBIAN_ROOTFS/usr/local/bin/anroot-info" << 'CMDEOF'
#!/bin/sh
echo "=== Anroot System Information ==="
echo "App:       Anroot (Debian on Android)"
echo "Website:   https://crossberry.vercel.app"
echo "GitHub:    https://github.com/grand369grand-lgtm/anroot"
echo "Debian:    $(cat /etc/debian_version 2>/dev/null || echo 'Unknown')"
echo "Kernel:    $(uname -r)"
echo "Arch:      $(uname -m)"
CMDEOF
chmod 755 "$DEBIAN_ROOTFS/usr/local/bin/anroot-info"

# anroot-update
cat > "$DEBIAN_ROOTFS/usr/local/bin/anroot-update" << 'CMDEOF'
#!/bin/sh
echo "=== Updating Debian packages ==="
apt update && apt upgrade -y
CMDEOF
chmod 755 "$DEBIAN_ROOTFS/usr/local/bin/anroot-update"

# anroot-shell
cat > "$DEBIAN_ROOTFS/usr/local/bin/anroot-shell" << 'CMDEOF'
#!/bin/sh
echo "Exiting Debian proot. Start a new session to return."
exit 0
CMDEOF
chmod 755 "$DEBIAN_ROOTFS/usr/local/bin/anroot-shell"

# --- Step 9: Setup Debian /root/.bashrc ---
cat > "$DEBIAN_ROOTFS/root/.bashrc" << 'BASHRC'
# ~/.bashrc: Anroot Debian shell
case $- in *i*) ;; *) return;; esac
HISTCONTROL=ignoreboth
shopt -s histappend
HISTSIZE=1000
HISTFILESIZE=2000
shopt -s checkwinsize
PS1='\[\033[01;32m\]root@anroot\[\033[00m\]:\[\033[01;34m\]\w\[\033[00m\]# '
alias ll='ls -alF'
alias la='ls -A'
alias l='ls -CF'
BASHRC
chmod 644 "$DEBIAN_ROOTFS/root/.bashrc"

# --- Done ---
rm -f "$PREFIX/ANROOT_FIRST_BOOT"
echo "$LOG === Anroot First Boot Setup Complete! ==="
echo "$LOG Restart the app to auto-login to Debian."
'''


# ============================================================================
# Other injected scripts
# ============================================================================
ANROOT_PKG = r'''#!/data/data/com.anroot/files/usr/bin/sh
export PREFIX=/data/data/com.anroot/files/usr
exec "$PREFIX/bin/pkg" "$@"
'''

ANROOT_CHANGE_REPO = r'''#!/data/data/com.anroot/files/usr/bin/sh
export PREFIX=/data/data/com.anroot/files/usr
if command -v termux-change-repo >/dev/null 2>&1; then
    exec termux-change-repo "$@"
else
    echo "termux-change-repo not found"
    exit 1
fi
'''

ANROOT_SETUP_STORAGE = r'''#!/data/data/com.anroot/files/usr/bin/sh
export PREFIX=/data/data/com.anroot/files/usr
if command -v termux-setup-storage >/dev/null 2>&1; then
    exec termux-setup-storage "$@"
else
    echo "Requesting storage access..."
    echo "Storage will be available at ~/storage/"
fi
'''

ANROOT_SETUP_PACKAGE_MANAGER = r'''#!/data/data/com.anroot/files/usr/bin/sh
export PREFIX=/data/data/com.anroot/files/usr
if command -v termux-setup-package-manager >/dev/null 2>&1; then
    exec termux-setup-package-manager "$@"
else
    mkdir -p $PREFIX/etc/apt/sources.list.d
    echo "deb https://packages.termux.dev/apt/termux-main stable main" > $PREFIX/etc/apt/sources.list
    apt update 2>/dev/null
fi
'''

ANROOT_PATH_SH = r'''#!/data/data/com.anroot/files/usr/bin/sh
export LD_LIBRARY_PATH=/data/data/com.anroot/files/usr/lib
clear
'''

INJECTED_FILES = {
    "bin/anroot-first-boot": ANROOT_FIRST_BOOT,
    "bin/dpkg-wrap": DPKG_WRAP,
    "bin/anroot-pkg": ANROOT_PKG,
    "bin/anroot-change-repo": ANROOT_CHANGE_REPO,
    "bin/anroot-setup-storage": ANROOT_SETUP_STORAGE,
    "bin/anroot-setup-package-manager": ANROOT_SETUP_PACKAGE_MANAGER,
    "etc/profile.d/anroot-path.sh": ANROOT_PATH_SH,
}


def is_elf(data):
    return data[:4] == b'\x7fELF'


def patch_elf_rpath(data):
    if not is_elf(data):
        return data
    old_bytes = OLD_PATH.encode('ascii')
    new_bytes = NEW_PATH.encode('ascii')
    if len(new_bytes) < len(old_bytes):
        padded_new = new_bytes + b'\x00' * (len(old_bytes) - len(new_bytes))
    else:
        padded_new = new_bytes[:len(old_bytes)]
    return data.replace(old_bytes, padded_new)


def patch_text(data):
    try:
        text = data.decode('utf-8', errors='replace')
        text = text.replace(OLD_PREFIX, NEW_PREFIX)
        text = text.replace(OLD_PATH, NEW_PATH)
        return text.encode('utf-8', errors='replace')
    except Exception:
        return data


def is_text_file(data, filename):
    text_extensions = {
        '.sh', '.bash', '.zsh', '.py', '.pl', '.rb', '.conf', '.cfg',
        '.txt', '.md', '.xml', '.json', '.yaml', '.yml', '.toml',
        '.properties', '.list', '.sources', '.installs', '.control',
        '.desc', '.pro', '.cmake', '.pc', '.la', '.header',
    }
    _, ext = os.path.splitext(filename)
    if ext.lower() in text_extensions:
        return True
    if data[:2] == b'#!':
        return True
    if b'\x00' in data[:8192]:
        return False
    try:
        data[:8192].decode('utf-8')
        return True
    except (UnicodeDecodeError, ValueError):
        return False


def patch_zip(zip_path):
    print(f"Patching {zip_path}...")
    with zipfile.ZipFile(zip_path, 'r') as zf:
        entries = zf.infolist()
        patched_entries = {}
        for entry in entries:
            if entry.is_dir():
                continue
            data = zf.read(entry.filename)
            if is_elf(data):
                patched_data = patch_elf_rpath(data)
                patch_type = "ELF"
            elif is_text_file(data, entry.filename):
                patched_data = patch_text(data)
                patch_type = "text"
            else:
                patched_data = data.replace(
                    OLD_PATH.encode('ascii'),
                    NEW_PATH.encode('ascii') + b'\x00' * (len(OLD_PATH) - len(NEW_PATH))
                )
                patched_data = patched_data.replace(
                    OLD_PREFIX.encode('ascii'),
                    NEW_PREFIX.encode('ascii') + b'\x00' * (len(OLD_PREFIX) - len(NEW_PREFIX))
                )
                patch_type = "binary"
            if patched_data != data:
                print(f"  Patched ({patch_type}): {entry.filename} ({len(data)} bytes)")
            patched_entries[entry.filename] = patched_data

    # Inject custom files
    for inject_path, content in INJECTED_FILES.items():
        content_bytes = content.encode('utf-8')
        patched_entries[inject_path] = content_bytes
        print(f"  Injected: {inject_path} ({len(content_bytes)} bytes)")

    # Write patched zip
    existing_files = set(e.filename for e in entries if not e.is_dir())
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        for entry in entries:
            if entry.is_dir():
                zf.writestr(entry, b'')
            elif entry.filename in patched_entries:
                zf.writestr(entry, patched_entries[entry.filename])
            else:
                zf.writestr(entry, b'')
        # Add new injected files
        for inject_path, content_bytes in INJECTED_FILES.items():
            if inject_path not in existing_files:
                zf.writestr(inject_path, content_bytes)
    print(f"Done patching {zip_path}")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <zip_file> [zip_file ...]")
        sys.exit(1)
    for zip_path in sys.argv[1:]:
        if not os.path.exists(zip_path):
            print(f"Error: {zip_path} not found")
            continue
        patch_zip(zip_path)


if __name__ == '__main__':
    main()
