#!/usr/bin/env python3
"""Write compile_commands.json so clangd indexes panda firmware and board/lwip.

SCons only compiles a few unity-build TUs, so a SCons compilation database
never sees lwIP .c files. This listing is what clangd needs for go-to-definition.
"""
from __future__ import annotations

import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OPENDBC = ROOT.parent.parent / "opendbc"

MCU_FLAGS = [
    "-mcpu=cortex-m7",
    "-mthumb",
    "-mfpu=fpv5-d16",
    "-mfloat-abi=hard",
    "-mlittle-endian",
    "-std=gnu11",
    "-nostdlib",
    "-fno-builtin",
    "-fsingle-precision-constant",
    "-Wall",
    "-g",
]

PANDA_DEFINES = [
    "STM32H7",
    "STM32H735xx",
    "RICHIE",
    "ALLOW_DEBUG",
]

LWIP_DEFINES = PANDA_DEFINES + ["USE_HAL_DRIVER"]

PANDA_INCLUDES = [
    ".",
    "board/stm32h7/inc",
    "board/obj",
    str(OPENDBC),
]

LWIP_INCLUDES = [
    "board/lwip/inc",
    "board/lwip/app",
    "board/lwip/drivers/hal/inc",
    "board/lwip/drivers/cmsis/inc",
    "board/lwip/drivers/bsp",
    "board/lwip/drivers/lan8742",
    "board/lwip/drivers/usb",
    "board/lwip/middlewares/libc",
    "board/lwip/middlewares/lwip/src/include",
    "board/lwip/middlewares/lwip/system",
    "board/lwip/middlewares/lwip/src/apps/http",
] + PANDA_INCLUDES


def collect_lwip_sources() -> list[str]:
    files = sorted(
        p.relative_to(ROOT).as_posix()
        for p in (ROOT / "board" / "lwip").rglob("*.c")
        if "board/lwip/middlewares/lwip/src/apps/http/fsdata.c" not in p.as_posix()
    )
    return files


def collect_panda_sources() -> list[str]:
    return [
        "board/main.c",
        "board/bootstub.c",
        "board/crypto/rsa.c",
        "board/crypto/sha.c",
        "board/jungle/main.c",
        "board/body/main.c",
        "board/stm32h7/startup_stm32h7x5xx.s",
    ]


def entry(cc: str, src: str, defines: list[str], includes: list[str]) -> dict:
    flags = MCU_FLAGS + [f"-D{d}" for d in defines] + [f"-I{p}" for p in includes]
    lang = ["-x", "assembler-with-cpp"] if src.endswith(".s") else ["-xc"]
    return {
        "directory": str(ROOT),
        "file": src,
        "arguments": [cc, "-c", *lang, src, "-o", f"board/obj/{src}.o", *flags],
    }


def main() -> None:
    cc = shutil.which("arm-none-eabi-gcc") or "arm-none-eabi-gcc"
    entries = [entry(cc, src, PANDA_DEFINES, PANDA_INCLUDES) for src in collect_panda_sources()]
    entries += [entry(cc, src, LWIP_DEFINES, LWIP_INCLUDES) for src in collect_lwip_sources()]
    (ROOT / "compile_commands.json").write_text(json.dumps(entries, indent=2) + "\n")
    print(f"wrote {ROOT / 'compile_commands.json'} ({len(entries)} entries)")


if __name__ == "__main__":
    main()
