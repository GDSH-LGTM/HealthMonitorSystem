# HealthMonitorSystem 项目交接文档

> **文档生成时间**：2026-10-13
> **项目名称**：HealthMonitorSystem（健康监测系统）
> **项目根目录**：`D:\Code4\HealthMonitorSystem`
> **本文档目的**：确保新智能体在完全不了解项目历史的前提下，能够准确、快速地接手任务，不重复踩坑。

---

## 一、项目最终目标

### 1.1 整体目标
构建一套完整的**健康监测系统**，包含两大部分：

1. **ESP32-S3 健康监测手环（固件端）**
   - 通过 MAX30102 传感器采集**心率、血氧**
   - 通过 SHT30 传感器采集**环境温度、湿度**
   - 通过 ILI9341 LCD 屏幕（LVGL 图形库）**本地显示**健康数据
   - 支持 XPT2046 **触摸屏**交互
   - 通过 WiFi/串口将数据传输给 PC 上位机
   - 通过 ATK-OV2640 摄像头采集图像（饮食拍照识别）
   - 异常时通过振动马达报警

2. **Qt PC 上位机（桌面端）**
   - 实时显示手环采集的心率、血氧、体温、湿度数据
   - 健康数据曲线图表展示
   - 饮食管理（菜品拍照、识别、记录）
   - 健康报告生成
   - 体检数据管理
   - 系统设置（串口、WiFi、阈值配置）
   - SQLite 本地数据库存储历史数据

### 1.2 验收标准
- 手环开机后 LCD 正常显示 LVGL 健康监测界面
- 手指按住 MAX30102 后能测出心率和血氧
- SHT30 能实时显示环境温湿度
- 上位机通过串口（或 WiFi）接收到真实数据并显示（不是模拟数据、不是 `--`）
- 触摸屏可以正常点击交互
- 摄像头可以拍照并传输到上位机

---

## 二、硬件环境详细说明

### 2.1 核心开发板
- **型号**：ESP32-S3-DevKitC-1 v1.1
- **模组**：ESP32-S3-WROOM-1-N16R8
  - Flash：16MB（Quad SPI）
  - PSRAM：8MB（Octal SPI，AP_3v3）
- **芯片**：ESP32-S3 (QFN56)，revision v0.2
- **特性**：WiFi、BLE、8MB Embedded PSRAM
- **晶振**：40MHz
- **MAC 地址**：7c:4f:ad:bd:49:54

### 2.2 串口连接
- **串口号**：**COM6**
- **USB 芯片**：USB-Enhanced-SERIAL CH343
- **烧录波特率**：460800
- **通信波特率**：115200
- **数据线**：用户已连接（注意必须是数据线，不能是纯充电线）
- **烧录方式**：通过开发板 Micro-USB 口（UART0，GPIO43=TXD0, GPIO44=RXD0）

### 2.3 外设清单

| 外设 | 型号 | 接口 | 用途 |
|------|------|------|------|
| LCD 显示屏 | ILI9341 2.8寸 TFT，240×320 | SPI | 本地图形显示 |
| 触摸芯片 | XPT2046（LCD 板载） | SPI（共用 LCD 总线） | 触摸输入 |
| 心率血氧传感器 | MAX30102 | I2C，地址 0x57 | 心率、血氧采集 |
| 温湿度传感器 | SHT30 | I2C，地址 0x44 | 环境温湿度采集 |
| 摄像头 | ATK-OV2640（内置12MHz晶振） | DVP | 饮食拍照 |
| 振动马达 | 直流振动马达 | GPIO 控制 | 异常报警 |
| 板载 RGB LED | WS2812 | GPIO38（RMT驱动） | 状态指示 |

### 2.4 LCD 模块引脚物理定义（重要）
LCD 模块背面左侧排针，**从上到下**共 14 个引脚：

```
 1. T_IRQ        ← 触摸中断
 2. T_DO         ← 触摸 MISO
 3. T_DIN        ← 触摸 MOSI
 4. T_CS         ← 触摸片选
 5. T_CLK        ← 触摸时钟
 6. SDO(MISO)    ← LCD MISO（不用）
 7. LED          ← 背光（直接接 VCC）
 8. SCK          ← LCD SPI 时钟
 9. SDI(MOSI)    ← LCD SPI 数据
10. DC           ← 数据/命令选择
11. RESET       ← 复位
12. CS           ← LCD 片选
13. GND          ← 地
14. VCC          ← 3.3V 电源
```

右侧排针是 SD 卡引脚（SD_SCK / SD_MISO / SD_MOSI / SD_CS），本项目不使用。

---

## 三、完整引脚分配表（最终版，已更新到代码）

> **引脚配置文件**：`D:\Code4\HealthMonitorSystem\firmware\esp32\main\pin_config.h`

### 3.1 LCD ILI9341（使用 SPI3_HOST）

| 信号 | GPIO | 说明 |
|------|------|------|
| LCD_PIN_SCLK | **GPIO1** | SPI 时钟 |
| LCD_PIN_MOSI | **GPIO2** | SPI 主机输出 |
| LCD_PIN_MISO | -1（不接） | LCD MISO 不用 |
| LCD_PIN_CS | **GPIO48** | LCD 片选 |
| LCD_PIN_DC | **GPIO3** | 数据/命令选择 |
| LCD_PIN_RST | **GPIO21** | LCD 硬件复位 |
| SPI 频率 | 1 MHz | 杜邦线安全值（可尝试提高到 10-20MHz） |

### 3.2 触摸 XPT2046（共用 LCD SPI 总线）

