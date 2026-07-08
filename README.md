# AWTRIX 3 DS3231 离线时钟固件

这是基于 [Blueforcer/awtrix3](https://github.com/Blueforcer/awtrix3) 0.98 修改的 Ulanzi TC001 / AWTRIX 3 固件分支，面向已加装 DS3231 RTC 芯片的设备。

原项目是适用于 ESP32 的开源自定义像素时钟固件，可用于 Ulanzi Smart Pixel Clock TC001、AWTRIX 2 升级主板或自制矩阵时钟。本分支保留原项目的 MQTT、HTTP API、Home Assistant discovery、通知、自定义页面、动画图标、RTTTL 播放、文件管理等能力，并增加离线时间基准、WiFi 开关和传感器自动隐藏逻辑。

> 说明：本分支不是 Blueforcer 官方版本。请先确认硬件、接线和刷机方式，使用风险自行承担。

## 本分支新增内容

- 支持 DS3231 RTC 芯片。
- 开机检测 DS3231 是否存在。
- 网络连接失败时，可使用 DS3231 内的时间作为系统时间基准继续显示时间。
- 网络可用时，开机会通过 NTP 校准系统时间，并自动写回 DS3231。
- 联网状态下每 6 小时自动把系统时间同步到 DS3231。
- 长按确认键菜单新增 `WIFI` 选项，可切换 `ON` / `OFF`。
- WiFi 设置默认启用。
- 新固件烧录后的第一次启动必须配置 WiFi；首次成功联网后，后续才允许 RTC 离线运行。
- WiFi 关闭后开机跳过真实 WiFi 连接，仅播放本地启动动画，不启用网络功能。
- WiFi 连接失败且满足离线条件时，显示红色 `OFFLINE` 闪烁提示后进入离线显示。
- 未检测到 DS3231 时显示 `RTC 404`，然后进入原版必须联网 / AP 配网逻辑。
- 未检测到温湿度传感器时自动隐藏温度和湿度页面。
- BMP280 只显示温度页面，自动隐藏湿度页面。
- BME280、HTU21DF、SHT31 会按实际读取能力自动显示温度和湿度页面。
- Release 附带 `awtrix3_http_control.html` Web 控制面板。

## 硬件要求

- ESP32 设备，典型目标为 Ulanzi TC001。
- LED 矩阵硬件与原 AWTRIX 3 兼容。
- 可选但推荐：DS3231 RTC 模块。
- 可选温湿度传感器：
  - BME280：温度、湿度
  - BMP280：温度
  - HTU21DF：温度、湿度
  - SHT31：温度、湿度

## DS3231 接线

DS3231 使用设备已有 I2C 总线。

Ulanzi TC001 默认引脚：

| DS3231 | ESP32 |
| --- | --- |
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

如果你的硬件使用不同板型，请参考 [src/PeripheryManager.cpp](src/PeripheryManager.cpp) 内的 `I2C_SDA_PIN` 与 `I2C_SCL_PIN` 定义。

## 开机行为

### 第一次烧录后

1. WiFi 默认为 `ON`。
2. 第一次启动必须完成 WiFi 配置并成功联网一次。
3. 成功联网后，固件会通过 NTP 获取时间，并在检测到 DS3231 时写入 RTC。
4. 之后如果 WiFi 失败或被关闭，只要 DS3231 存在且时间有效，设备可以离线显示时间。

### DS3231 存在

- 开机会优先从 DS3231 读取时间并设置系统时间。
- 如果 WiFi 成功连接，则继续启用 Web、MQTT、OTA、Art-Net、按钮 HTTP 回调等联网功能。
- 如果 WiFi 连接失败，满足已完成首次 WiFi 配置的条件时，会显示红色 `OFFLINE` 闪烁 3 次，然后进入离线显示。

### DS3231 不存在

- 开机会显示 `RTC 404`，其中 `RTC` 为白色，`404` 为红色。
- 随后执行原版必须联网的逻辑。
- 如果无法连接 WiFi，会进入 AP 配网模式，而不是离线显示时间。

### WiFi 设置为 OFF

- 开机显示版本号后，播放 3 秒居中的彩虹 `AWTRIX` 启动画面。
- 不会真实连接 WiFi。
- 随后显示 `WiFi OFF`，其中 `WiFi` 为白色，`OFF` 为红色。
- Web、MQTT、OTA、Art-Net、HTTP 回调等必须联网的功能会被禁用。
- 时间、日期、电池、本地传感器、本地动画和按键菜单等非联网功能继续可用。

## 按键菜单

长按确认键进入设备菜单。菜单新增：

- `WIFI`
  - `ON`：允许设备开机尝试连接 WiFi。
  - `OFF`：关闭联网能力，开机跳过 WiFi 连接。

切换 WiFi 选项并长按确认保存后，设备会重启以应用网络状态。

## 温湿度页面自动显示

开机时固件会扫描支持的 I2C 传感器：

- 未检测到支持的温湿度传感器：自动隐藏温度和湿度页面。
- BMP280：自动显示温度页面，隐藏湿度页面。
- BME280、HTU21DF、SHT31：按初次读取是否有效自动显示温度和湿度页面。

检测结果会写入设置。插入或更换传感器后，重启设备即可自动恢复对应页面。

## Release 固件文件

Release 中通常包含：

- `firmware.bin`：主固件。
- `bootloader.bin`：ESP32 bootloader。
- `partitions.bin`：分区表。
- `boot_app0.bin`：ESP32 OTA 引导辅助文件。
- `awtrix3_http_control.html`：HTTP Web 控制面板。

如果你只是在已有 AWTRIX 3 分区布局上升级，通常刷入 `firmware.bin` 即可。全量擦除或首次刷机时，请根据你的刷机工具同时使用 bootloader、partition、boot_app0 和 firmware 文件。

## Web 控制面板

本仓库根目录提供 `awtrix3_http_control.html`。使用方式：

1. 设备连接 WiFi 后，确认设备 IP 地址。
2. 在电脑或手机浏览器打开 `awtrix3_http_control.html`。
3. 输入设备 IP。
4. 使用页面内的按钮发送常用 HTTP API 指令。

WiFi 关闭或离线模式下，Web 控制面板无法连接设备，这是预期行为。

## 编译

本项目使用 PlatformIO。

```bash
pio run -e ulanzi
```

如果工程路径包含中文或特殊字符，ESP32 链接阶段可能无法生成 `firmware.map`。建议把工程放到纯 ASCII 路径下编译，或像本分支构建流程一样，在临时 ASCII 路径中编译后复制产物。

## 上游项目功能概览

AWTRIX 3 原有能力包括：

- 简单配网和在线刷机。
- Home Assistant discovery。
- 屏幕菜单直接修改设置。
- 时间、日期、温湿度、电池等原生页面。
- MQTT 与 HTTP API。
- 自定义页面 CustomApps。
- 通知、动画图标、全屏动画和效果。
- 自定义图标和文件浏览器。
- RTTTL 旋律播放。
- 无云端依赖，无遥测。

## 社区与文档

- 上游文档：https://blueforcer.github.io/awtrix3/
- 上游仓库：https://github.com/Blueforcer/awtrix3
- AWTRIX flows：https://flows.blueforcer.de/
- Discord：https://discord.gg/cyBCpdx

## 免责声明

本开源软件与 Ulanzi 公司没有从属、认可或官方支持关系。使用本固件需自行承担风险。刷机、改线、加装 DS3231 或传感器可能造成设备无法启动或硬件损坏，请确认你理解相关风险后再操作。

原始 AWTRIX 3 项目由 Stephan Muehl / Blueforcer 创建并维护。本分支仅在其开源代码基础上添加 DS3231 离线时间和相关本地化功能。
