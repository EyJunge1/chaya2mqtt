# Rust firmware (esp-hal)

| Crate | Role |
|-------|------|
| [`chaya-pure`](chaya-pure/) | Pure logic from C++ `*_pure.h` |
| [`chaya-fw`](chaya-fw/) | Firmware app + device binary (SoftAP/STA, FlashKv, EPD, HTTP, MQTT TCP) |

```bash
# Host
cargo test -p chaya-pure -p chaya-fw

# Device (espup toolchain)
cd chaya-fw && cargo +esp build --features device --release
# ELF ~1.9MB: ../target/xtensa-esp32s3-none-elf/release/chaya-fw
```

Details: [`docs/RUST_ESP_HAL.md`](../docs/RUST_ESP_HAL.md).
