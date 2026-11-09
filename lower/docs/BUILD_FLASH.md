# 下位机（ESP32-S3 手环）构建与烧录手册

> 工程根：`D:\YQ\Code\HealthMonitorSystem\lower`
> 芯片：ESP32-S3（模组型号待串口日志确认，见第六节 PSRAM 部分）
> ESP-IDF：**v5.5.5**（装在 `D:\ESP_IDF\Espressif`）
> **串口：本机实测为 `COM15`**（`USB-Enhanced-SERIAL CH343`）。
> ⚠️ 旧交接文档写的 `COM6` 在本机是"蓝牙链接上的标准串行"虚拟口，**不是开发板**。
> 用 `Get-CimInstance Win32_PnPEntity | ? Name -match 'CH343'` 可随时确认端口号。

---

## 一、环境变量（每次新开 PowerShell 都要设）

本机 ESP-IDF 装在非默认路径，且没有走 `export.ps1`，所以每次构建前要先铺环境：

```powershell
$env:IDF_PATH = "D:\ESP_IDF\Espressif\frameworks\esp-idf-v5.5.5"
$env:IDF_TOOLS_PATH = "D:\ESP_IDF\Espressif"
$env:IDF_PYTHON_ENV_PATH = "D:\ESP_IDF\Espressif\python_env\idf5.5_py3.11_env"
$env:ESP_ROM_ELF_DIR = "D:\ESP_IDF\Espressif\tools\esp-rom-elfs\20240305"
$env:PYTHONPATH = ""; $env:PYTHONHOME = ""; $env:PYTHONNOUSERSITE = "True"

$py = "D:\ESP_IDF\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$tools = @(
  "D:\ESP_IDF\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin",
  "D:\ESP_IDF\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin",
  "D:\ESP_IDF\Espressif\tools\ninja\1.12.1",
  "D:\ESP_IDF\Espressif\tools\cmake\3.30.2\bin",
  "D:\ESP_IDF\Espressif\tools\idf-exe\1.0.3",
  "D:\ESP_IDF\Espressif\tools\ccache\4.12.1",
  "D:\ESP_IDF\Espressif\tools\idf-git\2.44.0\cmd",
  "D:\ESP_IDF\Espressif\python_env\idf5.5_py3.11_env\Scripts"
)
$env:PATH = ($tools -join ";") + ";" + $env:PATH
Set-Location "D:\YQ\Code\HealthMonitorSystem\lower"
```

---

## 二、常用命令

```powershell
# 首次（或换芯片）设定目标
& $py "$env:IDF_PATH\tools\idf.py" set-target esp32s3

# 编译
& $py "$env:IDF_PATH\tools\idf.py" build

# 编译 + 烧录 + 看串口（COM6 是 CH343；退出监视用 Ctrl+]）
& $py "$env:IDF_PATH\tools\idf.py" -p COM6 -b 460800 build flash monitor

# 只烧录
& $py "$env:IDF_PATH\tools\idf.py" -p COM6 -b 460800 flash

# 只看串口
& $py "$env:IDF_PATH\tools\idf.py" -p COM6 monitor
```

> **COM6 是独占资源**：`idf.py monitor`、上位机、PowerShell 串口脚本三者不能同时开。
> 烧录前如果报串口占用，先关掉其它占用方。

---

## 三、不接硬件也能做的验证

协议层是纯 C 且平台无关，可以直接在 PC 上用 MinGW 编译运行自测：

```powershell
$gcc = "D:\Qt5.14.2\Tools\mingw730_64\bin\gcc.exe"
& $gcc -std=c99 -Wall -Wextra -O1 `
  -I components/fw_core/include `
  test/fw_core_selftest.c `
  components/fw_core/fw_proto.c `
  components/fw_core/fw_image_rx.c `
  -o test/fw_core_selftest.exe
