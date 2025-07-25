/*
 * pcm_watermark.c - Real-time PCM Audio Watermarking Module
 *
 * Copyright (C) 2025, Huaimin <huai_min@foxmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 */

#include <linux/module.h> // 需要这个头文件来使用 MODULE_LICENSE

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Huaimin <huai_min@foxmail.com>");
MODULE_DESCRIPTION("A kernel module for real-time PCM audio watermarking.");
/*
 * ===================================================================
 * ========================== 头文件包含 =============================
 * ===================================================================
 */
#include <linux/kernel.h> // For pr_info, KERN_INFO
#include <linux/string.h> // For strlen, memset
#include <linux/slab.h> // For kmalloc, kfree (if needed, but not in these core functions)
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/netdevice.h> // 需要这个头文件来使用 struct net_device, for_each_netdev, ETH_ALEN, etc.
#include <linux/rcupdate.h> // RCU for netdevice iteration
#include <linux/etherdevice.h> // 定义了 is_zero_ether_addr
#include <linux/list.h> // For list_head
#include <linux/ktime.h>
#include <linux/crc32.h>

#include "pcm_watermark.h"

/*
 * ===================================================================
 * ========================== 常量定义 ===============================
 * ===================================================================
 */
#define BASE_TIME_STAMP 1735689600UL // 定义基准时间戳 2025-01-01 00:00:00 UTC
#define MAX_MAC_ADDRESSES 4 // 最多存储4个MAC地址
#define WATERMARK_EMBED_PERIOD_SAMPLES 408 // 更新为408位
#define MAX_WATERMARK_BITS_FOR_BLOCK 408 // 更新为408位

/*
 * ===================================================================
 * ========================== 数据结构定义 ===========================
 * ===================================================================
 */
// 定义一个结构体来存储每个MAC地址
typedef struct mac_entry {
	__u8 addr[ETH_ALEN];
} mac_entry;

typedef struct watermark {
	__u32 sync_header; // Bytes 0-3: 同步头 (32 bits) - 0xDEADBEEF
	__u8 magic; // Byte 4: Magic Number (8 bits), e.g. 0xAC

	union {
		struct {
			__u32 timestamp_offset : 30; // Bytes 5-8: Timestamp offset from base (30 bits)
			__u32 reserved : 2; // Reserved bits (2 bits)
		};
		__u32 timestamp_raw; // 原始32位字段，可用于直接序列化
	};

	__u8 mac[6]; // Bytes 9-14: MAC address (48 bits)

	__u8 watermark_content[32]; // Bytes 15-46: 水印内容 (256 bits)

	__u32 crc32; // Bytes 47-50: CRC32 of previous fields (excluding sync_header)
} __attribute__((packed)) watermark;

/*
 * ===================================================================
 * ========================== 全局变量 ===============================
 * ===================================================================
 */
// MAC地址相关
static mac_entry saved_mac_addresses[MAX_MAC_ADDRESSES];
static int num_saved_mac_addresses = 0; // 实际保存的MAC地址数量

// 水印相关
_Bool is_initialized = false; // 标记水印模块是否已初始化
static __s16 watermark_block_bits_g
	[MAX_WATERMARK_BITS_FOR_BLOCK]; // 存储水印比特的缓冲区
static int watermark_bit_offset_g = 0; // 记录 watermark_block_bits_g 偏移位置
__s16 watermark_delta_g = 8; // QIM 量化步长

static watermark current_watermark = {
	.sync_header = WATERMARK_SYNC_PATTERN, // 设置同步头
	.magic = 0xAC, // 设置魔数
	.timestamp_raw = 0, // 初始时间戳为 0（你可以动态设置）
	.mac = { 0 }, // MAC 地址初始化为全 0（稍后通过函数设置）
	.watermark_content = { 0 }, // 水印内容初始化为空
	.crc32 = 0, // CRC32 初始为 0，稍后计算
};

/*
 * ===================================================================
 * ========================== 互斥锁定义 =============================
 * ===================================================================
 */
static DEFINE_MUTEX(mac_list_mutex); // 用于保护MAC地址列表的互斥锁
static DEFINE_MUTEX(
	watermark_offset_mutex); // 保护 watermark_bit_offset_g 的互斥锁
static DEFINE_MUTEX(watermark_buffer_init_mutex);

/*
 * ===================================================================
 * ========================== 函数声明 ===============================
 * ===================================================================
 */
static void get_and_save_mac_address(void);
void update_watermark(const char *watermark_str);
int convert_watermark_to_bits(void);

/*
 * ===================================================================
 * ========================== 水印日志功能 ===========================
 * ===================================================================
 */
