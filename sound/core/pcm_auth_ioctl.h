// file: sound/core/pcm_auth_ioctl.h

#ifndef __PCM_AUTH_IOCTL_H
#define __PCM_AUTH_IOCTL_H

#include <linux/ioctl.h>

// 1. 用户层自定义的水印内容
typedef struct {
	char content[32];

} pcm_watermark_content_t;

// 2. 定义我们的ioctl命令
//    _IOW 表示这是一个“写入”的ioctl，即用户空间向内核写入数据。
//    'A' 是ALSA驱动使用的“魔法”字符(Magic Number)。
//    0x60 是我们自定义的命令编号，选择一个ALSA中未使用的编号。
//    pcm_watermark_content_t 是命令关联的数据类型。
#define SNDRV_PCM_IOCTL_AUTHENTICATE _IOW('A', 0x60, pcm_watermark_content_t)

#endif