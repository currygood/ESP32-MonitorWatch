/**
 * OLED SSD1306 驱动程序（ESP32-S3 / ESP-IDF I2C Master 版本）
 *
 * 原始程序由江协科技创建并免费开源共享（jiangxiekeji.com）
 * ESP-IDF 移植：将 STM32 软件模拟 I2C 底层替换为 ESP-IDF i2c_master 驱动，
 * 所有显存操作、绘图函数、字符显示函数保持不变。
 *
 * 移植说明：
 *   - 删除 OLED_W_SCL / OLED_W_SDA / OLED_GPIO_Init / OLED_I2C_Start/Stop/SendByte
 *   - OLED_WriteCommand / OLED_WriteData 改用 i2c_master_transmit()
 *   - OLED_Init() 增加 i2c_master_dev_handle_t 参数，不再内部初始化 GPIO
 *   - 其余函数签名与原版完全相同
 */

#include "OLED.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "OLED";

/* OLED I2C 从机地址（7-bit 左移1位 = 0x78），WriteCommand/WriteData 中使用控制字节 */
#define OLED_I2C_ADDR   0x3C    /* 7-bit 地址，与 I2c_Add_Device() 的 dev_addr 一致 */

/* I2C 设备句柄，由 OLED_Init() 保存 */
static i2c_master_dev_handle_t oled_dev = NULL;

/*全局变量*********************/

/**
 * OLED显存数组
 * 所有的显示函数，都只是对此显存数组进行读写
 * 随后调用OLED_Update函数或OLED_UpdateArea函数
 * 才会将显存数组的数据发送到OLED硬件，进行显示
 */
uint8_t OLED_DisplayBuf[8][128];

/*********************全局变量*/


/*通信协议*********************/

/**
 * 函    数：OLED写命令
 * 参    数：Command 要写入的命令值，范围：0x00~0xFF
 * 返 回 值：无
 *
 * I2C 帧格式：[ADDR W] [0x00 控制字节] [Command]
 */
void OLED_WriteCommand(uint8_t Command)
{
    uint8_t buf[2] = {0x00, Command};   /* 0x00 = Co=0, D/C#=0 → 写命令 */
    esp_err_t ret = i2c_master_transmit(oled_dev, buf, sizeof(buf), -1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WriteCommand 0x%02X failed: %s", Command, esp_err_to_name(ret));
    }
}

/**
 * 函    数：OLED写数据
 * 参    数：Data  要写入数据的起始地址
 * 参    数：Count 要写入数据的数量
 * 返 回 值：无
 *
 * I2C 帧格式：[ADDR W] [0x40 控制字节] [Data[0]] [Data[1]] ... [Data[Count-1]]
 * 使用栈上缓冲（最大 129 字节 = 1字节控制 + 128字节数据），满足 OLED_Update 一次写 128 字节的需求
 */
void OLED_WriteData(uint8_t *Data, uint8_t Count)
{
    /* Count 最大 128，加上控制字节共 129 字节，栈上分配即可 */
    uint8_t buf[129];
    buf[0] = 0x40;                      /* 0x40 = Co=0, D/C#=1 → 写数据 */
    memcpy(&buf[1], Data, Count);

    esp_err_t ret = i2c_master_transmit(oled_dev, buf, (size_t)(Count + 1), -1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WriteData failed: %s", esp_err_to_name(ret));
    }
}

/*********************通信协议*/


/*硬件配置*********************/

/**
 * 函    数：OLED初始化
 * 参    数：dev_handle  已通过 I2c_Add_Device() 添加的 OLED I2C 设备句柄
 * 返 回 值：无
 * 说    明：
 *   调用前请先完成 I2C 总线初始化：
 *     I2c_Init_Bus(I2C_PORT, SDA_GPIO, SCL_GPIO, I2C_FREQ, &bus_handle);
 *     I2c_Add_Device(bus_handle, OLED_I2C_ADDR, I2C_FREQ, &dev_handle);
 *     OLED_Init(dev_handle);
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
	OLED_Update();

	return ESP_OK;
}

/**
 * 函    数：OLED设置显示光标位置
 * 参    数：Page 指定光标所在的页，范围：0~7
 * 参    数：X    指定光标所在的X轴坐标，范围：0~127
 * 返 回 值：无
 * 说    明：若使用 1.3 寸 OLED（SH1106 驱动，132列），请将 X += 2 的注释解除
 */
