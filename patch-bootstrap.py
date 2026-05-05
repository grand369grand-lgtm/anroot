#!/usr/bin/env python3
"""
Patch Anroot bootstrap zips to replace com.termux with com.anroot.

This script processes bootstrap zip files and:
1. Replaces all references to com.termux with com.anroot in both text files
   (scripts, configs) and ELF binaries (RPATH/RUNPATH entries).
2. Injects the libanroot-path-translate.so LD_PRELOAD library that intercepts
   filesystem calls and translates /data/data/com.termux/ → /data/data/com.anroot/
   so that packages from the official Termux repository can be installed.
3. Modifies the login script to include the path-translation library in LD_PRELOAD.

Usage: python3 patch-bootstrap.py <zip_file> [zip_file ...]
"""

import os
import sys
import struct
import tempfile
import shutil
import zipfile

# The old and new package names
OLD_PREFIX = "com.termux"
NEW_PREFIX = "com.anroot"
OLD_PATH = "/data/data/com.termux"
NEW_PATH = "/data/data/com.anroot"

# Architecture mapping from bootstrap zip filename to NDK ABI name
ARCH_MAP = {
    "aarch64": "arm64-v8a",
    "arm": "armeabi-v7a",
    "i686": "x86",
    "x86_64": "x86_64",
}


def is_elf(data):
    """Check if data starts with ELF magic bytes."""
    return data[:4] == b'\x7fELF'


def patch_elf_rpath(data):
    """
    Patch ELF binary to replace all com.termux references with com.anroot.
    
    Replaces both:
    - /data/data/com.termux -> /data/data/com.anroot (full data path)
    - com.termux -> com.anroot (standalone package name, e.g. in socket paths)
    
    Since the new strings are shorter, we pad the remaining bytes with nulls.
    This is safe because C strings are null-terminated.
    """
    if not is_elf(data):
        return data
    
    patched = data
    
    # First replace the full data path (longer match first to avoid partial overlap)
    old_path_bytes = OLD_PATH.encode('ascii')
    new_path_bytes = NEW_PATH.encode('ascii')
    if len(new_path_bytes) < len(old_path_bytes):
        padded_path = new_path_bytes + b'\x00' * (len(old_path_bytes) - len(new_path_bytes))
    else:
        padded_path = new_path_bytes[:len(old_path_bytes)]
    patched = patched.replace(old_path_bytes, padded_path)
    
    # Then replace standalone com.termux (package name in paths like apps/com.termux/...)
    old_prefix_bytes = OLD_PREFIX.encode('ascii')
    new_prefix_bytes = NEW_PREFIX.encode('ascii')
    if len(new_prefix_bytes) < len(old_prefix_bytes):
        padded_prefix = new_prefix_bytes + b'\x00' * (len(old_prefix_bytes) - len(new_prefix_bytes))
    else:
        padded_prefix = new_prefix_bytes[:len(old_prefix_bytes)]
    patched = patched.replace(old_prefix_bytes, padded_prefix)
    
    return patched


def patch_text(data):
    """
    Patch text files (scripts, configs) by replacing com.termux with com.anroot.
    For text files, the replacement can be shorter since there's no structural constraint.
    """
    try:
        text = data.decode('utf-8', errors='replace')
        # Replace the package name reference
        text = text.replace(OLD_PREFIX, NEW_PREFIX)
        # Also replace full paths
        text = text.replace(OLD_PATH, NEW_PATH)
        return text.encode('utf-8', errors='replace')
    except Exception:
        return data


