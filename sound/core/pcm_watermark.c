#include <linux/kernel.h> // For pr_info, KERN_INFO
#include <linux/string.h> // For strlen, memset
#include <linux/slab.h>   // For kmalloc, kfree (if needed, but not in these core functions)
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/netdevice.h> // 需要这个头文件来使用 struct net_device, for_each_netdev, ETH_ALEN, etc.
#include <linux/rcupdate.h>  // RCU for netdevice iteration
#include <linux/etherdevice.h> // 定义了 is_zero_ether_addr
#include <linux/list.h> // For list_head
#include <linux/ktime.h> 
#include <linux/crc32.h>

#include "pcm_watermark.h"


#define BASE_TIME_STAMP 1735689600UL // 定义基准时间戳 2025-01-01 00:00:00 UTC
#define MAX_MAC_ADDRESSES 4 // 最多存储4个MAC地址

// 定义一个结构体来存储每个MAC地址
typedef struct mac_entry {
    __u8 addr[ETH_ALEN];
}mac_entry;
// 全局静态数组，保存MAC地址
static mac_entry saved_mac_addresses[MAX_MAC_ADDRESSES];
static int num_saved_mac_addresses = 0; // 实际保存的MAC地址数量

// 用于轮询的当前MAC地址索引
static atomic_t current_mac_index = ATOMIC_INIT(0); // 原子变量，保证并发安全

// 用于保护MAC地址列表的互斥锁
static DEFINE_MUTEX(mac_list_mutex);

/*
 * ===================================================================
 * ======================= 水印相关全局变量和函数 ========================
 * ===================================================================
 */

// 定义水印内容和强度 (delta)
// 		在实际项目中，这些应作为模块参数或通过其他机制配置，
// 		而不是硬编码在这里，以便更灵活地更改水印。

#define WATERMARK_EMBED_PERIOD_SAMPLES 376
#define MAX_WATERMARK_BITS_FOR_BLOCK 376

// 标记水印模块是否已初始化
_Bool is_initialized = false; 
static DEFINE_MUTEX(watermark_buffer_init_mutex);

// 存储水印比特的缓冲区
static __s16 watermark_block_bits_g[MAX_WATERMARK_BITS_FOR_BLOCK];

// 累积已处理的样本帧数量的计数器
// 判断是否要更新 watermark
static unsigned long pcm_samples_counter = 0;

// 保护 pcm_samples_counter 变量的互斥锁
static DEFINE_MUTEX(watermark_counter_mutex);

// 记录 watermark_block_bits_g 偏移位置
static int watermark_bit_offset_g = 0;

// 用于保护 watermark_bit_offset_g 的互斥锁
static DEFINE_MUTEX(watermark_offset_mutex);

// QIM 量化步长
__s16 watermark_delta_g = 8;                                 

typedef struct watermark {
    __u8  magic;           // Byte 0: Magic Number (8 bits), e.g. 0xAC

    union {
        struct {
            __u32 timestamp_offset : 30; // Bytes 1-4: Timestamp offset from base (30 bits)
            __u32 reserved         : 2;  // Reserved bits (2 bits)
        };
        __u32 timestamp_raw;             // 原始32位字段，可用于直接序列化
    };

    __u8  mac[6];           // Bytes 5-10: MAC address (48 bits)

    __u8 watermark_content[32];

    __u32 crc32;            // Bytes 11-14: CRC32 of previous fields
} __attribute__((packed))watermark;

static watermark current_watermark = {
    .magic = 0xAC,                             // 设置魔数
    .timestamp_raw = 0,                        // 初始时间戳为 0（你可以动态设置）
    .mac = {0},                                // MAC 地址初始化为全 0（稍后通过函数设置）
    .watermark_content = {0},                  // 水印内容初始化为空
    .crc32 = 0,                                // CRC32 初始为 0，稍后计算
};


static void get_and_save_mac_address(void);
void update_watermark(const char *watermark_str);
/**
 * @brief 初始化水印比特缓冲区。
 * 这个函数应该在 ALSA 驱动模块加载时（例如 module_init 中）被调用一次。
 *
 * @param max_buffer_len_bits 期望的最大水印比特流长度。
 * 通常可以设置为一个足够大的值，例如 16KB 采样对应 8KB 比特。
 */
