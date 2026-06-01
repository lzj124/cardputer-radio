# Pre-upload script: Kill any process holding the serial port before flashing
# This prevents "The chip stopped responding" errors when usb_bridge or monitor is running

import os
import subprocess
Import("env")

def pre_upload(source, target, env):
    port = env.subst("$UPLOAD_PORT")
    if not port:
        print("[pre_upload] No upload port configured, skipping")
        return

    try:
        result = subprocess.run(
            ["lsof", "-ti", port],
            capture_output=True, text=True, timeout=5
        )
        pids = result.stdout.strip().split('\n')
        for pid in pids:
            pid = pid.strip()
            if pid:
                try:
                    os.kill(int(pid), 9)
                    print(f"[pre_upload] Killed PID {pid} holding {port}")
                except Exception as e:
                    print(f"[pre_upload] Could not kill {pid}: {e}")
    except FileNotFoundError:
        print("[pre_upload] lsof not found (Windows?), skipping")
    except Exception as e:
        print(f"[pre_upload] Error: {e}")

env.AddPreAction("upload", pre_upload)
