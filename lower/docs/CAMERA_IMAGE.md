# 摄像头（ATK-OV2640）与图像链路

## 1. 引脚

| 信号 | GPIO | | 信号 | GPIO |
|---|---|---|---|---|
| D0 | 4 | | D4 | 8 |
| D1 | 5 | | D5 | 9 |
| D2 | 6 | | D6 | 10 |
| D3 | 7 | | D7 | 11 |
| VSYNC | 12 | | HREF | 13 |
| PCLK | 14 | | **XCLK** | **-1（不接）** |
| SCCB_SDA | 17 | | SCCB_SCL | 16 |
| PWDN | 18 | | RESET | 47 |

> **XCLK 必须配 -1**：ATK-OV2640 模块自带 12MHz 有源晶振，不需要 ESP32 输出时钟。
> 额外供时钟反而可能不工作。

摄像头占 GPIO4-14/16/17/18/47，与 LCD（1/2/3/21/48）、触摸（39/45/46）、
I2C（19/20）、马达（15）**均不冲突**。

---

## 2. 帧缓冲落地：PSRAM 还是 DRAM

本板模组型号未确认（见 `docs/BUILD_FLASH.md`），所以**运行时判断**：

| 情况 | 配置 | 理由 |
|---|---|---|
| 检测到 PSRAM | `CAMERA_FB_IN_PSRAM`，`fb_count=2` | 双缓冲，采集与发送可重叠 |
| 无 PSRAM | `CAMERA_FB_IN_DRAM`，`fb_count=1` | QVGA JPEG 约 8~15KB，DRAM 放得下 |

启动日志会直接报出来：

```
I (xxx) main: PSRAM: 8388608 字节（8.0 MB）—— 可用，摄像头帧缓冲放 PSRAM
```
或
```
W (xxx) main: PSRAM: 未检测到！本板可能是 N16 而不是 N16R8。摄像头需改用 CAMERA_FB_IN_DRAM 且 fb_count=1
```

`fw_camera_init()` 里若 PSRAM 分配失败会自动退到 DRAM 重试一次，不会因为
PSRAM 缺失就完全用不了摄像头。

**必须用 JPEG 输出**：RGB565 QVGA = 240×320×2 = 150KB，远超可用 RAM；
JPEG 只要 8~15KB。这也是协议 §8.1「图像字节即摄像头输出的 JPEG 原始数据」的前提。

---

## 3. 图像链路（通道 B，协议 §8）

```
PC 发 0x0B(图ID)  ──通道 A──▶  手环
                                  │
                        ① 通道就绪？否 → 直接丢弃，不缓存图ID（§8.6）
                        ② fw_camera_capture() 取一帧 JPEG
                        ③ fw_image_tx_send()：
                             0x06  图ID + 总字节(4) + 分片数(2)      = 18 字节
                             0x07  图ID + 序号(2) + 数据≤1024       ≤ 1038 字节
                             0x08  图ID + CRC16(=0)                 = 14 字节
                        ④ fw_camera_return_frame() 归还缓冲
                                  │
                                  ▼
                        WiFi TCP → PC:45678 → ImageAssembler 重组
```

要点：

- **单槽、best-effort**：无 ACK、无重传；发送中断即整张丢弃
- **三帧共用同一采集时刻**（协议 §2）：`ts` 取设备软时钟，未校时时为 0，
  PC 侧会按接收时刻兜底并告警
- **图 ID 范围 1~0x7F**：PC 分配，设备在 0x06 中原样回填；越界直接拒绝
- **通道未就绪就丢弃**，不缓存图 ID（延迟拍摄可能拍到空盘，且实现最简）

对应实现：
- `components/fw_core/fw_image_tx.c` —— 组帧与切片（纯 C，回调注入发送方式）
- `components/fw_camera/fw_camera.c` —— OV2640 采集
- `main/main.c` 的 `camera_task` —— 把两者串起来

---

## 4. 下行帧路由（0x09 / 0x0B / 0x0A）

`components/fw_core/fw_host_router.c` 负责把通道 A 的字节流分流：

| 帧 | 去向 | 动作 |
|---|---|---|
| 0x03 校时 | `fw_band_app` | 校时 + 清零当日累计 |
| 0x05 补传请求 | `fw_band_app` | 触发补传 |
| **0x09 WiFi 配置** | `main` | `fw_wifi_apply_pc_ip()`（幂等，地址变则立即重连） |
| **0x0B 拍照** | `main` | 置位拍照请求，由 `camera_task` 取走 |
| 0x0A 震动 | `main` | 本版只记日志（协议 §4.10 已分配未实现） |

**为什么单独做一层**：`fw_band_app` 是纯 C 协议核心、被上位机的 `firmware_smoke`
逐字节验证过。让 main 直接嗅探下行帧，就不必改它的签名或加回调，
避免把板级动作耦合进协议核心。

该路由已自测覆盖（自测 G 段）：0x0B/0x09/0x0A 触发对应钩子、0x03/0x05 正确转交、
坏校验帧不触发钩子、脏字节后仍能重新同步、一次喂两帧全部解出。

---

## 5. 不接摄像头也能验证的部分

图像链路的**组帧与重组**是纯 C，可在 PC 上完整闭环验证（自测 H 段）：

```
fw_image_tx_send() 组帧 → 字节流 → fw_rx_feed() 滑窗 → fw_image_rx 重组
```

断言包括：共发 N+2 帧、字节总数与逐帧计算一致、重组后与原图**逐字节相同**、
图 ID 边界（0 与 0x80 被拒、0x7F 通过）、通道中断时立即停止。

跑法见 `README.md`「不接硬件也能做的验证」，当前 **145/145 通过**。

---

## 6. 上机调试顺序建议

1. 先看启动日志有没有 `摄像头就绪` 与传感器 PID
2. 若无 → 查 `camera_init` 的报错，多是接线（D0-D7 顺序接反很常见）
3. 再让上位机发起一次「拍照识别」→ 手环日志应出现
   `收到 0x0B 拍照指令，图 ID = N` 与 `图 ID=N 已发送：X 字节 / Y 片`
4. 若日志显示 `图像通道未就绪，丢弃拍照请求` → 通道 B 没连上，
   先解决 WiFi / PC IP（见 `docs/NETWORK_CONFIG.md`）
5. 若发送成功但上位机收不到图 → 抓包看 0x06/0x07/0x08 是否完整，
   重点核对 `PAYLOAD_LEN_MAX_IMAGE` 是否为 1027（上位机红线 1）