void pcm_watermark_module_init_buffer(void)
{
    printk("ALSA watermark: Initializing watermark buffer.\n");
    // 在水印缓冲区初始化之前，尝试获取MAC地址
    get_and_save_mac_address();
    mutex_lock(&watermark_buffer_init_mutex);
    if (!is_initialized) { // 避免重复初始化
        printk("ALSA watermark: Watermark buffer not initialized, proceeding with initialization.\n");
        update_watermark(NULL);

        // 打印结构体详细内容
        pr_info("=== Current Watermark Info ===\n");
        pr_info("Magic: 0x%02X\n", current_watermark.magic);

        // 打印 timestamp_offset 和 reserved 位域（先解包）
        pr_info("Timestamp Raw: %u\n", current_watermark.timestamp_raw);
        pr_info("Timestamp Offset: %u\n", current_watermark.timestamp_offset);
        pr_info("Reserved: %u\n", current_watermark.reserved);

        // 打印 MAC 地址
        pr_info("MAC Address: %pM\n", current_watermark.mac);

        // 打印 Watermark Content 作为字符串和 hex
        pr_info("Watermark Content (string): \"%.32s\"\n", current_watermark.watermark_content);
        pr_info("CRC32: 0x%08X\n", current_watermark.crc32);
        pr_info("==============================\n");
        is_initialized = true;
    }
    mutex_unlock(&watermark_buffer_init_mutex);
}

/**
 * @brief 清理水印比特缓冲区。
 * 这个函数应该在 ALSA 驱动模块卸载时（例如 module_exit 中）被调用一次。
 */
void pcm_watermark_module_exit_buffer(void)
{
    mutex_lock(&watermark_buffer_init_mutex);
    if (is_initialized) {
        pr_info("ALSA watermark: Cleaning up watermark buffer.\n");
    }
    mutex_unlock(&watermark_buffer_init_mutex);
}

void update_watermark(const char *watermark_str)
{
    current_watermark.magic = 0xAC;

    current_watermark.timestamp_raw = (ktime_get_real_seconds() - BASE_TIME_STAMP);

    // 复制 MAC 地址
    if (num_saved_mac_addresses > 0) {
        memcpy(current_watermark.mac, saved_mac_addresses[0].addr, ETH_ALEN);
    } else {
        memset(current_watermark.mac, 0, ETH_ALEN);
    }

    // 设置水印内容（最多32字节）
    memset(current_watermark.watermark_content, 0, sizeof(current_watermark.watermark_content));
    if (watermark_str) {
        strncpy((char *)current_watermark.watermark_content, watermark_str, sizeof(current_watermark.watermark_content) - 1);
    } else {
        strncpy((char *)current_watermark.watermark_content, "this is a watermark", sizeof(current_watermark.watermark_content) - 1);
    }

    // 计算 CRC32（不包括 crc32 字段本身）
    current_watermark.crc32 = crc32(0,
        (unsigned char *)&current_watermark,
        sizeof(current_watermark) - sizeof(current_watermark.crc32));
}


/**
 * @brief 尝试获取并保存系统的MAC地址。
 */
static void get_and_save_mac_address(void)
{
    struct net_device *ndev;
    // 确保对列表操作的互斥
    mutex_lock(&mac_list_mutex);

    if (num_saved_mac_addresses > 0) { // 如果已经获取过，直接返回
        mutex_unlock(&mac_list_mutex);
        return;
    }

    pr_info("ALSA watermark: Attempting to collect all MAC addresses...\n");

    rcu_read_lock(); // 读取网络设备列表需要RCU保护
    for_each_netdev(&init_net, ndev) { // 遍历所有网络设备
        if (ndev->addr_len == ETH_ALEN && !is_zero_ether_addr(ndev->dev_addr)) {
            // 找到有效的MAC地址，并添加到列表中
            if (num_saved_mac_addresses < MAX_MAC_ADDRESSES) {
                memcpy(saved_mac_addresses[num_saved_mac_addresses].addr, ndev->dev_addr, ETH_ALEN);
                pr_info("ALSA watermark: Found MAC Address %d: %pM (from interface '%s')\n",
                        num_saved_mac_addresses + 1, saved_mac_addresses[num_saved_mac_addresses].addr, ndev->name);
                num_saved_mac_addresses++;
            } else {
                pr_warn("ALSA watermark: Exceeded MAX_MAC_ADDRESSES (%d), skipping remaining interfaces.\n", MAX_MAC_ADDRESSES);
                break; 
            }
        }
    }
    rcu_read_unlock();

    if (num_saved_mac_addresses == 0) {
        pr_warn("ALSA watermark: No valid MAC address found on any network interface. Watermarking might be less unique.\n");
    } else {
        pr_info("ALSA watermark: Successfully collected %d MAC addresses.\n", num_saved_mac_addresses);
    }

    mutex_unlock(&mac_list_mutex);
}

/**
 * @brief 辅助函数：将一个字节数组转换为比特流并追加到目标数组
 * @param source 指向源字节数组的指针
 * @param num_bytes 要转换的字节数
 * @param dest_bits 目标比特流数组 (存储 __s16 类型的 0 或 1)
 * @param current_bit_index 指向当前比特流写入位置的指针，函数会更新此值
 * @param max_bits 目标数组的最大容量，用于防止溢出
 *
 * @note 此函数从每个字节的最高位(MSB)开始转换。
 */
