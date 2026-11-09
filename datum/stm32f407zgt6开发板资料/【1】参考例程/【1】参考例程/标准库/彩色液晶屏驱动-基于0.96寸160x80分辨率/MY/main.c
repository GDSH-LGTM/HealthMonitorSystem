/***
	***************************************************************************************************
	*	@file  	main.c
	*	@version V1.0
	*	@author  鹿小班科技	
	*	@brief   SPI驱动显示屏，屏幕控制器 ST7789
   ****************************************************************************************************
   *  @description
	*
	*	实验平台：鹿小班STM32F407ZGT6核心板 （型号：LXB407ZG-P1）
	* 客服微信：19949278543
   *
>>>>> 其他说明：
	*
	*	1. 中文字库使用的是小字库，即用到了对应的汉字再去取模，用户可以根据需求自行增添或删减
	*	2. 各个函数的功能和使用可以参考函数的说明
	*
	***************************************************************************************************
***/


#include "stm32f4xx.h"
#include "led.h"   
#include "delay.h"
#include "key.h"  
#include "usart.h"
#include "lcd_spi_096.h"

// LCD测试函数，函数定义在底部
void 	LCD_Test_Clear(void);			// 清屏测试
void 	LCD_Test_Text(void);			   // 文本测试
void 	LCD_Test_Variable (void);	   // 变量显示，包括整数和小数
void 	LCD_Test_Color(void);			// 矩形填充测试
void 	LCD_Test_Grahic(void);		   // 2D图形绘制
void 	LCD_Test_Image(void);			// 图片显示
void  LCD_Test_Direction(void);	   // 更换显示方向


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
	
	SPI_LCD_Init();	// LCD初始化

	while (1)
	{
		LCD_Test_Clear(); 		// 清屏测试
		LCD_Test_Text();			// 文本测试
		LCD_Test_Variable();		// 变量显示，包括整数和小数
		LCD_Test_Color();			// 颜色测试
		LCD_Test_Grahic();		// 2D图形绘制	
		LCD_Test_Image();			// 图片显示
		LCD_Test_Direction();	// 更换显示方向	
	}
}


/*************************************************************************************************
*	函 数 名:	LCD_Test_Clear
*
*	函数功能:	清屏测试
*************************************************************************************************/
void LCD_Test_Clear(void)
{
	uint8_t	i = 0;			// 计数变量
	
	LCD_SetDirection(Direction_H_Flip);		
	LCD_SetTextFont(&CH_Font16);			// 设置2424中文字体,ASCII字体对应为2412
	LCD_SetColor(LCD_BLACK);				// 设置画笔颜色

	for(i=0;i<8;i++)
	{
		switch (i)		// 切换背景色
		{
			case 0: LCD_SetBackColor(LIGHT_RED); 		break;	
			case 1: LCD_SetBackColor(LIGHT_GREEN); 	break;				
			case 2: LCD_SetBackColor(LIGHT_BLUE); 		break;
			case 3: LCD_SetBackColor(LIGHT_YELLOW); 	break;
			case 4: LCD_SetBackColor(LIGHT_CYAN);		break;
			case 5: LCD_SetBackColor(LIGHT_GREY); 		break;
			case 6: LCD_SetBackColor(LIGHT_MAGENTA); 	break;
			case 7: LCD_SetBackColor(LCD_WHITE); 		break;			
			default:	break;			
		}
		LCD_Clear();		// 清屏
		LCD_DisplayText(13, 5,"STM32 刷屏测试");
		LCD_DisplayText(13,30,"屏幕分辨率:160*80");
		LCD_DisplayText(13,55,"控制器:ST7735");	
		Delay_ms(1000);	// 延时
	}
}

