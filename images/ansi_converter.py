from PIL import Image

def generate_ansi_16_palette():
    # Standard 16 ANSI colors
    return [
        (0, 0, 0),       (188, 0, 0),       (0, 237, 0),       (194, 212, 0),
        (0, 0, 232),     (189, 0, 199),   (0, 220, 194),    (220, 218, 221),
        (117, 117, 117),    (223, 0, 0),     (0, 255, 0),       (246, 255, 3),
        (84, 71, 251),     (239, 0, 253),     (13, 255, 248),    (255, 255, 255),
    ]

ansi_palette = generate_ansi_16_palette()

def closest_ansi_color(r, g, b):
    min_distance = float('inf')
    closest_index = 0
    for i, (cr, cg, cb) in enumerate(ansi_palette):
        distance = (int(r) - cr) ** 2 + (int(g) - cg) ** 2 + (int(b) - cb) ** 2
        if distance < min_distance:
            min_distance = distance
            closest_index = i
    return closest_index

def conv_image(name_in, size_x, size_y, name_out):
    # Ensure width is even for 2-pixels-per-byte packing
    if size_x % 2 != 0:
        print("Warning: size_x should be even for 4-bit packing. Truncating last pixel.")

    print(f"Converting {name_in} to 4-bit (2 pixels per byte)...")
    with Image.open(name_in) as im:
        with open("./" + name_out, "wb") as o:
            rgb_im = im.convert("RGB")
            im2 = rgb_im.resize(size=[size_x, size_y])
            
            for y in range(0, im2.height):
                # Step by 2 in the X direction
                for x in range(0, im2.width, 2):
                    # Pixel 1 (Left)
                    r1, g1, b1 = im2.getpixel((x, y))
                    idx1 = closest_ansi_color(r1, g1, b1)
                    
                    # Pixel 2 (Right)
                    # Check for odd widths to avoid out-of-bounds
                    if x + 1 < im2.width:
                        r2, g2, b2 = im2.getpixel((x + 1, y))
                        idx2 = closest_ansi_color(r2, g2, b2)
                    else:
                        idx2 = 0 

                    # Packing: [Pixel1 (high nibble)][Pixel2 (low nibble)]
                    # Result: 0x(idx1)(idx2)
                    packed_byte = (idx1 << 4) | (idx2 & 0x0F)
                    
                    o.write(packed_byte.to_bytes(1, byteorder="little"))
    print(f"Done! Saved to {name_out}")

# Example usage:
conv_image("background_ansi_320x180.png", 320, 180, "background-320x180.bin")
conv_image("start_screen_180x180.png", 180, 180, "start_screen-180x180.bin")