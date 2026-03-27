import sys
import os

def create_floppy(output_file, bootloader_bin, kernel_bin):
    # Total size: 1.44MB (2880 sectors * 512 bytes)
    total_size = 1440 * 1024
    
    # Check if input files exist
    if not os.path.exists(bootloader_bin):
        print(f"Error: Bootloader binary '{bootloader_bin}' not found.")
        sys.exit(1)
    
    if not os.path.exists(kernel_bin):
        print(f"Error: Kernel binary '{kernel_bin}' not found.")
        sys.exit(1)
    
    # Read bootloader
    with open(bootloader_bin, 'rb') as f:
        boot_data = f.read()
        
    if len(boot_data) > 512:
        print(f"Error: Bootloader size ({len(boot_data)} bytes) exceeds 512 bytes.")
        sys.exit(1)
        
    # Read kernel
    with open(kernel_bin, 'rb') as f:
        kernel_data = f.read()
        
    # Check if kernel fits (sector 1 onwards)
    max_kernel_size = total_size - 512
    if len(kernel_data) > max_kernel_size:
        print(f"Error: Kernel size ({len(kernel_data)} bytes) exceeds available space ({max_kernel_size} bytes).")
        sys.exit(1)
    
    print(f"Creating floppy image: {output_file}")
    print(f"  Bootloader: {len(boot_data)} bytes")
    print(f"  Kernel:     {len(kernel_data)} bytes")
    
    # Create the image
    with open(output_file, 'wb') as f:
        # Write bootloader (padded to 512 bytes if necessary, though it should be exactly 512)
        f.write(boot_data)
        if len(boot_data) < 512:
            f.write(b'\x00' * (512 - len(boot_data)))
            
        # Write kernel immediately after bootloader (sector 1)
        f.write(kernel_data)
        
        # Pad the rest with zeros up to 1.44MB
        current_pos = f.tell()
        remaining = total_size - current_pos
        if remaining > 0:
            # Write in chunks to be memory efficient if needed, though 1.44MB is small
            f.write(b'\x00' * remaining)
            
    print(f"Successfully created {output_file} ({total_size} bytes)")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python mkfloppy.py <output_file> <bootloader_bin> <kernel_bin>")
        sys.exit(1)
        
    output_file = sys.argv[1]
    bootloader_bin = sys.argv[2]
    kernel_bin = sys.argv[3]
    
    create_floppy(output_file, bootloader_bin, kernel_bin)
