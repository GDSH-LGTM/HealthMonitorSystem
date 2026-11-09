# 体重秤的上报通道（需要上位机配合）

## 1. 现状与冲突

协议 `protocol_spec1.2.md` §3 把 **0x02 体重帧**归到**通道 A**：

| 帧 | 名称 | 方向 | 通道 |
|---|---|---|---|
| 0x01 | 体征 | 手环→PC | A |
| **0x02** | **体重** | **秤→PC** | **A** |

而通道 A 的物理形态，在手环那边是 **USB 串口**（因为 ESP32-S3 不支持蓝牙 Classic，
协议原文的"蓝牙 SPP"无法实现，见 `../lower/README.md`）。

**问题来了：秤没有多余的 USB 口。**

- 秤如果也用 ESP32-S3，Micro-USB 口被占用后，UART0 就是那个口 —— 但一块板子只有
  一个 USB 口，插上 PC 就没办法接电源以外的东西（而且秤的部署位置通常离 PC 远，
  拉 USB 线不现实）
- 上位机侧 `appconfig.ini` 里 `scalePortName=COM4` 是**串口**语义

所以本项目**默认让秤走 WiFi TCP**，端口 **45679**。

## 2. 为什么是 45679 而不是 45678

上位机的 `WifiHandler` 是**单客户端**设计（见 `WiFi通道设计.md` §1：
"PC 同一时刻只接受一个图像客户端连接"）。

手环的图像通道已经占着 **45678**。如果秤也连 45678：

- 谁先连上谁占坑，另一个的 `connect()` 会被接受但立刻被拒（或根本连不上）
- 两个设备交替重连，图像链路会被秤反复打断

所以秤用独立端口 **45679**。

## 3. 上位机侧已经补上了（可选开关，默认关闭）

原先上位机只有一个 TCP 监听（图像通道 45678），秤的 0x02 帧进不来。
**现在已实现第二个监听**，做法与图像通道同构：

```ini
[Wifi]
useWifiForScale=0   ; 0=关闭（秤走 scalePortName 串口，既有行为完全不变）
                    ; 1=启用：额外监听 45679，字节流喂给秤解析器
```

实现位置（`HealthMonitorSystem/communication/communicationmanager.cpp`）：

```cpp
const quint16 kScaleTcpPort = 45679;      // 与图像通道分开：WifiHandler 是单客户端

m_scaleWifiHandler = new WifiHandler(this);          // 独立实例，不能复用 m_wifiHandler
connect(m_scaleWifiHandler, &WifiHandler::dataReceived,
        m_scaleParser, &ProtocolParser::feedBytes);  // 喂给已有的秤解析器
...
if (m_config.useWifiForScale) {
    m_scaleWifiHandler->listen(kScaleTcpPort);
}
```

**为什么默认关闭**：保持"串口秤"的既有行为逐字节不变——上位机此前是完工状态，
这个开关关掉时不会改变任何现有行为（7 个冒烟 target 全绿可证）。

**为什么复用 `m_scaleParser` 而不是新建**：`m_scaleParser` 上已经接了
`bodyScaleFrameParsed → onBodyScaleParsed`，复用它意味着**下游业务链路完全不用动**，
业务层也不需要知道数据来自串口还是网络。串口秤与网络秤共存，谁先来数据都行。

### 端到端验证

新增 `HealthMonitorSystem/test/scale_tcp_smoke.cpp`（7 个 target 之一），
它直接验证这条链路：

```
起 CM（useWifiForScale=1，串口指向不存在的 COM99）
  → 监听 45679
  → 本进程作为 TCP 客户端连上来
  → 发一帧由「秤固件同一份代码」(scale_frame.c) 组出的 0x02 帧
  → 断言 CM 的 bodyScaleReceived 收到 65.43 kg 与正确的采集时刻
  → 再发一帧未标定的（载荷 0xFFFF），断言解析成 -1.0f（界面显示 --，不冒充 0）
```

**10 条断言全通过。** 用的是秤固件同一份源文件组帧，所以验证的是真实字节。

## 4. 秤侧的实现

`components/fw_scale/scale_transport.c`：

- WiFi STA，SSID/密码取 `scale_config.h`（`SCALE_DEFAULT_SSID/PASSWORD`）
- TCP 客户端连 `SCALE_DEFAULT_PC_IP:45679`
- 断连退避 **1s → 2s → 5s → 10s**（协议 §10）
- 收到 **0x09** 帧则刷新 PC IPv4，地址变化时**立即断开重连**（不等退避）
- 发送失败即转重连；秤**不缓存、不重发**（协议 §5）

地址来源与手环图像通道同一套逻辑，详见 `../lower/docs/NETWORK_CONFIG.md`。

## 5. 替代方案对比

| 方案 | 上位机改动 | 秤侧硬件 | 评价 |
|---|---|---|---|
| **WiFi TCP 45679**（本工程默认） | ✅ **已实现**（`useWifiForScale`，默认关） | 无额外硬件 | ✅ 推荐；与手环图像通道同构，已端到端验证 |
| 蓝牙 SPP | 零改动（上位机只看 COM 口） | 需**经典 ESP32**（内置） | 可行；但秤主控必须是经典 ESP32 而非 S3 |
| USB-TTL 转换器 + 串口 | 零改动 | 需转换器 + 线 | 最省事但秤被线拴住 |
| 外接 HC-05 | 零改动 | 需模块 + 2 个空闲 GPIO | HC-05 是独立模块走 UART，S3 也能用 |

**关于「ESP32 经典款内置 SPP」**：不需要改上位机、也不需要额外模块——
秤用经典 ESP32（WROOM-32），跑 Bluedroid 的 SPP acceptor，PC 配对后当串口用。
但需要在 IDF 里启用 Bluedroid 并写 SPP 服务端（`esp_spp_api.h` 存在）。
之所以没默认走这条：**经典 ESP32 与手环的 S3 是两块不同的板子**，
而 WiFi TCP 路线用同一块 S3 就能验证。若倾向 SPP，告诉我，
我可以在 `scale_transport.c` 旁边加一个 `scale_transport_spp.c`。

## 6. 联调自查清单

- [ ] 秤与 PC 在同一热点下
- [ ] PC 的 IPv4 与 `SCALE_DEFAULT_PC_IP`（或 0x09 下发值）一致
- [ ] Windows 防火墙放行 TCP **45679** 入站
- [ ] 上位机 `appconfig.ini`（**构建目录那份**）里 `useWifiForScale=1`
      —— 注意重建会把它复位成模板值 0，见 [`../../tools/use_real_band.ps1`](../../tools/use_real_band.ps1) 同款问题
- [ ] 秤日志出现 `拿到 IP:` 与 `已连接 PC <IP>:45679`
- [ ] 秤已标定（`s` 命令里 scale ≠ 0），否则载荷是 0xFFFF
- [ ] 放上重物并稳定 → 日志出现 `已上报体重 X.XX kg（0x02 帧 13 字节）`
- [ ] 上位机身体数据页出现真实体重

> 冒烟自查：`build-.../scale_tcp_smoke.exe` 应 **10/10 通过**——
> 它不需要硬件，验证的就是"监听 + 0x02 解析 + 下游信号"这条链路。
