# Rust firmware (esp-hal) — production

| Crate | Role |
|-------|------|
| [`chaya-pure`](chaya-pure/) | Pure logic from C++ `*_pure.h` |
| [`chaya-fw`](chaya-fw/) | Device firmware binary |

```bash
# Host tests
cargo test -p chaya-pure -p chaya-fw

# From repo root — build + flash (espup + espflash)
make upload
make upload-erase

# Release images → out/rust-release/
bash scripts/build_rust_release.sh
```

Details: [`docs/RUST_ESP_HAL.md`](../docs/RUST_ESP_HAL.md).
