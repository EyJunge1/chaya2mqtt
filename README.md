> [!WARNING]
> **Beta** is under active development and not yet as stable as a final release — features and behavior may still change. Star ⭐ and Watch 👀 the repo to get notified when new builds land.

![Chaya2MQTT – send a little heart](docs/images/social-preview.png)

<p align="center">
  <a href="https://github.com/EyJunge1/chaya2mqtt/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/EyJunge1/chaya2mqtt?style=flat-square&logo=github&logoColor=white&color=blue"></a>
  <a href="https://eyjunge1.github.io/chaya2mqtt/"><img alt="Web flasher" src="https://img.shields.io/badge/web%20flasher-install-red?style=flat-square&logo=googlechrome&logoColor=white"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/EyJunge1/chaya2mqtt?style=flat-square&logo=gnu&logoColor=white&color=blueviolet"></a>
</p>

Send a little heart to someone special—no matter how far away they are.

Chaya2MQTT connects two [Waveshare ESP32-S3 e-paper displays](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G):

Press the button on one and a red heart appears on the other.

Each display keeps track of the hearts you have sent and received, while MQTT quietly carries them between the two devices.

### What the display shows

**Online**

![Online heart with RX/TX](docs/images/heart-online.png)


| Product title                                            | SoftAP setup                                      |
| -------------------------------------------------------- | ------------------------------------------------- |
| ![Centered product title](docs/images/product-title.png) | ![WIFI QR setup splash](docs/images/setup-qr.png) |



| Offline                                                      | Power-off                                         |
| ------------------------------------------------------------ | ------------------------------------------------- |
| ![Cracked heart when offline](docs/images/heart-offline.png) | ![Power-off heart-off](docs/images/power-off.png) |


## Bring your heart to life

Open the [web flasher](https://eyjunge1.github.io/chaya2mqtt/) in Chrome or Edge, connect USB-C, and install. Erase is on by default.

## License

[GNU GPL v3.0 only](LICENSE)
