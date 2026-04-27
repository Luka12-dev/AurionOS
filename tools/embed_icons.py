import sys
import os

def embed_icons(img_path, icons_dir):
    icons = [
        "terminal.bmp", "Browser.bmp", "notepad.bmp", "calculator.bmp", 
        "files.bmp", "clock.bmp", "paint.bmp", "sys-info.bmp", 
        "settings.bmp", "folder.bmp", "snake.bmp", "3d_demo.bmp"
    ]
    
    # Starting LBA (60000 = ~30MB mark, safely after the 14MB wallpaper)
    start_lba = 60000
    lba_offset = 2000 # 1MB spacing (2000 * 512 = 1,024,000 bytes)

    with open(img_path, "r+b") as f:
        for i, name in enumerate(icons):
            path = os.path.join(icons_dir, name)
            # Handle .bmp missing (some are .rle)
            if not os.path.exists(path):
                # Try .rle just in case, but loader expects BMP
                alt = path.replace(".bmp", ".rle")
                if os.path.exists(alt):
                    path = alt
                else:
                    print(f"Skipping {name} - not found")
                    continue
            
            with open(path, "rb") as icon_f:
                data = icon_f.read()
                
            f.seek(start_lba * 512 + i * lba_offset * 512)
            f.write(data)
            print(f"Embedded {name} at LBA {start_lba + i * lba_offset}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: embed_icons.py <img_path> <icons_dir>")
        sys.exit(1)
    embed_icons(sys.argv[1], sys.argv[2])
