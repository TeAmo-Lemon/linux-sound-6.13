/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#ifndef __DMA_LOCAL_H_INCLUDED__
#define __DMA_LOCAL_H_INCLUDED__

#include <type_support.h>
#include "dma_global.h"

#include <bits.h>				/* _hrt_get_bits() */
#include <hive_isp_css_defs.h>		/* HIVE_DMA_NUM_CHANNELS */
#include <dma_v2_defs.h>

#define _DMA_FSM_GROUP_CMD_IDX						_DMA_V2_FSM_GROUP_CMD_IDX
#define _DMA_FSM_GROUP_ADDR_A_IDX					_DMA_V2_FSM_GROUP_ADDR_SRC_IDX
#define _DMA_FSM_GROUP_ADDR_B_IDX					_DMA_V2_FSM_GROUP_ADDR_DEST_IDX

#define _DMA_FSM_GROUP_CMD_CTRL_IDX					_DMA_V2_FSM_GROUP_CMD_CTRL_IDX

#define _DMA_FSM_GROUP_FSM_CTRL_IDX					_DMA_V2_FSM_GROUP_FSM_CTRL_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_STATE_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_STATE_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_DEV_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_DEV_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_ADDR_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_ADDR_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_STRIDE_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_STRIDE_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_XB_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_XB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_YB_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_YB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_REQ_DEV_IDX	_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_REQ_DEV_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_WR_DEV_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_WR_DEV_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_WR_ADDR_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_WR_ADDR_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_WR_STRIDE_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_WR_STRIDE_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_REQ_XB_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_REQ_XB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_WR_YB_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_WR_YB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_WR_XB_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_WR_XB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_ELEM_REQ_IDX	_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_ELEM_REQ_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_ELEM_WR_IDX	_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_ELEM_WR_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_S_Z_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_S_Z_IDX

#define _DMA_FSM_GROUP_FSM_PACK_IDX					_DMA_V2_FSM_GROUP_FSM_PACK_IDX
#define _DMA_FSM_GROUP_FSM_PACK_STATE_IDX			_DMA_V2_FSM_GROUP_FSM_PACK_STATE_IDX
#define _DMA_FSM_GROUP_FSM_PACK_CNT_YB_IDX			_DMA_V2_FSM_GROUP_FSM_PACK_CNT_YB_IDX
#define _DMA_FSM_GROUP_FSM_PACK_CNT_XB_REQ_IDX		_DMA_V2_FSM_GROUP_FSM_PACK_CNT_XB_REQ_IDX
#define _DMA_FSM_GROUP_FSM_PACK_CNT_XB_WR_IDX		_DMA_V2_FSM_GROUP_FSM_PACK_CNT_XB_WR_IDX

#define _DMA_FSM_GROUP_FSM_REQ_IDX					_DMA_V2_FSM_GROUP_FSM_REQ_IDX
#define _DMA_FSM_GROUP_FSM_REQ_STATE_IDX			_DMA_V2_FSM_GROUP_FSM_REQ_STATE_IDX
#define _DMA_FSM_GROUP_FSM_REQ_CNT_YB_IDX			_DMA_V2_FSM_GROUP_FSM_REQ_CNT_YB_IDX
#define _DMA_FSM_GROUP_FSM_REQ_CNT_XB_IDX			_DMA_V2_FSM_GROUP_FSM_REQ_CNT_XB_IDX

#define _DMA_FSM_GROUP_FSM_WR_IDX					_DMA_V2_FSM_GROUP_FSM_WR_IDX
#define _DMA_FSM_GROUP_FSM_WR_STATE_IDX				_DMA_V2_FSM_GROUP_FSM_WR_STATE_IDX
#define _DMA_FSM_GROUP_FSM_WR_CNT_YB_IDX			_DMA_V2_FSM_GROUP_FSM_WR_CNT_YB_IDX
#define _DMA_FSM_GROUP_FSM_WR_CNT_XB_IDX			_DMA_V2_FSM_GROUP_FSM_WR_CNT_XB_IDX