void OLED_SetCursor(uint8_t Page, uint8_t X)
{
//  X += 2;   /* 1.3 寸 SH1106 偏移修正 */
    OLED_WriteCommand(0xB0 | Page);                 //设置页位置
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));    //设置X位置高4位
    OLED_WriteCommand(0x00 | (X & 0x0F));           //设置X位置低4位
}

/*********************硬件配置*/


/*工具函数*********************/

uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;
    while (Y --)
    {
        Result *= X;
    }
    return Result;
}

uint8_t OLED_pnpoly(uint8_t nvert, int16_t *vertx, int16_t *verty, int16_t testx, int16_t testy)
{
    int16_t i, j, c = 0;
    for (i = 0, j = nvert - 1; i < nvert; j = i++)
    {
        if (((verty[i] > testy) != (verty[j] > testy)) &&
            (testx < (vertx[j] - vertx[i]) * (testy - verty[i]) / (verty[j] - verty[i]) + vertx[i]))
        {
            c = !c;
        }
    }
    return c;
}

uint8_t OLED_IsInAngle(int16_t X, int16_t Y, int16_t StartAngle, int16_t EndAngle)
{
    int16_t PointAngle;
    PointAngle = atan2(Y, X) / 3.14 * 180;
    if (StartAngle < EndAngle)
    {
        if (PointAngle >= StartAngle && PointAngle <= EndAngle)
        {
            return 1;
        }
    }
    else
    {
        if (PointAngle >= StartAngle || PointAngle <= EndAngle)
        {
            return 1;
        }
    }
    return 0;
}

/*********************工具函数*/


/*功能函数*********************/

void OLED_Update(void)
{
    uint8_t j;
    for (j = 0; j < 8; j ++)
    {
        OLED_SetCursor(j, 0);
        OLED_WriteData(OLED_DisplayBuf[j], 128);
    }
}

void OLED_UpdateArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
    int16_t j;
    int16_t Page, Page1;

    Page = Y / 8;
    Page1 = (Y + Height - 1) / 8 + 1;
    if (Y < 0)
    {
        Page -= 1;
        Page1 -= 1;
    }

    for (j = Page; j < Page1; j ++)
    {
        if (X >= 0 && X <= 127 && j >= 0 && j <= 7)
        {
            OLED_SetCursor(j, X);
            OLED_WriteData(&OLED_DisplayBuf[j][X], Width);
        }
    }
}

void OLED_Clear(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j ++)
    {
        for (i = 0; i < 128; i ++)
        {
            OLED_DisplayBuf[j][i] = 0x00;
        }
    }
}

void OLED_ClearArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
    int16_t i, j;
    for (j = Y; j < Y + Height; j ++)
    {
        for (i = X; i < X + Width; i ++)
        {
            if (i >= 0 && i <= 127 && j >= 0 && j <= 63)
            {
                OLED_DisplayBuf[j / 8][i] &= ~(0x01 << (j % 8));
            }
        }
    }
}

void OLED_Reverse(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j ++)
    {
        for (i = 0; i < 128; i ++)
        {
            OLED_DisplayBuf[j][i] ^= 0xFF;
        }
    }
}

void OLED_ReverseArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
    int16_t i, j;
    for (j = Y; j < Y + Height; j ++)
    {
        for (i = X; i < X + Width; i ++)
        {
            if (i >= 0 && i <= 127 && j >= 0 && j <= 63)
            {
                OLED_DisplayBuf[j / 8][i] ^= 0x01 << (j % 8);
            }
        }
    }
}

void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize)
{
    if (FontSize == OLED_8X16)
    {
        OLED_ShowImage(X, Y, 8, 16, OLED_F8x16[Char - ' ']);
    }
    else if (FontSize == OLED_6X8)
    {
        OLED_ShowImage(X, Y, 6, 8, OLED_F6x8[Char - ' ']);
    }
	else if(FontSize == OLED_12X24)
	{
		OLED_ShowImage(X, Y, 12, 24, OLED_F12x24[Char - ' ']);
	}
}

