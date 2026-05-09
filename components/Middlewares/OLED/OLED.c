#include "OLED.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rtc_driver.h"
#include <time.h>
#include "GetBaLevel.h"
#include "Key.h"
#include "Buzzer.h"

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
    int16_t i, j;
    uint8_t ImageRowHeight = (Height - 1) / 8 + 1; // 图像占用的“页”数（源数据）

    /* 1. 先清除指定区域，防止叠加乱码 */
    OLED_ClearArea(X, Y, Width, Height);

    /* 2. 遍历图像数据的每一个 8 像素切片(Vertical Byte) */
    for (j = 0; j < ImageRowHeight; j++)
    {
        for (i = 0; i < Width; i++)
        {
            // 当前处理的显存横坐标
            int16_t CurrentX = X + i;
            if (CurrentX < 0 || CurrentX > 127) continue; // 横向越界裁剪

            // 取出源数据中的一字节（8个垂直像素）
            uint8_t Data = Image[j * Width + i];

            // 计算这一字节数据在屏幕上的实际像素起始 Y 坐标
            int16_t RealY = Y + (j * 8);

            // 将这 8 位数据拆分映射到显存的 Page 中
            for (uint8_t bit = 0; bit < 8; bit++)
            {
                // 检查当前位是否超出了图像实际高度（防止非8倍数高度时的多余位干扰）
                if ((j * 8 + bit) >= Height) break;

                // 计算当前像素点在屏幕上的绝对 Y 坐标
                int16_t TargetY = RealY + bit;

                // 纵向越界裁剪
                if (TargetY < 0 || TargetY > 63) continue;

                // 写入显存
                if (Data & (0x01 << bit))
                {
                    OLED_DisplayBuf[TargetY / 8][CurrentX] |= (0x01 << (TargetY % 8));
                }
                else
                {
                    // 注意：由于前面调用了ClearArea，这里通常不需要显式写0
                    // 如果需要强制覆盖，可以取消下一行的注释
                    // OLED_DisplayBuf[TargetY / 8][CurrentX] &= ~(0x01 << (TargetY % 8));
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
typedef enum { PAGE_MAIN, PAGE_SENSOR } oled_page_t;
void OLED_DrawGauge(int cx, int cy, int r, int max_value)
{
    // 外圆
    OLED_DrawCircle(cx, cy, r, OLED_UNFILLED);

    // 刻度（每20一个）
    for (int v = 0; v <= max_value; v += max_value / 10)
    {
        float angle = (-120 + (float)v / max_value * 240) * 3.1416 / 180;

        int x1 = cx + (r - 2) * cos(angle);
        int y1 = cy + (r - 2) * sin(angle);

        int x2 = cx + r * cos(angle);
        int y2 = cy + r * sin(angle);

        OLED_DrawLine(x1, y1, x2, y2);
    }
}

void OLED_DrawPointer(int cx, int cy, int r, int value, int max_value)
{
    float angle = (-120 + (float)value / max_value * 240) * 3.1416 / 180;

    int x = cx + (r - 4) * cos(angle);
    int y = cy + (r - 4) * sin(angle);

    OLED_DrawLine(cx, cy, x, y);
}

/**
 * 仪表盘显示函数
 * @param cx, cy: 圆心坐标
 * @param r: 半径
 * @param value: 当前数值
 * @param max_value: 最大量程
 * @param label: 顶部标签 (如 "HR")
 * @param unit: 单位 (如 "bpm")
 */
void OLED_DrawMeter(int cx, int cy, int r, int value, int max_value, char *label, char *unit)
{
    // 1. 画表盘刻度
    OLED_DrawGauge(cx, cy, r, max_value);

    // 2. 限制数值范围，防止指针飞出
    int draw_val = value;
    if (draw_val > max_value) draw_val = max_value;
    if (draw_val < 0) draw_val = 0;

    // 3. 画指针
    OLED_DrawPointer(cx, cy, r, draw_val, max_value);

    // 4. 显示顶部标题 (如 HR 或 SpO2)
    OLED_ShowString(cx - 10, cy - r - 10, label, OLED_6X8);

    // 5. 显示下方数值和单位
    char buf[20];
    sprintf(buf, "%d%s", value, unit);
    
    // 简单的居中计算：每个字符约6像素宽
    int str_width = strlen(buf) * 6;
    OLED_ShowString(cx - str_width / 2, cy + r + 5, buf, OLED_6X8);
}

static uint8_t OLED_ShowState=1; //  1: 默认显示时间和电量；2: 显示心率和血氧

void OLED_Set_ShowState(uint8_t state)
{
	OLED_ShowState = state;
}

static bool isOLEDShow=false;

void OLED_Notify_Show(bool isShow)
{
	isOLEDShow=isShow;
	// ESP_LOGE("OLED_NOTIFY", "屏幕显示状态切换为: %s", isShow ? "显示" : "隐藏");
}

static void OLED_Show_HR_OxygenPage(void)
{
	Sensor_Message_t msg;
    QueueHandle_t oled_q = Message_Queue_Get_Handle(QUEUE_TYPE_OLED);
    
    // 设置默认值，防止队列为空时显示随机数
    uint32_t display_hr = 0;
    uint32_t display_spo2 = 0;
	OLED_Clear();
	
    while(1)
    {
		// 非阻塞或极短时间阻塞获取最新数据
        if (oled_q != NULL && xQueueReceive(oled_q, &msg, 0) == pdPASS) {
            if (msg.Message_Type == MESSAGE_TYPE_HEART_RATE_SPO2) {
                display_hr = msg.Data.Heart_Rate_SPO2_Data.Heart_Rate;
                display_spo2 = msg.Data.Heart_Rate_SPO2_Data.SpO2;
            }
        }

        // 左侧：心率盘 (0-200)
        // 圆心(32, 32), 半径20
		OLED_ClearArea(0, 0, 64, 64); // 每次更新前清除对应区域，防止指针重叠产生的残影
        OLED_DrawMeter(32, 32, 20, display_hr, 200, "H-R", "bpm");

        // 右侧：血氧盘 (0-100)
        // 圆心(96, 32), 半径20
		OLED_ClearArea(64, 0, 64, 64); // 每次更新前清除对应区域，防止指针重叠产生的残影
        OLED_DrawMeter(96, 32, 20, display_spo2, 100, "O-2", "%");

        OLED_Update();

        // 退出逻辑
        if(OLED_ShowState != 2) {
			OLED_Clear();
			return ;
		}
		else if(!isOLEDShow) {
			return ;
		}
    }
}

// OLED显示任务
void Task_OLED_Show(void *pvParameters)
{
	// 1. 基础硬件初始化 (尽快完成)
	i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
	if (i2c_bus != NULL) {
		if (OLED_Init(i2c_bus) == ESP_OK) {
			OLED_Clear();
			OLED_Update();
		} else {
			ESP_LOGE("OLED", "OLED初始化失败");
		}
	}

	
	// 状态变量
	float voltage;
	uint8_t batteryLevel = 0;
	bool isTimeSynced = false;          // 标记是否已经完成了系统时间->RTC的同步
	static uint32_t lastBatteryUpdate = 0;

	while(1)
	{
		// UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
		// ESP_LOGI("!!!Stack!!!", "Remaining stack: %d bytes", stack_high_water_mark * 4); // Xtensa 架构下单位通常是 4 字节
		// --- A. 时间对时逻辑 (关键：不再阻塞，在循环内异步检测) ---
		if (!isTimeSynced) {
			time_t now = time(NULL);
			// 如果时间戳大于 2024年 (1704067200)，说明 NTP 已成功同步，或者配网超时使用了默认时间
			if (now > 1704067200LL) {
				if (Rtc_Set_From_Unix((uint32_t)now) == ESP_OK) {
					isTimeSynced = true; // 标记对时成功，后续不再执行此对时逻辑
					ESP_LOGW("OLED", "检测到系统时间就绪，已同步至硬件RTC");
				}
			}
		}

		if(isOLEDShow)
		{
			if(OLED_ShowState == 1)
			{
				// --- B. 屏幕内容绘制 ---
				// 1. 绘制时间
				if (Rtc_Is_Initialized()) {
					rtc_time_t current_time;
					if (Rtc_Get_Time(&current_time) == ESP_OK) {
						// 清除时间区域并绘制（居中显示）
						OLED_ClearArea(0, 20, 128, 24);
						// 格式 HH:MM:SS (12x24 字体)
						OLED_ShowNum(16, 20, current_time.hours, 2, OLED_12X24);
						OLED_ShowChar(40, 20, ':', OLED_12X24);
						OLED_ShowNum(52, 20, current_time.minutes, 2, OLED_12X24);
						OLED_ShowChar(76, 20, ':', OLED_12X24);
						OLED_ShowNum(88, 20, current_time.seconds, 2, OLED_12X24);
					}
				} else {
					OLED_ShowString(0, 20, "RTC Waiting...", OLED_8X16);
				}

				// 2. 绘制电池电量
				uint32_t currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
				if (lastBatteryUpdate == 0 || (currentTime - lastBatteryUpdate >= 7000)) {
					Battery_Read_Voltage(&voltage);
					batteryLevel = Battery_Calculate_Percentage(voltage);
					lastBatteryUpdate = currentTime;
				}
				if (batteryLevel >= 80)
					OLED_ShowImage(99, 0, battery_pattern_5Img.width, battery_pattern_5Img.height, battery_pattern_5Img.data);
				else if (batteryLevel >= 60)
					OLED_ShowImage(99, 0, battery_pattern_4Img.width, battery_pattern_4Img.height, battery_pattern_4Img.data);
				else if (batteryLevel >= 40)
					OLED_ShowImage(99, 0, battery_pattern_3Img.width, battery_pattern_3Img.height, battery_pattern_3Img.data);
				else if (batteryLevel >= 20)
					OLED_ShowImage(99, 0, battery_pattern_2Img.width, battery_pattern_2Img.height, battery_pattern_2Img.data);
				else
					OLED_ShowImage(99, 0, battery_pattern_1Img.width, battery_pattern_1Img.height, battery_pattern_1Img.data);
			
				// 3. 绘制状态指示 (可选: 提示正在配网)
				if (isTimeSynced)
					OLED_ShowImage(0,0,wifiImg.width, wifiImg.height, wifiImg.data);
			}
			else if(OLED_ShowState == 2)
			{
				OLED_Show_HR_OxygenPage();
			}
		}
		else
		{
			OLED_Clear();
		}

		// --- C. 更新显示并休眠 ---

		OLED_Update();
		vTaskDelay(pdMS_TO_TICKS(200)); // 适当降低频率减少闪烁，200ms对秒表显示足够
	}
}
// void Task_OLED_Show(void *pvParameters)
// {
//     i2c_master_bus_handle_t i2c_bus = I2c_Get_Global_Bus_Handle();
//     OLED_Init(i2c_bus);

//     bool display_on = true;
//     oled_page_t current_page = PAGE_MAIN;
//     ui_command_t received_cmd;
    
//     // 状态变量
//     float voltage;
//     uint8_t batteryLevel = 0; // 缓存电量数值
//     bool isTimeSynced = false;
//     static uint32_t lastBatteryUpdate = 0;
    
//     QueueHandle_t cmd_queue = (QueueHandle_t)pvParameters;

//     while (1) {
//         // --- 1. 处理指令 ---
//         if (xQueueReceive(cmd_queue, &received_cmd, 0) == pdPASS) {
//             switch (received_cmd) {
//                 case UI_CMD_TOGGLE_POWER: display_on = !display_on; break;
//                 case UI_CMD_SWITCH_PAGE:  current_page = (current_page == PAGE_MAIN) ? PAGE_SENSOR : PAGE_MAIN; break;
//                 default: break;
//             }
//         }

//         if (!display_on) {
//             OLED_Clear();
//             OLED_Update();
//             vTaskDelay(pdMS_TO_TICKS(200));
//             continue;
//         }

//         // --- 2. 只有时间到了才去真正的读ADC (省电/减小干扰) ---
//         uint32_t currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
//         if (lastBatteryUpdate == 0 || (currentTime - lastBatteryUpdate >= 7000)) {
//             Battery_Read_Voltage(&voltage);
//             batteryLevel = Battery_Calculate_Percentage(voltage);
//             lastBatteryUpdate = currentTime;
//         }

//         // --- 3. 开始渲染 (每一帧都要画，因为开头有Clear) ---
//         OLED_Clear(); 

//         if (current_page == PAGE_SENSOR) {
//             // 传感器页面绘制...
//             OLED_DrawMeter(32, 32, 20, 75, 200, "H-R", "bpm");
//             OLED_DrawMeter(96, 32, 20, 98, 100, "O-2", "%");
//         } 
//         else {
//             // 主页面绘制
//             // A. 时间逻辑...
//             if (!isTimeSynced) {
//                 time_t now = time(NULL);
//                 if (now > 1704067200LL) {
//                     if (Rtc_Set_From_Unix((uint32_t)now) == ESP_OK) isTimeSynced = true;
//                 }
//             }

//             // B. 画时间
//             if (Rtc_Is_Initialized()) {
//                 rtc_time_t current_time;
//                 if (Rtc_Get_Time(&current_time) == ESP_OK) {
//                     OLED_ShowNum(16, 20, current_time.hours, 2, OLED_12X24);
//                     OLED_ShowChar(40, 20, ':', OLED_12X24);
//                     OLED_ShowNum(52, 20, current_time.minutes, 2, OLED_12X24);
//                     OLED_ShowChar(76, 20, ':', OLED_12X24);
//                     OLED_ShowNum(88, 20, current_time.seconds, 2, OLED_12X24);
//                 }
//             }

//             // C. 每一帧都把缓存的 batteryLevel 画出来
// 			if(batteryLevel <= 100 && batteryLevel >= 80)
// 				OLED_ShowImage(99,0,battery_pattern_5Img.width, battery_pattern_5Img.height, battery_pattern_5Img.data);
//             else if(batteryLevel < 80 && batteryLevel >= 60)
// 				OLED_ShowImage(99,0,battery_pattern_4Img.width, battery_pattern_4Img.height, battery_pattern_4Img.data);
//             else if(batteryLevel < 60 && batteryLevel >= 40)
// 				OLED_ShowImage(99,0,battery_pattern_3Img.width, battery_pattern_3Img.height, battery_pattern_3Img.data);
// 			else if(batteryLevel < 40 && batteryLevel >= 20)
// 				OLED_ShowImage(99,0,battery_pattern_2Img.width, battery_pattern_2Img.height, battery_pattern_2Img.data);
// 			else if(batteryLevel < 20 && batteryLevel >= 0)
// 				OLED_ShowImage(99,0,battery_pattern_1Img.width, battery_pattern_1Img.height, battery_pattern_1Img.data);

//             // D. 画状态
// 			if(isTimeSynced)	
// 				OLED_ShowImage(0, 0, wifiImg.width, wifiImg.height, wifiImg.data);
// 			else
// 				OLED_ClearArea(0, 0, wifiImg.width, wifiImg.height);
//         }
        
//         OLED_Update();
//         vTaskDelay(pdMS_TO_TICKS(200)); 
//     }
// }