# HealthMonitorSystem —— 从这里开始

一个「亚健康监测系统」：**ESP32-S3 手环**（体征 + 拍照）+ **ESP32 体重秤** + **Qt PC 上位机**，
三者按协议 **v1.2** 通信。

> 本文是**唯一入口**。三块代码各在自己的 git 仓库里，状态与下一步都在这里。

---

## 一、现在到哪一步了

| 部件 | 位置 | git | 验证状态 |
|---|---|---|---|
| **上位机** | `HealthMonitorSystem/` | `808cb8c` | 冒烟 **7 / 7 全绿** |
| **手环固件** | `lower/` | `6f00fa2` | 自测 **145 / 145**；固件 1,016,992 字节 |
| **体重秤固件** | `scale/` | `5934d22` | 自测 **43 / 43**；固件 732,896 字节 |

三级验证链（从规范到实现，逐级收紧）：

```
fw_core_selftest.c   (lower)     固件输出 == 协议规范             145 条
cross_smoke.cpp      (上位机)     固件输出 == 上位机输出，逐字节    47 条   ← 直接比对
scale_tcp_smoke.cpp  (上位机)     秤 TCP 链路端到端（监听→解析→信号） 10 条
comm_smoke.cpp       (上位机)     上位机输出 == 协议规范            既有
```

`cross_smoke` 把固件的协议层与上位机的 `ProtocolParser`/`ImageAssembler` 编进**同一个二进制**，
对同一份逻辑数据分别组帧后逐字节 diff；图像链路还做了
「固件组帧 → 上位机重组」与「固件自行重组」的双向闭环。

`scale_tcp_smoke` 用**秤固件同一份源文件**（`scale_frame.c`）组帧，通过真实 TCP 发进上位机，
验证「监听 45679 → 0x02 解析 → `bodyScaleReceived` 信号」整条链路。

---

## 二、唯一剩下的阻塞：烧录需要手按一次按键

**板子此刻仍在跑旧固件。** 这块扩展板的 **DTR/RTS 没有接到 EN/GPIO0**，
所以软件无法让芯片进入下载模式。已穷尽的尝试：

| 尝试 | 结果 |
|---|---|
| 5 类接线 × 9 组时序 = **45 种** DTR/RTS 组合 | 全部 `boot:0xc`（GPIO0 高） |
| esptool `--before` 三种模式 | 全部失败 |
| 5 种复位时序 + 立即同步 | 全部 `No serial data received` |
| USB-C 原生 USB-Serial/JTAG（可免按键） | 该口**没插设备**（无 `VID_303A`） |

### 你要做的（一条命令）

```powershell
cd D:\YQ\Code\HealthMonitorSystem\lower
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\bringup.ps1
```

倒计时 45 秒内：**按住 BOOT 键 → 点一下 RESET 键 → 松开 BOOT 键**
（在 RESET 弹起前松开 BOOT 是最常见的失败原因）。

脚本会自动**编译 → 提示按键 → 烧录（失败重试 3 次）→ 抓串口日志 → 打印判决表**，
并把完整日志写到 `lower/bringup_log.txt`。

**把判决表那 6 行贴回来即可**，一次就能同时确认：

| 判决项 | 决定什么 |
|---|---|
| PSRAM 是否存在 | 摄像头帧缓冲放 PSRAM(`fb_count=2`) 还是 DRAM(`fb_count=1`) |
| SHT30 @0x44 / MAX30102 @0x57 | 两个传感器是否真在 I2C 总线上 |
| MAX30102 有效 HR/SpO2 | 采样率 bug 是否真修好 |
| FIFO 有无积压 | 同上 |
| `uart driver error` 是否消失 | 旧固件另一个顽疾 |
| WiFi IP / 图像通道 | 通道 A / 通道 B 是否通 |

失败时的备选：

```powershell
.\bringup.ps1 -Baud 115200 -WaitSeconds 90   # 460800 老失败时
.\probe_reset.ps1                            # 单独确认 BOOT 键是否通 GPIO0
.\flash.ps1 monitor                          # 只看串口
```

### ⚠️ 烧完还要切一次上位机配置，否则界面显示的是模拟数据

上位机实际读的是 **构建目录** 那份 `appconfig.ini`（不是源码里那份），而 CMake 的
POST_BUILD 每次构建都会用源码模板覆盖它 —— 模板是 `useSimulatedSource=1` +
`bandPortName=COM3`。所以**只要重新构建过，配置就被复位成模拟源模式**。