void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize)
{
	uint16_t i = 0;
	char SingleChar[5];
	uint8_t CharLength = 0;
	uint16_t XOffset = 0;
	uint16_t pIndex;
	
	while (String[i] != '\0')	//遍历字符串
	{
		
#ifdef OLED_CHARSET_UTF8						//定义字符集为UTF8
		/*此段代码的目的是，提取UTF8字符串中的一个字符，转存到SingleChar子字符串中*/
		/*判断UTF8编码第一个字节的标志位*/
		if ((String[i] & 0x80) == 0x00)			//第一个字节为0xxxxxxx
		{
			CharLength = 1;						//字符为1字节
			SingleChar[0] = String[i ++];		//将第一个字节写入SingleChar第0个位置，随后i指向下一个字节
			SingleChar[1] = '\0';				//为SingleChar添加字符串结束标志位
		}
		else if ((String[i] & 0xE0) == 0xC0)	//第一个字节为110xxxxx
		{
			CharLength = 2;						//字符为2字节
			SingleChar[0] = String[i ++];		//将第一个字节写入SingleChar第0个位置，随后i指向下一个字节
			if (String[i] == '\0') {break;}		//意外情况，跳出循环，结束显示
			SingleChar[1] = String[i ++];		//将第二个字节写入SingleChar第1个位置，随后i指向下一个字节
			SingleChar[2] = '\0';				//为SingleChar添加字符串结束标志位
		}
		else if ((String[i] & 0xF0) == 0xE0)	//第一个字节为1110xxxx
		{
			CharLength = 3;						//字符为3字节
			SingleChar[0] = String[i ++];
			if (String[i] == '\0') {break;}
			SingleChar[1] = String[i ++];
			if (String[i] == '\0') {break;}
			SingleChar[2] = String[i ++];
			SingleChar[3] = '\0';
		}
		else if ((String[i] & 0xF8) == 0xF0)	//第一个字节为11110xxx
		{
			CharLength = 4;						//字符为4字节
			SingleChar[0] = String[i ++];
			if (String[i] == '\0') {break;}
			SingleChar[1] = String[i ++];
			if (String[i] == '\0') {break;}
			SingleChar[2] = String[i ++];
			if (String[i] == '\0') {break;}
			SingleChar[3] = String[i ++];
			SingleChar[4] = '\0';
		}
		else
		{
			i ++;			//意外情况，i指向下一个字节，忽略此字节，继续判断下一个字节
			continue;
		}
#endif
		
#ifdef OLED_CHARSET_GB2312						//定义字符集为GB2312
		/*此段代码的目的是，提取GB2312字符串中的一个字符，转存到SingleChar子字符串中*/
		/*判断GB2312字节的最高位标志位*/
		if ((String[i] & 0x80) == 0x00)			//最高位为0
		{
			CharLength = 1;						//字符为1字节
			SingleChar[0] = String[i ++];		//将第一个字节写入SingleChar第0个位置，随后i指向下一个字节
			SingleChar[1] = '\0';				//为SingleChar添加字符串结束标志位
		}
		else									//最高位为1
		{
			CharLength = 2;						//字符为2字节
			SingleChar[0] = String[i ++];		//将第一个字节写入SingleChar第0个位置，随后i指向下一个字节
			if (String[i] == '\0') {break;}		//意外情况，跳出循环，结束显示
			SingleChar[1] = String[i ++];		//将第二个字节写入SingleChar第1个位置，随后i指向下一个字节
			SingleChar[2] = '\0';				//为SingleChar添加字符串结束标志位
		}
#endif
		
		/*显示上述代码提取到的SingleChar*/
		if (CharLength == 1)	//如果是单字节字符
		{
			/*使用OLED_ShowChar显示此字符*/
			OLED_ShowChar(X + XOffset, Y, SingleChar[0], FontSize);
			XOffset += FontSize;
		}
		else					//否则，即多字节字符（常见的就是中文）
		{
			/*遍历整个字模库，从字模库中寻找此字符的数据*/
			/*如果找到最后一个字符（定义为空字符串），则表示字符未在字模库定义，停止寻找*/
			for (pIndex = 0; strcmp(OLED_CF16x16[pIndex].Index, "") != 0; pIndex ++)
			{
				/*找到匹配的字符*/
				if (strcmp(OLED_CF16x16[pIndex].Index, SingleChar) == 0)
				{
					break;		//跳出循环，此时pIndex的值为指定字符的索引
				}
			}
			if (FontSize == OLED_8X16)		//给定字体为8*16点阵
			{
				/*将字模库OLED_CF16x16的指定数据以16*16的图像格式显示*/
				OLED_ShowImage(X + XOffset, Y, 16, 16, OLED_CF16x16[pIndex].Data);
				XOffset += 16;
			}
			else if (FontSize == OLED_6X8)	//给定字体为6*8点阵
			{
				/*空间不足，此位置显示'?'*/
				OLED_ShowChar(X + XOffset, Y, '?', OLED_6X8);
				XOffset += OLED_6X8;
			}
		}
	}
}

