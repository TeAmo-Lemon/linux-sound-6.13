#ifndef _SND_PCM_WATERMARK_H
#define _SND_PCM_WATERMARK_H

#include <linux/types.h> // For __s16
#include <sound/pcm.h>
#include <linux/mutex.h> // For DEFINE_MUTEX, if mutex is needed externally
#include <linux/netdevice.h> // <-- 添加这个头文件，定义 ETH_ALEN

// 声明水印相关的全局变量，以便其他文件可以访问它们
extern __s16 watermark_delta_g;


// 声明用于存储MAC地址的全局变量
extern __u8 global_mac_address[ETH_ALEN]; // ETH_ALEN 是MAC地址的长度 (6字节)
extern _Bool is_initialized;        

void pcm_watermark_module_init_buffer(void);
void pcm_watermark_module_exit_buffer(void);

/*
 * QIM (Quantization Index Modulation)
 * 用于在音频数据中嵌入水印。
 *
 * @samples: 音频样本数组 (s16 类型)。
 * @watermark_bits: 要嵌入的水印比特数组 (0 或 1)。
 * @length: 样本和水印数组的长度。
 * @delta: 量化步长，决定水印强度和隐蔽性。
 *
 * 注意:
 * - 嵌入时修改 @samples 数组。
 * - 确保 @samples 和 @watermark_bits 长度相同。
 * - 该函数不进行边界检查，调用者需确保传入有效参数。
 * - 运算只使用定点数，符合内核环境要求。
 */
void snd_pcm_watermark_embed(__s16 *samples, snd_pcm_uframes_t length, __s16 delta);


/*
 * 将字符串内容转换为比特数组作为水印数据。
 *
 * @watermark_content: 原始水印字符串。
 * @watermark_bits: 用于存储转换后的水印比特数组。
 * @max_length: watermark_bits 数组的最大容量。
 *
 * 注意:
 * - 字符串的每个字符转换为8个比特，高位在前。
 * - 如果转换后的比特总数小于 max_length，水印内容会重复填充。
 * - 返回实际转换并填充的比特数。
 */
int snd_pcm_watermark_string_to_bits(const char *watermark_content, __s16 *watermark_bits, int max_length);

#endif /* _SND_PCM_WATERMARK_H */