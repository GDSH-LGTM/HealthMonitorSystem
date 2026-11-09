/***
	*************************************************************************************************
	*	@file  	main.c
	*	@version V1.0
	*	@author  鹿小班科技	
	*	@brief   SD卡读写测试
   *************************************************************************************************
   *  @description
	*
	*	实验平台：鹿小班STM32F103ZET6核心板 （型号：LXB103ZE-P1）
	* 客服微信：19949278543
	*
>>>>> 功能说明：
	*
	*  1. 使用SDIO接口与SD卡进行通信，进行简单的读写测试
	* 	2. 驱动文件移植于 STM32429-EVAL 板卡的驱动
	*
	*************************************************************************************************
***/
#include "main.h"
#include "led.h"   
#include "key.h"
#include "usart.h"
#include "sdio_sd.h"

/********************************************** 变量定义 *******************************************/

#define 	Test_BlockSize  ((BLOCKSIZE * NumOf_Blocks) >> 2)	 //定义数据大小,SD块大小为512字节，因为是32位的数组，所以这里除以4
#define	Test_Addr 	 0x00
#define	NumOf_Blocks	1

uint8_t SD_Status ; 		 //SD卡检测标志位
uint32_t SD_WriteBuffer[Test_BlockSize];	//	写数据数组
uint32_t SD_ReadBuffer[Test_BlockSize];	//	读数据数组

/********************************************** 函数声明 *******************************************/


// 函数：SD卡测试
//	功能：SD卡块读写测试
//	返回值：0 - 读写测试失败，1 - 读写测试成功
//
uint8_t SD_Test(void)
{
	uint32_t i = 0;	
	
	for(i=0;i<Test_BlockSize;i++)	//将要写入SD卡的数据写入数组
	{
		SD_WriteBuffer[i] = i+3;
	}
	
	BSP_SD_Erase(Test_Addr, NumOf_Blocks);
	while(BSP_SD_GetCardState() != SD_TRANSFER_OK);	//等待通信结束
	
	BSP_SD_WriteBlocks_DMA(SD_WriteBuffer, Test_Addr, NumOf_Blocks);	//块写入
	while(BSP_SD_GetCardState() != SD_TRANSFER_OK);	//等待通信结束
	
	BSP_SD_ReadBlocks_DMA(SD_ReadBuffer, Test_Addr, NumOf_Blocks);	//块读取
	while(BSP_SD_GetCardState() != SD_TRANSFER_OK);	//等待通信结束

	for(i=0;i<Test_BlockSize;i++)	//验证读出的数据是否等于写入的数据
	{
		if( SD_WriteBuffer[i] != SD_ReadBuffer[i] )	//如果数据不相等，则返回0
			return 0;
	}		
	return 1; 	//数据正确，读写测试通过，返回1
}


void SystemClock_Config(void);		// 系统时钟配置

/***************************************************************************************************
*	函 数 名: main

*	函数功能: 主函数

****************************************************************************************************/

int main(void)
{
	HAL_Init();					// 初始化HAL库	
	SystemClock_Config();   // 配置系统时钟，主频180MHz
	LED_Init();             // 初始化LED引脚
	KEY_Init();					// 初始化按键引脚
	USART1_Init();				// USART1初始化

	SD_Status = BSP_SD_Init();	//SD卡初始化
	
	if( SD_Status == MSD_OK )	//检测是否初始化成功
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
		HAL_Delay(150);  
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
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
}


/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

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