| 信号 | GPIO | 说明 |
|------|------|------|
| T_CLK | GPIO1 | 共用 LCD SCK，不单独接线 |
| T_DIN | GPIO2 | 共用 LCD MOSI，不单独接线 |
| TOUCH_PIN_MISO (T_DO) | **GPIO39** | 触摸 MISO（JTAG 引脚，低速 SPI 可用） |
| TOUCH_PIN_CS (T_CS) | **GPIO45** | 触摸独立片选 |
| TOUCH_PIN_IRQ (T_IRQ) | **GPIO46** | 触摸中断输出 |

### 3.3 I2C 总线（I2C_NUM_0）

| 信号 | GPIO | 说明 |
|------|------|------|
| I2C_PIN_SCL | **GPIO19** | I2C 时钟（原 USB_D-，用 Micro-USB 口时空闲） |
| I2C_PIN_SDA | **GPIO20** | I2C 数据（原 USB_D+，用 Micro-USB 口时空闲） |
| I2C 频率 | 100 kHz | 标准模式 |
| 内部上拉 | 已启用 | `enable_internal_pullup = true` |

I2C 设备地址：
- **0x44** = SHT30 温湿度传感器（已确认在线）
- **0x57** = MAX30102 心率血氧传感器（已确认在线）

### 3.4 摄像头 ATK-OV2640（DVP 接口）

| 信号 | GPIO | 信号 | GPIO |
|------|------|------|------|
| CAM_PIN_D0 | GPIO4 | CAM_PIN_D4 | GPIO8 |
| CAM_PIN_D1 | GPIO5 | CAM_PIN_D5 | GPIO9 |
| CAM_PIN_D2 | GPIO6 | CAM_PIN_D6 | GPIO10 |
| CAM_PIN_D3 | GPIO7 | CAM_PIN_D7 | GPIO11 |
| CAM_PIN_VSYNC | GPIO12 | CAM_PIN_HSYNC | GPIO13 |
| CAM_PIN_PCLK | GPIO14 | CAM_PIN_XCLK | **-1**（内置12MHz晶振） |
| CAM_PIN_SCCB_S (SCL) | GPIO16 | CAM_PIN_SCCB_D (SDA) | GPIO17 |
| CAM_PIN_PWDN | GPIO18 | CAM_PIN_RESET | GPIO47 |

### 3.5 其他引脚

| 功能 | GPIO | 说明 |
|------|------|------|
| MOTOR_PIN | **GPIO15** | 振动马达控制（高电平启动） |
| LED_PIN | **GPIO38** | 板载 WS2812 RGB LED |
| UART0 TXD | GPIO43 | 烧录/日志/串口数据（不可占用） |
| UART0 RXD | GPIO44 | 烧录/日志/串口数据（不可占用） |

### 3.6 不可用引脚（踩坑重点）

- **GPIO22 ~ GPIO37**：连接 SPI Flash / Octal PSRAM，**绝对不能使用**
  - GPIO22-25：SPI flash
  - GPIO26-32：SPI flash
  - GPIO33-37：Octal PSRAM（含 35/36/37）
- **GPIO39 ~ GPIO42**：默认 JTAG 调试引脚
  - 高速 LCD SPI 不能用（会导致白屏/花屏）
  - 但触摸这种低速 SPI 可以使用（当前 T_DO 接 GPIO39）
- **GPIO19 / GPIO20**：USB OTG 的 D-/D+，使用 Micro-USB 口供电烧录时可用于 I2C
- **GPIO0**：Boot 引脚，开机时电平影响启动模式，谨慎使用

---

## 四、软件环境详细说明

### 4.1 ESP-IDF 固件环境

- **ESP-IDF 版本**：**v5.5.5**
- **IDF 安装路径**：`D:\Espressif\frameworks\esp-idf-v5.5.5`
- **IDF 工具根路径**：`D:\Espressif`（IDF_TOOLS_PATH）
- **Python 虚拟环境**：`D:\Espressif\python_env\idf5.5_py3.11_env`
  - Python 可执行文件：`D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe`
- **工具链**：
  - xtensa-esp-elf：`D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin`
  - riscv32-esp-elf：`D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin`
  - ninja：`D:\Espressif\tools\ninja\1.12.1`
  - cmake：`D:\Espressif\tools\cmake\3.30.2\bin`
  - ccache：`D:\Espressif\tools\ccache\4.12.1`
  - idf-git：`D:\Espressif\tools\idf-git\2.44.0\cmd`

- **固件工程目录**：`D:\Code4\HealthMonitorSystem\firmware\esp32`
- **固件构建目录**：`D:\Code4\HealthMonitorSystem\firmware\esp32\build`

### 4.2 LVGL 图形库

- **版本**：**LVGL 8.x**（用户明确要求用 8.5，不用 9.6）
- **管理方式**：ESP Component Registry 托管组件
- **路径**：`D:\Code4\HealthMonitorSystem\firmware\esp32\managed_components\lvgl__lvgl`
- **组件声明**：`firmware/esp32/main/idf_component.yml`
- **显示缓冲区**：双缓冲，每块 `240 × 40` 像素

### 4.3 其他托管组件（managed_components）

| 组件 | 路径 | 用途 |
|------|------|------|
| espressif__esp32-camera | managed_components/espressif__esp32-camera | OV2640 摄像头驱动 |
| espressif__esp_jpeg | managed_components/espressif__esp_jpeg | JPEG 编解码 |
| espressif__led_strip | managed_components/espressif__led_strip | WS2812 RGB LED 驱动 |
| lvgl__lvgl | managed_components/lvgl__lvgl | LVGL 图形库 |

### 4.4 Qt 上位机环境

