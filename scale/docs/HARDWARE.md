# 体重秤 —— 硬件与主控选择

## 1. 这块秤要做什么

| 项 | 内容 |
|---|---|
| 主控 | ESP32（经典款）**或** ESP32-S3 —— 见第 3 节 |
| 称重 | HX711 + 桥式压力传感器（半桥/全桥称重片） |
| 上报 | 协议 **0x02 体重帧**，13 字节，载荷 = 体重 ×100（0.01kg） |
| 时机 | **称重稳定后发 1 帧**；不缓存、不补传（协议 §5） |
| 反馈 | 无屏幕、无按键、无马达（最少硬件） |

## 2. 接线

| HX711 引脚 | 接到 | 说明 |
|---|---|---|
| VCC | 3V3 或 5V | 5V 时称重更稳；确认模块有电平适配 |
| GND | GND | |
| DT (DOUT) | **GPIO1** | 数据（模块输出，开漏，需上拉，代码已开内部上拉） |
| SCK | **GPIO2** | 时钟（MCU 输出） |
| E+ / E- | 称重片激励 | 传感器激励电压 |
| A+ / A- | 称重片信号 | 信号输入（A 通道，增益 128） |

> **HX711 不是标准 SPI**，不能用 `esp_driver_spi`。它的时序是：
> DOUT 拉低表示数据就绪 → 每个 SCK 上升沿移出一位（MSB 先出）→
> 共 **25 个脉冲**（24 位数据 + 1 个脉冲选 A 通道增益 128）。
> 本工程用 GPIO bit-bang 实现（`components/fw_scale/hx711.c`），
> 脉冲间隔约 1us，落在 Datasheet 要求的 0.2~50us 窗口内。

引脚在 `components/fw_scale/include/scale_config.h` 里改：

```c
#define SCALE_HX711_PIN_DOUT  1
#define SCALE_HX711_PIN_SCK   2
```

## 3. 主控选哪个：ESP32 还是 ESP32-S3

| | ESP32（经典款 / WROOM-32） | ESP32-S3 |
|---|---|---|
| 工程默认 | ✅ 本工程默认按它配 | 需 `set-target esp32s3` |
| 蓝牙 Classic (SPP) | **支持** | **不支持**（只有 BLE） |
| 本项目是否需要蓝牙 | 不需要 —— 秤走 WiFi TCP | 同左 |
| 手上的板子 | 需另买 ESP32-WROOM + HX711 | 与手环同款，可直接拿手环板子试 |

**结论：本项目两者都行。** 秤的 0x02 帧走 WiFi TCP（见第 4 节），
不使用蓝牙，所以 ESP32-S3 完全没有劣势。

> 分工清单原稿写的是「ESP32-WROOM」，但那是在「通道 A = 蓝牙」的假设下。
> 由于 ESP32-S3 不支持蓝牙 Classic（手环那边已确认），秤也统一走网络更一致。

## 4. 上报通道（重要，需要上位机配合）

协议 §3 把 0x02 归到**通道 A**。手环的通道 A 是 USB 串口，但**秤没有多余的 USB 口**，
所以本工程默认走 **WiFi TCP，端口 45679**。

选 45679 而不是 45678 的原因：上位机的 `WifiHandler` 是**单客户端**设计，
手环的图像通道已经占着 45678，两个设备抢同一端口必然有一方被拒。

```
秤 ──WiFi TCP──▶ PC:45679
                     │
上位机侧需要一个 TCP 监听把收到的字节流喂给 scale 解析器
（现有的 scalePortName 是串口，走 TCP 时用不上）
```

**这一步需要在上位机加一个小监听** —— 详见
[`docs/SCALE_TRANSPORT.md`](SCALE_TRANSPORT.md)。

## 5. 没有 HX711 也能做的验证

`components/fw_scale/scale_frame.c` 是纯逻辑，可在 PC 上编译自测
（**43 条断言**），覆盖克→载荷换算、0x02 帧字节布局、
以及"秤组帧 → 按上位机口径解析"的往返一致性：

```powershell
$gcc = "D:\Qt5.14.2\Tools\mingw730_64\bin\gcc.exe"
& $gcc -std=c99 -Wall -Wextra -O1 `
    -I components/fw_scale/include -I ..\lower\components\fw_core\include `
    test/scale_selftest.c components/fw_scale/scale_frame.c `
    ..\lower\components\fw_core\fw_proto.c -o test/scale_selftest.exe
.\test\scale_selftest.exe     # 期望：通过 43  失败 0
```

关键断言：`65430 g → 6543 → 帧内 0x19 0x8F`、
未标定时载荷必须是 `0xFFFF`（**不冒充 0**）、
超量程/负读数同样填 `0xFFFF`、`ts=0` 不影响解析。