用这个脚本一键切换（只改构建目录那份，不动源码模板）：

```powershell
cd D:\YQ\Code\HealthMonitorSystem\tools

.\use_real_band.ps1 -Port COM15 -PcIp 192.168.43.190   # 切到真实硬件
.\use_real_band.ps1 -Show                              # 看当前生效配置
.\use_real_band.ps1 -Simulated                         # 切回模拟源（跑冒烟用）
```

已验证：`-Simulated` 后与源码模板**逐字节一致**（无 BOM、行数不变、注释与各段完整），
正向切换只改 3 个值、其余内容原样保留，可反复往返。

> `wifiPcIp` 建议显式指定：本机除了 WLAN 还有 VMware 虚拟网卡，
> 上位机的"自动挑第一个非回环 IPv4"可能挑错。

---

## 三、还需要你拍板的两件事

### ① 扩展板按键丝印是什么？

有没有独立的 `BOOT`/`IO0` 键，还是只有 `RESET`/`EN`？
**若没有 BOOT 键，就得把 GPIO0 短接到 GND 代替按键** —— 需要你按丝印找引脚。

### ② 体重秤用哪块主控？

协议 §3 把 0x02 归到「通道 A」。手环那条是 USB 串口，但**秤没有多余的 USB 口**，
所以秤固件默认走 **WiFi TCP 45679**。

**这条路的上位机侧已经实现好了**（`[Wifi] useWifiForScale`，默认关闭，
并有 `scale_tcp_smoke` 端到端验证）。要用它只需打开开关：

```ini
[Wifi]
useWifiForScale=1
```

| 方案 | 上位机改动 | 秤侧硬件 | 状态 |
|---|---|---|---|
| **WiFi TCP 45679** | ✅ 已实现（默认关，打开即用） | 无额外硬件 | **推荐** |
| 蓝牙 SPP | 零改动 | 需**经典 ESP32**（内置） | 未实现，需要就说 |
| USB-TTL 转换器 + 串口 | 零改动 | 转换器 | 未实现，最简单 |
| 外接 HC-05 | 零改动 | 模块 + 2 个 GPIO | 未实现 |

之所以默认没走 SPP：**经典 ESP32 与手环的 S3 是两块不同的板子**，
而 WiFi TCP 用同一块 S3 就能验完。详见 [`scale/docs/SCALE_TRANSPORT.md`](scale/docs/SCALE_TRANSPORT.md)。

---

## 四、各部件文档入口

### 手环固件 —— [`lower/`](lower/)
- [`lower/README.md`](lower/README.md) —— 结构、双通道设计、已修问题清单、脚本用法
- [`lower/docs/BUILD_FLASH.md`](lower/docs/BUILD_FLASH.md) —— 构建烧录、启动日志逐项检查、PSRAM 处理、故障排查
- [`lower/docs/NETWORK_CONFIG.md`](lower/docs/NETWORK_CONFIG.md) —— 两条通道的地址来源、WiFi 凭据、联调清单
- [`lower/docs/LCD_TOUCH.md`](lower/docs/LCD_TOUCH.md) —— 引脚表、为何触摸 MISO 是 GPIO39、标定方法
- [`lower/docs/CAMERA_IMAGE.md`](lower/docs/CAMERA_IMAGE.md) —— 摄像头接线、帧缓冲落地、图像链路、调试顺序

### 体重秤固件 —— [`scale/`](scale/)
- [`scale/README.md`](scale/README.md) —— 结构、快速开始、不接硬件的验证
- [`scale/docs/HARDWARE.md`](scale/docs/HARDWARE.md) —— 接线、HX711 为何不能用 SPI、ESP32 vs S3 选择
- [`scale/docs/CALIBRATION.md`](scale/docs/CALIBRATION.md) —— 去皮 + 砝码标定完整流程、调参建议、排查表
- [`scale/docs/SCALE_TRANSPORT.md`](scale/docs/SCALE_TRANSPORT.md) —— 上报通道冲突分析、上位机改动点、替代方案

