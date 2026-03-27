"""
tools/mkiso.py - AurionOS ISO Builder

Uses xorriso (via WSL on Windows, or natively on Linux) to build a bootable
ISO 9660 image compatible with VMware, VirtualBox, QEMU, and real hardware.

Boot method: El Torito FLOPPY EMULATION (1.44MB boot image).
  - The 1.44MB floppy image (boot sector + kernel) is embedded in the ISO as
    the El Torito boot image in 1.44MB floppy emulation mode.
  - The BIOS exposes this image as a virtual floppy drive (DL = 0x00).
  - Our INT 13h bootloader reads the kernel from sector 1 onwards using CHS,
    transparently routed by the BIOS to the correct bytes inside the ISO.
  - This is the most compatible method: VMware, VirtualBox, QEMU, and real
    hardware all implement 1.44MB floppy emulation identically.
  - No-emulation mode was previously tried but broke VMware because it passes
    DL = 0xE0 (CD-ROM drive) and uses 2048-byte sector addressing, which
    conflicts with our 512-byte floppy sector logic.

Why floppy emulation works everywhere:
  The BIOS intercepts all INT 13h calls for DL = 0x00 and maps 512-byte CHS
  requests directly to the correct byte offsets inside the ISO boot image.
  The actual CD-ROM sector size (2048 bytes) is invisible to us.
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
        sys.exit(1)


def to_wsl_path(win_path):
    # Convert an absolute Windows path to the WSL /mnt/... equivalent.
    win_path = os.path.abspath(win_path)
    drive = win_path[0].lower()
    rest = win_path[2:].replace('\\', '/')
    return '/mnt/%s%s' % (drive, rest)


def build_iso(output_path, floppy_path):
    # Build a bootable ISO using xorriso with 1.44MB floppy emulation.
    # This is the most universally compatible BIOS boot method.

    if not os.path.exists(floppy_path):
        print('Error: floppy image not found: %s' % floppy_path)
        sys.exit(1)

    floppy_size = os.path.getsize(floppy_path)
    print('Building ISO : %s' % output_path)
    print('  Floppy     : %s (%d bytes)' % (floppy_path, floppy_size))
    print('  Boot mode  : El Torito 1.44MB floppy emulation')

    # The floppy must be exactly 1.44MB for floppy emulation to work.
    # If it's smaller, pad it; if larger, warn loudly.
    FLOPPY_SIZE = 1474560  # 1.44MB = 2880 * 512
    if floppy_size > FLOPPY_SIZE:
        print('ERROR: Floppy image is %d bytes, max is %d (1.44MB).' % (floppy_size, FLOPPY_SIZE))
        print('       The kernel is too large to fit. Reduce KERNEL_SECTORS.')
        # We continue anyway - xorriso may still accept it, but boot will fail.

    mode = find_xorriso()

    staging_dir = tempfile.mkdtemp(prefix='mkiso_staging_')
    try:
        # Copy the floppy image into the staging directory.
        boot_image_in_staging = os.path.join(staging_dir, BOOT_IMAGE_NAME)

        # Pad to exactly 1.44MB if needed (required for valid floppy emulation)
        if floppy_size < FLOPPY_SIZE:
            with open(floppy_path, 'rb') as f_in:
                data = f_in.read()
            data = data + b'\x00' * (FLOPPY_SIZE - len(data))
            with open(boot_image_in_staging, 'wb') as f_out:
                f_out.write(data)
            print('  Padded to  : %d bytes (1.44MB)' % FLOPPY_SIZE)
        else:
            shutil.copy2(floppy_path, boot_image_in_staging)

        print('  Staging    : %s' % staging_dir)

        out_abs = os.path.abspath(output_path)

        # xorriso flags:
        #   -b <img>      : Boot image path inside the ISO
        #   -c boot.cat   : Boot catalog path inside the ISO
        #   (no -no-emul-boot) = floppy emulation mode (the default)
        #   -boot-load-size 4 : How many 512-byte sectors the BIOS loads
        #                       (4 = 2048 bytes = 1 CD sector, standard)
        #
        # Crucially, we do NOT use -no-emul-boot, -isohybrid-mbr, or
        # -partition_offset here. Those were causing the VMware failure.

        common_flags = [
            '-o', '',  # Placeholder - filled in below
            '-V', 'AURIONOS',
            '-R', '-J',
            '-b', BOOT_IMAGE_NAME,
            '-c', 'boot.cat',
            '-boot-load-size', '4',
        ]

        if mode == 'wsl':
            out_wsl     = to_wsl_path(out_abs)
            staging_wsl = to_wsl_path(staging_dir)
            total_flags = common_flags + [staging_wsl]
            cmd = ['wsl', 'xorriso', '-as', 'mkisofs'] + total_flags
            cmd[cmd.index('-o') + 1] = out_wsl
        else:
            total_flags = common_flags + [staging_dir]
            cmd = ['xorriso', '-as', 'mkisofs'] + total_flags
            cmd[cmd.index('-o') + 1] = out_abs

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
    print('  QEMU       : qemu-system-i386 -cdrom %s -boot d -m 512M' % output_path)
    print('  VirtualBox : Settings > Storage > add CD-ROM, boot order: Optical first')
    print('  VMware     : VM Settings > CD/DVD > use ISO file, boot from CD-ROM')
    print('               (ensure VM firmware is set to BIOS, not UEFI)')
    print('  USB        : dd if=%s of=/dev/sdX bs=4M (replace /dev/sdX with USB device)' % output_path)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('Usage: python mkiso.py <output.iso> <floppy.img>')
        sys.exit(1)
    build_iso(sys.argv[1], sys.argv[2])
