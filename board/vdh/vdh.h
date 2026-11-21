/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MV100_VDH_H
#define MV100_VDH_H
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>
#define VDH_AVC_SLOTS 9
#define VDH_MAX_W 1920
#define VDH_MAX_H 1088
#define VDH_MAX_NAL (1024 * 1024)
struct vdh_surface {
	struct device *dev;
	u8 *cpu;
	size_t size;
	bool coherent;
	dma_addr_t dma;
};
struct vdh_layout {
	u32 recon_size, pmv_size, pmv, sed, pmv_top, rcn, dblk, ppfd;
	u32 pmv_left, dblk_left, sao_left, sao_top, arena_size, nal_size;
};
struct vdh_hw {
	struct device *dev;
	void __iomem *regs;
	struct regmap *crg;
	u8 *mem;
	dma_addr_t dma;
	struct vdh_surface surfaces[VDH_AVC_SLOTS];
	void *extra_pmv[VDH_AVC_SLOTS];
	dma_addr_t pmv_dma[VDH_AVC_SLOTS];
	struct vdh_layout layout;
	bool wedged, hevc_ready, h264_cabac_ready;
	u8 hevc_cabac[928], h264_cabac[5120];
	unsigned long frames, errors;
};
struct vdh_reference {
	bool valid;
	u16 frame_num;
	s32 poc;
	u64 timestamp;
};
struct vdh_stream {
	struct vdh_reference refs[VDH_AVC_SLOTS];
	s32 output_poc;
	bool output_idr;
	s32 prev_poc_msb;
	u16 prev_poc_lsb;
	int last;
	u64 timestamp;
	s32 poc;
};
struct vdh_picture {
	const struct v4l2_ctrl_h264_sps *sps;
	const struct v4l2_ctrl_h264_pps *pps;
	const struct v4l2_ctrl_h264_decode_params *decode;
	const struct v4l2_ctrl_hevc_sps *hsps;
	const struct v4l2_ctrl_hevc_pps *hpps;
	const struct v4l2_ctrl_hevc_decode_params *hdec;
	const struct v4l2_ctrl_hevc_slice_params *hsl;
	const u8 *data;
	size_t bytes;
	u8 *out;
	bool stateful;
	u32 width, height;
	u64 timestamp;
};
int vdh_start_stream(struct vdh_hw *hw, u32 width, u32 height);
void vdh_stop_stream(struct vdh_hw *hw);
int vdh_hw_init(struct device *dev, struct vdh_hw *hw);
int vdh_hevc_picture(struct vdh_hw *hw, struct vdh_stream *stream, const struct vdh_picture *p);
int vdh_picture(struct vdh_hw *hw, struct vdh_stream *stream,
		const struct vdh_picture *pic);
#endif