def patch_motd(data):
    """
    Patch the MOTD (message of the day) file to replace user-visible "Termux" branding
    with "Anroot", while keeping command names and URLs intact for compatibility.
    """
    try:
        text = data.decode('utf-8', errors='replace')
        
        # First, do the standard com.termux → com.anroot replacement
        text = text.replace(OLD_PREFIX, NEW_PREFIX)
        text = text.replace(OLD_PATH, NEW_PATH)
        
        # Replace user-visible branding
        text = text.replace("Welcome to Termux!", "Welcome to Anroot!")
        
        # Replace the Termux docs/donate/community URL section with Anroot branding
        # Keep the URLs pointing to termux.dev since they're the official resources
        text = text.replace("Docs:       https://termux.dev/docs", "Docs:       https://termux.dev/docs")
        text = text.replace("Donate:     https://termux.dev/donate", "Donate:     https://termux.dev/donate")
        text = text.replace("Community:  https://termux.dev/community", "Community:  https://termux.dev/community")
        text = text.replace("Report issues at https://termux.dev/issues", "Report issues at https://termux.dev/issues")
        
        return text.encode('utf-8', errors='replace')
    except Exception:
        return data


def patch_login_script(data):
    """
    Patch the login script to include libanroot-path-translate.so in LD_PRELOAD.
    
    The login script sets LD_PRELOAD to the termux-exec library. We need to
    prepend our path-translation library so it's loaded first. This is critical
    because the path-translation library intercepts filesystem calls (mkdir, open, etc.)
    and translates /data/data/com.termux/ → /data/data/com.anroot/ so that
    packages from the official Termux repository can be installed via dpkg.
    """
    try:
        text = data.decode('utf-8', errors='replace')
        
        # First, do the standard com.termux → com.anroot replacement
        text = text.replace(OLD_PREFIX, NEW_PREFIX)
        text = text.replace(OLD_PATH, NEW_PATH)
        
        # Now modify the LD_PRELOAD setup to include our path-translation library
        # The login script has a section like:
        #   if [ -f "/data/data/com.anroot/files/usr/lib/libtermux-exec-ld-preload.so" ]; then
        #       export LD_PRELOAD="/data/data/com.anroot/files/usr/lib/libtermux-exec-ld-preload.so"
        #       ...
        #   elif [ -f "/data/data/com.anroot/files/usr/lib/libtermux-exec.so" ]; then
        #       export LD_PRELOAD="/data/data/com.anroot/files/usr/lib/libtermux-exec.so"
        #       ...
        #   fi
        #
        # We need to add libanroot-path-translate.so BEFORE the termux-exec library
        # in LD_PRELOAD, with a space separator.
        
        prefix = "/data/data/com.anroot/files/usr"
        
        # Replace the LD_PRELOAD line for libtermux-exec-ld-preload.so
        old_ld_preload_1 = f'export LD_PRELOAD="{prefix}/lib/libtermux-exec-ld-preload.so"'
        new_ld_preload_1 = f'export LD_PRELOAD="{prefix}/lib/libanroot-path-translate.so {prefix}/lib/libtermux-exec-ld-preload.so"'
        text = text.replace(old_ld_preload_1, new_ld_preload_1)
        
        # Replace the LD_PRELOAD line for libtermux-exec.so (fallback)
        old_ld_preload_2 = f'export LD_PRELOAD="{prefix}/lib/libtermux-exec.so"'
        new_ld_preload_2 = f'export LD_PRELOAD="{prefix}/lib/libanroot-path-translate.so {prefix}/lib/libtermux-exec.so"'
        text = text.replace(old_ld_preload_2, new_ld_preload_2)
        
        return text.encode('utf-8', errors='replace')
    except Exception:
        return data


def is_text_file(data, filename):
    """Determine if a file is likely a text file."""
    # Check by extension
    text_extensions = {
        '.sh', '.bash', '.zsh', '.py', '.pl', '.rb', '.conf', '.cfg',
        '.txt', '.md', '.xml', '.json', '.yaml', '.yml', '.toml',
        '.properties', '.list', '.sources', '.installs', '.control',
        '.desc', '.pro', '.cmake', '.pc', '.la', '.header',
    }
    _, ext = os.path.splitext(filename)
    if ext.lower() in text_extensions:
        return True
    
    # Check for shebang
    if data[:2] == b'#!':
        return True
    
    # Check if data contains null bytes (binary)
    if b'\x00' in data[:8192]:
        return False
    
    # Try to decode as UTF-8
    try:
        data[:8192].decode('utf-8')
        return True
    except (UnicodeDecodeError, ValueError):
        return False


