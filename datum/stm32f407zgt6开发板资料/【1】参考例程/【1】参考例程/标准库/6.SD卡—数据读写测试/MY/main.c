/***
	***************************************************************************************************
	*	@file  	main.c
	*	@version V1.0
	*	@author  鹿小班科技	
	*	@brief   SD卡读写测试
   ****************************************************************************************************
   *  @description
	*
	*	实验平台：鹿小班STM32F407ZGT6核心板 （型号：LXB407ZG-P1）
	* 客服微信：19949278543
	*
>>>>> 功能说明：
	*
	*  1.在不移植文件系统下对SD卡进行简单的数据读取测试
	* 	2.SD卡底层驱动移植于官方评估板例程
	* 		
	***************************************************************************************************
***/


#include "stm32f4xx.h"
#include "led.h"   
#include "delay.h"
#include "key.h"  
#include "usart.h"
#include "sdio_sd.h"


#define 	SD_BlockSize  512	 //SD卡块大小

SD_Error	SD_Status ; 		 //SD卡检测标志位
uint8_t 	SD_WriteBuffer[SD_BlockSize];	//	写数据数组
uint8_t 	SD_ReadBuffer[SD_BlockSize];	//	读数据数组

// 函数：SD卡测试
//	功能：SD卡块读写测试
//	返回值：0 - 读写测试失败，1 - 读写测试成功
//
uint8_t SD_Test(void)
{
	uint16_t i = 0;	
	
	for(i=0;i<SD_BlockSize;i++)	//将要写入SD卡的数据写入数组
	{
		SD_WriteBuffer[i] = i;
	}
	SD_WriteBlock(SD_WriteBuffer, 0x00, SD_BlockSize);	//块写入
	SD_WaitWriteOperation();						//等待DMA传输结束
	while(SD_GetStatus() != SD_TRANSFER_OK);	//等待通信结束
	
	SD_ReadBlock(SD_ReadBuffer, 0x00, SD_BlockSize);	//块读取
	SD_WaitReadOperation();							//等待DMA传输结束	
	while(SD_GetStatus() != SD_TRANSFER_OK);	//等待通信结束

	for(i=0;i<SD_BlockSize;i++)	//验证读出的数据是否等于写入的数据
	{
		if( SD_WriteBuffer[i] != SD_ReadBuffer[i] )	//如果数据不相等，则返回0
			return 0;
	}		
	return 1; 	//数据正确，读写测试通过，返回1
}

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
	
	SD_Status = SD_Init();	//SD卡初始化
	
	if( SD_Status == SD_OK )	//检测是否初始化成功
	{		
		printf("SD卡初始化成功 \r\n");	
		
		if( SD_Test() == 1 )		//SD卡读写测试
			printf("SD卡 Block 读写成功 \r\n");	
		else
			printf("读写失败，请更换SD卡 \r\n");			
	}
	else
		printf("\r\n 检测不到SD卡 \r\n");	
	
	while (1)
	{
		LED1_Toggle;
		Delay_ms(100);
	}
	
}