void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(X + i * FontSize, Y, Number / OLED_Pow(10, Length - i - 1) % 10 + '0', FontSize);
    }
}

void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i;
    uint32_t Number1;

    if (Number >= 0)
    {
        OLED_ShowChar(X, Y, '+', FontSize);
        Number1 = Number;
    }
    else
    {
        OLED_ShowChar(X, Y, '-', FontSize);
        Number1 = -Number;
    }

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(X + (i + 1) * FontSize, Y, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0', FontSize);
    }
}

void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i, SingleNumber;
    for (i = 0; i < Length; i++)
    {
        SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
        if (SingleNumber < 10)
        {
            OLED_ShowChar(X + i * FontSize, Y, SingleNumber + '0', FontSize);
        }
        else
        {
            OLED_ShowChar(X + i * FontSize, Y, SingleNumber - 10 + 'A', FontSize);
        }
    }
}

void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(X + i * FontSize, Y, Number / OLED_Pow(2, Length - i - 1) % 2 + '0', FontSize);
    }
}

void OLED_ShowFloatNum(int16_t X, int16_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize)
{
    uint32_t PowNum, IntNum, FraNum;

    if (Number >= 0)
    {
        OLED_ShowChar(X, Y, '+', FontSize);
    }
    else
    {
        OLED_ShowChar(X, Y, '-', FontSize);
        Number = -Number;
    }

    IntNum = Number;
    Number -= IntNum;
    PowNum = OLED_Pow(10, FraLength);
    FraNum = round(Number * PowNum);
    IntNum += FraNum / PowNum;

    OLED_ShowNum(X + FontSize, Y, IntNum, IntLength, FontSize);
    OLED_ShowChar(X + (IntLength + 1) * FontSize, Y, '.', FontSize);
    OLED_ShowNum(X + (IntLength + 2) * FontSize, Y, FraNum, FraLength, FontSize);
}

void OLED_ShowImage(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image)
{
    uint8_t i = 0, j = 0;
    int16_t Page, Shift;

    OLED_ClearArea(X, Y, Width, Height);

    for (j = 0; j < (Height - 1) / 8 + 1; j ++)
    {
        for (i = 0; i < Width; i ++)
        {
            if (X + i >= 0 && X + i <= 127)
            {
                Page = Y / 8;
                Shift = Y % 8;
                if (Y < 0)
                {
                    Page -= 1;
                    Shift += 8;
                }

                if (Page + j >= 0 && Page + j <= 7)
                {
                    OLED_DisplayBuf[Page + j][X + i] |= Image[j * Width + i] << (Shift);
                }

                if (Page + j + 1 >= 0 && Page + j + 1 <= 7)
                {
                    OLED_DisplayBuf[Page + j + 1][X + i] |= Image[j * Width + i] >> (8 - Shift);
                }
            }
        }
    }
}

void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...)
{
    char String[256];
    va_list arg;
    va_start(arg, format);
    vsprintf(String, format, arg);
    va_end(arg);
    OLED_ShowString(X, Y, String, FontSize);
}