- **Qt 版本**：Qt 5.14.2
- **编译器套件**：Desktop Qt 5.14.2 MinGW 64-bit
- **构建目录**：`D:\Code4\build-HealthMonitorSystem-Desktop_Qt_5_14_2_MinGW_64_bit-Debug`
- **可执行文件**：`D:\Code4\build-HealthMonitorSystem-Desktop_Qt_5_14_2_MinGW_64_bit-Debug\HealthMonitorSystem.exe`
- **数据库**：SQLite，运行时生成 `health.db`
- **构建系统**：CMake（根目录 CMakeLists.txt）

### 4.5 WiFi 网络配置

- **热点 SSID**：`瞳瞳的iphone`（手机热点）
- **热点密码**：`1234567890`
- **手环获取 IP**：`172.20.10.2`
- **PC（上位机）IP**：`172.20.10.6`
- **网关**：`172.20.10.1`
- **子网掩码**：255.255.255.240
- **WiFi 图像通道 TCP 端口**：**45678**
- **连接信道**：channel 6，RSSI -30（信号很好）

---

## 五、通信协议说明

### 5.1 协议版本
- **协议版本**：v1.1
- **固件端实现**：`firmware/core/fw_proto.h`（纯 C）
- **上位机端实现**：`communication/protocolparser.cpp`
- **要求**：两端字节级一致

### 5.2 帧格式

```
| 帧头(2) | 设备(1) | 类型(1) | 时间戳(4,大端) | 长度(2,大端) | 载荷(N) | 校验和(1) |
| AA 55   | dev     | type    | ts (BE)        | len (BE)     | payload | sum       |
```

- 帧头：`0xAA 0x55`
- 帧头总长度：10 字节
- 校验：从 dev 开始到 payload 结束所有字节的累加和取低 8 位

### 5.3 设备类型定义

| 值 | 设备 |
|----|------|
| 0x00 | FW_DEV_HOST（PC 主机） |
| 0x01 | FW_DEV_BAND（手环） |
| 0x02 | FW_DEV_SCALE（体重秤） |

### 5.4 帧类型定义

| 类型值 | 名称 | 方向 | 载荷长度 |
|--------|------|------|----------|
| 0x01 | FW_T_VITAL（体征数据） | 手环 → PC | 13 字节，整帧 24 字节 |
| 0x02 | FW_T_WEIGHT（体重） | 体重秤 → PC | 2 字节，整帧 13 字节 |
| 0x03 | FW_T_TIMESYNC（时间同步） | PC → 手环 | 4 字节，整帧 15 字节 |
| 0x04 | FW_T_BACKLOG（补传请求应答） | 手环 → PC | 3 字节，整帧 14 字节 |
| 0x05 | FW_T_SYNCREQ（同步请求） | PC → 手环 | 4 字节，整帧 15 字节 |

### 5.5 体征数据（VITAL）载荷结构（13 字节）

| 字段 | 长度 | 说明 | 无效值 |
|------|------|------|--------|
| ts | 4 字节 | 采集时刻 epoch 秒（0=未校时） | - |
| hr | 2 字节 | 心率 BPM | 0xFFFF |
| spo2 | 1 字节 | 血氧百分比 | 0xFF |
| temp_x10 | 2 字节 | 温度 ×10（0.1℃） | 0x7FFF |
| hum_x10 | 2 字节 | 湿度 ×10（0.1%RH） | 0xFFFF |
| sweat | 1 字节 | 出汗等级 0~5 | 0xFF |
| cal | 4 字节 | 当日累计千卡 | 0xFFFFFFFF |
| hr_state | 1 字节 | 0正常/1无效/2降级 | - |

> 注意：载荷字段总长度按协议为 13 字节，具体打包顺序以 `fw_build_vital()` 实现为准。

### 5.6 当前数据传输方式
- **当前主要通道**：**UART0 串口（COM6）**
- 固件在 `send_vital_to_pc()` 中通过 `uart_write_bytes(UART_NUM_0, buf, len)` 发送
- 上位机通过 `serialporthandler` 接收，`protocolparser` 解析
- WiFi TCP 通道代码已写（`wifi_tcp_connect/send`），但当前主程序未启用

---

## 六、项目目录结构

