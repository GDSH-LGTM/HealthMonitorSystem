/***
	*************************************************************************************************
	*	@file  	main.c
	*	@version V1.0
	*	@author  鹿小班科技
	*	@brief   RTC实验
   ************************************************************************************************
   *  @description
	*
	*	实验平台：鹿小班STM32F103ZET6核心板 （型号：LXB103ZE-P1）
	* 客服微信：19949278543
	*
>>>>> 功能说明：
	*
	*	1.初始化RTC获取时间和日期
	*	2.通过串口打印时间信息，串口波特率115200，使用USART1的PA9/PA10
	*
	************************************************************************************************
***/

#include "main.h"
#include "led.h"
#include "key.h"
#include "usart.h"
#include "rtc.h"


//********************************************** 变量声明 *******************************************/

extern RTC_HandleTypeDef hrtc;	// RTC句柄

RTC_TimeTypeDef Time_Struct;		// 日期
RTC_DateTypeDef Date_Struct;		// 时间



/********************************************** 函数声明 *******************************************/

void SystemClock_Config(void);		// 时钟初始化

/***************************************************************************************************
*	函 数 名: main
*	入口参数: 无
*	返 回 值: 无
*	函数功能: 运行主程序
*	说    明: 无
****************************************************************************************************/

int main(void)
{
	HAL_Init();					// 初始化HAL库
	SystemClock_Config();	// 配置系统时钟
	LED_Init();					// 初始化LED引脚
	USART1_Init();				// USART1初始化	

	MX_RTC_Init();				// RTC初始化
	
	printf("STM32 RTC 测试 \r\n");

	while (1)
	{
		HAL_RTC_GetTime(&hrtc,&Time_Struct,RTC_FORMAT_BIN);	// 获取时间

//		STM32的RTC为确保这 亚秒、时间、日期 3个值来自同一时刻点，
//		读取 亚秒（RTC_SSR ）或 时间（RTC_TR ）时会锁定高阶影子寄存器中的值，直到读取RTC_DR。	
//		也就是说用户不读日期的话，时间值会被锁定	
		HAL_RTC_GetDate(&hrtc,&Date_Struct,RTC_FORMAT_BIN);	// 获取日期
		
		printf("%d-%d-%d ",Date_Struct.Year+2000,Date_Struct.Month,Date_Struct.Date);
		printf("%d:%d:%d\r\n",Time_Struct.Hours,Time_Struct.Minutes,Time_Struct.Seconds);

		HAL_Delay(1000);
		LED1_Toggle;
	}

}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  
  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
  
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
