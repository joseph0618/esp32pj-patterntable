import json
from pathlib import Path

# Load JSON data
with open("control.json", "r") as f:
    control = json.load(f)
with open("LED.json", "r") as f:
    led_data = json.load(f)
with open("OF.json", "r") as f:
    of_data = json.load(f)

# Extract parts info
fps = control["fps"]
led_parts = control["LEDPARTS"]
of_parts = control["OFPARTS"]

# Combine all parts in order: LED0, LED1, ..., OF0, OF1, ...
ordered_parts = list(led_parts.keys()) + list(sorted(of_parts.keys(), key=lambda x: int(x[2:])))
lengths = [led_parts[p]["len"] if "LED" in p else 1 for p in ordered_parts]

# Frame composition: index by time
frame_dict = {}

# Add LED frames
for led_id, frames in led_data.items():
    for frame in frames:
        time = frame["start"]
        if time not in frame_dict:
            frame_dict[time] = {"fade": frame["fade"], "colors": {}}
        frame_dict[time]["colors"][led_id] = frame["color"] 

# Add OF frames
for frame in of_data:
    time = frame["start"]
    if time not in frame_dict:
        frame_dict[time] = {"fade": frame["fade"], "colors": {}}
    # For OF frames, we need to map them to the correct OF part names
    for i, of_key in enumerate(sorted(of_parts.keys(), key=lambda x: int(x[2:]))):
        frame_dict[time]["colors"][of_key] = frame["color"]

# Sort frames by time
sorted_times = sorted(frame_dict.keys())

# Create main output content
output_lines = []
output_lines.append(str(len(ordered_parts)))  # N
output_lines.append(" ".join(str(l) for l in lengths))  # L1 L2 L3 ... LN
output_lines.append(str(fps))  # fps

# Create timing file content
timing_lines = []

def rgba_to_hex(rgb_a):
    r, g, b, a = rgb_a
    scale = a / 255
    r = int(r * scale)
    g = int(g * scale)
    b = int(b * scale)
    return f"{r:02x}{g:02x}{b:02x}"

for t in sorted_times:
    frame = frame_dict[t]
    
    # Add fade line
    output_lines.append(f"{str(frame['fade']).lower()}")
    
    # Add to timing file
    timing_lines.append(str(t))
    
    # Colors in one continuous string for the frame
    color_str_parts = []
    for part in ordered_parts:
        rgba = frame["colors"].get(part)
        if rgba:
            if isinstance(rgba[0], list):  # LED strip: list of RGBA
                for color in rgba:
                    color_str_parts.append(rgba_to_hex(color))
            else:  # OF: single RGBA
                color_str_parts.append(rgba_to_hex(rgba))
        else:
            # Default black
            length = led_parts[part]["len"] if "LED" in part else 1
            for _ in range(length):
                color_str_parts.append("000000")
    
    # Join without spaces and append as one line
    output_lines.append("".join(color_str_parts))

# Save main file
main_output_path = "lightdance_data.txt"
with open(main_output_path, "w") as f:
    f.write("\n".join(output_lines))

# Save timing file
timing_output_path = "frame_times.txt"
with open(timing_output_path, "w") as f:
    f.write("\n".join(timing_lines))

print(f"Created {main_output_path} with main light dance data")
print(f"Created {timing_output_path} with frame timing data")
print(f"Total frames: {len(sorted_times)}")
print(f"Total parts: {len(ordered_parts)} ({list(led_parts.keys())} + {sorted(of_parts.keys(), key=lambda x: int(x[2:]))})")