/*************************************************************************************************
*	函 数 名:	LCD_Test_Text
*
*	函数功能:	文本显示测试
*
*	说    明:	显示文本，包括各种字体大小的中文和ASCII字符 
*************************************************************************************************/
void LCD_Test_Text(void)
{
	LCD_SetBackColor(LCD_BLACK); 			//	设置背景色
	LCD_Clear(); 								// 清屏

	LCD_SetColor(LCD_CYAN);   
	LCD_SetAsciiFont(&ASCII_Font12); LCD_DisplayString(0,  0,"ASCII:12*12"); 	
	LCD_SetAsciiFont(&ASCII_Font16); LCD_DisplayString(0, 12,"ASCII:16*16");																																		                                                                   					  		
	LCD_SetAsciiFont(&ASCII_Font20); LCD_DisplayString(0, 28,"ASCII:20*20");  
	LCD_SetColor(LCD_WHITE);				    		
	LCD_SetAsciiFont(&ASCII_Font24); LCD_DisplayString(0, 48,"ASCII:2424"); 
	LCD_SetColor(LCD_YELLOW);		
	LCD_SetAsciiFont(&ASCII_Font32); LCD_DisplayString(135, 25,"*");
	LCD_SetAsciiFont(&ASCII_Font32); LCD_DisplayString(128,  0,"32"); LCD_DisplayString(128,  48,"32");

	Delay_ms(2000);	// 延时等待
	LCD_Clear(); 								// 清屏

	LCD_SetTextFont(&CH_Font12);			// 设置1212中文字体,ASCII字体对应为1206
	LCD_SetColor(0X8AC6D1);					// 设置画笔
	LCD_DisplayText(0, 0,"1212:鹿小班科技");	
	
	LCD_SetTextFont(&CH_Font16);			// 设置1616中文字体,ASCII字体对应为1608
	LCD_SetColor(0XC5E1A5);					// 设置画笔
	LCD_DisplayText(0, 12,"1616:鹿小班科技");		
	
	LCD_SetTextFont(&CH_Font20);			// 设置2020中文字体,ASCII字体对应为2010
	LCD_SetColor(0XFFB549);					// 设置画笔
	LCD_DisplayText(0, 28,"2020:鹿小班科技");		

	LCD_SetTextFont(&CH_Font24);			// 设置2424中文字体,ASCII字体对应为2412
	LCD_SetColor(0XFF585D);					// 设置画笔
	LCD_DisplayText(0, 48,"2424:鹿小班科技");		

	Delay_ms(2000);	// 延时等待
}

