/* SPDX-License-Identifier: GPL-2.0 */
/* V300R001 progressive, short-term H.264 picture and slice state. */
struct avc_slice {
	u8 type, active[2], list[2][32], cabac, deblock;
	u8 remove[VDH_AVC_SLOTS], removals;
	bool idr, reference, direct, adaptive;
	int bits, qp, alpha, beta, frame, poc, poc_lsb, poc_msb;
	u32 weights[2][3][32];
};

static int wrapped_frame(const struct vdh_reference *ref, int curr, int max)
{
	return ref->frame_num > curr ? ref->frame_num - max : ref->frame_num;
}

static bool comes_before(int a, int b, int poc, unsigned int list)
{
	if ((a < poc) != (b < poc))
		return list ? a >= poc : a < poc;
	return a < poc ? a > b : a < b;
}

static int read_list(struct bits *b, const struct vdh_stream *st, struct avc_slice *sl,
		     int l, int max, bool modify, int count)
{
	u8 *order = sl->list[l];
	unsigned int iter = 0;
	int pred = sl->frame;
	if (!modify)
		return count >= sl->active[l] ? 0 : -EINVAL;
	while (!b->bad) {
		u32 op = read_ue(b), delta;
		int slot, i, n;
		if (op == 3) break;
		if (op > 1 || iter >= sl->active[l]) return -EOPNOTSUPP;
		delta = read_ue(b) + 1;
		if (delta > max) return -EINVAL;
		pred = (pred + (op ? (int)delta : -(int)delta) + max) % max;
		for (slot = 0; slot < VDH_AVC_SLOTS; slot++)
			if (st->refs[slot].valid && st->refs[slot].frame_num == pred) break;
		if (slot == VDH_AVC_SLOTS) return -EINVAL;
		for (i = min(count, 30); i >= (int)iter; i--) order[i + 1] = order[i];
		order[iter++] = slot;
		n = iter;
		for (i = iter; i <= min(count, 31); i++)
			if (order[i] != slot) order[n++] = order[i];
		count = n;
	}
	return !b->bad && count >= sl->active[l] ? 0 : -EINVAL;
}

static int init_lists(struct bits *b, const struct vdh_stream *st, struct avc_slice *sl,
		      const struct v4l2_ctrl_h264_pps *pps, int max)
{
	int count = 0, l, i, j, prev, key;
	int lists = sl->type == 1 ? 2 : 1;
	bool override = get_bits(b, 1);
	for (l = 0; l < lists; l++) {
		u32 m = override ? read_ue(b) : l ? pps->num_ref_idx_l1_default_active_minus1 :
						  pps->num_ref_idx_l0_default_active_minus1;
		if (m >= 32) return -EINVAL;
		sl->active[l] = m + 1;
		count = 0;
		for (i = 0; i < VDH_AVC_SLOTS; i++) {
			if (!st->refs[i].valid) continue;
			key = sl->type ? st->refs[i].poc : wrapped_frame(&st->refs[i], sl->frame, max);
			for (j = count; j > 0; j--) {
				const struct vdh_reference *r = &st->refs[sl->list[l][j-1]];
				prev = sl->type ? r->poc : wrapped_frame(r, sl->frame, max);
				if (!(sl->type ? comes_before(key, prev, sl->poc, l) : key > prev)) break;
				sl->list[l][j] = sl->list[l][j-1];
			}
			sl->list[l][j] = i;
			count++;
		}
	}
	if (lists == 2 && count > 1 && !memcmp(sl->list[0], sl->list[1], count))
		swap(sl->list[1][0], sl->list[1][1]);
	for (l = 0; l < lists; l++) {
		int ret = read_list(b, st, sl, l, max, get_bits(b, 1), count);
		if (ret) return ret;
	}
	return 0;
}

static int read_weights(struct bits *b, struct avc_slice *sl)
{
	u32 ld = read_ue(b), cd = read_ue(b);
	int l, i, c;
	if (ld > 7 || cd > 7) return -EINVAL;
	for (l = 0; l < (sl->type == 1 ? 2 : 1); l++) {
		for (i = 0; i < sl->active[l]; i++) {
			bool flag = get_bits(b, 1);
			for (c = 0; c < 3; c++) {
				int denom = c ? cd : ld, w, off;
				if (c == 1) flag = get_bits(b, 1);
				w = flag ? read_se(b) : 1 << denom;
				off = flag ? read_se(b) : 0;
				if (w < -128 || w > (flag ? 127 : 128) || off < -128 || off > 127)
					return -EINVAL;
				sl->weights[l][c][i] = c == 2 ? (w & 511) | ((off & 255) << 9) :
						denom | ((w & 511) << 3) | ((off & 255) << 12);
			}
		}
	}
	return b->bad ? -EINVAL : 0;
}

