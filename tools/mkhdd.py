import sys
import os

def create_hdd(output_file, size_mb):
    size_mb = int(size_mb)
    size_bytes = size_mb * 1024 * 1024
    
    if os.path.exists(output_file):
        print(f"Skipping: {output_file} already exists (preserving data).")
        sys.exit(0)
    
    print(f"Creating HDD image: {output_file} ({size_mb} MB)")
    
    with open(output_file, 'wb') as f:
        # Create a sparse file if filesystem supports it, or just seek and write
        f.seek(size_bytes - 1)
        f.write(b'\x00')
    
    print(f"Successfully created {output_file} ({size_bytes} bytes)")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python mkhdd.py <output_file> <size_mb>")
        sys.exit(1)
        
    output_file = sys.argv[1]
    size_mb = sys.argv[2]
    
    create_hdd(output_file, size_mb)