void OLED_DrawPoint(int16_t X, int16_t Y)
{
    if (X >= 0 && X <= 127 && Y >= 0 && Y <= 63)
    {
        OLED_DisplayBuf[Y / 8][X] |= 0x01 << (Y % 8);
    }
}

uint8_t OLED_GetPoint(int16_t X, int16_t Y)
{
    if (X >= 0 && X <= 127 && Y >= 0 && Y <= 63)
    {
        if (OLED_DisplayBuf[Y / 8][X] & 0x01 << (Y % 8))
        {
            return 1;
        }
    }
    return 0;
}

void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1)
{
    int16_t x, y, dx, dy, d, incrE, incrNE, temp;
    int16_t x0 = X0, y0 = Y0, x1 = X1, y1 = Y1;
    uint8_t yflag = 0, xyflag = 0;

    if (y0 == y1)
    {
        if (x0 > x1) {temp = x0; x0 = x1; x1 = temp;}
        for (x = x0; x <= x1; x ++)
        {
            OLED_DrawPoint(x, y0);
        }
    }
    else if (x0 == x1)
    {
        if (y0 > y1) {temp = y0; y0 = y1; y1 = temp;}
        for (y = y0; y <= y1; y ++)
        {
            OLED_DrawPoint(x0, y);
        }
    }
    else
    {
        if (x0 > x1)
        {
            temp = x0; x0 = x1; x1 = temp;
            temp = y0; y0 = y1; y1 = temp;
        }

        if (y0 > y1)
        {
            y0 = -y0;
            y1 = -y1;
            yflag = 1;
        }

        if (y1 - y0 > x1 - x0)
        {
            temp = x0; x0 = y0; y0 = temp;
            temp = x1; x1 = y1; y1 = temp;
            xyflag = 1;
        }

        dx = x1 - x0;
        dy = y1 - y0;
        incrE = 2 * dy;
        incrNE = 2 * (dy - dx);
        d = 2 * dy - dx;
        x = x0;
        y = y0;

        if (yflag && xyflag)        {OLED_DrawPoint(y, -x);}
        else if (yflag)             {OLED_DrawPoint(x, -y);}
        else if (xyflag)            {OLED_DrawPoint(y, x);}
        else                        {OLED_DrawPoint(x, y);}

        while (x < x1)
        {
            x ++;
            if (d < 0)
            {
                d += incrE;
            }
            else
            {
                y ++;
                d += incrNE;
            }

            if (yflag && xyflag)    {OLED_DrawPoint(y, -x);}
            else if (yflag)         {OLED_DrawPoint(x, -y);}
            else if (xyflag)        {OLED_DrawPoint(y, x);}
            else                    {OLED_DrawPoint(x, y);}
        }
    }
}

void OLED_DrawRectangle(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled)
{
    int16_t i, j;
    if (!IsFilled)
    {
        for (i = X; i < X + Width; i ++)
        {
            OLED_DrawPoint(i, Y);
            OLED_DrawPoint(i, Y + Height - 1);
        }
        for (i = Y; i < Y + Height; i ++)
        {
            OLED_DrawPoint(X, i);
            OLED_DrawPoint(X + Width - 1, i);
        }
    }
    else
    {
        for (i = X; i < X + Width; i ++)
        {
            for (j = Y; j < Y + Height; j ++)
            {
                OLED_DrawPoint(i, j);
            }
        }
    }
}

void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled)
{
    int16_t minx = X0, miny = Y0, maxx = X0, maxy = Y0;
    int16_t i, j;
    int16_t vx[] = {X0, X1, X2};
    int16_t vy[] = {Y0, Y1, Y2};

    if (!IsFilled)
    {
        OLED_DrawLine(X0, Y0, X1, Y1);
        OLED_DrawLine(X0, Y0, X2, Y2);
        OLED_DrawLine(X1, Y1, X2, Y2);
    }
    else
    {
        if (X1 < minx) {minx = X1;}
        if (X2 < minx) {minx = X2;}
        if (Y1 < miny) {miny = Y1;}
        if (Y2 < miny) {miny = Y2;}

        if (X1 > maxx) {maxx = X1;}
        if (X2 > maxx) {maxx = X2;}
        if (Y1 > maxy) {maxy = Y1;}
        if (Y2 > maxy) {maxy = Y2;}

        for (i = minx; i <= maxx; i ++)
        {
            for (j = miny; j <= maxy; j ++)
            {
                if (OLED_pnpoly(3, vx, vy, i, j)) {OLED_DrawPoint(i, j);}
            }
        }
    }
}