static void __append_bytes_to_bits(const __u8 *source, int num_bytes, 
                                   __s16 *dest_bits, int *current_bit_index, 
                                   int max_bits)
{
    int i, j; // 循环变量
    
    if (!source || !dest_bits || !current_bit_index) {
        return;
    }

    for (i = 0; i < num_bytes; i++) {
        __u8 current_byte = source[i];
        // 从最高有效位 (MSB, bit 7) 到最低有效位 (LSB, bit 0)
        for (j = 7; j >= 0; j--) {
            if (*current_bit_index >= max_bits) {
                pr_err("ALSA watermark: Buffer overflow detected in __append_bytes_to_bits! Max bits %d reached.\n", max_bits);
                return; // 防止溢出
            }
            // 提取第j位，并存为 0 或 1
            dest_bits[*current_bit_index] = (current_byte >> j) & 1;
            (*current_bit_index)++;
        }
    }
}

/**
 * @brief 将 watermark 结构体转换为固定长度的比特流
 * @param wm 指向要转换的 watermark 结构体的指针
 * @return 成功转换的比特数，如果失败则为负数
 *
 * 此函数将给定的 watermark 结构体严格按照字段顺序序列化为比特流，
 * 并存储在全局数组 watermark_block_bits_g 中。
 */
int convert_watermark_to_bits(const watermark *wm)
{
    int bit_idx = 0; // 当前写入的比特位索引

    if (!wm) {
        pr_err("ALSA watermark: Input watermark struct is NULL.\n");
        return -EINVAL;
    }

    // 检查目标缓冲区大小是否足够
    if (MAX_WATERMARK_BITS_FOR_BLOCK < 376) {
        pr_err("ALSA watermark: MAX_WATERMARK_BITS_FOR_BLOCK (%d) is smaller than required (376). Aborting conversion.\n",
               MAX_WATERMARK_BITS_FOR_BLOCK);
        return -ENOMEM;
    }

    // 将结构体视为一个连续的字节流进行转换
    // 因为结构体使用了 __attribute__((packed))，所以字段是紧密排列的。
    // 我们可以直接按顺序转换每个部分。

    // 1. Magic number (1 byte = 8 bits)
    __append_bytes_to_bits(&wm->magic, sizeof(wm->magic), watermark_block_bits_g, &bit_idx, MAX_WATERMARK_BITS_FOR_BLOCK);
    // 2. timestamp_raw (包含 timestamp_offset 和 reserved) (4 bytes = 32 bits)
    //    由于是 packed struct，位域会按照定义顺序排列。直接转换整个 u32 即可。
    __append_bytes_to_bits((const __u8 *)&wm->timestamp_raw, sizeof(wm->timestamp_raw), watermark_block_bits_g, &bit_idx, MAX_WATERMARK_BITS_FOR_BLOCK);

    // 3. MAC address (6 bytes = 48 bits)
    __append_bytes_to_bits(wm->mac, sizeof(wm->mac), watermark_block_bits_g, &bit_idx, MAX_WATERMARK_BITS_FOR_BLOCK);
    pr_info("ALSA watermark: MAC: %s",wm->mac);
    // 4. Watermark content (32 bytes = 256 bits)
    __append_bytes_to_bits(wm->watermark_content, sizeof(wm->watermark_content), watermark_block_bits_g, &bit_idx, MAX_WATERMARK_BITS_FOR_BLOCK);

    // 5. CRC32 (4 bytes = 32 bits)
    __append_bytes_to_bits((const __u8 *)&wm->crc32, sizeof(wm->crc32), watermark_block_bits_g, &bit_idx, MAX_WATERMARK_BITS_FOR_BLOCK);

    if (bit_idx != 376) {
         pr_warn("ALSA watermark: Conversion resulted in %d bits, expected 376.\n", bit_idx);
    } else {
         pr_info("ALSA watermark: Successfully converted watermark struct to %d bits.\n", bit_idx);
    }
    
    return bit_idx;
}

/*
 * QIM 嵌入函数
 *
 * 遵循 GNU89 规范，避免浮点数。
 * 假设 short 为 16 位有符号整数。
 */
