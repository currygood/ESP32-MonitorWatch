/**
 * @file OLED.c
 * @brief OLED屏幕驱动代码
 * @details 本文件实现了ESP32S3与OLED屏幕的I2C通信，使用与MAX30102相同的I2C总线
 */

#include "OLED.h"

// OLED设备句柄定义
i2c_master_dev_handle_t oled_dev = NULL;

/**
 * @brief OLED初始化
 * @param bus_handle I2C总线句柄
 * @return esp_err_t 初始化结果
 * @details 使用共享I2C总线初始化OLED设备
 */
esp_err_t OLED_Init(i2c_master_bus_handle_t bus_handle)
{
	// 添加OLED设备到I2C总线
	esp_err_t ret = I2c_Add_Device(bus_handle, OLED_ADDR, I2C_FREQ, &oled_dev);
	if (ret != ESP_OK) {
		return ret;
	}

	uint32_t i, j;

	// 延时一段时间，确保OLED电源稳定
	for (i = 0; i < 1000; i++) // 延时
	{
		for (j = 0; j < 1000; j++)
			;
	}

	// 关闭OLED显示
	OLED_WriteCommand(0xAE); // 关闭显示

	// 设置时钟分频因子/振荡器频率
	OLED_WriteCommand(0xD5); // 设置显示时钟分频因子/振荡器频率
	OLED_WriteCommand(0x80);

	// 设置多路复用率
	OLED_WriteCommand(0xA8); // 设置多路复用率
	OLED_WriteCommand(0x3F);

	// 设置显示偏移
	OLED_WriteCommand(0xD3); // 设置显示偏移
	OLED_WriteCommand(0x00);

	// 设置显示开始行
	OLED_WriteCommand(0x40); // 设置显示开始行

	// 设置左右方向，0xA1正常 0xA0左右反置
	OLED_WriteCommand(0xA1); // 设置左右方向，0xA1正常 0xA0左右反置

	// 设置上下方向，0xC8正常 0xC0上下反置
	OLED_WriteCommand(0xC8); // 设置上下方向，0xC8正常 0xC0上下反置

	// 设置COM引脚硬件配置
	OLED_WriteCommand(0xDA); // 设置COM引脚硬件配置
	OLED_WriteCommand(0x12);

	// 设置对比度
	OLED_WriteCommand(0x81); // 设置对比度
	OLED_WriteCommand(0xCF);

	// 设置预充电周期
	OLED_WriteCommand(0xD9); // 设置预充电周期
	OLED_WriteCommand(0xF1);

	// 设置VCOMH电压倍率
	OLED_WriteCommand(0xDB); // 设置VCOMH电压倍率
	OLED_WriteCommand(0x30);

	// 设置整个显示打开/关闭
	OLED_WriteCommand(0xA4); // 设置整个显示打开/关闭

	// 设置显示方式，0xA6正常显示 0xA7反相显示
	OLED_WriteCommand(0xA6); // 设置显示方式，0xA6正常显示 0xA7反相显示

	// 设置电荷泵
	OLED_WriteCommand(0x8D); // 设置电荷泵
	OLED_WriteCommand(0x14);

	// 打开OLED显示
	OLED_WriteCommand(0xAF); // 打开显示

	// 清除OLED屏幕
	OLED_Clear(); // OLED清屏

	return ESP_OK;
}

/**
 * @brief 获取OLED设备句柄
 * @return i2c_master_dev_handle_t OLED设备句柄
 */
i2c_master_dev_handle_t OLED_Get_Device_Handle(void)
{
	return oled_dev;
}

/**
 * @brief OLED写入命令
 * @param Command 要写入的命令
 * @details 使用I2C模块发送控制命令到OLED
 */
void OLED_WriteCommand(uint8_t Command)
{
	uint8_t buffer[2] = {0x00, Command}; // 控制字节 + 命令
	OLED_Write(oled_dev, buffer, sizeof(buffer));
}

/**
 * @brief OLED写入数据
 * @param Data 要写入的数据
 * @details 使用I2C模块发送显示数据到OLED
 */
void OLED_WriteData(uint8_t Data)
{
	uint8_t buffer[2] = {0x40, Data}; // 数据字节 + 数据
	OLED_Write(oled_dev, buffer, sizeof(buffer));
}

/**
 * @brief OLED设置光标位置
 * @param Y 行坐标，范围：0~7
 * @param X 列坐标，范围：0~127
 * @details 设置OLED的显示光标位置
 */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
	// 设置行地址（Y坐标）
	OLED_WriteCommand(0xB0 | Y); // 设置Y位置
	// 设置列地址的高4位
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4)); // 设置X位置高4位
	// 设置列地址的低4位
	OLED_WriteCommand(0x00 | (X & 0x0F)); // 设置X位置低4位
}

