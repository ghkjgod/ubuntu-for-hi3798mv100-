// SPDX-License-Identifier: GPL-2.0
/* Bounded Annex B header reader for the single-reference, progressive VDH path. */
#include <linux/kernel.h>
#include "vdh-avc.h"

struct reader {
	const u8 *p, *end;
	u8 byte, bits, zeros;
	bool bad;
};

static u32 bits(struct reader *r, unsigned int n)
{
	u32 result = 0;

	while (n-- && !r->bad) {
		if (!r->bits) {
			if (r->zeros == 2 && r->p < r->end && *r->p == 3) {
				r->p++;
				if (r->p == r->end || *r->p > 3)
					r->bad = true;
				r->zeros = 0;
			}
			if (r->bad || r->p == r->end) {
				r->bad = true;
				return 0;
			}
			r->byte = *r->p++;
			r->zeros = r->byte ? 0 : min(2, r->zeros + 1);
			r->bits = 8;
		}
		result = result << 1 | ((r->byte >> --r->bits) & 1);
	}
	return result;
}

static u32 ue(struct reader *r, unsigned int max)
{
	unsigned int n = 0;
	u32 value;
	while (!bits(r, 1) && !r->bad)
		if (++n > 16)
			r->bad = true;
	if (r->bad)
		return 0;
	value = (1U << n) - 1 + bits(r, n);
	if (value > max)
		r->bad = true;
	return r->bad ? 0 : value;
}

static int se(struct reader *r, unsigned int max)
{
	u32 v = ue(r, 2 * max);
	return v & 1 ? (int)(v + 1) / 2 : -(int)(v / 2);
}

static int sequence(struct mv100_avc *a, struct reader *r)
{
	struct v4l2_ctrl_h264_sps *s = &a->sps;
	unsigned int right = 0, bottom = 0, w, h;
	a->has_sps = false;
	memset(s, 0, sizeof(*s));
	s->profile_idc = bits(r, 8);
	s->constraint_set_flags = bits(r, 8) >> 2;
	s->level_idc = bits(r, 8);
	s->seq_parameter_set_id = ue(r, 31);
	s->chroma_format_idc = 1;
	if (s->profile_idc == 100) {
		if (ue(r, 3) != 1 || ue(r, 6) || ue(r, 6) || bits(r, 1) || bits(r, 1))
			return -EOPNOTSUPP;
	} else if (s->profile_idc != 77 && s->profile_idc != 66) {
		return -EOPNOTSUPP;
	}
	s->log2_max_frame_num_minus4 = ue(r, 12);
	s->pic_order_cnt_type = ue(r, 2);
	if (!s->pic_order_cnt_type)
		s->log2_max_pic_order_cnt_lsb_minus4 = ue(r, 12);
	else if (s->pic_order_cnt_type == 1)
		return -EOPNOTSUPP;
	s->max_num_ref_frames = ue(r, 1);
	if (bits(r, 1))
		return -EOPNOTSUPP;
	s->pic_width_in_mbs_minus1 = ue(r, VDH_MAX_W / 16 - 1);
	s->pic_height_in_map_units_minus1 = ue(r, VDH_MAX_H / 16 - 1);
	if (!bits(r, 1))
		return -EOPNOTSUPP;
	s->flags = V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY;
	if (bits(r, 1))
		s->flags |= V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE;
	w = 16 * (1 + s->pic_width_in_mbs_minus1);
	h = 16 * (1 + s->pic_height_in_map_units_minus1);
	if (bits(r, 1)) {
		if (ue(r, w / 2))
			return -EOPNOTSUPP;
		right = 2 * ue(r, w / 2);
		if (ue(r, h / 2))
			return -EOPNOTSUPP;
		bottom = 2 * ue(r, h / 2);
	}
	if (r->bad || right >= w || bottom >= h)
		return -EINVAL;
	if (a->width && (a->width != w || a->height != h ||
			 a->visible_w != w - right || a->visible_h != h - bottom))
		return -EOPNOTSUPP;
	a->width = w; a->height = h;
	a->visible_w = w - right; a->visible_h = h - bottom;
	a->has_sps = true;
	return 0;
}

/* rbsp_trailing_bits follows the base PPS if no extension was signalled. */
static bool extension(struct reader r)
{
	if (!bits(&r, 1))
		return !r.bad;
	while (!r.bad && (r.bits || r.p < r.end))
		if (bits(&r, 1))
			return true;
	return false;
}

