Import("env")

# Ensure sanitizer flags reach the linker on host clang/gcc.
flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
env.Append(LINKFLAGS=flags)
