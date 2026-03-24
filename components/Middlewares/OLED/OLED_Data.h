#ifndef __OLED_DATA_H
#define __OLED_DATA_H

#include <stdint.h>

/*字符集定义*/
/*以下两个宏定义只可解除其中一个的注释*/
#define OLED_CHARSET_UTF8			//定义字符集为UTF8
//#define OLED_CHARSET_GB2312		//定义字符集为GB2312

/*字模基本单元*/
typedef struct 
{
	
#ifdef OLED_CHARSET_UTF8			//定义字符集为UTF8
	char Index[5];					//汉字索引，空间为5字节
#endif
	
#ifdef OLED_CHARSET_GB2312			//定义字符集为GB2312
	char Index[3];					//汉字索引，空间为3字节
#endif
	
	uint8_t Data[32];				//字模数据
} ChineseCell_t;

/*ASCII字模数据声明*/
extern const uint8_t OLED_F8x16[][16];
extern const uint8_t OLED_F6x8[][6];
extern const uint8_t OLED_F12x24[][36];

/*汉字字模数据声明*/
extern const ChineseCell_t OLED_CF16x16[];


/*图像数据声明*/
extern const uint8_t Diode[];
extern const uint8_t Simle[];
extern const uint8_t Enter_Return[];
extern const uint8_t Slip_Return[];
extern const uint8_t Slip_Clock[];
extern const uint8_t Slip_Mood[];
extern const uint8_t Unhappy[];
extern const uint8_t Anxiety[];
extern const uint8_t Empty[];
extern const uint8_t Frame[];
extern const uint8_t Slip_Game[];
extern const uint8_t Level[];
extern const uint8_t Big_Anxiety[];
extern const uint8_t Big_Unhappy[];
extern const uint8_t Big_Simle[];
extern const uint8_t Ground[];
extern const uint8_t Barrier[][48];
extern const uint8_t Big_Barrier[][64];
extern const uint8_t Cloud[];
extern const uint8_t Dino[][48];
extern const uint8_t Light[];

/*按照上面的格式，在这个位置加入新的图像数据声明*/
//...

#endif

