#!/usr/bin/env python3
"""
裁剪GIF边缘空白区域
"""
from PIL import Image, ImageSequence

def crop_gif(input_path, output_path, left=0, top=0, right=0, bottom=0):
    img = Image.open(input_path)
    orig_w, orig_h = img.size

    new_w = orig_w - left - right
    new_h = orig_h - top - bottom

    print(f"Original: {orig_w}x{orig_h}")
    print(f"Crop: left={left}, top={top}, right={right}, bottom={bottom}")
    print(f"New size: {new_w}x{new_h}")

    frames = []
    durations = []
    disposal = []

    for frame in ImageSequence.Iterator(img):
        frames.append(frame.copy())
        durations.append(frame.info.get('duration', 100))
        disposal.append(frame.disposal_method)

    cropped_frames = []
    for frame in frames:
        cropped = frame.crop((left, top, orig_w - right, orig_h - bottom))
        cropped_frames.append(cropped)

    cropped_frames[0].save(
        output_path,
        save_all=True,
        append_images=cropped_frames[1:],
        duration=durations,
        loop=img.info.get('loop', 0),
        disposal=disposal,
        optimize=True
    )

    print(f"Saved to: {output_path}")

if __name__ == "__main__":
    input_file = "/home/tcvdog/桌面/esp32rlcd_vendor_base/cat.gif"
    output_file = "/home/tcvdog/桌面/esp32rlcd_vendor_base/cat_cropped.gif"

    crop_gif(input_file, output_file, left=40, top=40, right=40, bottom=40)