void OLED_DrawCircle(int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled)
{
    int16_t x, y, d, j;

    d = 1 - Radius;
    x = 0;
    y = Radius;

    OLED_DrawPoint(X + x, Y + y);
    OLED_DrawPoint(X - x, Y - y);
    OLED_DrawPoint(X + y, Y + x);
    OLED_DrawPoint(X - y, Y - x);

    if (IsFilled)
    {
        for (j = -y; j < y; j ++)
        {
            OLED_DrawPoint(X, Y + j);
        }
    }

    while (x < y)
    {
        x ++;
        if (d < 0)
        {
            d += 2 * x + 1;
        }
        else
        {
            y --;
            d += 2 * (x - y) + 1;
        }

        OLED_DrawPoint(X + x, Y + y);
        OLED_DrawPoint(X + y, Y + x);
        OLED_DrawPoint(X - x, Y - y);
        OLED_DrawPoint(X - y, Y - x);
        OLED_DrawPoint(X + x, Y - y);
        OLED_DrawPoint(X + y, Y - x);
        OLED_DrawPoint(X - x, Y + y);
        OLED_DrawPoint(X - y, Y + x);

        if (IsFilled)
        {
            for (j = -y; j < y; j ++)
            {
                OLED_DrawPoint(X + x, Y + j);
                OLED_DrawPoint(X - x, Y + j);
            }

            for (j = -x; j < x; j ++)
            {
                OLED_DrawPoint(X - y, Y + j);
                OLED_DrawPoint(X + y, Y + j);
            }
        }
    }
}

void OLED_DrawEllipse(int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled)
{
    int16_t x, y, j;
    int16_t a = A, b = B;
    float d1, d2;

    x = 0;
    y = b;
    d1 = b * b + a * a * (-b + 0.5);

    if (IsFilled)
    {
        for (j = -y; j < y; j ++)
        {
            OLED_DrawPoint(X, Y + j);
            OLED_DrawPoint(X, Y + j);
        }
    }

    OLED_DrawPoint(X + x, Y + y);
    OLED_DrawPoint(X - x, Y - y);
    OLED_DrawPoint(X - x, Y + y);
    OLED_DrawPoint(X + x, Y - y);

    while (b * b * (x + 1) < a * a * (y - 0.5))
    {
        if (d1 <= 0)
        {
            d1 += b * b * (2 * x + 3);
        }
        else
        {
            d1 += b * b * (2 * x + 3) + a * a * (-2 * y + 2);
            y --;
        }
        x ++;

        if (IsFilled)
        {
            for (j = -y; j < y; j ++)
            {
                OLED_DrawPoint(X + x, Y + j);
                OLED_DrawPoint(X - x, Y + j);
            }
        }

        OLED_DrawPoint(X + x, Y + y);
        OLED_DrawPoint(X - x, Y - y);
        OLED_DrawPoint(X - x, Y + y);
        OLED_DrawPoint(X + x, Y - y);
    }

    d2 = b * b * (x + 0.5) * (x + 0.5) + a * a * (y - 1) * (y - 1) - a * a * b * b;

    while (y > 0)
    {
        if (d2 <= 0)
        {
            d2 += b * b * (2 * x + 2) + a * a * (-2 * y + 3);
            x ++;
        }
        else
        {
            d2 += a * a * (-2 * y + 3);
        }
        y --;

        if (IsFilled)
        {
            for (j = -y; j < y; j ++)
            {
                OLED_DrawPoint(X + x, Y + j);
                OLED_DrawPoint(X - x, Y + j);
            }
        }

        OLED_DrawPoint(X + x, Y + y);
        OLED_DrawPoint(X - x, Y - y);
        OLED_DrawPoint(X - x, Y + y);
        OLED_DrawPoint(X + x, Y - y);
    }
}

