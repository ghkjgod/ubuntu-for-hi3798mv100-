// SPDX-License-Identifier: GPL-2.0
/* Hi3798MV100 VDH V300R001 progressive H.264 and HEVC Main decoder. */
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "vdh.h"

/* Two reconstructions and two motion-vector buffers stay private to VDH. */
enum { UP = 0x1000, PIC = 0x2000, SLICE = 0x2800, CABAC = 0x3000, TILE = 0x6000, NAL = 0x7000 };
static void size_layout(struct vdh_layout *l, u32 w, u32 h)
{
	u32 stride = ALIGN(w, 64);
	u32 rows = ALIGN(h, 64);
	u32 top = PAGE_ALIGN(w * 256), left = PAGE_ALIGN(h * 256);
	l->recon_size = PAGE_ALIGN((stride + 16) * rows + stride * rows / 2 + PAGE_SIZE);
	l->pmv_size = PAGE_ALIGN((w / 16) * (h / 16) * 128 + PAGE_SIZE);
	l->nal_size = min_t(u32, VDH_MAX_NAL, PAGE_ALIGN(w * h * 2 + SZ_64K));
	l->pmv = PAGE_ALIGN(NAL + l->nal_size + 64);
	l->sed = l->pmv + 2 * l->pmv_size;
	l->pmv_top = l->sed + top;
	l->rcn = l->pmv_top + top;
	l->dblk = l->rcn + top;
	l->ppfd = l->dblk + top;
	l->pmv_left = l->ppfd + SZ_256K;
	l->dblk_left = l->pmv_left + left;
	l->sao_left = l->dblk_left + left;
	l->sao_top = l->sao_left + left;
	l->arena_size = l->sao_top + top;
}
#define CRG_BITS (BIT(0) | BIT(4) | BIT(6) | GENMASK(9, 8) | GENMASK(17, 12))
struct bits {
	const u8 *data;
	size_t size, next;
	unsigned int left;
	u8 byte;
	bool bad;
};
static u32 get_bits(struct bits *b, unsigned int count)
{
	u32 value = 0;
	while (count-- && !b->bad) {
		if (!b->left) {
			if (b->next >= b->size)
				goto bad;
			b->byte = b->data[b->next++];
			b->left = 8;
		}
		value = (value << 1) | ((b->byte >> --b->left) & 1);
	}
	return value;
bad:
	b->bad = true;
	return 0;
}
static u32 read_ue(struct bits *b)
{
	unsigned int zeros = 0;
	while (!get_bits(b, 1) && !b->bad)
		if (++zeros > 24)
			b->bad = true;
	return b->bad ? 0 : (1u << zeros) - 1 + get_bits(b, zeros);
}
static int read_se(struct bits *b)
{
	u32 v = read_ue(b);
	return v & 1 ? (int)((v + 1) / 2) : -(int)(v / 2);
}
struct slice_info {
	bool predictive, reference;
	unsigned int bits, qp, deblock, cabac_init;
	int alpha, beta;
};
static int parse_slice(const struct vdh_picture *p, const u8 *data, size_t len,
		       struct slice_info *sl)
{
	const struct v4l2_ctrl_h264_sps *s = p->sps;
	const struct v4l2_ctrl_h264_pps *q = p->pps;
	struct bits b = { .data = data, .size = len, .next = 1 };
	u32 type, frame_num;
	int qp;
	bool idr = (data[0] & 31) == 5;
	if (data[0] & 0x80 || (!idr && (data[0] & 31) != 1) ||
	    (s->profile_idc != 66 && s->profile_idc != 77 && s->profile_idc != 100) || s->chroma_format_idc != 1 ||
	    s->bit_depth_luma_minus8 || s->bit_depth_chroma_minus8 ||
	    !(s->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) ||
	    s->max_num_ref_frames > 1 || s->log2_max_frame_num_minus4 > 12 ||
	    s->log2_max_pic_order_cnt_lsb_minus4 > 12 || s->pic_order_cnt_type > 2 ||
	    q->num_slice_groups_minus1 ||
	    (q->flags & (V4L2_H264_PPS_FLAG_WEIGHTED_PRED |
			 V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT)))
		return -EOPNOTSUPP;
	if (read_ue(&b))
		return -EOPNOTSUPP;
	type = read_ue(&b);
	if (type > 9 || (type % 5 != 0 && type % 5 != 2) ||
	    (idr && type % 5 != 2) || read_ue(&b) != q->pic_parameter_set_id)
		return -EINVAL;
	sl->predictive = type % 5 == 0;
	sl->reference = (data[0] & 0x60) != 0;
	frame_num = get_bits(&b, 4 + s->log2_max_frame_num_minus4);
	if (frame_num != p->decode->frame_num)
		return -EINVAL;
	if (idr)
		read_ue(&b);
	if (s->pic_order_cnt_type == 0) {
		get_bits(&b, 4 + s->log2_max_pic_order_cnt_lsb_minus4);
		if (q->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT)
			read_se(&b);
	} else if (s->pic_order_cnt_type == 1 &&
		   !(s->flags & V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO)) {
		read_se(&b);
		if (q->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT)
			read_se(&b);
	}
	if (q->flags & V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT)
		if (read_ue(&b))
			return -EOPNOTSUPP;
	if (sl->predictive) {
		u32 refs = get_bits(&b, 1) ? read_ue(&b) : q->num_ref_idx_l0_default_active_minus1;
		if (refs || get_bits(&b, 1))
			return -EOPNOTSUPP;
	}
	if (sl->reference) {
		if (idr) {
			get_bits(&b, 1);
			if (get_bits(&b, 1))
				return -EOPNOTSUPP;
		} else if (get_bits(&b, 1)) {
			return -EOPNOTSUPP;
		}
	}
	if (sl->predictive && (q->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE)) {
		sl->cabac_init = read_ue(&b);
		if (sl->cabac_init > 2)
			return -EINVAL;
	}
	qp = 26 + q->pic_init_qp_minus26 + read_se(&b);
	if (qp < 0 || qp > 51)
		return -EINVAL;
	sl->qp = qp;
	if (q->flags & V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT) {
		sl->deblock = read_ue(&b);
		if (sl->deblock != 1) {
			sl->alpha = read_se(&b);
			sl->beta = read_se(&b);
		}
	}
	if (b.bad || sl->deblock > 2 || abs(sl->alpha) > 6 || abs(sl->beta) > 6)
		return -EINVAL;
	sl->bits = b.next * 8 - b.left;
	if (q->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE)
		sl->bits = ALIGN(sl->bits, 8);
	return sl->bits < len * 8 ? 0 : -EINVAL;
}
static void free_surface(void *data)
{
	struct vdh_surface *s = data;
	dma_free_noncoherent(s->dev, s->size, s->cpu, s->dma, DMA_BIDIRECTIONAL);
}
int vdh_hw_init(struct device *dev, struct vdh_hw *hw)
{
	const struct firmware *fw;
	hw->dev = dev;
	hw->crg = syscon_regmap_lookup_by_compatible("hisilicon,hi3798mv100-crg");
	if (IS_ERR(hw->crg))
		return PTR_ERR(hw->crg);
	if (!devm_request_mem_region(dev, 0xf8c30000, 0xd000, "mv100-vdh"))
		return -EBUSY;
	hw->regs = devm_ioremap(dev, 0xf8c30000, 0xd000);
	if (!hw->regs)
		return -ENOMEM;
	if (!firmware_request_nowarn(&fw, "hisilicon/mv100-hevc-cabac.bin", dev)) {
		if (fw->size == sizeof(hw->hevc_cabac)) {
			memcpy(hw->hevc_cabac, fw->data, fw->size);
			hw->hevc_ready = true;
		}
		release_firmware(fw);
	}
	if (!firmware_request_nowarn(&fw, "hisilicon/mv100-h264-cabac.bin", dev)) {
		if (fw->size == sizeof(hw->h264_cabac)) {
			memcpy(hw->h264_cabac, fw->data, fw->size);
			hw->h264_cabac_ready = true;
		}
		release_firmware(fw);
	}
	return 0;
}
static int allocate_stream(struct vdh_hw *hw)
{
	struct device *dev = hw->dev;
	unsigned int i;
	int ret;
	hw->mem = dmam_alloc_coherent(dev, hw->layout.arena_size, &hw->dma, GFP_KERNEL);
	if (!hw->mem)
		return -ENOMEM;
	if (!IS_ALIGNED(hw->dma, 1024) || upper_32_bits(hw->dma + hw->layout.arena_size - 1))
		return -ERANGE;
	for (i = 0; i < ARRAY_SIZE(hw->surfaces); i++) {
		struct vdh_surface *s = &hw->surfaces[i];
		s->dev = dev;
		s->size = hw->layout.recon_size;
		s->coherent = false;
		s->cpu = dma_alloc_noncoherent(dev, s->size, &s->dma, DMA_BIDIRECTIONAL,
					       GFP_KERNEL | __GFP_NOWARN);
		if (s->cpu) {
			ret = devm_add_action_or_reset(dev, free_surface, s);
			if (ret)
				return ret;
		} else {
			s->cpu = dmam_alloc_coherent(dev, s->size, &s->dma, GFP_KERNEL);
			if (!s->cpu)
				return -ENOMEM;
			s->coherent = true;
		}
		if (!IS_ALIGNED(s->dma, 1024) || upper_32_bits(s->dma + hw->layout.recon_size - 1))
			return -ERANGE;
		if (!s->coherent)
			dma_sync_single_for_cpu(dev, s->dma, hw->layout.recon_size, DMA_BIDIRECTIONAL);
		memset(s->cpu, 0, hw->layout.recon_size);
		if (!s->coherent)
			dma_sync_single_for_device(dev, s->dma, hw->layout.recon_size, DMA_BIDIRECTIONAL);
	}
	dev_dbg(hw->dev, "stream arena %u recon %u coherent %d/%d\n", hw->layout.arena_size, hw->layout.recon_size, hw->surfaces[0].coherent, hw->surfaces[1].coherent);
	memset(hw->mem, 0, hw->layout.arena_size);
	memcpy(hw->mem + CABAC, hw->h264_cabac, sizeof(hw->h264_cabac));
	memcpy(hw->mem + CABAC + 8192, hw->hevc_cabac, sizeof(hw->hevc_cabac));
	return 0;
}
int vdh_start_stream(struct vdh_hw *hw, u32 width, u32 height)
{
	int ret;
	if (hw->wedged)
		return -EIO;
	if (!devres_open_group(hw->dev, hw->surfaces, GFP_KERNEL))
		return -ENOMEM;
	size_layout(&hw->layout, width, height);
	ret = allocate_stream(hw);
	devres_close_group(hw->dev, hw->surfaces);
	if (ret)
		vdh_stop_stream(hw);
	return ret;
}
void vdh_stop_stream(struct vdh_hw *hw)
{
	if (hw->wedged)
		return;
	devres_release_group(hw->dev, hw->surfaces);
	hw->mem = NULL;
}
static void copy_capture(u8 *dest, const u8 *src, u32 w, u32 h, u32 uv)
{
	u32 chroma[256 / sizeof(u32)];
	unsigned int y, x;
	u32 stride = ALIGN(w, 64);
	/* The last 256-byte tile has its own, possibly shorter, line pitch. */
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x += 256) {
			u32 pitch = min(256u, stride - x);
			u32 off = (y / 16) * stride * 16 + x * 16 + (y % 16) * pitch;
			memcpy(dest + y*w + x, src + off, min(256u, w - x));
		}
	}
	dest += w * h;
	src += uv;
	for (y = 0; y < h / 2; y++) {
		for (x = 0; x < w; x += 256) {
			u32 pitch = min(256u, stride - x);
			u32 off = (y / 8) * stride * 8 + x * 8 + (y % 8) * pitch;
			u32 i, count = min(256u, w - x);
			u32 *out = (u32 *)(dest + y * w + x);
			/* Burst-read the tile before swapping NV21 chroma. Byte reads
			 * from the coherent fallback mapping serialize on this CPU. */
			memcpy(chroma, src + off, count);
			for (i = 0; i < count / sizeof(*out); i++)
				out[i] = swahb32(chroma[i]);
		}
	}
}
/* SCD normally removes emulation-prevention bytes before the VDH reads a slice. */
static int unpack_nal(u8 *dst, const u8 *src, size_t length)
{
	size_t i, n = 0;
	unsigned int zeros = 0;
	for (i = 0; i < length; i++) {
		u8 byte = src[i];
		if (zeros == 2 && byte == 3) {
			if (i + 1 == length || src[i+1] > 3)
				return -EINVAL;
			zeros = 0;
			continue;
		}
		dst[n++] = byte;
		zeros = byte ? 0 : min(zeros + 1, 2u);
	}
	return n;
}
static int finish_picture(struct vdh_hw *hw, const struct vdh_picture *p,
			  unsigned int dst, u32 blocks, bool hevc)
{
	u32 width = p->width, height = p->height, stride = ALIGN(width, 64);
	u32 uv = ALIGN(height, hevc ? 64 : 32) * (stride + 16), base = (u32)hw->dma;
	u32 saved, emar, gate, state, ack, i, *up = (u32 *)(hw->mem + UP);
	int ret, reset;
	ret = regmap_read(hw->crg, 0x78, &saved);
	if (ret)
		return ret;
	ret = regmap_update_bits(hw->crg, 0x78, CRG_BITS, BIT(0));
	if (ret)
		return ret;
	udelay(30);
	emar = readl(hw->regs + 0xc004);
	gate = readl(hw->regs + 0xc00c);
	writel(~0u, hw->regs + 0x24);
	writel(~0u, hw->regs + 0x20);
	writel(0x17710, hw->regs + 0xc004);
	writel(hevc ? 0 : 3, hw->regs + 0xc00c);
	writel((hevc ? 0x01000000 : 0x41000000) | (blocks-1), hw->regs + 0x08);
	writel(hevc ? 0x2003400d : (0x20014000 | (stride << 2)), hw->regs + 0x0c);
	writel(base + PIC, hw->regs + 0x10);
	writel(base + UP, hw->regs + 0x14);
	writel(base + NAL, hw->regs + 0x18);
	for (i = 0x3c; i <= 0x54; i += 4)
		writel(0x300c03, hw->regs + i);
	writel(hw->surfaces[dst].dma, hw->regs + 0x60);
	writel(stride * 16, hw->regs + 0x64);
	writel(uv, hw->regs + 0x68);
	writel(ALIGN(height, 32) * 16, hw->regs + 0x6c);
	writel(base + hw->layout.ppfd, hw->regs + 0x80);
	writel(0, hw->regs + 0x84);
	writel(0, hw->regs + 0x94);
	writel(2, hw->regs + 0x98);
	dma_wmb();
	writel(0, hw->regs); writel(1, hw->regs); writel(0, hw->regs);
	ret = readl_poll_timeout(hw->regs + 0x1c, state, state & BIT(17), 50, 500000);
	dma_rmb();
	if (!ret && (up[1] || up[2] != blocks-1))
		ret = -EIO;
	if (ret)
		dev_err_ratelimited(hw->dev, "state=%08x UP=%08x,%08x,%08x (%d)\n",
				    state, up[0], up[1], up[2], ret);
	writel(~0u, hw->regs + 0x20);
	reset = regmap_update_bits(hw->crg, 0x78, BIT(6), BIT(6));
	if (!reset)
		reset = regmap_read_poll_timeout(hw->crg, 0x174, ack, ack & BIT(2), 10, 50000);
	if (reset) {
		hw->wedged = true;
		__module_get(THIS_MODULE);
		dev_err(hw->dev, "reset not acknowledged; retaining DMA until power-off\n");
		return -EIO;
	}
	regmap_update_bits(hw->crg, 0x78, BIT(6), 0);
	writel(gate, hw->regs + 0xc00c);
	writel(emar, hw->regs + 0xc004);
	regmap_update_bits(hw->crg, 0x78, CRG_BITS, saved & CRG_BITS);
	if (ret)
		return ret;
	if (!hw->surfaces[dst].coherent)
		dma_sync_single_for_cpu(hw->dev, hw->surfaces[dst].dma, uv + stride * ALIGN(height, 16)/2, DMA_BIDIRECTIONAL);
	copy_capture(p->out, hw->surfaces[dst].cpu, width, height, uv);
	if (!hw->surfaces[dst].coherent)
		dma_sync_single_for_device(hw->dev, hw->surfaces[dst].dma, uv + stride * ALIGN(height, 16)/2, DMA_BIDIRECTIONAL);
	return 0;
}
int vdh_picture(struct vdh_hw *hw, struct vdh_stream *stream, const struct vdh_picture *p)
{
	const u8 *data = p->data;
	size_t len = p->bytes;
	struct slice_info sl = {};
	u32 width = p->width, height = p->height;
	u32 mbs = width * height / 256;
	u32 *pic = (u32 *)(hw->mem + PIC), *slice = (u32 *)(hw->mem + SLICE);
	u32 *up = (u32 *)(hw->mem + UP), base = (u32)hw->dma;
	unsigned int i, dst, ref;
	int ret;
	if (hw->wedged)
		return -EIO;
	if (width < 16 || height < 16 || width > VDH_MAX_W || height > VDH_MAX_H ||
	    width != 16u * (p->sps->pic_width_in_mbs_minus1 + 1) ||
	    height != 16u * (p->sps->pic_height_in_map_units_minus1 + 1) ||
	    !data || !p->out || len < 5 || len > hw->layout.nal_size)
		return -EINVAL;
	while (len > 3 && !data[0] && !data[1] && !data[2]) {
		data++; len--;
	}
	if (len < 4 || data[0] || data[1] || data[2] != 1)
		return -EINVAL;
	data += 3; len -= 3;
	for (i = 2; i < len; i++)
		if (!data[i-2] && !data[i-1] && data[i] == 1)
			return -EOPNOTSUPP;
	ret = unpack_nal(hw->mem + NAL, data, len);
	if (ret < 0)
		return ret;
	len = ret;
	ret = parse_slice(p, hw->mem + NAL, len, &sl);
	if (ret)
		return ret;
	if (sl.predictive) {
		unsigned int refs = 0;
		for (i = 0; i < ARRAY_SIZE(p->decode->dpb); i++) {
			const struct v4l2_h264_dpb_entry *e = &p->decode->dpb[i];
			if (!(e->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
				continue;
			if (e->reference_ts != stream->timestamp || stream->last < 0 ||
			    (e->flags & (V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM | V4L2_H264_DPB_ENTRY_FLAG_FIELD)))
				return -EINVAL;
			refs++;
		}
		if (refs != 1)
			return -EINVAL;
	}
	dst = stream->last == 0 ? 1 : 0;
	ref = sl.predictive ? stream->last : dst;
	memset(up, 0, 1024);
	memset(pic, 0, 1024);
	memset(slice, 0, 1024);
	memset(hw->mem + NAL + len, 0, 64);
	pic[0] = (width/16 - 1) | ((height/16 - 1) << 16) | BIT(25);
	if (p->pps->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE) {
		if (!hw->h264_cabac_ready)
			return -EOPNOTSUPP;
		pic[0] |= BIT(27);
	}
	if (p->pps->flags & V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE)
		pic[0] |= BIT(28);
	if (sl.reference)
		pic[0] |= BIT(31);
	if (p->pps->flags & V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED)
		pic[0] |= BIT(26);
	pic[1] = hw->surfaces[dst].dma;
	pic[2] = min(p->decode->top_field_order_cnt, p->decode->bottom_field_order_cnt);
	pic[5] = base + hw->layout.sed; pic[6] = base + hw->layout.pmv_top;
	pic[7] = base + hw->layout.pmv + dst * hw->layout.pmv_size; pic[8] = base + hw->layout.rcn;
	pic[9] = base + SLICE;
	for (i = 0; i < 16; i++)
		pic[10+i] = hw->surfaces[ref].dma;
	pic[26] = base + CABAC;
	if (sl.predictive)
		pic[27] = pic[28] = stream->poc;
	pic[59] = base + hw->layout.dblk;
	memset(&pic[64], 0x10, 224);
	slice[0] = (sl.bits / 8) & ~15u;
	slice[1] = sl.bits & 127;
	slice[2] = len * 8 - sl.bits;
	slice[6] = (sl.qp << 26) | (sl.cabac_init << 24);
	slice[7] = sl.predictive ? 5 : 0;
	if (p->sps->flags & V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE)
		slice[7] |= BIT(14);
	slice[10] = (p->pps->second_chroma_qp_index_offset & 31) |
		    ((p->pps->chroma_qp_index_offset & 31) << 5);
	slice[11] = sl.deblock | ((sl.beta & 15) << 8) | ((sl.alpha & 15) << 16);
	slice[44] = mbs - 1;
	ret = finish_picture(hw, p, dst, mbs, false);
	if (ret)
		return ret;
	if (sl.reference) {
		stream->last = dst;
		stream->timestamp = p->timestamp;
		stream->poc = pic[2];
	}
	hw->frames++;
	return 0;
}

int vdh_hevc_picture(struct vdh_hw *hw, struct vdh_stream *st, const struct vdh_picture *p)
{
	const struct v4l2_ctrl_hevc_sps *s = p->hsps;
	const struct v4l2_ctrl_hevc_pps *q = p->hpps;
	const struct v4l2_ctrl_hevc_slice_params *v = p->hsl;
	const struct v4l2_ctrl_hevc_decode_params *d = p->hdec;
	u32 *pic = (u32 *)(hw->mem + PIC), *slice = (u32 *)(hw->mem + SLICE);
	u32 *tile = (u32 *)(hw->mem + TILE), base = (u32)hw->dma;
	u32 cb = 3 + s->log2_min_luma_coding_block_size_minus3;
	u32 ctb = cb + s->log2_diff_max_min_luma_coding_block_size;
	u32 tb = 2 + s->log2_min_luma_transform_block_size_minus2;
	u32 maxtb = tb + s->log2_diff_max_min_luma_transform_block_size;
	u32 w = s->pic_width_in_luma_samples, h = s->pic_height_in_luma_samples;
	u32 cw, ch, blocks, offset = v->data_byte_offset, nal, i, dst, ref;
	bool pred = v->slice_type == V4L2_HEVC_SLICE_TYPE_P;
	int len, ret, qp = 26 + q->init_qp_minus26 + v->slice_qp_delta;
	if (hw->wedged || !hw->hevc_ready)
		return -ENODEV;
	if (!p->data || !p->out || p->bytes < 3 || p->bytes > hw->layout.nal_size ||
	    !w || !h || w > VDH_MAX_W || h > VDH_MAX_H ||
	    ALIGN(w, 16) != p->width || ALIGN(h, 16) != p->height ||
	    s->chroma_format_idc != 1 || s->bit_depth_luma_minus8 || s->bit_depth_chroma_minus8 ||
	    cb > 6 || ctb < 4 || ctb > 6 || tb > 5 || maxtb > 5 ||
	    s->max_transform_hierarchy_depth_inter > 4 || s->max_transform_hierarchy_depth_intra > 4 ||
	    q->diff_cu_qp_delta_depth > ctb - cb || q->log2_parallel_merge_level_minus2 > 4 ||
	    v->five_minus_max_num_merge_cand > 4 || qp < 0 || qp > 51)
		return -EINVAL;
	if ((!pred && v->slice_type != V4L2_HEVC_SLICE_TYPE_I) ||
	    (s->flags & (V4L2_HEVC_SPS_FLAG_PCM_ENABLED | V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED)) ||
	    (q->flags & (V4L2_HEVC_PPS_FLAG_TILES_ENABLED | V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED |
			 V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED)) || v->num_entry_point_offsets ||
	    v->slice_segment_addr || v->bit_size != p->bytes * 8)
		return -EOPNOTSUPP;
	nal = (p->data[0] >> 1) & 63;
	if (nal != v->nal_unit_type || nal > 31 || p->data[0] & 0x81 ||
	    (p->data[1] & 0xf8) || !(p->data[1] & 7))
		return -EINVAL;
	if (pred && (v->num_ref_idx_l0_active_minus1 || st->last < 0 ||
		     d->num_active_dpb_entries > 16 || v->ref_idx_l0[0] >= d->num_active_dpb_entries ||
		     d->dpb[v->ref_idx_l0[0]].timestamp != st->timestamp))
		return -EOPNOTSUPP;
	len = unpack_nal(hw->mem + NAL, p->data, p->bytes);
	if (len < 3 || offset < 2 || offset >= len)
		return -EINVAL;
	memset(hw->mem + NAL + len, 0, 64);
	cw = DIV_ROUND_UP(w, 1u << ctb);
	ch = DIV_ROUND_UP(h, 1u << ctb);
	blocks = cw * ch;
	dst = st->last == 0 ? 1 : 0;
	ref = pred ? st->last : dst;
	memset(hw->mem + UP, 0, 1280);
	memset(pic, 0, 1280); memset(slice, 0, 1280); memset(tile, 0, 1280);
	tile[1024/4] = (cw-1) << 16; tile[1104/4] = (ch-1) << 16;
	pic[0] = BIT(19) | ((ch-1) << 9) | (cw-1);
	if (s->flags & V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED) pic[0] |= BIT(25);
	if (s->flags & V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET) pic[0] |= BIT(22);
	if (s->flags & V4L2_HEVC_SPS_FLAG_AMP_ENABLED) pic[0] |= BIT(21);
	pic[1] = d->pic_order_cnt_val;
	pic[2] = ((u32)q->log2_parallel_merge_level_minus2 << 29) |
		 ((u32)s->max_transform_hierarchy_depth_inter << 24) |
		 ((u32)s->max_transform_hierarchy_depth_intra << 21) | (tb << 12) | (maxtb << 9) |
		 ((ctb-cb) << 6) | (cb << 3) | ctb;
	pic[3] = 0x88; pic[4] = base + hw->layout.sed; pic[5] = base + hw->layout.pmv_top;
	pic[6] = base + hw->layout.pmv + dst * hw->layout.pmv_size; pic[7] = base + hw->layout.rcn;
	for (i = 0; i < 16; i++)
		pic[8+i] = hw->surfaces[ref].dma;
	pic[25] = base + CABAC + 8192;
	if (pred) pic[26] = st->poc;
	pic[43] = base + hw->layout.pmv_left; pic[54] = (h << 16) | w; pic[55] = base + TILE;
	pic[56] = base + hw->layout.sao_left; pic[57] = base + hw->layout.dblk_left;
	pic[58] = base + hw->layout.sao_top; pic[59] = base + hw->layout.dblk;
	pic[60] = ((ctb - q->diff_cu_qp_delta_depth) << 16) |
		  ((u32)q->diff_cu_qp_delta_depth << 13) |
		  ((q->pps_cb_qp_offset & 31) << 5) | (q->pps_cr_qp_offset & 31);
	if (q->flags & V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED) pic[60] |= BIT(23);
	if (q->flags & V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED) pic[60] |= BIT(22);
	if (q->flags & V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED) pic[60] |= BIT(12);
	if (q->flags & V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED) pic[60] |= BIT(11);
	if (q->flags & V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED) pic[60] |= BIT(10);
	pic[63] = base + SLICE;
	memset(pic + 64, 16, 1024);
	slice[0] = offset & ~15u; slice[1] = (offset & 15) * 8; slice[2] = (len-offset)*8;
	slice[6] = BIT(27) | ((u32)qp << 19);
	if (v->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_CABAC_INIT) slice[6] |= BIT(18);
	slice[7] = (5-v->five_minus_max_num_merge_cand) << 24;
	if (pred) {
		slice[7] |= 5;
		if (v->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED) {
			if (v->collocated_ref_idx) return -EOPNOTSUPP;
			slice[7] |= BIT(15) | BIT(27);
			slice[23] = base + hw->layout.pmv + ref * hw->layout.pmv_size;
		}
	}
	slice[11] = ((v->slice_cb_qp_offset & 31) << 8) | (v->slice_cr_qp_offset & 31);
	slice[12] = ((v->slice_tc_offset_div2 & 15) << 16) | ((v->slice_beta_offset_div2 & 15) << 8);
	if (v->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA) slice[12] |= BIT(3);
	if (v->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA) slice[12] |= BIT(2);
	if (v->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED) slice[12] |= BIT(1);
	if (v->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED) slice[12] |= BIT(0);
	slice[24] = 0x400a; slice[43] = slice[44] = blocks-1;
	ret = finish_picture(hw, p, dst, blocks, true);
	if (ret)
		return ret;
	if (nal >= 16 || (nal & 1)) {
		st->last = dst;
		st->timestamp = p->timestamp;
		st->poc = d->pic_order_cnt_val;
	}
	hw->frames++;
	return 0;
}