static int parse_slice(const struct vdh_picture *p, const struct vdh_stream *st,
		       const u8 *data, size_t len, struct avc_slice *sl)
{
	const struct v4l2_ctrl_h264_sps *s = p->sps;
	const struct v4l2_ctrl_h264_pps *q = p->pps;
	struct bits b = { .data = data, .size = len, .next = 1 };
	u32 type, maxframe, maxpoc;
	int ret, qp;
	sl->idr = (data[0] & 31) == 5;
	sl->reference = (data[0] & 0x60) != 0;
	if (data[0] & 0x80 || (!sl->idr && (data[0] & 31) != 1) ||
	    (s->profile_idc != 66 && s->profile_idc != 77 && s->profile_idc != 100) ||
	    s->chroma_format_idc != 1 || s->bit_depth_luma_minus8 || s->bit_depth_chroma_minus8 ||
	    !(s->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) || s->max_num_ref_frames >= VDH_AVC_SLOTS ||
	    s->log2_max_frame_num_minus4 > 12 || s->log2_max_pic_order_cnt_lsb_minus4 > 12 ||
	    s->pic_order_cnt_type > 2 || q->num_slice_groups_minus1 || q->weighted_bipred_idc > 2 ||
	    (q->flags & V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT) || read_ue(&b))
		return -EOPNOTSUPP;
	type = read_ue(&b);
	sl->type = type % 5;
	if (type > 9 || sl->type > 2 || (sl->idr && sl->type != 2) ||
	    read_ue(&b) != q->pic_parameter_set_id) return -EINVAL;
	maxframe = 1U << (4 + s->log2_max_frame_num_minus4);
	sl->frame = get_bits(&b, 4 + s->log2_max_frame_num_minus4);
	if (sl->frame != p->decode->frame_num) return -EINVAL;
	if (sl->idr) read_ue(&b);
	sl->poc = min(p->decode->top_field_order_cnt, p->decode->bottom_field_order_cnt);
	if (!s->pic_order_cnt_type) {
		int last_lsb = sl->idr ? 0 : st->prev_poc_lsb;
		int last_msb = sl->idr ? 0 : st->prev_poc_msb;
		int delta = 0;
		maxpoc = 1U << (4 + s->log2_max_pic_order_cnt_lsb_minus4);
		sl->poc_lsb = get_bits(&b, 4 + s->log2_max_pic_order_cnt_lsb_minus4);
		sl->poc_msb = last_msb;
		if (sl->poc_lsb < last_lsb && last_lsb - sl->poc_lsb >= maxpoc / 2)
			sl->poc_msb += maxpoc;
		else if (sl->poc_lsb > last_lsb && sl->poc_lsb - last_lsb > maxpoc / 2)
			sl->poc_msb -= maxpoc;
		if (q->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT)
			delta = read_se(&b);
		if (p->stateful) sl->poc = sl->poc_msb + sl->poc_lsb + min(delta, 0);
	} else if (s->pic_order_cnt_type == 1 &&
		   !(s->flags & V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO)) {
		read_se(&b);
		if (q->flags & V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT)
			read_se(&b);
	}
	if ((q->flags & V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT) && read_ue(&b)) return -EOPNOTSUPP;
	if (sl->type == 1) sl->direct = get_bits(&b, 1);
	if (sl->type != 2) {
		ret = init_lists(&b, st, sl, q, maxframe);
		if (ret) return ret;
		if ((!sl->type && (q->flags & V4L2_H264_PPS_FLAG_WEIGHTED_PRED)) ||
		    (sl->type == 1 && q->weighted_bipred_idc == 1)) {
			ret = read_weights(&b, sl);
			if (ret) return ret;
		}
	}
	if (sl->reference) {
		if (sl->idr) {
			get_bits(&b, 1);
			if (get_bits(&b, 1)) return -EOPNOTSUPP;
		} else if ((sl->adaptive = get_bits(&b, 1))) {
			u32 op;
			while ((op = read_ue(&b))) {
				u32 delta;
				unsigned int i;
				int target;
				if (op != 1 || sl->removals >= VDH_AVC_SLOTS) return -EOPNOTSUPP;
				delta = read_ue(&b) + 1;
				if (delta > maxframe) return -EINVAL;
				target = (sl->frame + maxframe - delta) % maxframe;
				for (i = 0; i < VDH_AVC_SLOTS; i++)
					if (st->refs[i].valid && st->refs[i].frame_num == target) break;
				if (i == VDH_AVC_SLOTS) return -EINVAL;
				sl->remove[sl->removals++] = i;
			}
		}
	}
	if (sl->type != 2 && (q->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE)) {
		u32 init = read_ue(&b);
		if (init > 2) return -EINVAL;
		sl->cabac = init;
	}
	qp = 26 + q->pic_init_qp_minus26 + read_se(&b);
	if (qp < 0 || qp > 51) return -EINVAL;
	sl->qp = qp;
	if (q->flags & V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT) {
		u32 mode = read_ue(&b);
		if (mode > 2) return -EINVAL;
		sl->deblock = mode;
		if (mode != 1) {
			sl->alpha = read_se(&b);
			sl->beta = read_se(&b);
		}
	}
	if (b.bad || abs(sl->alpha) > 6 || abs(sl->beta) > 6) return -EINVAL;
	sl->bits = b.next * 8 - b.left;
	if (q->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE) sl->bits = ALIGN(sl->bits, 8);
	return sl->bits < len * 8 ? 0 : -EINVAL;
}
