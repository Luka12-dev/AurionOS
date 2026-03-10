import os
from PIL import Image

# List of icons to convert
icons = [
    'terminal', 'notepad', 'calculator', 'files', 
    'clock', 'paint', 'sys-info', 'folder', 
    'file_on_desktop', 'settings', 'snake'
]

def convert_icon(name, size):
    try:
        input_path = f'icons/{name}.png'
        if not os.path.exists(input_path):
            print(f'Skipping {name}: {input_path} not found')
            return

        img = Image.open(input_path).convert('RGBA')
        img = img.resize((size, size), Image.Resampling.LANCZOS)
        
        # Convert RGBA to BGRA (AurionOS frame buffer format)
        r, g, b, a = img.split()
        bgra = Image.merge('RGBA', (b, g, r, a))
        
        output_path = f'icons/{name}_{size}.raw'
        with open(output_path, 'wb') as f:
            f.write(bgra.tobytes())
        print(f'Converted {name}.png to {size}x{size} RAW')
    except Exception as e:
        print(f'Error converting {name} ({size}x{size}): {e}')

if __name__ == '__main__':
    for name in icons:
        # Generate standard 48x48 dock icons
        convert_icon(name, 48)
        # Generate 512x512 high-res icons (optional, used for assets)
        convert_icon(name, 512)