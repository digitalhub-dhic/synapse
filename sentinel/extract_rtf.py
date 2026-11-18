import os, re

rtf_path = r'\\192.168.1.50\public\Synapse\screenshots.rtf'
out_dir = r'C:\Users\administrator\.gemini\antigravity\brain\8cec796d-dd47-4bd5-a25b-5b4114f2b7f3\rtf_images'
os.makedirs(out_dir, exist_ok=True)

with open(rtf_path, 'rb') as f:
    content = f.read()

# Look for PNG or JPEG magic bytes encoded in hex
# PNG header: 89504e470d0a1a0a
# JPEG header: ffd8ffe0 or ffd8ffe1 or ffd8ff
hex_blocks = re.findall(b'([0-9a-fA-F\\r\\n\\s]{1000,})', content)
print(f'Found {len(hex_blocks)} hex blocks')

idx = 0
for h in hex_blocks:
    cleaned = re.sub(b'\\s+', b'', h)
    if len(cleaned) % 2 != 0:
        cleaned = cleaned[:-1]
    try:
        raw = bytes.fromhex(cleaned.decode('ascii'))
        p = raw.find(b'\x89PNG\r\n\x1a\n')
        if p != -1:
            out_file = os.path.join(out_dir, f'img_{idx}.png')
            with open(out_file, 'wb') as img_f:
                img_f.write(raw[p:])
            print(f'Saved PNG {out_file} ({len(raw)-p} bytes)')
            idx += 1
            continue
        j = raw.find(b'\xff\xd8\xff')
        if j != -1:
            out_file = os.path.join(out_dir, f'img_{idx}.jpg')
            with open(out_file, 'wb') as img_f:
                img_f.write(raw[j:])
            print(f'Saved JPEG {out_file} ({len(raw)-j} bytes)')
            idx += 1
            continue
        if len(raw) > 50000:
            out_file = os.path.join(out_dir, f'raw_{idx}.bin')
            with open(out_file, 'wb') as img_f:
                img_f.write(raw)
            print(f'Saved raw {out_file} ({len(raw)} bytes)')
            idx += 1
    except Exception as e:
        print('Error decoding block:', e)
