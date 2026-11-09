/***
	***************************************************************************************************
	*	@file  	main.c
	*	@version V1.0
	*	@author  鹿小班科技	
	*	@brief   RTC读取时间
   ****************************************************************************************************
   *  @description
	*
	*	实验平台：鹿小班STM32F407ZGT6核心板 （型号：LXB407ZG-P1）
	* 客服微信：19949278543
	*
>>>>> 功能说明：
	*
	*	读取时间，通过串口打印到PC
	* 		
	***************************************************************************************************
***/


#include "stm32f4xx.h"
#include "led.h"   
#include "delay.h"
#include "key.h"  
#include "usart.h"
#include "rtc.h" 

RTC_TimeTypeDef Time_Struct;		// 时间
RTC_DateTypeDef Date_Struct;		//	日期


/***************************************************************************************************
*	函 数 名: main
*	入口参数: 无
*	返 回 值: 无
*	函数功能: 运行主程序
*	说    明: 无
****************************************************************************************************/

int main(void)
{
		
	Delay_Init();		//延时函数初始化
	LED_Init();			//LED初始化
	KEY_Init();			//按键IO口初始化
	Usart_Config ();	// USART初始化函数
	
	RTC_Initialize();			// 初始化RTC

	printf("STM32 RTC读取\r\n");	
	
	while (1)
	{		
		RTC_GetTime(RTC_Format_BIN,&Time_Struct);	 // 读取时分秒
		RTC_GetDate(RTC_Format_BIN,&Date_Struct);	 // 读取年月日
		printf("%d-%d-%d ",Date_Struct.RTC_Year+2000,Date_Struct.RTC_Month,Date_Struct.RTC_Date);			// 打印年月日
		printf("%d:%d:%d\r\n",Time_Struct.RTC_Hours,Time_Struct.RTC_Minutes,Time_Struct.RTC_Seconds);	// 打印时分秒
		LED1_Toggle;			// LED闪烁
		Delay_ms(999);		
	}
	
}