```
D:\Code4\HealthMonitorSystem\
├── main.cpp                     # Qt 上位机入口
├── CMakeLists.txt               # Qt 上位机构建脚本
├── resources.qrc                # Qt 资源文件
├── lcd_pinout.png               # LCD 引脚图
│
├── config/                      # 配置层
│   ├── appconfig.ini            # 应用配置文件（源码模板）
│   ├── appconfigmanager.cpp/h   # 配置管理器
│
├── communication/               # 通信适配层
│   ├── communicationmanager.cpp/h  # 通信总管理
│   ├── protocolparser.cpp/h     # 协议帧解析
│   ├── serialporthandler.cpp/h  # 串口处理
│   ├── wifihandler.cpp/h        # WiFi 处理
│   ├── imageassembler.cpp/h     # 图像分片重组
│
├── dao/                         # 数据访问层（SQLite）
├── model/                       # 数据模型层
├── service/                     # 业务逻辑层
├── view/                        # 表现层
│   ├── mainwindow.cpp/h         # 主窗口
│   └── pages/
│       ├── realtimemonitorpage.cpp/h   # 实时监测页
│       ├── bodydatapage.cpp/h          # 身体数据页
│       ├── dietmanagepage.cpp/h        # 饮食管理页
│       ├── healthreportpage.cpp/h      # 健康报告页
│       ├── imagepage.cpp/h             # 图像页
│       ├── physicalexampage.cpp/h      # 体检页
│       └── systemsettingpage.cpp/h     # 系统设置页
│
├── utils/                       # 工具类
├── examples/                    # 示例程序
├── test/                        # 测试代码
├── photo/                       # 图片保存目录
├── docs/                        # 项目设计文档（1.0/1.1/1.2 三个版本）
│
└── firmware/                    # 固件代码
    ├── core/
    │   └── fw_proto.h           # 通信协议核心定义（纯C，跨平台）
    ├── hal/                     # 硬件抽象层
    ├── sim/                     # 模拟器实现
    ├── stm32/                   # STM32 版本（备选，未完成）
    └── esp32/                   # ★ ESP32 工程（当前主力）
        ├── CMakeLists.txt       # ESP-IDF 工程顶层
        ├── sdkconfig            # SDK 配置
        ├── .vscode/             # VSCode 配置
        ├── build/               # 构建输出目录
        ├── managed_components/  # 托管组件（LVGL、摄像头等）
        └── main/
            ├── CMakeLists.txt
            ├── idf_component.yml    # 组件依赖声明
            ├── pin_config.h         # ★ 引脚配置（最终版）
            ├── test_modules.c       # ★ 当前主程序（app_main 所在）
            ├── main.c               # 旧版主程序（未使用）
            ├── i2c_scan_main.c      # I2C 扫描测试程序
            └── drivers/
                ├── display.c/h      # ★ LCD ILI9341 驱动
                ├── sensors.c/h      # ★ 传感器+WiFi+TCP+马达驱动
                ├── max30102_algo.c/h # MAX30102 Maxim 官方算法
```

---

## 七、已完成任务清单（详细记录）

### 7.1 VSCode ESP-IDF 开发环境配置 ✅
- 已定位并修复 ESP-IDF 插件配置问题
- 确认 IDF v5.5.5 路径、工具链路径、Python 环境
- 命令行编译烧录路径已跑通（当插件 GUI 出问题时可用命令行替代）
- 串口驱动正常（CH343，COM6）

### 7.2 LCD 显示屏驱动调通 ✅
- **最初问题**：LCD 白屏，无任何颜色变化
- **排查过程中发现的引脚错误**：
  1. 最初引脚分配使用了 GPIO39-42（JTAG 引脚），导致高速 SPI 无法正常工作
  2. CS 控制逻辑错误
  3. 多次引脚分配冲突
- **最终解决**：
  - LCD SPI 改为 GPIO1(SCLK)/GPIO2(MOSI)/GPIO3(DC)/GPIO48(CS)/GPIO21(RST)
  - 使用 ESP-IDF 官方 SPI master 驱动（见 display.c）
  - 初始化流程：硬件复位 → 发送 ILI9341 初始化命令序列 → 填充测试色 → 正常显示
- **当前状态**：LCD 可正常显示，LVGL 界面渲染正常

### 7.3 LVGL 图形界面集成 ✅
- 集成 LVGL 8.x（用户明确不用 9.x）
- 实现 display flush 回调，对接 LCD 驱动
- 创建健康监测界面：
  - 标题 "Health Monitor"
  - HR（心率）标签，红色
  - SpO2（血氧）标签，蓝色
  - Temp（温度）标签，黄色
  - Hum（湿度）标签，绿色
  - 底部 WiFi 连接状态标签
- 双缓冲配置，主循环中调用 `lv_timer_handler()`

### 7.4 WiFi 连接调通 ✅
- **最初 Bug**：只调用了 `esp_wifi_start()`，没有调用 `esp_wifi_connect()`，导致一直不连接
- **修复后**：
  - 在 WIFI_EVENT_STA_START 事件中调用 `esp_wifi_connect()`
  - 断线自动重连
  - 成功连接手机热点 "瞳瞳的iphone"
  - 成功获取 IP：**172.20.10.2**
  - LVGL 底部状态可显示 connected/disconnected

### 7.5 串口通信链路打通 ✅
- 固件通过 UART0 发送协议帧
- 上位机串口接收链路已通
- 上位机配置文件已修改（构建目录中的 appconfig.ini）：
  - `useSimulatedSource=0`（关闭模拟数据，使用真实硬件）
  - `bandPortName=COM6`（手环串口号）
  - `baudRate=115200`

### 7.6 I2C 传感器硬件检测 ✅
- 编写了纯 I2C 总线扫描程序（遍历地址 1~126，用 `i2c_master_probe()` 探测）
- **扫描结果确认两个设备都在线**：
  ```
  Found device at 0x44  → SHT30 温湿度
  Found device at 0x57  → MAX30102 心率血氧
  Found 2 devices total
  ```
- **结论**：I2C 接线（SCL=GPIO19, SDA=GPIO20）完全正确，硬件无故障，问题在读取代码

### 7.7 触摸引脚分配更新 ✅
- 发现原触摸引脚定义错误（TOUCH_PIN_CS=1, TOUCH_PIN_IRQ=2 与 LCD SCLK/MOSI 冲突）
- 已更新 pin_config.h：
  - TOUCH_PIN_CS = 45
  - TOUCH_PIN_IRQ = 46
  - TOUCH_PIN_MISO = 39
- 用户已按新引脚接线

---

## 八、目前进度总览

