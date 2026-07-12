#ifndef _ONENET_TOKEN_H_
#define _ONENET_TOKEN_H_
#include <stdint.h>


enum Escaped {
        WC_STD_ENC = 0,       /* normal \n line ending encoding */
        WC_ESC_NL_ENC,        /* use escape sequence encoding   */
        WC_NO_NL_ENC          /* no encoding at all             */
    }; /* Encoding types */


#ifndef byte
typedef unsigned char  byte;
#endif
typedef unsigned short word16;
typedef unsigned int   word32;
typedef byte           word24[3]; 	
	
int Base64_Decode(const byte* in, word32 inLen, byte* out,word32* outLen);
int Base64_Encode(const byte* in, word32 inLen, byte* out,word32* outLen);
int Base64_EncodeEsc(const byte* in, word32 inLen, byte* out,word32* outLen);
int Base64_Encode_NoNl(const byte* in, word32 inLen, byte* out,word32* outLen);

enum sig_method_e
{
    SIG_METHOD_MD5,
    SIG_METHOD_SHA1,
    SIG_METHOD_SHA256
};

//计算onenet  设备token
int32_t dev_token_generate(char* token, enum sig_method_e method, 
						uint32_t exp_time, const char* product_id, 
						const char* dev_name, const char* access_key);

#endif
