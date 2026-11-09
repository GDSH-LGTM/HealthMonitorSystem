#include <Wire.h>
#include <Adafruit_SHT31.h>

// 创建SHT30传感器对象（SHT3x系列通用库）
Adafruit_SHT31 sht30 = Adafruit_SHT31();

void setup() {
  Serial.begin(9600);
  while (!Serial) delay(10); // 等待串口连接

  Serial.println("SHT30温湿度传感器测试");
  
  // 初始化传感器，尝试默认地址0x44和备选地址0x45
  if (!sht30.begin(0x44)) {
    Serial.print("0x44地址未检测到传感器，尝试0x45地址...");
    if (!sht30.begin(0x45)) {
      Serial.println("失败");
      Serial.println("请检查接线或传感器是否正常！");
      while (1); // 初始化失败则停机
    }
  }
  
  Serial.println("传感器初始化成功！");
  
  // 可选：开启加热器（在高湿度环境中防止结露，默认关闭）
  // sht30.heater(true);
}

void loop() {
  // 读取温度和湿度数据
  float temp = sht30.readTemperature();
  float humi = sht30.readHumidity();
  
  // 检查数据有效性
  if (isnan(temp) || isnan(humi)) {
    Serial.println("数据读取失败！");
  } else {
    // 打印带两位小数的测量结果
    Serial.print("温度: ");
    Serial.print(temp, 2);
    Serial.println(" °C");
    
    Serial.print("湿度: ");
    Serial.print(humi, 2);
    Serial.println(" %");
  }
  
  Serial.println("---------------------");
  delay(2000); // 每2秒读取一次
}
    