| 模块 | 完成度 | 状态说明 |
|------|--------|----------|
| ESP-IDF 环境配置 | 100% | 命令行编译烧录完全可用 |
| LCD 显示驱动 | 100% | ILI9341 正常显示 |
| LVGL 界面框架 | 90% | 基础界面跑通，待接入真实数据、触摸交互 |
| WiFi 连接 | 100% | 可连接热点获取 IP |
| 串口通信链路 | 80% | 链路通，数据解析/冲突待解决 |
| I2C 硬件检测 | 100% | 两个传感器均确认在线 |
| SHT30 数据读取 | 20% | 设备在线但读不到有效数据 |
| MAX30102 数据读取 | 10% | 设备在线，算法集成但未测出数据 |
| 触摸功能 | 15% | 引脚分配完成，驱动代码未写 |
| 上位机真实数据显示 | 30% | 界面就绪，真实数据未正确显示 |
| 摄像头 | 10% | 配置代码已写，未实际调试 |
| WiFi TCP 图像传输 | 20% | 代码框架已写，未联调 |
| 马达报警 | 40% | 驱动代码已写，未在主程序完整启用 |

**整体进度估计：约 70%**

---

## 九、尚未解决的问题（重点，含现象与排查方向）

### 问题 1：SHT30 温湿度读不到有效数据 ⚠️ 优先级：最高

- **现象**：
  - I2C 扫描能找到 0x44（设备 ACK 正常）
  - 但屏幕上 Temp/Hum 一直是初始值/`--`，没有真实室温数据
  - 之前尝试在 `sht30_read()` 加日志，但日志未出现在串口输出中（可能修改未成功编译，或函数未被正确调用）

- **当前读取代码逻辑**（sensors.c 第 155-172 行）：
  1. 发送单次测量命令：`0x2C 0x06`（高重复性，时钟拉伸）
  2. 延时 20ms
  3. 读取 6 字节：温度 MSB/LSB/CRC + 湿度 MSB/LSB/CRC
  4. 换算：
     - 温度 = -45 + raw_t × 175 / 65535（代码中用 ×1750 得到 0.1℃）
     - 湿度 = raw_h × 100 / 65535（代码中用 ×1000 得到 0.1%）

- **可能原因与排查方向**：
  1. **测量命令问题**：`0x2C 0x06` 启用了时钟拉伸，若主机不支持可能挂起；可改用 `0x24 0x00`（单次高重复性，无时钟拉伸）或 `0x24 0x0B`
  2. **接收方式问题**：当前用 `i2c_master_receive()` 单独发起读操作，部分传感器要求用 `transmit_receive`（重复起始条件）；可改用 `i2c_master_transmit_receive()`
  3. **设备句柄问题**：确认 `dev_sht30` 句柄在添加设备后确实可用
  4. **延时不足**：0x2C0x06 高重复性最大测量时间约 15ms，20ms 一般够，可加到 30ms
  5. **CRC 校验**：当前未校验 CRC，可加上验证数据有效性
  6. **软复位命令**：初始化时发了 `0x30 0xA2`，确认之后等待时间足够（至少 1.5ms）
  7. **建议先写独立最小测试**：单独一个 app_main 只做 SHT30 读取并打印原始 6 字节，确认能拿到非 0x00/0xFF 的数据

### 问题 2：uart_write_bytes 报错 ⚠️ 优先级：高

- **现象**：串口日志周期性出现
  ```
  E (xxxx) uart: uart_write_bytes(1632): uart driver error
  ```
- **原因分析**：
  - UART0 默认被日志系统（console）使用，但没有通过 `uart_driver_install()` 安装 UART 驱动
  - 直接调用 `uart_write_bytes()` 需要先安装驱动，否则报错
  - 日志输出（ESP_LOG）走的是 console 底层写入，不需要驱动；但 `uart_write_bytes()` 需要
- **解决方案（二选一）**：
  1. **方案 A（推荐）**：在初始化早期调用 `uart_driver_install(UART_NUM_0, 256, 256, 0, NULL, 0)`，安装驱动后日志和数据可共存
  2. **方案 B**：换一个 UART（如 UART1）专门传数据，用其他 GPIO，UART0 只做日志；但需要额外接线
  3. **方案 C**：不用 `uart_write_bytes()`，改用 `fwrite(stdout)` 或 `esp_rom_uart_tx`，但不规范

### 问题 3：上位机显示真实数据为 `--` ⚠️ 优先级：高

- **现象**：上位机实时监测页面心率、血氧、体温等都显示 `--`，看不到真实数据
- **可能原因**：
  1. 固件发送数据失败（与问题 2 相关，uart_write_bytes 报错导致数据根本没发出）
  2. 固件发送的是无效值（传感器读取失败，hr=0xFFFF 等），上位机正确显示为 `--`
  3. 数据解析不匹配（字段顺序、大小端、帧长度）
  4. 上位机没有正确选中/打开 COM6
- **排查顺序**：
  1. 先解决固件传感器读取（问题 1）和 uart 驱动（问题 2）
  2. 用串口助手/抓包确认 COM6 上确实有 `AA 55` 开头的数据帧
  3. 对照 `protocolparser.cpp` 和 `fw_build_vital()` 检查字段是否一致
  4. 注意：打开上位机串口监视器时，其他工具（如 idf.py monitor）不能同时占用 COM6

### 问题 4：MAX30102 心率血氧未测出 ⚠️ 优先级：中

- **现象**：设备 I2C 在线，但未得到有效心率/血氧
- **当前代码逻辑**：
  - 配置为 SpO2 模式（红光+红外），100sps，4096 ADC，400μs 脉宽
  - LED 电流 0x24（约 7mA）
  - 采集 500 样本（5 秒）后调用 Maxim 官方算法 `maxim_heart_rate_and_oxygen_saturation()`
  - 之后滑动窗口，每次丢弃 100 旧样本、采 100 新样本