def get_arch_from_filename(zip_path):
    """Extract the architecture from the bootstrap zip filename."""
    basename = os.path.basename(zip_path)
    # Filename format: bootstrap-<arch>.zip
    if basename.startswith("bootstrap-") and basename.endswith(".zip"):
        arch = basename[len("bootstrap-"):-len(".zip")]
        return arch
    return None


def find_path_translate_so(zip_path):
    """Find the libanroot-path-translate.so for the given bootstrap architecture."""
    arch = get_arch_from_filename(zip_path)
    if arch is None:
        print(f"  Warning: Could not determine architecture from filename, skipping injection")
        return None
    
    abi = ARCH_MAP.get(arch)
    if abi is None:
        print(f"  Warning: Unknown architecture '{arch}', skipping injection")
        return None
    
    # Look for the .so file relative to this script's location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    so_path = os.path.join(script_dir, "app", "src", "main", "cpp", "libanroot-path-translate", abi, "libanroot-path-translate.so")
    
    # Also try relative to the cpp directory
    if not os.path.exists(so_path):
        so_path = os.path.join(script_dir, "src", "main", "cpp", "libanroot-path-translate", abi, "libanroot-path-translate.so")
    
    if not os.path.exists(so_path):
        # Try a flat structure
        cpp_dir = os.path.join(script_dir, "app", "src", "main", "cpp")
        so_path = os.path.join(cpp_dir, f"libanroot-path-translate-{abi}.so")
    
    if os.path.exists(so_path):
        return so_path
    
    print(f"  Warning: Could not find libanroot-path-translate.so for {abi} at {so_path}")
    return None


def patch_zip(zip_path):
    """Patch a bootstrap zip file in place."""
    print(f"Patching {zip_path}...")
    
    # Read the original zip
    with zipfile.ZipFile(zip_path, 'r') as zf:
        entries = zf.infolist()
        patched_entries = {}
        
        for entry in entries:
            if entry.is_dir():
                continue
            
            data = zf.read(entry.filename)
            original_size = len(data)
            
            # Special handling for the login script
            if entry.filename == "bin/login":
                patched_data = patch_login_script(data)
                patch_type = "login"
            # Special handling for the MOTD file
            elif entry.filename == "etc/motd" or entry.filename == "etc/motd-playstore":
                patched_data = patch_motd(data)
                patch_type = "motd"
            elif is_elf(data):
                # Patch ELF binary
                patched_data = patch_elf_rpath(data)
                patch_type = "ELF"
            elif is_text_file(data, entry.filename):
                # Patch text file
                patched_data = patch_text(data)
                patch_type = "text"
            else:
                # For other binary files, try ELF-style byte replacement
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
                print(f"  Patched ({patch_type}): {entry.filename} ({original_size} bytes)")
            
            patched_entries[entry.filename] = patched_data
    
    # Inject the path-translation library
    so_path = find_path_translate_so(zip_path)
    if so_path:
        with open(so_path, 'rb') as f:
            so_data = f.read()
        # The library goes to $PREFIX/lib/libanroot-path-translate.so
        # In the zip, paths are relative to $PREFIX
        so_entry = "lib/libanroot-path-translate.so"
        patched_entries[so_entry] = so_data
        print(f"  Injected: {so_entry} ({len(so_data)} bytes) from {so_path}")
    
    # Write the patched zip
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        for entry in entries:
            if entry.is_dir():
                zf.writestr(entry, b'')
            elif entry.filename in patched_entries:
                zf.writestr(entry, patched_entries[entry.filename])
            else:
                zf.writestr(entry, patched_entries.get(entry.filename, b''))
        
        # Write any new entries that weren't in the original zip
        original_filenames = {e.filename for e in entries if not e.is_dir()}
        for filename, data in patched_entries.items():
            if filename not in original_filenames:
                zf.writestr(filename, data)
    
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