/************************************************************************************************
*	函 数 名:	LCD_Test_Variable
*
*	函数功能:	变量显示，包括整数和小数
*************************************************************************************************/
void LCD_Test_Variable (void)
{
	uint16_t i;					// 计数变量
	int32_t	a = 0;			// 定义整数变量，用于测试
	int32_t	b = 0;			// 定义整数变量，用于测试
	int32_t	c = 0;			// 定义整数变量，用于测试

	double f = -1234.1234;	// 定义浮点数变量，用于测试
	
	LCD_SetBackColor(LCD_BLACK); 			//	设置背景色
	LCD_Clear(); 								// 清屏
	
   LCD_SetTextFont(&CH_Font16);     
	LCD_SetColor(LIGHT_CYAN);					// 设置画笔，蓝绿色	

	LCD_DisplayText(0, 0,"整数:");									
				
	LCD_SetColor(LIGHT_YELLOW);				// 设置画笔，亮黄色		
	LCD_DisplayText(0,20,"填充空格:");	
	LCD_DisplayText(0,40,"填充0:");	
	
	LCD_SetColor(LIGHT_RED);					// 设置画笔	，亮红色		
	LCD_DisplayText(0,60,"小数:");	

	
	for(i=0;i<100;i++)
   {
		LCD_SetColor(LIGHT_CYAN);								// 设置画笔	，蓝绿色	
		LCD_ShowNumMode(Fill_Space);							// 多余位填充空格
		LCD_DisplayNumber( 50,0, b+i*10, 4) ;				// 显示变量			
		LCD_DisplayNumber( 100,0, c-i*10, 4) ;				// 显示变量			
		
		LCD_SetColor(LIGHT_YELLOW);								// 设置画笔，亮黄色	

		LCD_ShowNumMode(Fill_Space);								// 多余位填充 空格
		LCD_DisplayNumber( 80,20, a+i*150, 8) ;				// 显示变量		

		LCD_ShowNumMode(Fill_Zero);								// 多余位填充0      
		LCD_DisplayNumber( 80,40, b+i*150, 8) ;				// 显示变量			
		
		LCD_SetColor(LIGHT_RED);									// 设置画笔，亮红色			
		LCD_ShowNumMode(Fill_Space);								// 多余位填充空格		
		LCD_DisplayDecimals( 50,60, f+i*0.01, 11,4);		// 显示小数		
		
		Delay_ms(15);				
   }
	Delay_ms(2500);		
}
/*************************************************************************************************
*	函 数 名:	LCD_Test_Color
*
*	函数功能:	颜色测
*************************************************************************************************/
void LCD_Test_Color(void)
{
	uint16_t i;					// 计数变量
	uint16_t y;
// 颜色测试>>>>>	
	LCD_SetBackColor(LCD_BLACK); //设置背景色
	LCD_Clear(); //清屏，刷背景色
	
	LCD_SetTextFont(&CH_Font16);			// 设置字体
	LCD_SetColor(LCD_WHITE);				// 设置画笔颜色
	LCD_DisplayText(0,0,"RGB三基色:");

	//使用画线函数绘制三基色色条
	for(i=0;i<160;i++)
	{
		LCD_SetColor( LCD_RED-(i<<16) );
      LCD_DrawLine_V(0+i,  20,10);
	}
	for(i=0;i<240;i++)
	{
		LCD_SetColor( LCD_GREEN-(i<<8) );
      LCD_DrawLine_V(0+i,  30,10);
	}
	for(i=0;i<240;i++)
	{
		LCD_SetColor( LCD_BLUE-i );
      LCD_DrawLine_V(0+i,  40,10);
	}	

   y = 50;
   LCD_SetColor(LIGHT_CYAN);    LCD_FillRect(85,y+5     ,75,10);  LCD_DisplayString(0,y     ,"LIGHT_CYAN");	   
	LCD_SetColor(LIGHT_MAGENTA); LCD_FillRect(105,y+16*1+5,55,10);  LCD_DisplayString(0,y+16*1,"LIGHT_MAGENTA");	

	Delay_ms(2000);
}