- **关键 Bug 嫌疑**：
  - **采样率不匹配**：当前主循环每 **1000ms** 才调用一次 `max30102_read()`，每次只读 1 个 FIFO 样本，但传感器配置为 **100Hz**，即每秒产生 100 个样本。这会导致 FIFO 严重溢出（OVF），数据完全错乱
  - **正确做法**：应该每 10ms 读取一次（或一次读取 FIFO 中所有可用样本），保证样本不丢失
  - 建议单独创建一个 MAX30102 采样任务，周期 10ms，专门读 FIFO
- **其他注意**：
  - 测量时手指必须**紧贴、完全覆盖**传感器窗口（红光+红外两个灯）
  - 手指不能太凉、不能按压过重
  - 前 5-10 秒数据不稳定是正常的
  - 可先打印原始 red/ir 值，确认手指放上后数值有明显变化（红外值通常升到几万）

### 问题 5：触摸驱动未实现 ⚠️ 优先级：中

- **现状**：只有引脚定义（CS=45, IRQ=46, MISO=39），没有 XPT2046 驱动代码
- **需要做的**：
  1. XPT2046 是 SPI 从机，可以：
     - 方案 A：在 LCD 同一 SPI 主机上添加 XPT2046 设备（不同 CS），注意 MISO 共用问题（LCD MISO 没接，触摸 MISO 接 GPIO39，需要 SPI 主机配置 MISO 为 GPIO39）
     - 方案 B：单独用一个 SPI 主机（如 SPI2_HOST）驱动触摸，引脚 SCK=GPIO1 会与 LCD 冲突，不推荐
     - **推荐方案 A**：把 LCD SPI 主机的 MISO 引脚配置为 GPIO39，LCD 不输出 MISO 不影响，添加 XPT2046 设备用 GPIO45 做 CS
  2. XPT2046 命令字节格式：`[start=1][A2A1A0通道][mode=1差分][PD1PD0]`
     - 读 X 通道：0x90（或 0xD0 连续）
     - 读 Y 通道：0xD0
  3. 读取时序：拉低 CS → 发 1 字节命令 → 读 2 字节（12 位结果，高位在前）→ 拉高 CS
  4. IRQ 引脚：未触摸时为高/低（取决于配置），可配置中断唤醒或轮询
  5. 坐标校准：原始 ADC 值需映射到 240×320 屏幕坐标，注意 X/Y 可能互换、方向可能反转
  6. 对接 LVGL：可使用 LVGL 8 的 indev（pointer）驱动，在 `read_cb` 中返回坐标和按压状态

### 问题 6：摄像头未调试 ⚠️ 优先级：低

- 配置代码已写（sensors.c 中 camera_init/camera_capture）
- ATK-OV2640 内置 12MHz 有源晶振，所以 XCLK 配 -1
- 配置为 JPEG、QVGA、PSRAM 帧缓冲、双缓冲
- **注意引脚冲突**：摄像头 RESET 接 GPIO47，与其他功能不冲突；但摄像头占用 GPIO4-14/16/17/18/47，接线较多
- 待调：初始化是否成功、拍照获取 JPEG、通过 TCP 分片发送、上位机 imageassembler 重组

### 问题 7：上位机曾出现崩溃/异常结束

- **现象**：Qt 程序启动后打印 `database initialized: "./health.db"`，随后"程序异常结束"
- **可能原因**：串口/通信线程与 UI 线程交互问题、空指针、配置读取问题
- 后续若复现，需要在 Qt Creator 中调试定位崩溃栈

---

## 十、新智能体接下来要做什么（具体操作指南）

### 10.1 推荐执行顺序（按优先级）

**第一阶段：打通 SHT30 温湿度（最简单的闭环）**

1. 先读 `pin_config.h`、`drivers/sensors.c`、`test_modules.c`，了解现状
2. 编写**独立的 SHT30 最小测试程序**（临时替换 app_main）：
   - 初始化 I2C（SDA=20, SCL=19，内部上拉）
   - 添加 0x44 设备
   - 循环：发测量命令 → 延时 → 读 6 字节 → 打印原始 hex 和换算结果
   - 优先尝试命令 `0x24 0x00`（无时钟拉伸），读取用 `i2c_master_transmit_receive` 或分两步
3. 确认能读到合理室温（约 20-30℃）和湿度（30-70%）
4. 将验证通过的读取函数合并回 `sht30_read()`

**第二阶段：修复串口驱动与上位机显示**

5. 在固件初始化中（app_main 开头、LCD 初始化前后均可）调用：
   `uart_driver_install(UART_NUM_0, 256, 256, 0, NULL, 0);`
   消除 `uart driver error`
6. 烧录后，关闭其他占用 COM6 的程序，打开上位机
7. 确认上位机构建目录 `appconfig.ini`：
   - `useSimulatedSource=0`
   - `bandPortName=COM6`
8. 在上位机实时监测页确认温湿度真实数据显示
9. 若仍为 `--`，抓串口原始字节，对照协议帧格式和 `protocolparser.cpp` 排查

**第三阶段：MAX30102 心率血氧**

10. 修复采样率不匹配问题：
    - 新建独立 FreeRTOS 任务（如 `max30102_task`），周期 **10ms**
    - 每次读取 FIFO（最好先读 WR_PTR/RD_PTR 差值，一次读完所有样本，防止溢出）
    - 将样本填入 500 长度缓冲区
    - 集满后调用 Maxim 算法
11. 先调试原始波形：手指放上后打印 red/ir 原始值，确认有变化、未溢出
12. 再验证算法输出心率（正常 60-100）和血氧（正常 95-100）
13. 结果送 LVGL 显示 + 上位机

**第四阶段：触摸功能**

