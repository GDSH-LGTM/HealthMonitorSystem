#include "led.h"
#include "delay.h"
#include "sys.h"
#include "usart.h"	 
#include "OLED.h"
#include "string.h" 	
#include "max30102.h"

#define MAX_BRIGHTNESS 255
#define INTERRUPT_REG 0X00

/* MAX30102	
	VCC<->3.3V
	GND<->GND
	SCL<->PB7
	SDA<->PB8
	INT<->PB9
	OLED
	SCL<->PB11
	SDA<->PB10
	VCC<->3.3V
	GND<->GND*/
	
uint32_t aun_ir_buffer[500]; 	 //IR LED   红外光数据，用于计算血氧
int32_t n_ir_buffer_length;    //数据长度
uint32_t aun_red_buffer[500];  //Red LED	红光数据，用于计算心率曲线以及计算心率
int32_t n_sp02; //SPO2值
int8_t ch_spo2_valid;   //用于显示SP02计算是否有效的指示符
int32_t n_heart_rate;   //心率值
int8_t  ch_hr_valid;    //用于显示心率计算是否有效的指示符

uint8_t Temp;

uint32_t un_min, un_max, un_prev_data;  
int i;
int32_t n_brightness;
float f_temp;
//u8 temp_num=0;
u8 temp[6];
u8 str[100];
u8 dis_hr=0,dis_spo2=0;

int main(void)
{
	delay_init(72);	  
	LED_Init();		  				//初始化与控制设备连接的硬件接口
	OLED_Init();					//OLED初始化
	delay_ms(50);
	OLED_Clear();						//清屏

	MAX30102_Init();
	USART1_Config();//串口初始化
	
	un_min=0x3FFFF;
	un_max=0;
	
	//显示“心率：”
	OLED_ShowChinese(0,0,0,16,1);
	OLED_ShowChinese(16,0,1,16,1);
	OLED_ShowChar(40,0,':',16,1);
	OLED_ShowString(80,0,"BMP",16,1);
	
	//显示“血氧：”
	OLED_ShowChinese(0,16,2,16,1);
	OLED_ShowChinese(16,16,3,16,1);
	OLED_ShowChar(40,16,':',16,1);
	OLED_ShowChar(80,16,'%',16,1);

	n_ir_buffer_length=500; //缓冲区长度为100，可存储以100sps运行的5秒样本
	//读取前500个样本，并确定信号范围
	for(i=0;i<n_ir_buffer_length;i++)
	{
			while(MAX30102_INT==1);   //等待，直到中断引脚断言
			
			max30102_FIFO_ReadBytes(REG_FIFO_DATA,temp);
			aun_red_buffer[i] =  (long)((long)((long)temp[0]&0x03)<<16) | (long)temp[1]<<8 | (long)temp[2];    // 将值合并得到实际数字
			aun_ir_buffer[i] = (long)((long)((long)temp[3] & 0x03)<<16) |(long)temp[4]<<8 | (long)temp[5];   	 // 将值合并得到实际数字
					
			if(un_min>aun_red_buffer[i])
					un_min=aun_red_buffer[i];    //更新计算最小值
			if(un_max<aun_red_buffer[i])
					un_max=aun_red_buffer[i];    //更新计算最大值
	}
	un_prev_data=aun_red_buffer[i];
	
	//计算前500个样本（前5秒的样本）后的心率和血氧饱和度
	maxim_heart_rate_and_oxygen_saturation(aun_ir_buffer, n_ir_buffer_length, aun_red_buffer, &n_sp02, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid); 
	

	while(1)
	{
		//舍去前100组样本，并将后400组样本移到顶部，将100~500缓存数据移位到0~400
			for(i=100;i<500;i++)
			{
					aun_red_buffer[i-100]=aun_red_buffer[i];	//将100-500缓存数据移位到0-400
					aun_ir_buffer[i-100]=aun_ir_buffer[i];		//将100-500缓存数据移位到0-400
					
					//update the signal min and max
					if(un_min>aun_red_buffer[i])			//寻找移位后0-400中的最小值
					un_min=aun_red_buffer[i];
					if(un_max<aun_red_buffer[i])			//寻找移位后0-400中的最大值
					un_max=aun_red_buffer[i];
			}
			
			//在计算心率前取100组样本，取的数据放在400-500缓存数组中
			for(i=400;i<500;i++)
			{
					un_prev_data=aun_red_buffer[i-1];	//在计算心率前取100组样本，取的数据放在400-500缓存数组中
					while(MAX30102_INT==1);
					max30102_FIFO_ReadBytes(REG_FIFO_DATA,temp);		//读取传感器数据，赋值到temp中
					aun_red_buffer[i] =  (long)((long)((long)temp[0]&0x03)<<16) | (long)temp[1]<<8 | (long)temp[2];    //将值合并得到实际数字，数组400-500为新读取数据
					aun_ir_buffer[i] = (long)((long)((long)temp[3] & 0x03)<<16) |(long)temp[4]<<8 | (long)temp[5];   	//将值合并得到实际数字，数组400-500为新读取数据
					if(aun_red_buffer[i]>un_prev_data)		//用新获取的一个数值与上一个数值对比
					{
							f_temp=aun_red_buffer[i]-un_prev_data;
							f_temp/=(un_max-un_min);
							f_temp*=MAX_BRIGHTNESS;			//公式（心率曲线）=（新数值-旧数值）/（最大值-最小值）*255
							n_brightness-=(int)f_temp;
							if(n_brightness<0)
									n_brightness=0;
					}
					else
					{
							f_temp=un_prev_data-aun_red_buffer[i];
							f_temp/=(un_max-un_min);
							f_temp*=MAX_BRIGHTNESS;			//公式（心率曲线）=（旧数值-新数值）/（最大值-最小值）*255
							n_brightness+=(int)f_temp;
							if(n_brightness>MAX_BRIGHTNESS)
									n_brightness=MAX_BRIGHTNESS;
					}
			//通过UART将样本和计算结果发送到终端程序
			if(ch_hr_valid == 1 && n_heart_rate<120)//**/ ch_hr_valid == 1 && ch_spo2_valid ==1 && n_heart_rate<120 && n_sp02<101
			{
				dis_hr = n_heart_rate;
				dis_spo2 = n_sp02;
			}
			else
			{
				dis_hr = 0;
				dis_spo2 = 0;
			}
		}
		maxim_heart_rate_and_oxygen_saturation(aun_ir_buffer, n_ir_buffer_length, aun_red_buffer, &n_sp02, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid);
		
		//MAX30102
		if(dis_hr==0)
			OLED_ShowNum(47,0,0,3,16,1);
		else
		OLED_ShowNum(47,0,dis_hr-20,3,16,1);
		
		OLED_ShowNum(47,16,dis_spo2,2,16,1);
		printf("心率= %d BPM 血氧= %d\r\n ",dis_hr,dis_spo2);
		
		 //Serial_SendString("\r\n");

	}
}

