# 1.py - 字模数据专用修复脚本
import re

INPUT_FILE = "OLED_Data.c"
OUTPUT_FILE = "OLED_Data_fixed.c"

def fix_file():
    with open(INPUT_FILE, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # # 修复 OLED_F8x16 (每字符 16 字节)
    # content = fix_array(content, 'OLED_F8x16', 16)
    
    # # 修复 OLED_F6x8 (每字符 6 字节)
    # content = fix_array(content, 'OLED_F6x8', 6)
    
    # 修复 OLED_F12x24 (每字符 36 字节)
    content = fix_array(content, 'OLED_F12x24', 36)
    
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        f.write(content)
    
    print(f"✅ 修复完成！输出：{OUTPUT_FILE}")

def fix_array(content, array_name, cols_per_char):
    """修复指定数组，给每个字符的数据添加 {}"""
    
    # 匹配数组定义到结束
    pattern = rf'(const\s+uint8_t\s+{array_name}\s*\[\]\[{cols_per_char}\]\s*=\s*\{{)(.*?)(\}};)'
    
    def replace_func(match):
        prefix = match.group(1)
        data_block = match.group(2)
        suffix = match.group(3)
        
        # 提取所有 0x 开头的数值
        nums = re.findall(r'0x[0-9A-Fa-f]+', data_block)
        
        if len(nums) % cols_per_char != 0:
            print(f"⚠️ 警告：{array_name} 数据量 ({len(nums)}) 不是 {cols_per_char} 的倍数")
            return match.group(0)  # 原样返回
        
        # 按每字符字节数分组
        char_count = len(nums) // cols_per_char
        new_blocks = []
        
        # 同时提取注释（按行）
        lines = data_block.split('\n')
        comments = []
        for line in lines:
            if '//' in line:
                comments.append(line.split('//')[1].strip())
            else:
                comments.append('')
        
        # 重建格式
        for i in range(char_count):
            start = i * cols_per_char
            end = start + cols_per_char
            group = nums[start:end]
            
            # 尝试获取对应注释
            comment = ''
            if i < len(comments) and comments[i]:
                comment = ' //' + comments[i]
            
            new_blocks.append('    {' + ','.join(group) + '}' + comment)
        
        return prefix + '\n' + '\n'.join(new_blocks) + '\n' + suffix
    
    return re.sub(pattern, replace_func, content, flags=re.DOTALL)

if __name__ == '__main__':
    print(f"📂 当前目录：{__import__('os').getcwd()}")
    print(f"📄 处理文件：{INPUT_FILE}")
    fix_file()