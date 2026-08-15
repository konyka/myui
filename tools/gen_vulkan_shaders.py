#!/usr/bin/env python3
"""Regenerate the checked-in SPIR-V .inc files for the Vulkan backend
(M25b). Requires glslangValidator. Usage: python3 tools/gen_vulkan_shaders.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER_DIR = os.path.join(HERE, "..", "src", "myr", "vulkan_shaders")
SHADERS = ["flat.vert", "flat.frag", "tex.vert", "text.frag", "img.frag"]


def gen(name):
    src = os.path.join(SHADER_DIR, name)
    spv = src + ".spv"
    subprocess.run(["glslangValidator", "-V", src, "-o", spv], check=True)
    with open(spv, "rb") as f:
        data = f.read()
    sym = "VKSPV_" + name.replace(".", "_").upper()
    words = [
        "0x%08xu" % int.from_bytes(data[i:i + 4], "little")
        for i in range(0, len(data), 4)
    ]
    rows = [", ".join(words[i:i + 8]) for i in range(0, len(words), 8)]
    out = ("/* generated from %s by glslangValidator -V "
           "(tools/gen_vulkan_shaders.py); do not edit */\n" % name)
    out += "static const uint32_t %s[] = {\n  " % sym
    out += ",\n  ".join(rows)
    out += "\n};\n"
    with open(src + ".inc", "w") as f:
        f.write(out)
    print("generated %s.inc (%d words)" % (name, len(words)))


def main():
    for s in SHADERS:
        gen(s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