void snd_pcm_watermark_embed(__s16 *samples, snd_pcm_uframes_t length, __s16 delta)
{
    int should_generate_new_block = 0;

    // 针对 delta 为 0 的情况进行保护，避免除零错误
    if (delta == 0) {
        pr_info(KERN_WARNING "ALSA watermark: delta is zero, embedding skipped.\n");
        return;
    }

    // // --- 样本数量周期检查逻辑 ---
    // mutex_lock(&watermark_counter_mutex); // 加锁保护计数器

    // // 将当前处理的样本帧数累加到计数器
    // // 300 + 90 > 376
    // pcm_samples_counter += length;

    // // 检查累积的样本数是否达到阈值
    // if (pcm_samples_counter >= WATERMARK_EMBED_PERIOD_SAMPLES) {
    //     should_generate_new_block = 1;
    //     // 从计数器中减去一个周期的数量，保留余数用于下一次计数
    //     pcm_samples_counter %= WATERMARK_EMBED_PERIOD_SAMPLES;
    // }

    // mutex_unlock(&watermark_counter_mutex); // 状态已更新，可以立即解锁，减少锁的持有时间
    // // --- 周期检查结束 ---
    
    // // 如果一个完整的嵌入周期过去了，就生成一个新的水印比特流
    // if (should_embed_new_block) {
    //     pr_info("ALSA watermark: New watermark block period reached. Generating fresh bitstream.\n");
        
    //     // 1. 更新水印结构体的内容 (时间戳、CRC等)
    //     update_watermark(NULL); 
        
    //     // 2. 将更新后的结构体转换为比特流，存入全局数组
    //     convert_watermark_to_bits(&current_watermark);

    //     // 3. 重置比特流的嵌入偏移量，从新块的开头开始
    //     mutex_lock(&watermark_offset_mutex);
    //     watermark_bit_offset_g = 0;
    //     mutex_unlock(&watermark_offset_mutex);
    // }

    // // --- 执行连续的比特嵌入 ---
    // mutex_lock(&watermark_offset_mutex);
    int i; // 声明循环变量在块开始
    for (i = 0; i < length; i++) {
        __s16 s;
        __s16 m;
        __s16 base;
        __s16 offset;

        // 检查当前水印块是否已经嵌入完毕
        if (watermark_bit_offset_g >= MAX_WATERMARK_BITS_FOR_BLOCK) {
            // 当前块的所有比特都已嵌入。
            update_watermark(NULL); // 更新水印内容
            convert_watermark_to_bits(&current_watermark); // 生成新的比特流
            // 重置比特流的嵌入偏移量，从新块的开头开始
            mutex_lock(&watermark_offset_mutex);
            watermark_bit_offset_g = 0;
            mutex_unlock(&watermark_offset_mutex);
            break; 
        }

        // 从全局比特流数组中获取当前要嵌入的比特
        m = watermark_block_bits_g[watermark_bit_offset_g];
        
        s = samples[i];

        // --- QIM 嵌入算法 ---
        // if (s >= 0) {
        //     base = (s / delta) * delta;
        // } else {
        //     // 修正 C 语言中负数除法向零取整的行为
        //     if (s % delta != 0) {
        //         base = (s / delta - 1) * delta;
        //     } else {
        //         base = s;
        //     }
        // }

        // 计算 base = (s / delta) * delta
        // 修正 C 语言中负数除法向零取整的行为，使其更接近数学上的“向下取整”
        // base=⌊s/deltas​⌋×delta
        if (s >= 0) {
            base = (s / delta) * delta;
        } else {
            base = ((s - delta + 1) / delta) * delta; // 保证负数向负无穷方向取整
            if (s % delta != 0) { // 如果不是delta的整数倍，则需要进一步调整
                base = (s / delta) * delta;
                if (base > s) { // 如果s是负数，且s不是delta的倍数，s/delta会向上取整，需要减去一个delta
                    base -= delta;
                }
            }
        }

        offset = ((__s16)2 * m + 1) * delta / 4;
        samples[i] = base + offset;
        // --- 算法结束 ---

        // 将偏移量向前移动一位，准备嵌入下一个比特
        watermark_bit_offset_g++;
    }

    mutex_unlock(&watermark_offset_mutex); // 嵌入循环结束，释放锁

    // int i; // 声明循环变量在块开始
    // for (i = 0; i < length; i++) {
    //     __s16 s = samples[i];
    //     __s16 m = watermark_bits[i % watermark_bits_buffer_len_g];
    //     __s16 base;
    //     __s16 offset;

    //     // 计算 base = (s / delta) * delta
    //     // 修正 C 语言中负数除法向零取整的行为，使其更接近数学上的“向下取整”
    //     // base=⌊s/deltas​⌋×delta
    //     if (s >= 0) {
    //         base = (s / delta) * delta;
    //     } else {
    //         base = ((s - delta + 1) / delta) * delta; // 保证负数向负无穷方向取整
    //         if (s % delta != 0) { // 如果不是delta的整数倍，则需要进一步调整
    //             base = (s / delta) * delta;
    //             if (base > s) { // 如果s是负数，且s不是delta的倍数，s/delta会向上取整，需要减去一个delta
    //                 base -= delta;
    //             }
    //         }
    //     }

    //     // 计算 offset = ((2 * m + 1) * delta) / 4
    //     // 注意：2*m+1 结果只可能是 1 或 3 (m=0或1)
    //     offset = ((__s16)2 * m + 1) * delta / 4;

    //     samples[i] = base + offset;
    // }
}