& .\test\fw_core_selftest.exe
```

期望结果：`通过: 116    失败: 0` + `全部通过`，退出码 0。

这一点很重要：**烧板之前就能证明**固件组帧与上位机 `communication/protocolparser.cpp` 字节级一致，
包括两条红线（图像分片载荷上限 1027、下行帧必须 15 字节定长）。

---

## 四、启动日志逐项检查

烧录后串口应依次出现：

| 日志关键字段 | 含义 | 对应预期 |
|---|---|---|
| `SPIRAM` / `Found 8MB PSRAM` | PSRAM 被识别 | **若显示 "no PSRAM" 或反复重启，说明本板是 N16 而不是 N16R8**，见第六节 |
| `health_monitor_band v1.2.0 启动` | 应用启动 | |
| `I2C 设备在线: 0x44 (SHT30)` | 温湿度传感器在位 | 缺它 → 查 I2C 接线与 4.7K 上拉 |
| `I2C 设备在线: 0x57 (MAX30102)` | 心率血氧传感器在位 | 缺它 → 同上 |
| `I2C 扫描结束，共 2 个设备` | 两个传感器都在 | 应为 2 |
| `STA 启动，开始连接 AP` | WiFi 开始连接 | |
| `拿到 IP: 172.20.10.x` | WiFi 已连上热点 | 与 PC 同网段 |
| `MAX30102 FIFO 积压 …` | **不应频繁出现** | 出现说明 10ms 采样任务被阻塞 |
| `HR=.. SpO2=..` | 算法输出（每 1 秒） | 手指贴紧约 5 秒后应有有效值 |
| `图像通道已连接 PC 172.20.10.6:45678` | 通道 B 建立 | 需 PC 上位机已启动且选中同一热点 |

---

## 五、与上位机联调步骤

1. **手机热点**同时承载 PC 与手环；PC 走该热点上网（云端识别需要外网）
2. PC 的 IPv4 固定下来（见 `docs/NETWORK_CONFIG.md`），或连接后由 0x09 动态刷新
3. 上位机 `appconfig.ini`（**构建目录里的那份才是实际生效的**）：
   ```ini
   [System]
   useSimulatedSource=0
   [Serial]
   bandPortName=COM6
   baudRate=115200
   [Wifi]
   useWifiForImage=1
   ```
4. Windows 防火墙放行 TCP **45678** 入站（首次监听会弹授权框）
5. 先确认实时监测页出现真实心率/血氧/温湿度（通道 A）
6. 再确认设置页图像通道显示"已连接"（通道 B）

---

## 六、如果 PSRAM 不存在（N16 而非 N16R8）

本工程按 **N16R8** 配置（`CONFIG_SPIRAM_MODE_OCT=y`）。若实际板子没有 PSRAM，
会出现启动反复重启 / `no PSRAM` 报错。处理：

1. `sdkconfig.defaults` 里注释掉整个 PSRAM 段
2. 摄像头帧缓冲改为 DRAM：`fw_camera` 的 `.fb_location = CAMERA_FB_IN_DRAM`
   （QVGA JPEG 单帧约 8~15KB，DRAM 放得下；但 `fb_count` 要降到 1）
3. 重新 `idf.py build`

**先看板子上模组的丝印**：`ESP32-S3-WROOM-1-N16R8` 才有 8MB PSRAM。

---

## 七、烧录故障排查

| 现象 | 处理 |
|---|---|
| `Invalid head of packet` | 降波特率到 115200；换数据线；重新插拔 |
| 串口被占用 | 关掉 monitor / 上位机 / 其它串口工具 |
| 改了代码但现象没变 | 确认改了正确文件；确认 `idf.py build` 真的重新链接；确认烧录成功并复位 |
| `uart driver error` | 本工程已在 `app_main` 开头 `uart_driver_install`，若仍出现说明该调用被跳过 |
| 反复重启 | 多半是 PSRAM 配置与实际芯片不符（见第六节） |
