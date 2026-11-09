# ESP32-S3 健康监测手环 —— 固件工程

> 本工程只放**下位机固件**。上位机（Qt 5.14.2）在仓库外层的 `HealthMonitorSystem/`。
> 体重秤固件在 [`../scale/`](../scale/)（独立 ESP-IDF 工程）。

| 项 | 值 |
|---|---|
| 芯片 | ESP32-S3（模组型号待串口日志确认，见 [`docs/BUILD_FLASH.md`](docs/BUILD_FLASH.md)） |
| 框架 | **ESP-IDF v5.5.5**（本机装在 `D:\ESP_IDF\Espressif`） |
| 协议 | **v1.2**（与上位机 `communication/protocolparser.cpp` 字节级一致） |
| 串口 | **COM15** (`USB-Enhanced-SERIAL CH343`)。旧文档写的 COM6 在本机是蓝牙虚拟口 |
| 固件 | `build/health_monitor_band.bin`，约 1.0 MB，3 MB 应用分区 |

---

## 目录结构

```
components/
  fw_core/        协议 v1.2 纯 C（无平台依赖）+ app_config.h
    fw_proto.c        组帧/解析：0x01~0x05、0x06/0x07/0x08、0x09/0x0A/0x0B、接收滑窗
    fw_image_rx.c     图像分片重组（单槽五场景，对齐上位机 ImageAssembler）
    fw_image_tx.c     图像组帧与切片（0x06/0x07/0x08，回调注入发送方式）
    fw_host_router.c  下行帧路由（0x09/0x0B/0x0A 归板级，0x03/0x05 转交 app）
    fw_band_app.c     手环应用状态机（采样节拍 / 0x03 校时 / 0x05 补传）
    fw_log.c          离线缓存环形日志
  fw_net/         WiFi 图像通道（通道 B）：PC 服务端 45678，退避重连
  fw_sensors/     MAX30102（含 Maxim 算法）+ SHT30 + 马达
  fw_camera/      ATK-OV2640 采集（PSRAM/DRAM 帧缓冲运行时自适应）
  fw_lcd/         ILI9341 显示 + XPT2046 触摸 + LVGL 界面
main/             单一 app_main：双通道装配、启动自检报告、UI/摄像头任务
test/             协议层自测（PC 上可直接跑，145 条断言，不需要硬件）
scripts/          fetch_deps.ps1（准备第三方组件）
docs/             构建烧录、网络配置、LCD/触摸、摄像头/图像链路
third_party/      第三方组件（lvgl / esp32_camera，不进版本控制）
```

> ⚠️ **两个组件目录的坑**：
> 1. ESP-IDF **不递归扫描嵌套组件**。`lvgl` 必须放在 `components/` 或
>    `EXTRA_COMPONENT_DIRS` 所列目录的**直接子目录**下。放进
>    `components/fw_lcd/lvgl` 会报 `Failed to resolve component 'lvgl'`。
> 2. **组件名 = 目录名**。所以第三方摄像头组件放在 `third_party/esp32_camera`
>    （下划线），而不是上游的 `esp32-camera`（横线），否则 `REQUIRES` 写不对。

## 双通道设计

| 通道 | 承载 | 实现 |
|---|---|---|
| **A** | 体征 / 控制 0x01~0x05、0x09、0x0B | UART0 → PC 的 COM 口，115200 8N1 |
| **B** | 图像 0x06/0x07/0x08 | WiFi TCP，PC 服务端 **45678**，设备客户端 |

> **为什么通道 A 不是蓝牙 SPP**：协议原文写「通道 A = 蓝牙 SPP」，但 **ESP32-S3 不支持蓝牙
> Classic**（`soc_caps.h` 只有 `SOC_BLE_SUPPORTED`，没有 `SOC_BT_CLASSIC_SUPPORTED`；ESP-IDF
> `examples/bluetooth/` 下也没有 classic_bt 示例）。改用 USB 串口对 PC 端协议栈完全等价——
> 两者都只是字节流，所以上位机一行都不用改。

---

## 快速开始

```powershell
# 准备第三方组件（只需一次；从旧工程归档复制，不联网）
.\scripts\fetch_deps.ps1

# 编译
.\flash.ps1 build

# 看串口
.\flash.ps1 monitor
```

> 首次运行脚本若报"未对文件进行数字签名"，先执行：
> `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force`

### ⚠️ 这块板的烧录有个坑 —— 用 `bringup.ps1` 一把过

本板 **DTR/RTS 没有接到 EN/GPIO0**——`find_download_timing.ps1` 扫了 5 类接线 ×
9 组时序共 45 种组合，**每一种都返回 `boot:0xc (SPI_FAST_FLASH_BOOT)`**（GPIO0 高 =
正常启动）；esptool 的 `default_reset` / `usb_reset` / `no_reset_no_sync` 三种模式
也全部失败。所以**必须手按按键进下载模式**。

既然要手按，就把整条链路压成**一次运行**：

```powershell
.\bringup.ps1
```

它按顺序做完：**① 编译 → ② 倒计时（你在此期间按下述按键）→ ③ 烧录（失败自动重试并
重新提示按键）→ ④ 立刻抓串口日志 → ⑤ 打印判决表 → ⑥ 完整日志落到 `bringup_log.txt`**

按键动作（倒计时期间）：

```
1. 按住 BOOT 键（别松）
2. 点一下 RESET 键
3. 松开 BOOT 键
```

> 在 RESET 还没弹起前就松开 BOOT 是最常见的失败原因。
> **若这块板没有 BOOT 键，就把 GPIO0 短接到 GND 代替第 1 步。**

