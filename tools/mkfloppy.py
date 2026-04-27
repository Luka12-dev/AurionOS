import sys
import os
import struct

FS_DATA_START_LBA = 1000
FS_CONTENT_START_LBA = 1200

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

def add_file_to_floppy(img_file, target_path, source_file):
    if not os.path.exists(img_file):
        print(f"Error: Image file '{img_file}' not found.")
        return False
    if not os.path.exists(source_file):
        print(f"Error: Source file '{source_file}' not found.")
        return False

    with open(source_file, 'rb') as f:
        data = f.read()
    
    size = len(data)
    if size > 16 * 512:
        print(f"Error: File '{source_file}' too large ({size} bytes). Max is 8KB.")
        return False

    with open(img_file, 'r+b') as f:
        # 1. Find free slot in file table (starts at LBA 500)
        file_idx = -1
        for i in range(64):
            f.seek((FS_DATA_START_LBA + i) * 512)
            name_buf = f.read(56)
            if name_buf[0] == 0:
                file_idx = i
                break
        
        if file_idx == -1:
            print("Error: Disk full (no free file slots).")
            return False
        
        # 2. Write FSEntry (56 bytes name, 4 bytes size, 1 byte type, 1 byte attr, 2 bytes parent, 2 bytes reserved = 66 bytes? No, FSEntry is 64 bytes total)
        # Structure in C:
        # typedef struct {
        #     char name[56];
        #     uint32_t size;
        #     uint8_t type;
        #     uint8_t attr;
        #     uint16_t parent_idx;
        #     uint16_t reserved;
        # } FSEntry;
        
        f.seek((FS_DATA_START_LBA + file_idx) * 512)
        name_bytes = target_path.encode('ascii')
        if len(name_bytes) > 55:
            name_bytes = name_bytes[:55]
        
        entry = name_bytes + b'\x00' * (56 - len(name_bytes))
        entry += struct.pack('<I', size)
        entry += b'\x00' # type=0 file
        entry += b'\x00' # attr=0
        entry += struct.pack('<H', 0xFFFF) # parent=root
        entry += b'\x00\x00' # reserved
        
        f.write(entry)
        
        # 3. Write content (starts at LBA 700 + file_idx * 16)
        f.seek((FS_CONTENT_START_LBA + file_idx * 16) * 512)
        f.write(data)
        if len(data) < 16 * 512:
            f.write(b'\x00' * (16 * 512 - len(data)))
            
    print(f"Added '{target_path}' ({size} bytes) to '{img_file}' at index {file_idx}")
    return True

if __name__ == "__main__":
    if len(sys.argv) == 4:
        output_file = sys.argv[1]
        bootloader_bin = sys.argv[2]
        kernel_bin = sys.argv[3]
        create_floppy(output_file, bootloader_bin, kernel_bin)
    else:
        print("Usage: python mkfloppy.py <output_file> <bootloader_bin> <kernel_bin>")
        sys.exit(1)
