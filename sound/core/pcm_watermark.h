#ifndef _SND_PCM_WATERMARK_H
#define _SND_PCM_WATERMARK_H

#include <linux/types.h> // For __s16
#include <sound/pcm.h>
#include <linux/mutex.h> // For DEFINE_MUTEX, if mutex is needed externally
#include <linux/netdevice.h> // <-- 添加这个头文件，定义 ETH_ALEN

// --- Sync Header Definition ---
#define WATERMARK_SYNC_PATTERN_BITS 32   // 32位同步头
#define WATERMARK_SYNC_PATTERN 0xDEADBEEF // 同步模式：0xDEADBEEF 11101111 10111110 10101101 11011110 
// 11101111101111101010110111011110

// --- Watermark Structure Offsets and Sizes (in bits) ---
#define WATERMARK_BITS_SYNC       32     // 新增：同步头
#define WATERMARK_BITS_MAGIC      8
#define WATERMARK_BITS_TIMESTAMP  32
#define WATERMARK_BITS_MAC        48
#define WATERMARK_BITS_CONTENT    256
#define WATERMARK_BITS_CRC32      32
#define WATERMARK_BITS_TOTAL      (WATERMARK_BITS_SYNC + WATERMARK_BITS_MAGIC + WATERMARK_BITS_TIMESTAMP + WATERMARK_BITS_MAC + WATERMARK_BITS_CONTENT + WATERMARK_BITS_CRC32) // 408 bits

#define WATERMARK_OFFSET_SYNC       0
#define WATERMARK_OFFSET_MAGIC      (WATERMARK_OFFSET_SYNC + WATERMARK_BITS_SYNC)          // 32
#define WATERMARK_OFFSET_TIMESTAMP  (WATERMARK_OFFSET_MAGIC + WATERMARK_BITS_MAGIC)        // 40
#define WATERMARK_OFFSET_MAC        (WATERMARK_OFFSET_TIMESTAMP + WATERMARK_BITS_TIMESTAMP) // 72
#define WATERMARK_OFFSET_CONTENT    (WATERMARK_OFFSET_MAC + WATERMARK_BITS_MAC)            // 120
#define WATERMARK_OFFSET_CRC32      (WATERMARK_OFFSET_CONTENT + WATERMARK_BITS_CONTENT)    // 376

// 确保总长度与我们期望的一致
#if WATERMARK_BITS_TOTAL != 408
#error "Watermark total bit size calculation is incorrect!"
#endif

// 声明水印相关的全局变量，以便其他文件可以访问它们
extern __s16 watermark_delta_g;

// 声明用于存储MAC地址的全局变量
// extern __u8 global_mac_address[ETH_ALEN]; // ETH_ALEN 是MAC地址的长度 (6字节)
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
void snd_pcm_watermark_embed(__s16 *samples, snd_pcm_uframes_t length, const char *watermark_str, __s16 delta);

#endif /* _SND_PCM_WATERMARK_H */