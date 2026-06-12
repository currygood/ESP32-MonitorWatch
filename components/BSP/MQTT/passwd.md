password = version=2018-10-31&res=products%2F`username`%2Fdevices%2F`client_id`&et=2091187496&method=md5&sign=`sign`

用 `` 弄起来的就是要替换成对应的值
其中sign = base64(hmac_<method>(base64decode(`key`), utf-8(StringForSignature))) 
sign就是key进行处理得到的，按照上述公式计算

version=2018-10-31&res=products%2F1nF1D22kt0%2Fdevices%2FMyTest&et=2091187496&method=md5&sign=VuvjYUj0KPTQ4e8zOJeyOw%3D%3D