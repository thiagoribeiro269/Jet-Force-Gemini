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
    supported = {0: (1026, 16, 25, 193), 1: (1027, 16, 29, 231), 2: (1028, 16, 42, 336),
                 3: (1025, 16, 27, 212), 14: (1030, 10, 10, 77), 16: (1019, 50, 12, 91),
                 28: (1055, 16, 14, 110), 36: (1061, 16, 23, 183), 51: (1071, 3, 0, 0),
                 # Movement profile: jumps, strafe, skid, idle variants, fall and landing.
                 5: (1039, 31, 35, 280), 6: (1040, 21, 33, 259), 7: (1041, 21, 33, 259), 9: (1042, 16, 31, 242),
                 10: (1043, 16, 31, 245), 15: (1044, 6, 6, 47), 17: (1020, 40, 6, 42), 18: (1021, 50, 11, 86),
                 24: (1048, 9, 30, 237), 25: (1050, 11, 26, 202),
                 # Gun-held variants (pistol remap columns 2 and 1) and crouch/roll moves.
                 19: (1022, 40, 15, 117), 20: (1023, 40, 7, 56), 26: (1054, 16, 14, 110), 27: (1053, 5, 5, 35),
                 29: (1056, 16, 17, 130), 30: (1057, 16, 20, 155), 31: (1058, 16, 19, 147), 32: (1059, 16, 20, 158),
                 34: (1063, 16, 37, 296), 35: (1062, 16, 35, 276), 45: (1024, 55, 15, 116), 47: (1064, 16, 32, 250),
                 48: (1065, 16, 32, 252), 4: (1033, 16, 47, 375), 44: (1034, 16, 49, 385), 8: (1038, 22, 50, 394),
                 11: (1031, 26, 52, 414), 12: (1036, 14, 50, 394), 13: (1029, 15, 26, 203), 22: (1035, 8, 11, 84),
                 33: (1060, 4, 22, 171), 49: (1032, 26, 52, 414), 50: (1037, 14, 50, 394)}
    require(model_id == 220 and clip_index in supported, "Animation index outside the validated native subset")
    expected_id, expected_count, expected_stride, expected_bits = supported[clip_index]
    require((animation_id, channel_bones, frame_count, stride) == (expected_id, 21, expected_count, expected_stride),
            "Selected native animation header differs")
    looping = clip_index not in (16, 51, 6, 15, 17, 18, 24, 25, 19, 20, 45, 8, 11, 12, 13, 33, 49, 50)
    require((raw[1] & 0xF0) == (0x10 if looping else 0), "Selected native animation playback flags differ")
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
    if clip_index == 51:
        require(all(frame == frames[0] for frame in frames), "Constant stance unexpectedly varies")
        require(any(angle != 0 for bone in frames[0][1] for angle in bone), "Stance is not a bind pose")
    return frames, {"id": animation_id, "model_clip_index": clip_index, "model_clip_count": len(ids),
                    "keyframes": frame_count, "loop": bool(raw[1] & 16), "frame_stride": stride,
                    "bits_per_frame": bits_per_frame, "source_sha256": hashlib.sha256(raw).hexdigest(),
                    "bit_readers_compared": 2, "source_fps_controlled": 15,
                    "semantic_name": None, "scale_channels": 0}