static int picture_params(struct mv100_avc *a, struct reader *r)
{
	struct v4l2_ctrl_h264_pps *p = &a->pps;
	a->has_pps = false;
	memset(p, 0, sizeof(*p));
	p->pic_parameter_set_id = ue(r, 255);
	p->seq_parameter_set_id = ue(r, 31);
	if (bits(r, 1)) p->flags |= V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE;
	if (bits(r, 1)) p->flags |= V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT;
	if (ue(r, 7)) return -EOPNOTSUPP;
	p->num_ref_idx_l0_default_active_minus1 = ue(r, 31);
	p->num_ref_idx_l1_default_active_minus1 = ue(r, 31);
	if (bits(r, 1)) return -EOPNOTSUPP;
	p->weighted_bipred_idc = bits(r, 2);
	p->pic_init_qp_minus26 = se(r, 26);
	p->pic_init_qs_minus26 = se(r, 26);
	p->chroma_qp_index_offset = se(r, 12);
	if (bits(r, 1)) p->flags |= V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT;
	if (bits(r, 1)) p->flags |= V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED;
	if (bits(r, 1)) p->flags |= V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT;
	p->second_chroma_qp_index_offset = p->chroma_qp_index_offset;
	if (extension(*r)) {
		if (bits(r, 1)) p->flags |= V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE;
		if (bits(r, 1)) return -EOPNOTSUPP;
		p->second_chroma_qp_index_offset = se(r, 12);
	}
	if (r->bad || !a->has_sps || p->seq_parameter_set_id != a->sps.seq_parameter_set_id)
		return -EINVAL;
	a->has_pps = true;
	return 0;
}

static size_t prefix(const u8 *p, size_t n, size_t from)
{
	for (; from + 3 <= n; from++)
		if (!p[from] && !p[from+1] && p[from+2] == 1)
			break;
	return from + 3 <= n ? from : n;
}

int mv100_avc_parse(struct mv100_avc *a, const u8 *data, size_t n,
		    const u8 **slice, size_t *length, const struct vdh_stream *ref)
{
	size_t start = prefix(data, n, 0), next, end;
	int ret, found = 0;
	if (start == n || start > 1 || (start && data[0]))
		return -EINVAL;
	while (start < n) {
		size_t pos = start + 3;
		unsigned int type, poc;
		struct reader r;
		next = prefix(data, n, pos);
		end = next;
		while (end > pos && !data[end - 1]) end--;
		if (pos >= end || data[pos] & 0x80) return -EINVAL;
		type = data[pos] & 31;
		r = (struct reader){ .p = data + pos + 1, .end = data + end };
		ret = 0;
		if (found && (type == 7 || type == 8)) return -EOPNOTSUPP;
		if (type == 7)
			ret = sequence(a, &r);
		else if (type == 8)
			ret = picture_params(a, &r);
		else if (type == 1 || type == 5) {
			unsigned int slice_type;
			if (found || !a->has_sps || !a->has_pps || ue(&r, 0)) return -EINVAL;
			slice_type = ue(&r, 9) % 5;
			if (slice_type != 0 && slice_type != 2) return -EOPNOTSUPP;
			if (ue(&r, 255) != a->pps.pic_parameter_set_id) return -EINVAL;
			memset(&a->dec, 0, sizeof(a->dec));
			a->dec.frame_num = bits(&r, a->sps.log2_max_frame_num_minus4 + 4);
			if (type == 5) ue(&r, 65535);
			poc = a->sps.pic_order_cnt_type == 0 ?
				bits(&r, a->sps.log2_max_pic_order_cnt_lsb_minus4 + 4) : a->dec.frame_num * 2;
			a->dec.top_field_order_cnt = a->dec.bottom_field_order_cnt = poc;
			if (r.bad) return -EINVAL;
			if (ref->last >= 0) {
				a->dec.dpb[0].reference_ts = ref->timestamp;
				a->dec.dpb[0].flags = V4L2_H264_DPB_ENTRY_FLAG_ACTIVE;
			}
			*slice = data + start; *length = end - start;
			found = 1;
		} else if (type != 6 && type != 9 && type != 12 && type != 10 && type != 11)
			return -EOPNOTSUPP;
		if (ret) return ret;
		start = next;
	}
	return found;
}