14. 将 LCD SPI 主机的 MISO 配置为 GPIO39（LCD 不用 MISO，不影响）
15. 在同一 SPI 总线添加 XPT2046 设备（CS=GPIO45），SPI 频率建议 1-2MHz 以下
16. 实现 XPT2046 读 X/Y 原始坐标函数（命令 0x90 / 0xD0）
17. 配置 T_IRQ(GPIO46) 为输入（可上拉），用于判断是否按下；或直接轮询
18. 做坐标校准映射到 240×320
19. 注册 LVGL pointer indev，实现界面触摸交互（如切换页面、按钮）

**第五阶段：摄像头与 WiFi 图像**

20. 接好摄像头全部排线/杜邦线，调用 camera_init() 验证
21. 拍照获取 JPEG，通过 WiFi TCP（端口 45678）按协议分片发送
22. 上位机 imageassembler 重组并在图像页/饮食页显示
23. 完善马达报警联动（心率/血氧异常时 motor_on）

### 10.2 命令行编译烧录方法（重要，GUI 插件异常时用）

当 VSCode ESP-IDF 插件按钮不好使时，用 PowerShell 执行以下流程：

```powershell
# 1. 杀掉可能占用串口的 python 进程（谨慎，确认无其他重要任务）
Get-Process python -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# 2. 设置环境变量
$env:IDF_PATH = "D:\Espressif\frameworks\esp-idf-v5.5.5"
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif\python_env\idf5.5_py3.11_env"
$env:PYTHONPATH = ""
$env:PYTHONHOME = ""
$env:PYTHONNOUSERSITE = "True"
$python = "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$tools = @(
    "D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin",
    "D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin",
    "D:\Espressif\tools\ninja\1.12.1",
    "D:\Espressif\tools\cmake\3.30.2\bin",
    "D:\Espressif\tools\idf-exe\1.0.3",
    "D:\Espressif\tools\ccache\4.12.1",
    "D:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260424\bin",
    "D:\Espressif\tools\idf-git\2.44.0\cmd",
    "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts"
)
$env:PATH = ($tools -join ";") + ";" + $env:PATH

# 3. 进入工程并编译+烧录
Set-Location "D:\Code4\HealthMonitorSystem\firmware\esp32"
& $python "$env:IDF_PATH\tools\idf.py" -p COM6 -b 460800 build flash

# 只编译：idf.py build
# 只烧录：idf.py -p COM6 -b 460800 flash
# 监视串口：idf.py -p COM6 monitor（退出 Ctrl+]）
# 编译+烧录+监视：idf.py -p COM6 build flash monitor
```

### 10.3 读取串口日志的辅助方法（PowerShell，无需额外工具）

```powershell
$port = New-Object System.IO.Ports.SerialPort COM6,115200,None,8,one
$port.ReadTimeout = 1000
$port.Open()
$port.RtsEnable = $true
Start-Sleep -Milliseconds 100
$port.RtsEnable = $false
Start-Sleep -Seconds 15
$output = $port.ReadExisting()
$port.Close()
Write-Output $output
```

> 注意：运行此脚本和 idf.py monitor、上位机不能同时进行（COM6 被独占）。

---

## 十一、关键文件速查表

### 固件端（ESP32）

| 文件 | 作用 | 重要程度 |
|------|------|----------|
| `firmware/esp32/main/test_modules.c` | **当前主程序 app_main**，LVGL 界面、主循环、数据发送 | ★★★★★ |
| `firmware/esp32/main/pin_config.h` | **所有引脚定义、WiFi 配置、参数** | ★★★★★ |
| `firmware/esp32/main/drivers/display.c` | LCD ILI9341 SPI 驱动、初始化、写窗口 | ★★★★★ |
| `firmware/esp32/main/drivers/display.h` | LCD 驱动接口 | ★★★★ |
| `firmware/esp32/main/drivers/sensors.c` | I2C 初始化、SHT30/MAX30102、WiFi、TCP、摄像头、马达 | ★★★★★ |
| `firmware/esp32/main/drivers/sensors.h` | 传感器/通信接口声明 | ★★★★ |
| `firmware/esp32/main/drivers/max30102_algo.c` | Maxim 官方心率血氧算法 | ★★★★ |
| `firmware/esp32/main/drivers/max30102_algo.h` | 算法接口、缓冲区大小定义 | ★★★ |
| `firmware/esp32/main/i2c_scan_main.c` | I2C 扫描测试参考 | ★★★ |
| `firmware/core/fw_proto.h` | 通信协议定义（纯 C，跨平台） | ★★★★★ |
| `firmware/esp32/CMakeLists.txt` | ESP-IDF 工程顶层构建 | ★★★ |
| `firmware/esp32/main/CMakeLists.txt` | main 组件构建（源文件列表） | ★★★ |
| `firmware/esp32/main/idf_component.yml` | 外部组件依赖（LVGL、camera 等） | ★★★ |
| `firmware/esp32/sdkconfig` | SDK 配置（WiFi、PSRAM、分区等） | ★★★ |

> **注意**：新增 .c 源文件后，必须把文件名加到 `main/CMakeLists.txt` 的 SRCS 列表中，否则不会编译。

### 上位机（Qt）