void OLED_DrawArc(int16_t X, int16_t Y, uint8_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled)
{
    int16_t x, y, d, j;

    d = 1 - Radius;
    x = 0;
    y = Radius;

    if (OLED_IsInAngle(x, y, StartAngle, EndAngle))   {OLED_DrawPoint(X + x, Y + y);}
    if (OLED_IsInAngle(-x, -y, StartAngle, EndAngle)) {OLED_DrawPoint(X - x, Y - y);}
    if (OLED_IsInAngle(y, x, StartAngle, EndAngle))   {OLED_DrawPoint(X + y, Y + x);}
    if (OLED_IsInAngle(-y, -x, StartAngle, EndAngle)) {OLED_DrawPoint(X - y, Y - x);}

    if (IsFilled)
    {
        for (j = -y; j < y; j ++)
        {
            if (OLED_IsInAngle(0, j, StartAngle, EndAngle)) {OLED_DrawPoint(X, Y + j);}
        }
    }

    while (x < y)
    {
        x ++;
        if (d < 0)
        {
            d += 2 * x + 1;
        }
        else
        {
            y --;
            d += 2 * (x - y) + 1;
        }

        if (OLED_IsInAngle(x, y, StartAngle, EndAngle))   {OLED_DrawPoint(X + x, Y + y);}
        if (OLED_IsInAngle(y, x, StartAngle, EndAngle))   {OLED_DrawPoint(X + y, Y + x);}
        if (OLED_IsInAngle(-x, -y, StartAngle, EndAngle)) {OLED_DrawPoint(X - x, Y - y);}
        if (OLED_IsInAngle(-y, -x, StartAngle, EndAngle)) {OLED_DrawPoint(X - y, Y - x);}
        if (OLED_IsInAngle(x, -y, StartAngle, EndAngle))  {OLED_DrawPoint(X + x, Y - y);}
        if (OLED_IsInAngle(y, -x, StartAngle, EndAngle))  {OLED_DrawPoint(X + y, Y - x);}
        if (OLED_IsInAngle(-x, y, StartAngle, EndAngle))  {OLED_DrawPoint(X - x, Y + y);}
        if (OLED_IsInAngle(-y, x, StartAngle, EndAngle))  {OLED_DrawPoint(X - y, Y + x);}

        if (IsFilled)
        {
            for (j = -y; j < y; j ++)
            {
                if (OLED_IsInAngle(x, j, StartAngle, EndAngle))  {OLED_DrawPoint(X + x, Y + j);}
                if (OLED_IsInAngle(-x, j, StartAngle, EndAngle)) {OLED_DrawPoint(X - x, Y + j);}
            }

            for (j = -x; j < x; j ++)
            {
                if (OLED_IsInAngle(-y, j, StartAngle, EndAngle)) {OLED_DrawPoint(X - y, Y + j);}
                if (OLED_IsInAngle(y, j, StartAngle, EndAngle))  {OLED_DrawPoint(X + y, Y + j);}
            }
        }
    }
}

/*********************功能函数*/

// OLED显示任务
void Task_OLED_Show(void *pvParameters)
{
	// 等待各种硬件初始化完成
	vTaskDelay(pdMS_TO_TICKS(1000));

	// 获取I2C总线句柄并初始化OLED
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
	if (i2c_bus != NULL) {
		if (OLED_Init(i2c_bus) == ESP_OK) {
			ESP_LOGI("OLED", "OLED初始化成功");
			
			// OLED显示初始化界面 - 64x128屏幕适配
			OLED_Clear();
			OLED_ShowString(0, 0, "Health Monitor",OLED_6X8);
			OLED_ShowString(0, 8, "Status: Normal",OLED_6X8);
			OLED_ShowString(0, 16, "HR: ---",OLED_6X8);
			OLED_ShowString(0, 24, "SpO2: ---%",OLED_6X8);
			OLED_ShowString(0, 32, "Ready",OLED_6X8);
			OLED_Update();
			while(1);
		} else {
			ESP_LOGE("OLED", "OLED初始化失败");
		}
	} else {
		ESP_LOGE("OLED", "无法获取I2C总线句柄");
	}
	
	while(1)
	{
		
		
		OLED_Update();
		
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}