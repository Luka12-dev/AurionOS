# Build bootloader with correct KERNEL_SECTORS value based on kernel size.

import sys
import subprocess
import os

def main():
    if len(sys.argv) != 5:
        print("Usage: python build_bootloader.py <nasm> <nasmflags> <boot_asm> <output_bin>")
        sys.exit(1)
    
    nasm = sys.argv[1]
    nasmflags = sys.argv[2]
    boot_asm = sys.argv[3]
    output_bin = sys.argv[4]
    kernel_bin = "build/kernel.bin"
    
    # Calculate kernel sectors
    if os.path.exists(kernel_bin):
        kernel_size = os.path.getsize(kernel_bin)
        kernel_sectors = (kernel_size + 511) // 512
        print(f"Kernel size: {kernel_size} bytes ({kernel_sectors} sectors)")
    else:
        print(f"Warning: {kernel_bin} not found, using default 64 sectors")
        kernel_sectors = 64
    
    # Assemble bootloader
    cmd = [
        nasm,
        "-f", "bin",
        f"-DKERNEL_SECTORS={kernel_sectors}",
        "-DKERNEL_LBA=1",
        "-DKERNEL_DEST=0x10000",
        boot_asm,
        "-o", output_bin
    ]
    
    cmd_str = ' '.join(cmd)
    print(f"Assembling: {cmd_str}")
    result = subprocess.run(cmd_str, shell=True)
    
    if result.returncode != 0:
        print("ERROR: Assembly failed!")
        sys.exit(1)
    
    # Verify size
    boot_size = os.path.getsize(output_bin)
    if boot_size != 512:
        print(f"ERROR: Bootloader must be exactly 512 bytes, got {boot_size}!")
        sys.exit(1)
    
    print(f"✓ Bootloader built successfully (512 bytes)")

if __name__ == "__main__":
    main()
