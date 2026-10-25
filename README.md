![Chaya2MQTT – send a little heart](.github/pics/social-preview.png)

![Release](https://img.shields.io/github/v/release/EyJunge1/chaya2mqtt?style=flat-square&logo=github&logoColor=white&color=blue) ![Web flasher](https://img.shields.io/badge/web%20flasher-install-red?style=flat-square&logo=googlechrome&logoColor=white) ![License](https://img.shields.io/github/license/EyJunge1/chaya2mqtt?style=flat-square&logo=gnu&logoColor=white&color=blueviolet)

Languages: **🇬🇧** | [🇩🇪](./.github/i18n/README.de.md)

Send a little heart to someone special—no matter how far away they are.

Chaya2MQTT pairs two people over [Waveshare ESP32-S3 e-paper displays](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G)—each can have more than one board (home, office, …).

Press the button on yours and a red heart appears on theirs.

Every display keeps track of the hearts you have sent and received, while MQTT quietly carries them between you.

### What the display shows

**Online**

![Online heart with RX/TX](.github/pics/heart-online.png)

| Product title                                            | SoftAP setup                                      |
| -------------------------------------------------------- | ------------------------------------------------- |
| ![Centered product title](.github/pics/product-title.png) | ![WIFI QR setup splash](.github/pics/setup-qr.png) |

| Offline                                                      | Power-off                                         |
| ------------------------------------------------------------ | ------------------------------------------------- |
| ![Cracked heart when offline](.github/pics/heart-offline.png) | ![Power-off heart-off](.github/pics/power-off.png) |

Optional **E-Ink dark mode** (black background, white text; heart stays red) can be toggled in Settings → Display. SoftAP setup stays light.

> [!NOTE]
> Open the [web flasher](https://eyjunge1.github.io/chaya2mqtt/) in Chrome or Edge, connect USB-C, and install.

## License

[GNU GPL v3.0 only](LICENSE)