/**
 * @brief 将当前水印的关键元数据写入内核日志，用于事后取证
 *
 * 这形成了一条独立于音频数据的、可供交叉验证的证据链。
 * 日志格式: [WATERMARK_LOG] timestamp=<ts>, hash=<crc32_hex>
 */
static void log_watermark_metadata(void)
{
	// 我们复用 CRC32 作为这个水印块的哈希摘要。
	// 这个哈希是在 update_watermark() 中计算的，覆盖了除crc32字段外的所有内容。
	__u32 hash_digest = current_watermark.crc32;

	// 使用 pr_warn 级别使其在默认的内核日志级别中更容易被看到和收集
	// KERN_WARNING 的日志级别通常会被系统日志服务(rsyslog, journald)捕获。
	pr_warn("[WATERMARK_LOG] timestamp=%u, hash=0x%08x\n",
		current_watermark.timestamp_raw, hash_digest);
}

/*
 * ===================================================================
 * ========================== MAC地址处理 ===========================
 * ===================================================================
 */
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
	for_each_netdev(&init_net, ndev) {
		// 跳过回环接口
		if (ndev->flags & IFF_LOOPBACK) {
			pr_debug(
				"ALSA watermark: Skipping loopback interface '%s'\n",
				ndev->name);
			continue;
		}

		// 检查接口是否有有效的MAC地址长度
		if (ndev->addr_len != ETH_ALEN) {
			pr_debug(
				"ALSA watermark: Interface '%s' has invalid addr_len %d, skipping\n",
				ndev->name, ndev->addr_len);
			continue;
		}

		// 保存MAC地址
		memcpy(saved_mac_addresses[num_saved_mac_addresses].addr,
		       ndev->dev_addr, ETH_ALEN);

		pr_info("ALSA watermark: Collected MAC [%d]: %pM from interface '%s' (type: %s, state: %s)\n",
			num_saved_mac_addresses + 1,
			saved_mac_addresses[num_saved_mac_addresses].addr,
			ndev->name,
			ndev->netdev_ops && ndev->netdev_ops->ndo_get_stats64 ?
				"ethernet" :
				"other",
			(ndev->flags & IFF_UP) ? "UP" : "DOWN");

		num_saved_mac_addresses++;
	}

	rcu_read_unlock();

	if (num_saved_mac_addresses == 0) {
		pr_warn("ALSA watermark: No valid MAC address found on any network interface. Watermarking might be less unique.\n");
	} else {
		pr_info("ALSA watermark: Successfully collected %d MAC addresses.\n",
			num_saved_mac_addresses);
	}

	mutex_unlock(&mac_list_mutex);
}

/*
 * ===================================================================
 * ========================== 水印更新功能 ===========================
 * ===================================================================
 */
void update_watermark(const char *watermark_str)
{
	current_watermark.sync_header = WATERMARK_SYNC_PATTERN; // 设置同步头
	current_watermark.magic = 0xAC;

	current_watermark.timestamp_raw =
		(ktime_get_real_seconds() - BASE_TIME_STAMP);

	// 复制 MAC 地址
	if (num_saved_mac_addresses > 0) {
		int random_index = current_watermark.timestamp_offset %
				   num_saved_mac_addresses;
		memcpy(current_watermark.mac,
		       saved_mac_addresses[random_index].addr, ETH_ALEN);
	} else {
		memset(current_watermark.mac, 0, ETH_ALEN);
	}

	// 设置水印内容（最多32字节）
	memset(current_watermark.watermark_content, 0,
	       sizeof(current_watermark.watermark_content));
	if (watermark_str) {
		strncpy((char *)current_watermark.watermark_content,
			watermark_str,
			sizeof(current_watermark.watermark_content) - 1);
	} else {
		strncpy((char *)current_watermark.watermark_content,
			"this is a watermark",
			sizeof(current_watermark.watermark_content) - 1);
	}

	// 计算 CRC32（不包括 sync_header 和 crc32 字段本身）
	// 从 magic 字段开始计算
	current_watermark.crc32 =
		crc32(0, (unsigned char *)&current_watermark.magic,
		      sizeof(current_watermark) -
			      sizeof(current_watermark.sync_header) -
			      sizeof(current_watermark.crc32));
}

