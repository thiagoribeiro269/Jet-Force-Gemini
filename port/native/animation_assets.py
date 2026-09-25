"""Offline decoding of the original animation data format, without CPU emulation."""
import hashlib
import math
import struct
from prepare_assets import region, require


def clip_for_model(assets, model_id, clip_index=0):
    start, end = struct.unpack(">HH", region(assets.section(0x28), model_id * 2, 4))
    ids = struct.unpack(">" + str((end - start) // 2) + "H", region(assets.section(0x29), start, end - start))
    require(0 <= clip_index < len(ids), "Animation index outside model")
    animation_id = ids[clip_index]
    start, end = struct.unpack(">II", region(assets.section(0x2A), animation_id * 4, 8))
    raw = region(assets.section(0x2B), start, end - start)
    frame_offset = struct.unpack_from(">H", raw, 2)[0]
    channel_bones, frame_count, stride = raw[9], raw[11], raw[13]
    supported = {0: (1026, 16, 25, 193), 14: (1030, 10, 10, 77)}
    require(model_id == 220 and clip_index in supported, "Animation index outside the validated native subset")
    expected_id, expected_count, expected_stride, expected_bits = supported[clip_index]
    require((animation_id, channel_bones, frame_count, stride) == (expected_id, 21, expected_count, expected_stride),
            "Selected native animation header differs")
    mapping_start, mapping_end = struct.unpack(">II", region(assets.section(0x2C), model_id * 4, 8))
    mappings = region(assets.section(0x2D), mapping_start, mapping_end - mapping_start)
    mapping = region(mappings, clip_index * 21, 21)
    require(all(x < 21 for x in mapping), "Animation maps outside channel table")
    root_base = struct.unpack_from(">b", raw, 8)[0], struct.unpack_from(">b", raw, 10)[0], struct.unpack_from(">b", raw, 12)[0]
    root_widths = (raw[14] >> 4, raw[14] & 15, raw[15] & 15)
    descriptors = list(struct.unpack(">60H", region(raw, 16, 120)))
    require(not any(d & 16 for d in descriptors), "Animated scale requires a separate decoder extension")
    require(frame_offset == 142 and region(raw, 136, 6) == bytes(6), "Unexpected auxiliary channel descriptors")
    bits_per_frame = sum(root_widths) + sum(d & 15 for d in descriptors)
    require(bits_per_frame == expected_bits and bits_per_frame <= stride * 8, "Packed frame stride differs")
    payload = region(raw, frame_offset, stride * frame_count)
    frames = []
    for frame in range(frame_count):
        packet = region(payload, frame * stride, stride)
        stream = int.from_bytes(packet, "big")
        used = 0

        def take(width):
            nonlocal used
            require(used + width <= stride * 8, "Animation bitstream overflow")
            used += width
            return (stream >> (stride * 8 - used)) & ((1 << width) - 1) if width else 0

        root = [base * 2 + take(width) for base, width in zip(root_base, root_widths)]
        channels = [((d & 0xFFF0) + take(d & 15) * 32) & 0xFFFF for d in descriptors] + [0, 0, 0]
        require(used == bits_per_frame, "Animation bit consumption differs")
        angles = [[channels[channel * 3 + axis] * (2 * math.pi / 65536) for axis in range(3)] for channel in mapping]
        frames.append((root, angles))
    # Independently unpack MSB-first bits one by one and compare the raw fields.
    # This checks offsets/widths against a different bit-reading method, not CPU execution.
    widths = [*root_widths, *(d & 15 for d in descriptors)]
    for index in range(frame_count):
        bit_list = [int(bit) for byte in payload[index * stride:(index + 1) * stride] for bit in f"{byte:08b}"]
        position, values = 0, []
        for width in widths:
            value = 0
            for bit in bit_list[position:position + width]: value = value * 2 + bit
            position += width
            values.append(value)
        require([root_base[a] * 2 + values[a] for a in range(3)] == frames[index][0], "Root bit-reader disagreement")
        for bone, channel in enumerate(mapping):
            for axis in range(3):
                k = channel * 3 + axis
                angle = ((descriptors[k] & 0xFFF0) + values[3 + k] * 32) & 0xFFFF if k < 60 else 0
                require(abs(frames[index][1][bone][axis] - angle * (2 * math.pi / 65536)) < 1e-10, "Angle bit-reader disagreement")
    return frames, {"id": animation_id, "model_clip_index": clip_index, "model_clip_count": len(ids),
                    "keyframes": frame_count, "loop": bool(raw[1] & 16), "frame_stride": stride,
                    "bits_per_frame": bits_per_frame, "source_sha256": hashlib.sha256(raw).hexdigest(),
                    "bit_readers_compared": 2, "source_fps_controlled": 15,
                    "semantic_name": None, "scale_channels": 0}
