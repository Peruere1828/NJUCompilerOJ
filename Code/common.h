#ifndef COMMON_H
#define COMMON_H

// 将字符串复制到新建立的空间
char* strdup(const char* src_str);

#define HASH_TABLE_SIZE 0x3fff
unsigned int gen_hash(const char* name);

#endif