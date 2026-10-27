"""Force C23 / C++23 after Arduino framework flags (last -std= wins)."""

Import = globals()["Import"]
Import("env")
environment = globals()["env"]


def _force(target_env, label: str) -> None:
    # Drop every -std= then append ours last so g++/gcc see exactly one dialect.
    for key in ("CCFLAGS", "CFLAGS", "CXXFLAGS"):
        filtered = [flag for flag in target_env.get(key, []) if "-std=" not in str(flag)]
        target_env.Replace(**{key: filtered})
    target_env.Append(CXXFLAGS=["-std=gnu++23"])
    target_env.Append(CFLAGS=["-std=gnu23"])
    print(f"pio_cxx_std: {label} forced CXX=gnu++23 C=gnu23")


_force(environment, "env")

try:
    Import("projenv")
    _force(globals()["projenv"], "projenv")
except Exception as exc:  # noqa: BLE001
    print(f"pio_cxx_std: projenv skipped ({exc})")
