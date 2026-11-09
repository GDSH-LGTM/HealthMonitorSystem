# 体重秤固件（ESP32 + HX711）

> 本工程只放**体重秤固件**。手环固件在 [`../lower/`](../lower/)（独立 ESP-IDF 工程与 git 仓库）。
> 上位机（Qt 5.14.2）在仓库外层的 `HealthMonitorSystem/`。

| 项 | 值 |
|---|---|
| 主控 | ESP32（经典款）或 ESP32-S3 —— 见 [`docs/HARDWARE.md`](docs/HARDWARE.md) |
| 传感 | HX711 24 位称重 ADC + 桥式压力传感器 |
| 协议 | 协议 v1.2 的 **0x02 体重帧**（13 字节，载荷 = 体重 ×100，单位 0.01kg） |
| 上报 | 称重稳定后发 1 帧；不缓存、不补传（协议 §5） |
| 框架 | ESP-IDF v5.5.5 |
| 固件 | `build/health_monitor_scale.bin`，约 731 KB |

---

## 目录结构

```
components/
  fw_proto/     协议层：直接编译 ../lower/components/fw_core/fw_proto.c（同一份源文件）
  fw_scale/     HX711 驱动 + 采样稳定判定 + 0x02 组帧 + 上报通道
    hx711.c          GPIO bit-bang 读 24 位（HX711 不是标准 SPI）
    scale_frame.c    克→载荷换算 + 0x02 帧组装（纯逻辑，可 PC 自测）
    fw_scale_app.c   采样节拍 / 稳定判定 / NVS 标定持久化
    scale_transport.c WiFi TCP 上报 + 收 0x09 刷新 PC 地址
main/           单一 app_main + UART0 标定命令台
test/           帧组装自测（PC 上可直接跑，43 条断言）
docs/           硬件与主控选择、标定手册、上报通道说明
```

> **协议层是共享的**：`components/fw_proto/CMakeLists.txt` 用 `FWP_ROOT` 指向
> 手环工程的 `fw_core`，只编译 `fw_proto.c`（不编 `fw_band_app.c` —— 那是手环
> 专用的应用状态机）。这样两端**不可能**出现协议实现漂移。

---

## 快速开始

```powershell
# 首次运行脚本若报"未对文件进行数字签名"，先执行：
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force

# 设定目标（本工程默认按经典 ESP32 配；用 S3 就执行这条）
.\flash.ps1 set-target esp32s3

# 编译
.\flash.ps1 build

# 烧录 + 看串口（标定命令在这个串口里输入）
.\flash.ps1 flash monitor
```

### ⚠️ 首次必须标定

未标定时重量恒为 0，且 0x02 帧载荷填 `0xFFFF`（无效值），上位机显示 `--`。
标定走串口命令：

```
t         空载去皮
c500      放上 500g 砝码后用 c500 标定（数字换成你的砝码克数）
s         查看状态确认
r         打印原始 ADC 值（排查接线用）
h         帮助
```

完整流程见 [`docs/CALIBRATION.md`](docs/CALIBRATION.md)。

---

## 不接硬件也能做的验证

帧组装是纯逻辑，可在 PC 上编译自测（**43 条断言**）：

```powershell
$gcc = "D:\Qt5.14.2\Tools\mingw730_64\bin\gcc.exe"
& $gcc -std=c99 -Wall -Wextra -O1 `
    -I components/fw_scale/include -I ..\lower\components\fw_core\include `
    test/scale_selftest.c components/fw_scale/scale_frame.c `
    ..\lower\components\fw_core\fw_proto.c -o test/scale_selftest.exe
.\test\scale_selftest.exe     # 期望：通过 43  失败 0
```

关键断言：

| 断言 | 意义 |
|---|---|
| `65430 g → 6543 → 帧内 0x19 0x8F` | 65.43kg 的字节布局正确 |
| 0/10/500/1000/50000/65430/80000/120000 g **往返误差 ≤ 0.005kg** | 按上位机口径解析回来一致 |
| 未标定 → 载荷 `0xFFFF` | **不冒充 0**，上位机显示 `--` |
| 超量程 / 负读数 → `0xFFFF` | 异常读数不当成有效体重 |
| `ts=0` 帧仍合法且不影响体重解析 | 秤没有校时流程（协议 §7 允许） |

---

## 上报通道（需要上位机配合 ⚠️）

协议 §3 把 0x02 归到**通道 A**（手环那条是 USB 串口），但**秤没有多余的 USB 口**，
所以本工程默认走 **WiFi TCP，端口 45679**。

为什么不是 45678：上位机的 `WifiHandler` 是**单客户端**，手环图像通道已占 45678，
两个设备抢同一端口必然互相打断。

**这需要在上位机再加一个 TCP 监听**（复用 `WifiHandler` 类 + 喂给 `m_scaleParser`，
下游链路不用动）。详见 [`docs/SCALE_TRANSPORT.md`](docs/SCALE_TRANSPORT.md)，
里面也列了三条不需要改上位机的替代路线（经典 ESP32 内置 SPP / USB-TTL / HC-05）。

---

## 文档

- [`docs/HARDWARE.md`](docs/HARDWARE.md) —— 接线、HX711 时序为何不能用 SPI、ESP32 vs ESP32-S3 选择
- [`docs/CALIBRATION.md`](docs/CALIBRATION.md) —— 去皮 + 砝码标定完整流程、调参建议、排查表
- [`docs/SCALE_TRANSPORT.md`](docs/SCALE_TRANSPORT.md) —— 上报通道的冲突分析、上位机改动点、替代方案