### 上位机 —— [`HealthMonitorSystem/`](HealthMonitorSystem/)
- `docs/项目设计1.2/` —— 现行设计文档（1.0/1.1 仅历史参照，不要改）
- `交接文件_v1.2.md`（仓库根）—— v1.2 时代的交接说明
- `HealthMonitorSystem_交接文档.md`（仓库根，**较新**）—— 下位机侧交接与已知问题清单

---

## 五、不受硬件限制、随时可跑的验证

```powershell
$gcc = "D:\Qt5.14.2\Tools\mingw730_64\bin\gcc.exe"
$R   = "D:\YQ\Code\HealthMonitorSystem"

# 手环协议层（145 条断言）
Set-Location "$R\lower"
& $gcc -std=c99 -Wall -Wextra -O1 -I components/fw_core/include `
    test/fw_core_selftest.c `
    components/fw_core/fw_proto.c components/fw_core/fw_image_rx.c `
    components/fw_core/fw_image_tx.c components/fw_core/fw_host_router.c `
    -o test/fw_core_selftest.exe
.\test\fw_core_selftest.exe

# 体重秤帧组装（43 条断言）
Set-Location "$R\scale"
& $gcc -std=c99 -Wall -Wextra -O1 -I components/fw_scale/include -I ..\lower\components\fw_core\include `
    test/scale_selftest.c components/fw_scale/scale_frame.c `
    ..\lower\components\fw_core\fw_proto.c -o test/scale_selftest.exe
.\test\scale_selftest.exe

# 上位机 + 跨端交叉验证（6 个 target）
Set-Location "$R\build-HealthMonitorSystem-Desktop_Qt_5_14_2_MinGW_64_bit-Debug"
$env:PATH = "D:\Qt5.14.2\5.14.2\mingw73_64\bin;D:\Qt5.14.2\Tools\mingw730_64\bin;" + $env:PATH
foreach($t in @('dao_smoke','service_smoke','comm_smoke','firmware_smoke','view_smoke','cross_smoke')){
    & ".\$t.exe"
}
```

---

## 六、已被证伪 / 需要澄清的旧信息

这些是排查过程中确认的**旧文档错误**，别再照着做：

| 旧说法 | 实际 |
|---|---|
| 串口是 `COM6` | 本机 COM3~COM10 全是**蓝牙虚拟口**；开发板在 **COM15**（CH343） |
| PC IP 是 `172.20.10.6` | 当前热点下是 **192.168.43.190**（Android 网段） |
| `datum/接线说明.md` 的 LCD 引脚表（SCLK=40/MOSI=41/MISO=42） | GPIO39-42 是 **JTAG**，高速 SPI 会白屏；正确值是 1/2/48/3/21 |
| 协议「通道 A = 蓝牙 SPP」 | **ESP32-S3 不支持蓝牙 Classic**（无 `SOC_BT_CLASSIC_SUPPORTED`）；改用 UART0，对 PC 端协议栈等价 |
| `lower/` 是下位机工程 | 它是**整套上位机的副本**里夹着固件，且 `main/` 有两份冲突的 `app_main`。现在 `lower/` 已重写为纯下位机工程 |
| 仓库根 `CAN_UDS项目交接文档.md` | 与**本项目无关**（另一个 CAN/UDS 工具项目），已删除 |

### 另一个待观察的硬件风险：三个 strapping 脚被外设占用

ESP32-S3 在**硬件复位瞬间**采样 GPIO0/3/45/46 决定启动模式，而本设计把
`LCD_DC=GPIO3`、`TOUCH_CS=GPIO45`、`TOUCH_IRQ=GPIO46` 三个外设信号放在了这些脚上。
若某个在复位期间被拉低，会出现**上电不启动或时好时坏**。

**目前是好的**：ROM 日志报 `boot:0xc (SPI_FAST_FLASH_BOOT)`，说明复位时确实是高电平。

固件已在 `app_main` 开头调用 `fw_lcd_harden_straps()` 把这三个脚显式定为输入+上拉，
但**它修不了本次启动**（采样在软件运行之前）——它的作用是让后续复位的行为确定。
真正的保障在硬件：给 GPIO3/45/46 加 10k 上拉，或把这些信号挪到非 strapping 脚。
详见 [`lower/docs/LCD_TOUCH.md`](lower/docs/LCD_TOUCH.md) 第 0 节。

原始旧代码完整保留在 `lower_old_archive/`（4224 个文件，未删除，随时可回溯）。