脚本会先判断板上跑的是哪一版固件；**若发现还是旧固件，会直接退出（码 2）并给出
下一步指引**，而不是刷出一堆误导性的 "NOT FOUND"（旧固件根本不打印 PSRAM 和 I2C 扫描行）。

判决表逐项回答我们真正关心的问题：

| 判决项 | 为什么关心 |
|---|---|
| PSRAM 是否存在 | 决定摄像头帧缓冲放 PSRAM(`fb_count=2`) 还是 DRAM(`fb_count=1`) |
| SHT30 @0x44 / MAX30102 @0x57 | 两个传感器是否真的在 I2C 总线上 |
| MAX30102 是否输出有效 HR/SpO2 | 采样率 bug 是否真修好了 |
| 是否有 FIFO 积压 | 同上，积压说明 10ms 采样任务被饿死 |
| `uart driver error` 是否消失 | 旧固件的另一个顽疾 |
| WiFi 是否拿到 IP、图像通道是否建立 | 通道 A / 通道 B 是否通 |

常用变体：

```powershell
.\bringup.ps1 -Baud 115200 -WaitSeconds 90   # 460800 老是失败时
.\bringup.ps1 -SkipBuild                     # 已经编译过
.\bringup.ps1 -BuildOnly                     # 只编译
.\probe_reset.ps1                            # 单独确认 BOOT 键是否真的通 GPIO0
```

详细排查见 [`docs/BUILD_FLASH.md`](docs/BUILD_FLASH.md)。

---

## 不接硬件也能做的验证

协议层是纯 C，可在 PC 上直接编译运行自测（**145 条断言**）：

```powershell
$gcc = "D:\Qt5.14.2\Tools\mingw730_64\bin\gcc.exe"
& $gcc -std=c99 -Wall -Wextra -O1 -I components/fw_core/include `
    test/fw_core_selftest.c `
    components/fw_core/fw_proto.c components/fw_core/fw_image_rx.c `
    components/fw_core/fw_image_tx.c components/fw_core/fw_host_router.c `
    -o test/fw_core_selftest.exe
.\test\fw_core_selftest.exe     # 期望：通过 145  失败 0
```

覆盖内容：

| 段 | 内容 |
|---|---|
| A | 帧结构 / 校验和 / **红线 1**（分片载荷上限 1027，不是 1024） |
| B | 0x06/0x07/0x08 三帧布局、分片数计算（边界 0/1/1024/1025/12659） |
| C | **红线 2**（下行帧必须 15 字节定长；截断/拉长/长度不符全判非法） |
| D | 接收滑窗：逐字节喂、粘包 3 帧、脏字节重同步、坏校验后仍能解后续帧 |
| E | 图像重组单槽五场景（正常/乱序/丢片超时/重复片/新图抢占）+ 头字段非法 |
| F | 整图链路（组帧→滑窗→重组） |
| G | 下行帧路由（0x0B/0x09/0x0A 触发钩子、0x03/0x05 转交、坏帧不触发） |
| H | 图像发送端到端：JPEG→组帧→重组**逐字节相同**、图 ID 边界、通道中断即停 |

> 另有一层更强的验证在上位机仓库里：`HealthMonitorSystem/test/cross_smoke.cpp`
> 把**本工程的协议层**与**上位机的 ProtocolParser/ImageAssembler** 编进同一个二进制，
> 对同一份逻辑数据分别组帧并逐字节比较（47 条断言）。见
> [`../HealthMonitorSystem/test/cross_smoke.cpp`](../HealthMonitorSystem/test/cross_smoke.cpp)。

---

## 相对旧固件修掉的问题

| 问题（来自交接文档） | 本版处理 |
|---|---|
| `main` 目录有两份冲突的 `app_main`（`test_modules.c` 走串口+LVGL、`main.c` 走 WiFi，后者从未进 CMake） | 只留一份 `main/main.c`，双通道装配 |
| 摄像头直发裸 JPEG，未按协议分片 | `fw_image_tx` 组 0x06/0x07/0x08，自测 H 段端到端验证 |
| 体征帧每发一帧新建一次 TCP 连接 | 改为长连接 + 退避重连（1s/2s/5s/10s） |
| MAX30102 采样率不匹配（1000ms 读 1 个样本 vs 100sps） | 独立 **10ms** 任务，一次读干 FIFO |
| SHT30 读不到数据 | 改 `0x24 0x00`（无时钟拉伸）+ 重复起始读 + **CRC8 校验** |
| `uart_write_bytes` 报 `uart driver error` | `app_main` 开头 `uart_driver_install` |
| 无效值返回 0 冒充有效值 | 按协议填哨兵 `0xFFFF`/`0xFF`/`0x7FFF` |
| 触摸驱动是空壳 | 真写 XPT2046：命令字节、12 位读取、标定/方向运行时接口 |
| 上位机不认 `hr_state`/降级显示 | LVGL 界面降级值加 `?` 标记 |

## 文档

- [`docs/BUILD_FLASH.md`](docs/BUILD_FLASH.md) —— 构建、烧录、启动日志逐项检查、PSRAM 处理、故障排查
- [`docs/NETWORK_CONFIG.md`](docs/NETWORK_CONFIG.md) —— 两条通道的地址来源、WiFi 凭据、联调清单
- [`docs/LCD_TOUCH.md`](docs/LCD_TOUCH.md) —— 引脚表、为何触摸 MISO 是 GPIO39、标定方法、LVGL 配置
- [`docs/CAMERA_IMAGE.md`](docs/CAMERA_IMAGE.md) —— 摄像头接线、帧缓冲落地、图像链路、调试顺序