/*************************************************************************************************
*	函 数 名:	LCD_Test_Grahic
*
*	函数功能:	2D图形绘制
*************************************************************************************************/
void LCD_Test_Grahic(void)
{
	LCD_SetBackColor(LCD_BLACK); //设置背景色
	LCD_Clear(); //清屏，刷背景色	

	LCD_SetColor(LCD_WHITE);	
	LCD_DrawRect(0,0,160,80); 			//绘制矩形

	LCD_SetColor(LCD_RED);    LCD_FillCircle(50,40,30);		//填充圆形
	LCD_SetColor(LCD_GREEN);  LCD_FillCircle(80,40,30); 	
	LCD_SetColor(LCD_BLUE);   LCD_FillCircle(110,40,30);  	
	
	Delay_ms(1000);	
	LCD_Clear(); //清屏，刷背景色	
	
	LCD_SetColor(LIGHT_CYAN);
	LCD_DrawCircle(80,40,30);			//绘制圆形
	LCD_DrawCircle(80,40,20);   

	LCD_SetColor(LIGHT_RED);	
	LCD_DrawEllipse(80,40,70,28); 	//绘制椭圆 
	LCD_DrawEllipse(80,40,60,35); 	//绘制椭圆    

	Delay_ms(1000);		
}
/*************************************************************************************************
*	函 数 名:	LCD_Test_Image
*
*	函数功能:	图片显示测试
*************************************************************************************************/
void LCD_Test_Image(void)
{
	LCD_SetBackColor(LCD_BLACK); 			//	设置背景色
	LCD_Clear(); 								// 清屏
	
	LCD_SetColor( 0xffF6E58D);
	LCD_DrawImage( 10, 10, 60, 60, Image_Android_60x60) ;	   // 显示图片

	LCD_SetColor( 0xff9DD3A8);
	LCD_DrawImage( 90, 10, 60, 60, Image_Toys_60x60) ;		// 显示图片

	Delay_ms(2000);	
}
/*************************************************************************************************
*	函 数 名:	LCD_Test_Direction
*
*	函数功能:	更换显示方向
*************************************************************************************************/
void  LCD_Test_Direction(void)
{
   LCD_SetDirection(Direction_V);		   
	LCD_SetBackColor(LCD_BLACK); 			//	设置背景色
	LCD_Clear(); 								// 清屏
	LCD_SetTextFont(&CH_Font12);  
	LCD_SetColor( 0xffDFF9FB);  	
	LCD_DisplayText(5,10,"Direction_V"); 	
	LCD_SetColor( 0xffF6E58D);
	LCD_DrawImage( 10,50, 60, 60, Image_Android_60x60) ;	   // 显示图片
	LCD_SetTextFont(&CH_Font24);
	LCD_SetColor( 0xff9DD3A8);  
	LCD_DisplayText(16,112,"鹿小班");
	LCD_DisplayText(16,136,"科技");
	Delay_ms(1000);	// 延时	

   LCD_SetDirection(Direction_H);		   
	LCD_SetBackColor(LCD_BLACK); 			//	设置背景色
	LCD_Clear(); 								// 清屏
	LCD_SetTextFont(&CH_Font16);  
	LCD_SetColor( 0xffDFF9FB);  	
	LCD_DisplayText(20,0,"Direction_H"); 	
	LCD_SetColor( 0xffF6E58D);
	LCD_DrawImage( 30,20, 60, 60, Image_Android_60x60) ;	   // 显示图片
	LCD_SetTextFont(&CH_Font24);
	LCD_SetColor( 0xff9DD3A8);  
	LCD_DisplayText(100,16,"鹿小班");
	LCD_DisplayText(100,40,"科技");
	Delay_ms(1000);	// 延时		

   LCD_SetDirection(Direction_V_Flip);		   
	LCD_SetBackColor(LCD_BLACK); 			//	设置背景色
	LCD_Clear(); 								// 清屏
	LCD_SetTextFont(&CH_Font16);  
	LCD_SetColor( 0xffDFF9FB);  	
	LCD_DisplayText(20,10,"_V_Flip"); 	
	LCD_SetColor( 0xffF6E58D);
	LCD_DrawImage( 10,50, 60, 60, Image_Android_60x60) ;	   // 显示图片
	LCD_SetTextFont(&CH_Font24);
	LCD_SetColor( 0xff9DD3A8);  
	LCD_DisplayText(16,112,"鹿小班");
	LCD_DisplayText(16,136,"科技");
	Delay_ms(1000);	// 延时		

   LCD_SetDirection(Direction_H_Flip);		   
	LCD_SetBackColor(LCD_BLACK); 			//	设置背景色
	LCD_Clear(); 								// 清屏
	LCD_SetTextFont(&CH_Font16);  
	LCD_SetColor( 0xffDFF9FB);  	
	LCD_DisplayText(20,0,"Direction_H_Flip"); 	
	LCD_SetColor( 0xffF6E58D);
	LCD_DrawImage( 30,20, 60, 60, Image_Android_60x60) ;	   // 显示图片
	LCD_SetTextFont(&CH_Font24);
	LCD_SetColor( 0xff9DD3A8);  
	LCD_DisplayText(100,16,"鹿小班");
	LCD_DisplayText(100,40,"科技");
	Delay_ms(1000);	// 延时		

}