| 文件 | 作用 |
|------|------|
| `main.cpp` | 程序入口 |
| `view/mainwindow.cpp/h` | 主窗口、页面导航 |
| `view/pages/realtimemonitorpage.cpp/h` | **实时监测页（心率血氧体温显示）** |
| `view/pages/dietmanagepage.cpp/h` | 饮食管理页 |
| `view/pages/imagepage.cpp/h` | 图像显示页 |
| `communication/communicationmanager.cpp/h` | 通信总调度 |
| `communication/protocolparser.cpp/h` | **协议帧解析（与 fw_proto 对应）** |
| `communication/serialporthandler.cpp/h` | 串口收发 |
| `communication/wifihandler.cpp/h` | WiFi TCP 通信 |
| `communication/imageassembler.cpp/h` | 图像分片重组 |
| `config/appconfig.ini` | 配置文件模板 |
| `config/appconfigmanager.cpp/h` | 配置读写 |

### 运行时配置（实际生效）

| 文件 | 说明 |
|------|------|
| `D:\Code4\build-HealthMonitorSystem-Desktop_Qt_5_14_2_MinGW_64_bit-Debug\appconfig.ini` | **上位机实际读取的配置**，已改为 useSimulatedSource=0, bandPortName=COM6 |
| `D:\Code4\build-HealthMonitorSystem-Desktop_Qt_5_14_2_MinGW_64_bit-Debug\health.db` | SQLite 数据库（运行生成） |

> **注意**：源码 `config/appconfig.ini` 仍是默认模板（useSimulatedSource=1, COM3），如果重新构建/清理导致配置被覆盖，需要再次修改构建目录中的文件，或直接修改源码模板。

---

## 十二、参考资料

### 12.1 本地资料
- **四阶段项目资料目录**：`D:\YQ\参考文献\四阶段项目资料`（包含项目各阶段参考资料，建议先浏览）

### 12.2 在线官方资料
- **ESP32-S3-DevKitC-1 v1.1 用户指南（中文）**：
  https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html
- **LCD 2.8寸 ILI9341 (MSP2807) Wiki（中文）**：
  https://www.lcdwiki.com/zh/2.8inch_SPI_Module_ILI9341_SKU:MSP2807
- ESP-IDF v5.5 官方文档：https://docs.espressif.com/projects/esp-idf/zh_CN/v5.5/
- LVGL 8 文档：https://docs.lvgl.io/8.5/

### 12.3 项目设计文档
- `docs/项目设计1.0/`
- `docs/项目设计1.1/`
- `docs/项目设计1.2/`（最新，含分层架构、类设计、表设计、装配约定）

---

## 十三、重要踩坑记录与注意事项（务必阅读）

1. **GPIO39-42 是 JTAG 引脚**：不要用于高速 LCD SPI（会白屏）；低速触摸 SPI 可以用。
2. **GPIO22-37 全部接 Flash/PSRAM**：绝对不能配置使用，会导致崩溃/无法启动。
3. **WiFi 必须在 STA_START 事件里调用 `esp_wifi_connect()`**：只 start 不 connect 不会连接。
4. **MAX30102 采样率必须匹配**：100sps 配置就要每 10ms 读 FIFO，否则溢出数据错乱（当前代码的主要嫌疑 Bug）。
5. **UART0 用 `uart_write_bytes` 前必须 `uart_driver_install`**，否则报 `uart driver error`。
6. **COM6 是独占资源**：idf.py monitor、PowerShell 串口脚本、上位机三者不能同时打开。
7. **上位机配置有两份**：源码 config 下是模板（默认模拟数据 COM3），实际生效在 Qt 构建目录（已改 COM6、真实数据）；重新构建可能覆盖。
8. **LCD 和触摸共用 SCK/MOSI**：通过各自独立 CS 区分，不要重复接时钟/数据线。
9. **触摸 MISO 接 GPIO39**：配置 SPI 主机时 MISO 填 GPIO39，LCD 不使用 MISO 不影响。
10. **ATK-OV2640 自带 12MHz 晶振**：XCLK 必须配 -1，不要额外提供时钟。
11. **烧录失败 "Invalid head of packet"**：多为串口干扰/接触不良/波特率过高，可降低烧录波特率到 115200、检查数据线、重新插拔；之前用 460800 大部分情况正常。
12. **改了代码但现象没变**：确认修改的是正确文件、确认编译真的重新链接（注意 app_main 实际在 test_modules.c，不是 main.c）、确认烧录成功并复位。
13. **LVGL 用 8.5 不用 9.6**：API 以 LVGL 8 为准（lv_disp_draw_buf_init / lv_disp_drv_init 等是 v8 写法）。
14. **代码风格偏好**：用户喜欢干净、按功能分块的代码；排查问题喜欢直接看现象、看日志、给直接可执行的修复方案，不要泛泛而谈。
15. **用户技术背景**：物联网工程专业，熟悉 C/C++、嵌入式、Qt，正在做毕设/项目，动手能力强，希望高效推进。

---

## 十四、接手后的第一步建议（快速上手 Checklist）

- [ ] 通读本交接文档，特别是「引脚分配」「未解决问题」「踩坑记录」
- [ ] 浏览 `D:\YQ\参考文献\四阶段项目资料`
- [ ] 打开 `pin_config.h` 核对引脚
- [ ] 读 `test_modules.c`（主程序）、`drivers/sensors.c`、`drivers/display.c`
- [ ] 确认开发板通过 COM6 连接、LCD/传感器接线正常
- [ ] 先用命令行编译烧录当前固件，确认 LCD LVGL 界面正常（建立基线）
- [ ] 从「SHT30 独立最小测试」开始，打通第一个真实数据闭环
- [ ] 每完成一步，烧录验证，再进入下一步，不要一次性改太多

---

> **结语**：项目最困难的部分（LCD 白屏、WiFi 连接、环境搭建、硬件检测）已经攻克，当前处于「传感器数据读取与联调」阶段。按本文档顺序推进即可，祝顺利。
