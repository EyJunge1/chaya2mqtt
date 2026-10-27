"""Add sanitizer linker flags to the PlatformIO native environment."""

platformio_import = globals()["Import"]
platformio_import("env")
environment = globals()["env"]

# Ensure sanitizer flags reach the linker on host clang/gcc.
flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
environment.Append(LINKFLAGS=flags)
