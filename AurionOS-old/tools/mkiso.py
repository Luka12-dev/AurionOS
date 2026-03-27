"""
tools/mkiso.py - AurionOS ISO Builder

Uses xorriso (via WSL on Windows, or natively on Linux/macOS) to build a proper
bootable ISO 9660 image compatible with VMware, VirtualBox, QEMU, and real hardware.

Boot method: El Torito no-emulation, with -boot-info-table.
  - A trimmed boot image (boot sector + kernel, no 1.44MB padding) is placed
    as /aurionos.img inside the ISO root.
  - xorriso boots from it with no floppy emulation (-no-emul-boot).
  - -boot-info-table patches bytes 8-23 of the boot sector with:
      [0x08] LBA of the PVD (always 16)
      [0x0C] LBA of the boot file on CD (4 bytes, 2048-byte sectors)
      [0x10] size of the boot file in bytes (4 bytes)
  - The bootloader's cd_boot path reads these values to locate the kernel.

The boot image is NOT the full 1.44MB floppy. It contains only the boot sector
followed by the kernel binary. This keeps the image small enough to load entirely
via real-mode INT 13h without exceeding the 1MB addressing limit.
"""

import sys
import os
import shutil
import subprocess
import platform
import tempfile


BOOT_IMAGE_NAME = 'aurionos.img'


def find_xorriso():
    # Find xorriso - native on Linux/macOS, via WSL on Windows.
    if platform.system() == 'Windows':
        try:
            result = subprocess.run(
                ['wsl', 'which', 'xorriso'],
                capture_output=True, text=True, timeout=10
            )
            if result.returncode == 0 and 'xorriso' in result.stdout:
                return 'wsl'
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass
        print('Error: xorriso not found. Install it in WSL:')
        print('  wsl sudo apt-get install xorriso')
        sys.exit(1)
    else:
        result = subprocess.run(['which', 'xorriso'], capture_output=True, text=True)
        if result.returncode == 0:
            return 'native'
        print('Error: xorriso not found. Install it:')
        print('  Ubuntu/Debian: sudo apt-get install xorriso')
        print('  Arch:          sudo pacman -S libisoburn')
        print('  Fedora:        sudo dnf install xorriso')
        print('  macOS:         brew install xorriso')
        sys.exit(1)


def to_wsl_path(win_path):
    # Convert an absolute Windows path to the WSL /mnt/... equivalent.
    win_path = os.path.abspath(win_path)
    drive = win_path[0].lower()
    rest = win_path[2:].replace('\\', '/')
    return '/mnt/%s%s' % (drive, rest)


def build_iso(output_path, floppy_path):
    # Build a bootable ISO using xorriso with a proper staging directory.

    if not os.path.exists(floppy_path):
        print('Error: floppy image not found: %s' % floppy_path)
        sys.exit(1)

    floppy_size = os.path.getsize(floppy_path)
    print('Building ISO : %s' % output_path)
    print('  Floppy     : %s (%d bytes)' % (floppy_path, floppy_size))
    print('  Boot mode  : El Torito no-emulation + boot-info-table')

    mode = find_xorriso()

    staging_dir = tempfile.mkdtemp(prefix='mkiso_staging_')
    try:
        # For standard floppy emulation, the boot image must be exactly 1.44MB.
        # We copy the full floppy image instead of trimming it.
        boot_image_in_staging = os.path.join(staging_dir, BOOT_IMAGE_NAME)
        shutil.copy2(floppy_path, boot_image_in_staging)
        print('  Staging    : %s' % staging_dir)

        out_abs = os.path.abspath(output_path)

        if mode == 'wsl':
            out_wsl     = to_wsl_path(out_abs)
            staging_wsl = to_wsl_path(staging_dir)

            cmd = [
                'wsl', 'xorriso', '-as', 'mkisofs',
                '-o', out_wsl,
                '-V', 'AURIONOS',
                '-R',
                '-J',
                '-b', BOOT_IMAGE_NAME,
                '-c', 'boot.cat',
                staging_wsl,
            ]
        else:
            cmd = [
                'xorriso', '-as', 'mkisofs',
                '-o', out_abs,
                '-V', 'AURIONOS',
                '-R',
                '-J',
                '-b', BOOT_IMAGE_NAME,
                '-c', 'boot.cat',
                staging_dir,
            ]

        print('  Running    : %s' % ' '.join(cmd))
        result = subprocess.run(cmd)

        if result.returncode != 0:
            print('Error: xorriso failed with exit code %d' % result.returncode)
            sys.exit(1)

    finally:
        shutil.rmtree(staging_dir, ignore_errors=True)

    if not os.path.exists(output_path):
        print('Error: ISO was not created at %s' % output_path)
        sys.exit(1)

    iso_size = os.path.getsize(output_path)
    print('ISO created  : %s (%d bytes)' % (output_path, iso_size))
    print()
    print('Boot instructions:')
    print('  QEMU      : qemu-system-i386 -cdrom %s -boot d -m 512M' % output_path)
    print('  VirtualBox: Settings > Storage > add CD-ROM, boot order: Optical first')
    print('  VMware    : VM Settings > CD/DVD > use ISO file, boot from CD-ROM')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('Usage: python mkiso.py <output.iso> <floppy.img>')
        sys.exit(1)
    build_iso(sys.argv[1], sys.argv[2])