#define _DMA_DEV_INTERF_MAX_BURST_IDX			_DMA_V2_DEV_INTERF_MAX_BURST_IDX

/*
 * Macro's to compute the DMA parameter register indices
 */
#define DMA_SEL_COMP(comp)     (((comp)  & _hrt_ones(_DMA_V2_ADDR_SEL_COMP_BITS))            << _DMA_V2_ADDR_SEL_COMP_IDX)
#define DMA_SEL_CH(ch)         (((ch)    & _hrt_ones(_DMA_V2_ADDR_SEL_CH_REG_BITS))          << _DMA_V2_ADDR_SEL_CH_REG_IDX)
#define DMA_SEL_PARAM(param)   (((param) & _hrt_ones(_DMA_V2_ADDR_SEL_PARAM_BITS))           << _DMA_V2_ADDR_SEL_PARAM_IDX)
/* CG = Connection Group */
#define DMA_SEL_CG_INFO(info)  (((info)  & _hrt_ones(_DMA_V2_ADDR_SEL_GROUP_COMP_INFO_BITS)) << _DMA_V2_ADDR_SEL_GROUP_COMP_INFO_IDX)
#define DMA_SEL_CG_COMP(comp)  (((comp)  & _hrt_ones(_DMA_V2_ADDR_SEL_GROUP_COMP_BITS))      << _DMA_V2_ADDR_SEL_GROUP_COMP_IDX)
#define DMA_SEL_DEV_INFO(info) (((info)  & _hrt_ones(_DMA_V2_ADDR_SEL_DEV_INTERF_INFO_BITS)) << _DMA_V2_ADDR_SEL_DEV_INTERF_INFO_IDX)
#define DMA_SEL_DEV_ID(dev)    (((dev)   & _hrt_ones(_DMA_V2_ADDR_SEL_DEV_INTERF_IDX_BITS))  << _DMA_V2_ADDR_SEL_DEV_INTERF_IDX_IDX)

#define DMA_COMMAND_FSM_REG_IDX					(DMA_SEL_COMP(_DMA_V2_SEL_FSM_CMD) >> 2)
#define DMA_CHANNEL_PARAM_REG_IDX(ch, param)	((DMA_SEL_COMP(_DMA_V2_SEL_CH_REG) | DMA_SEL_CH(ch) | DMA_SEL_PARAM(param)) >> 2)
#define DMA_CG_INFO_REG_IDX(info_id, comp_id)	((DMA_SEL_COMP(_DMA_V2_SEL_CONN_GROUP) | DMA_SEL_CG_INFO(info_id) | DMA_SEL_CG_COMP(comp_id)) >> 2)
#define DMA_DEV_INFO_REG_IDX(info_id, dev_id)	((DMA_SEL_COMP(_DMA_V2_SEL_DEV_INTERF) | DMA_SEL_DEV_INFO(info_id) | DMA_SEL_DEV_ID(dev_id)) >> 2)
#define DMA_RST_REG_IDX							(DMA_SEL_COMP(_DMA_V2_SEL_RESET) >> 2)

#define DMA_GET_CONNECTION(val)    _hrt_get_bits(val, _DMA_V2_CONNECTION_IDX,    _DMA_V2_CONNECTION_BITS)
#define DMA_GET_EXTENSION(val)     _hrt_get_bits(val, _DMA_V2_EXTENSION_IDX,     _DMA_V2_EXTENSION_BITS)
#define DMA_GET_ELEMENTS(val)      _hrt_get_bits(val, _DMA_V2_ELEMENTS_IDX,      _DMA_V2_ELEMENTS_BITS)
#define DMA_GET_CROPPING(val)      _hrt_get_bits(val, _DMA_V2_LEFT_CROPPING_IDX, _DMA_V2_LEFT_CROPPING_BITS)

#endif /* __DMA_LOCAL_H_INCLUDED__ */
