"""Exercise the JKCraft shared-memory handshake without launching Minecraft.

Run this while the JKCraft OpenJK client is open.  The probe verifies that the
host heartbeat advances, briefly publishes a Minecraft heartbeat, and exits.
"""

import ctypes
import mmap
import os
import struct
import time


MAGIC = 0x43594B53
VERSION = 11
MAPPING_NAME = "Local\\JKCraft_v1"
COLLISION_OFFSET = 0x20000
COLLISION_BYTES = 32 << 20
OVERLAY_OFFSET = COLLISION_OFFSET + COLLISION_BYTES
OVERLAY_SLOT_BYTES = 3840 * 2160 * 4
RENDER_OFFSET = OVERLAY_OFFSET + OVERLAY_SLOT_BYTES * 3
MAPPING_BYTES = RENDER_OFFSET + (64 << 20)


def tick_ms():
    return ctypes.windll.kernel32.GetTickCount64()


def main():
    ctypes.windll.kernel32.GetTickCount64.restype = ctypes.c_uint64
    link = mmap.mmap(-1, MAPPING_BYTES, tagname=MAPPING_NAME)

    magic, version, host_pid, _ = struct.unpack_from("<IIII", link, 0)
    if magic != MAGIC or version != VERSION or not host_pid:
        raise SystemExit(
            f"JKCraft host is not ready: magic={magic:#x}, version={version}, pid={host_pid}"
        )

    first_heartbeat = struct.unpack_from("<Q", link, 0x10)[0]
    first_state_seq = struct.unpack_from("<I", link, 0x100)[0]
    time.sleep(0.15)
    second_heartbeat = struct.unpack_from("<Q", link, 0x10)[0]
    second_state_seq = struct.unpack_from("<I", link, 0x100)[0]
    if second_heartbeat <= first_heartbeat:
        raise SystemExit("JKCraft host heartbeat is not advancing")
    if second_state_seq <= first_state_seq or second_state_seq & 1:
        raise SystemExit("JKCraft host state is not advancing cleanly")

    state = struct.unpack_from("<IIIIdddffIIIf", link, 0x100)

    struct.pack_into("<I", link, 0x0C, os.getpid())
    started = tick_ms()
    while tick_ms() - started < 1200:
        struct.pack_into("<Q", link, 0x18, tick_ms())
        time.sleep(0.02)

    struct.pack_into("<Q", link, 0x18, 0)
    struct.pack_into("<I", link, 0x0C, 0)
    print(
        "JKCraft handshake OK; "
        f"host pid={host_pid}, state seq={state[0]}, flags={state[1]:#x}, "
        f"world={state[2]:#x}, pos=({state[4]:.3f}, {state[5]:.3f}, {state[6]:.3f}), "
        f"view={state[10]}x{state[11]}"
    )


if __name__ == "__main__":
    main()