/*
 * ===================================================================
 * ========================== 比特转换功能 ===========================
 * ===================================================================
 */
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
				pr_err("ALSA watermark: Buffer overflow detected in __append_bytes_to_bits! Max bits %d reached.\n",
				       max_bits);
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
int convert_watermark_to_bits()
{
	int bit_idx = 0; // 当前写入的比特位索引

	// 检查目标缓冲区大小是否足够
	if (MAX_WATERMARK_BITS_FOR_BLOCK < 408) {
		pr_err("ALSA watermark: MAX_WATERMARK_BITS_FOR_BLOCK (%d) is smaller than required (408). Aborting conversion.\n",
		       MAX_WATERMARK_BITS_FOR_BLOCK);
		return -ENOMEM;
	}

	// 将结构体视为一个连续的字节流进行转换
	// 因为结构体使用了 __attribute__((packed))，所以字段是紧密排列的。
	// 我们可以直接按顺序转换每个部分。

	// 1. 同步头 (4 bytes = 32 bits)
	__append_bytes_to_bits((const __u8 *)&current_watermark.sync_header,
			       sizeof(current_watermark.sync_header),
			       watermark_block_bits_g, &bit_idx,
			       MAX_WATERMARK_BITS_FOR_BLOCK);

	// 2. Magic number (1 byte = 8 bits)
	__append_bytes_to_bits(&current_watermark.magic,
			       sizeof(current_watermark.magic),
			       watermark_block_bits_g, &bit_idx,
			       MAX_WATERMARK_BITS_FOR_BLOCK);

	// 3. timestamp_raw (包含 timestamp_offset 和 reserved) (4 bytes = 32 bits)
	//    由于是 packed struct，位域会按照定义顺序排列。直接转换整个 u32 即可。
	__append_bytes_to_bits((const __u8 *)&current_watermark.timestamp_raw,
			       sizeof(current_watermark.timestamp_raw),
			       watermark_block_bits_g, &bit_idx,
			       MAX_WATERMARK_BITS_FOR_BLOCK);

	// 4. MAC address (6 bytes = 48 bits)
	__append_bytes_to_bits(current_watermark.mac,
			       sizeof(current_watermark.mac),
			       watermark_block_bits_g, &bit_idx,
			       MAX_WATERMARK_BITS_FOR_BLOCK);

	// 5. Watermark content (32 bytes = 256 bits)
	__append_bytes_to_bits(current_watermark.watermark_content,
			       sizeof(current_watermark.watermark_content),
			       watermark_block_bits_g, &bit_idx,
			       MAX_WATERMARK_BITS_FOR_BLOCK);

	// 6. CRC32 (4 bytes = 32 bits)
	__append_bytes_to_bits((const __u8 *)&current_watermark.crc32,
			       sizeof(current_watermark.crc32),
			       watermark_block_bits_g, &bit_idx,
			       MAX_WATERMARK_BITS_FOR_BLOCK);

	if (bit_idx != 408) {
		pr_warn("ALSA watermark: Conversion resulted in %d bits, expected 408.\n",
			bit_idx);
	}

	return bit_idx;
}

/*
 * ===================================================================
 * ========================== 模块初始化和清理 =======================
 * ===================================================================
 */
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
		convert_watermark_to_bits();
		log_watermark_metadata();
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

/*
 * ===================================================================
 * ========================== 水印嵌入核心功能 =======================
 * ===================================================================
 */
/*
 * QIM 嵌入函数
 *
 * 遵循 GNU89 规范，避免浮点数。
 * 假设 short 为 16 位有符号整数。
 */
void snd_pcm_watermark_embed(__s16 *samples, snd_pcm_uframes_t length,
			     const char *watermark_str, __s16 delta)
{
	// printk("ALSA watermark: length %lu samples.\n", length);
	int i;
	__s16 s, m, base, offset;

	// 检查模块是否已初始化和delta是否有效
	if (unlikely(!is_initialized || delta == 0)) {
		return;
	}

	// 用一个锁保护对全局偏移量的所有操作
	mutex_lock(&watermark_offset_mutex);

	for (i = 0; i < length; i++) {
		// 在每次循环迭代前检查是否需要换块
		if (watermark_bit_offset_g >= MAX_WATERMARK_BITS_FOR_BLOCK) {
			update_watermark(watermark_str);
			convert_watermark_to_bits();
			// log_watermark_metadata();
			watermark_bit_offset_g = 0; // 重置偏移量
		}

		// 从全局比特流数组中获取当前要嵌入的比特
		m = watermark_block_bits_g[watermark_bit_offset_g];
		s = samples[i];

		// --- QIM 嵌入算法 ---
		if (s >= 0) {
			base = (s / delta) * delta;
		} else {
			base = ((s - delta + 1) / delta) * delta;
			if (s % delta != 0) {
				base = (s / delta) * delta;
				if (base > s) {
					base -= delta;
				}
			}
		}
		offset = ((__s16)2 * m + 1) * delta / 4;
		samples[i] = base + offset;
		// --- 算法结束 ---

		// 偏移量增加
		watermark_bit_offset_g++;
	}
	log_watermark_metadata();
	mutex_unlock(&watermark_offset_mutex);
}