/**
 * @brief OLED清屏
 * @details 清除OLED屏幕上的所有显示内容
 */
void OLED_Clear(void)
{
	uint8_t i, j;
	// 循环遍历所有行（共8行）
	for (j = 0; j < 8; j++)
	{
		// 设置当前行的起始位置
		OLED_SetCursor(j, 0);
		// 循环遍历当前行的所有列（共128列）
		for (i = 0; i < 128; i++)
		{
			// 写入0x00，清除当前位置的显示
			OLED_WriteData(0x00);
		}
	}
}

/**
 * @brief OLED显示单个字符
 * @param Line 行号，范围：1~4
 * @param Column 列号，范围：1~16
 * @param Char 要显示的字符，ASCII码
 * @details 在指定位置显示一个字符
 */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
	uint8_t i;
	// 设置字符上半部分的起始位置
	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8); // 设置光标位置在上半部分
	// 显示字符的上半部分（8行）
	for (i = 0; i < 8; i++)
	{
		// 从字库中获取字符的上半部分数据并显示
		OLED_WriteData(OLED_F8x16[Char - ' '][i]); // 显示上半部分
	}
	// 设置字符下半部分的起始位置
	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8); // 设置光标位置在下半部分
	// 显示字符的下半部分（8行）
	for (i = 0; i < 8; i++)
	{
		// 从字库中获取字符的下半部分数据并显示
		OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]); // 显示下半部分
	}
}

/**
 * @brief OLED显示字符串
 * @param Line 行号，范围：1~4
 * @param Column 列号，范围：1~16
 * @param String 要显示的字符串，ASCII码
 * @details 在指定位置显示一个字符串
 */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
	uint8_t i;
	// 循环显示字符串中的每个字符，直到遇到结束符'\0'
	for (i = 0; String[i] != '\0'; i++)
	{
		// 显示当前字符，列号自动递增
		OLED_ShowChar(Line, Column + i, String[i]);
	}
}

/**
 * @brief 计算X的Y次方
 * @param X 底数
 * @param Y 指数
 * @retval 计算结果
 * @details 用于数字显示时的位权计算
 */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	// 循环Y次，计算X的Y次方
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
 * @brief OLED显示无符号数字
 * @param Line 行号，范围：1~4
 * @param Column 列号，范围：1~16
 * @param Number 要显示的数字，范围：0~4294967295
 * @param Length 要显示的数字长度，范围：1~10
 * @details 在指定位置显示一个无符号数字
 */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	// 循环显示数字的每一位
	for (i = 0; i < Length; i++)
	{
		// 计算当前位的数字并显示
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
 * @brief OLED显示有符号数字
 * @param Line 行号，范围：1~4
 * @param Column 列号，范围：1~16
 * @param Number 要显示的数字，范围：-2147483648~2147483647
 * @param Length 要显示的数字长度，范围：1~10
 * @details 在指定位置显示一个有符号数字
 */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;
	// 判断数字的符号
	if (Number >= 0)
	{
		// 显示正号
		OLED_ShowChar(Line, Column, '+');
		// 保存数字的绝对值
		Number1 = Number;
	}
	else
	{
		// 显示负号
		OLED_ShowChar(Line, Column, '-');
		// 保存数字的绝对值
		Number1 = -Number;
	}
	// 循环显示数字的每一位
	for (i = 0; i < Length; i++)
	{
		// 计算当前位的数字并显示
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
 * @brief OLED显示十六进制数字
 * @param Line 行号，范围：1~4
 * @param Column 列号，范围：1~16
 * @param Number 要显示的数字，范围：0~0xFFFFFFFF
 * @param Length 要显示的数字长度，范围：1~8
 * @details 在指定位置显示一个十六进制数字
 */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i, SingleNumber;
	// 循环显示数字的每一位
	for (i = 0; i < Length; i++)
	{
		// 计算当前位的数字
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		// 根据数字大小显示对应的字符
		if (SingleNumber < 10)
		{
			// 0-9直接显示
			OLED_ShowChar(Line, Column + i, SingleNumber + '0');
		}
		else
		{
			// 10-15显示为A-F
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
		}
	}
}

/**
 * @brief OLED显示二进制数字
 * @param Line 行号，范围：1~4
 * @param Column 列号，范围：1~16
 * @param Number 要显示的数字，范围：0~1111 1111 1111 1111
 * @param Length 要显示的数字长度，范围：1~16
 * @details 在指定位置显示一个二进制数字
 */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	// 循环显示数字的每一位
	for (i = 0; i < Length; i++)
	{
		// 计算当前位的数字并显示
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
	}
}