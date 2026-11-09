/***
	************************************************************************************************
	*	@version V1.0
	*	@author  鹿小班科技
	*	@brief   RTC相关函数
   ************************************************************************************************
   *  @description
	*
	*	实验平台：鹿小班STM32F103ZET6核心板 （型号：LXB103ZE-P1）
	* 客服微信：19949278543
	*
>>>>> 文件说明：
	*
	*  RTC相关初始化
	*
	************************************************************************************************
***/

#include "rtc.h"


RTC_HandleTypeDef hrtc;	// RTC句柄


/*************************************************************************************************
*	函 数 名:	MX_RTC_Init
*
*	函数功能:	初始化RTC
*************************************************************************************************/

void MX_RTC_Init(void)
{

	hrtc.Instance 					= RTC;					
	hrtc.Init.HourFormat 		= RTC_HOURFORMAT_24;				// 24小时格式
	hrtc.Init.AsynchPrediv 		= 127;								// 分频设置，当启用外部32.768KHz晶振时，这里设置为最大值127
	hrtc.Init.SynchPrediv 		= 255;								// 分频设置，当启用外部32.768KHz晶振时，这里设置为最大值255
	hrtc.Init.OutPut 				= RTC_OUTPUT_DISABLE;			// 禁止输出信号
	hrtc.Init.OutPutPolarity 	= RTC_OUTPUT_POLARITY_HIGH;	// 输出极性为高电平
	hrtc.Init.OutPutType 		= RTC_OUTPUT_TYPE_OPENDRAIN;	// 开漏输出
	
	if (HAL_RTC_Init(&hrtc) != HAL_OK)	// 初始化RTC
	{

	}

}

/*************************************************************************************************
*	函 数 名:	HAL_RTC_MspInit
*	入口参数:	hrtc - RTC_HandleTypeDef结构体变量，即表示定义的RTC（句柄）
*	函数功能:	开启时钟
*************************************************************************************************/

void HAL_RTC_MspInit(RTC_HandleTypeDef* hrtc)
{
  if(hrtc->Instance==RTC)
  {
    __HAL_RCC_RTC_ENABLE();	//	开启RTC时钟

  }
}


