Import("env")
import os, sys

# Derive the pio executable path from the Python interpreter running this script.
# Both live in the same PlatformIO virtualenv bin/ directory, so this works
# regardless of whether pio is on the user's PATH.
_pio = os.path.join(os.path.dirname(sys.executable), "pio")

def upload_fs(source, target, env):
    env.Execute(_pio + " run -t uploadfs -e " + env["PIOENV"])

env.AddPostAction("upload", upload_fs)
