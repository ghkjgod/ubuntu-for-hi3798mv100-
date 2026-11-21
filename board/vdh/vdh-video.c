// SPDX-License-Identifier: GPL-2.0
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <media/media-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-vmalloc.h>
#include "vdh.h"
#include "vdh-avc.h"

#define NAME "mv100-vdh"
struct context {
	struct v4l2_fh fh;
	struct v4l2_ctrl_handler controls;
	struct v4l2_ctrl *sps, *pps, *decode;
	struct vdh_stream stream;
	struct work_struct work;
	struct mv100_avc *avc, *check;
	u32 input_size;
	bool announced;
	struct mutex display_lock;
	struct { struct vb2_v4l2_buffer *buffer; s32 poc; } pending[17];
	unsigned int pending_count, output_sequence;
	u32 width, height, sequence, codec;
	struct v4l2_ctrl *hsps, *hpps, *hsl, *hdec;
};
static struct {
	struct platform_device *pdev;
	struct v4l2_device v4l2;
	struct video_device video, web;
	struct media_device media;
	struct v4l2_m2m_dev *m2m;
	struct mutex mutex;
	struct vdh_hw hw;
	struct dentry *debug;
	struct context *owner;
	atomic_t avc_refusals;
} vdh;
static ssize_t decoded_frames_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", READ_ONCE(vdh.hw.frames));
}
static ssize_t rejected_frames_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", READ_ONCE(vdh.hw.errors));
}
/* Published for players that select the stateful short-term-reference decoder. */
static ssize_t avc_refusals_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", atomic_read(&vdh.avc_refusals));
}
static DEVICE_ATTR_RO(avc_refusals);
static DEVICE_ATTR_RO(decoded_frames);
static DEVICE_ATTR_RO(rejected_frames);
static struct attribute *vdh_attributes[] = {
	&dev_attr_decoded_frames.attr, &dev_attr_rejected_frames.attr, &dev_attr_avc_refusals.attr, NULL,
};
static const struct attribute_group vdh_stats = { .attrs = vdh_attributes };
static inline struct context *context(void *fh)
{
	return container_of(fh, struct context, fh);
}
static int querycap(struct file *file, void *fh, struct v4l2_capability *c)
{
	strscpy(c->driver, NAME, sizeof(c->driver));
	strscpy(c->card, "Hi3798MV100 VDH", sizeof(c->card));
	strscpy(c->bus_info, "platform:" NAME, sizeof(c->bus_info));
	return 0;
}
static bool web_node(struct file *file)
{
	return video_devdata(file) == &vdh.web;
}
static u32 capture_bytes(struct context *c)
{
	return c->width * c->height * 3 / 2;
}
static int subscribe(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	if (fh->vdev == &vdh.web && sub->type == V4L2_EVENT_SOURCE_CHANGE)
		return v4l2_src_change_event_subscribe(fh, sub);
	if (fh->vdev == &vdh.web && sub->type == V4L2_EVENT_EOS)
		return v4l2_event_subscribe(fh, sub, 1, NULL);
	return v4l2_ctrl_subscribe_event(fh, sub);
}
static int enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	if (web_node(file)) {
		bool out = V4L2_TYPE_IS_OUTPUT(f->type);
		if (f->index)
			return -EINVAL;
		f->pixelformat = out ? V4L2_PIX_FMT_H264 : V4L2_PIX_FMT_NV12;
		if (out) f->flags = V4L2_FMT_FLAG_COMPRESSED | V4L2_FMT_FLAG_DYN_RESOLUTION;
		return 0;
	}
	if (f->index > (V4L2_TYPE_IS_OUTPUT(f->type) && vdh.hw.hevc_ready ? 1 : 0))
		return -EINVAL;
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		f->pixelformat = f->index ? V4L2_PIX_FMT_HEVC_SLICE : V4L2_PIX_FMT_H264_SLICE;
		f->flags = V4L2_FMT_FLAG_COMPRESSED;
	} else {
		f->pixelformat = V4L2_PIX_FMT_NV12;
	}
	return 0;
}
static void fill_fmt(struct v4l2_format *f, u32 w, u32 h, u32 codec)
{
	struct v4l2_pix_format_mplane *p = &f->fmt.pix_mp;
	bool out = V4L2_TYPE_IS_OUTPUT(f->type);
	memset(p, 0, sizeof(*p));
	p->width = w; p->height = h;
	p->pixelformat = out ? codec : V4L2_PIX_FMT_NV12;
	p->field = V4L2_FIELD_NONE;
	p->colorspace = V4L2_COLORSPACE_REC709;
	p->num_planes = 1;
	p->plane_fmt[0].bytesperline = out ? 0 : w;
	p->plane_fmt[0].sizeimage = out ? VDH_MAX_NAL : w*h*3/2;
}
static int get_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct context *c = context(fh);
	fill_fmt(f, c->width, c->height, c->codec);
	if (c->avc && V4L2_TYPE_IS_OUTPUT(f->type))
		f->fmt.pix_mp.plane_fmt[0].sizeimage = c->input_size;
	return 0;
}
static int try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct context *c = context(fh);
	u32 w = c->width, h = c->height;
	if (c->avc) {
		u32 bytes = clamp(f->fmt.pix_mp.plane_fmt[0].sizeimage, (u32)VDH_MAX_NAL, 4U * SZ_1M);
		fill_fmt(f, w, h, V4L2_PIX_FMT_H264);
		if (V4L2_TYPE_IS_OUTPUT(f->type))
			f->fmt.pix_mp.plane_fmt[0].sizeimage = bytes;
		return 0;
	}
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		w = ALIGN(clamp(f->fmt.pix_mp.width, 16u, (u32)VDH_MAX_W), 16);
		h = ALIGN(clamp(f->fmt.pix_mp.height, 16u, (u32)VDH_MAX_H), 16);
	}
	fill_fmt(f, w, h, vdh.hw.hevc_ready && f->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_HEVC_SLICE ? V4L2_PIX_FMT_HEVC_SLICE : V4L2_PIX_FMT_H264_SLICE);
	return 0;
}
static int set_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct context *c = context(fh);
	struct vb2_queue *q = v4l2_m2m_get_vq(c->fh.m2m_ctx, f->type);
	if (vb2_is_busy(q))
		return -EBUSY;
	try_fmt(file, fh, f);
	if (c->avc) {
		if (V4L2_TYPE_IS_OUTPUT(f->type)) c->input_size = f->fmt.pix_mp.plane_fmt[0].sizeimage;
		return 0;
	}
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		if ((c->width != f->fmt.pix_mp.width || c->height != f->fmt.pix_mp.height) &&
		    vb2_is_busy(v4l2_m2m_get_dst_vq(c->fh.m2m_ctx)))
			return -EBUSY;
		c->codec = f->fmt.pix_mp.pixelformat;
		c->width = f->fmt.pix_mp.width;
		c->height = f->fmt.pix_mp.height;
	}
	return 0;
}
static int enum_size(struct file *file, void *fh, struct v4l2_frmsizeenum *f)
{
	if (f->index || (web_node(file) ? f->pixel_format != V4L2_PIX_FMT_H264 : (f->pixel_format != V4L2_PIX_FMT_H264_SLICE &&
	    (!vdh.hw.hevc_ready || f->pixel_format != V4L2_PIX_FMT_HEVC_SLICE))))
		return -EINVAL;
	f->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	f->stepwise = (struct v4l2_frmsize_stepwise){16, VDH_MAX_W, 16, 16, VDH_MAX_H, 16};
	return 0;
}
static int get_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct context *c = context(fh);
	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    (s->target != V4L2_SEL_TGT_COMPOSE && s->target != V4L2_SEL_TGT_COMPOSE_BOUNDS &&
	     s->target != V4L2_SEL_TGT_COMPOSE_DEFAULT && s->target != V4L2_SEL_TGT_CROP))
		return -EINVAL;
	s->r = (struct v4l2_rect){0, 0, c->width, c->height};
	if (c->avc && c->announced && s->target != V4L2_SEL_TGT_COMPOSE_BOUNDS) {
		s->r.width = c->avc->visible_w;
		s->r.height = c->avc->visible_h;
	}
	return 0;
}
static void release_display(struct context *c, unsigned int keep, enum vb2_buffer_state state)
{
	while (c->pending_count > keep) {
		struct vb2_v4l2_buffer *buf = c->pending[0].buffer;
		buf->sequence = c->output_sequence++;
		memmove(c->pending, c->pending + 1, (--c->pending_count) * sizeof(c->pending[0]));
		v4l2_m2m_buf_done(buf, state);
	}
}

static int decoder_command(struct file *file, void *fh, struct v4l2_decoder_cmd *cmd)
{
	struct context *c = context(fh);
	int ret;
	mutex_lock(&c->display_lock);
	if (cmd->cmd == V4L2_DEC_CMD_STOP && !v4l2_m2m_next_src_buf(c->fh.m2m_ctx) &&
	    c->pending_count && !c->fh.m2m_ctx->is_draining) {
		ret = v4l2_m2m_ioctl_try_decoder_cmd(file, fh, cmd);
		if (!ret) {
			const struct v4l2_event ev = { .type = V4L2_EVENT_EOS };
			c->pending[c->pending_count-1].buffer->flags |= V4L2_BUF_FLAG_LAST;
			release_display(c, 0, VB2_BUF_STATE_DONE);
			v4l2_m2m_mark_stopped(c->fh.m2m_ctx);
			v4l2_event_queue_fh(&c->fh, &ev);
		}
		mutex_unlock(&c->display_lock);
		return ret;
	}
	ret = v4l2_m2m_ioctl_decoder_cmd(file, fh, cmd);
	if (!ret && cmd->cmd == V4L2_DEC_CMD_STOP && c->fh.m2m_ctx->has_stopped) {
		const struct v4l2_event ev = { .type = V4L2_EVENT_EOS };
		v4l2_event_queue_fh(&c->fh, &ev);
	}
	mutex_unlock(&c->display_lock);
	return ret;
}
static const struct v4l2_ioctl_ops io = {
	.vidioc_querycap = querycap,
	.vidioc_enum_fmt_vid_cap = enum_fmt, .vidioc_enum_fmt_vid_out = enum_fmt,
	.vidioc_try_fmt_vid_cap_mplane = try_fmt, .vidioc_try_fmt_vid_out_mplane = try_fmt,
	.vidioc_g_fmt_vid_cap_mplane = get_fmt, .vidioc_g_fmt_vid_out_mplane = get_fmt,
	.vidioc_s_fmt_vid_cap_mplane = set_fmt, .vidioc_s_fmt_vid_out_mplane = set_fmt,
	.vidioc_enum_framesizes = enum_size, .vidioc_g_selection = get_selection,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs, .vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs, .vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf, .vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf, .vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
	.vidioc_decoder_cmd = decoder_command, .vidioc_try_decoder_cmd = v4l2_m2m_ioctl_try_decoder_cmd,
	.vidioc_subscribe_event = subscribe, .vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};
static int queue_setup(struct vb2_queue *q, unsigned int *n, unsigned int *planes,
		       unsigned int sizes[], struct device *devs[])
{
	struct context *c = q->drv_priv;
	u32 size = V4L2_TYPE_IS_OUTPUT(q->type) ? c->input_size : capture_bytes(c);
	if (*planes && (*planes != 1 || sizes[0] < size))
		return -EINVAL;
	if (c->avc && !V4L2_TYPE_IS_OUTPUT(q->type) && c->announced)
		*n = max(*n, c->avc->reorder + 3);
	*planes = 1;
	sizes[0] = size;
	return 0;
}
static int prepare(struct vb2_buffer *b)
{
	struct context *c = b->vb2_queue->drv_priv;
	if (V4L2_TYPE_IS_OUTPUT(b->type)) {
		if (b->planes[0].data_offset || vb2_get_plane_payload(b, 0) > c->input_size)
			return -EINVAL;
		if (c->check) {
			const u8 *slice = NULL;
			size_t n = 0;
			const u8 *data = vb2_plane_vaddr(b, 0);
			struct vdh_stream no_ref = { .last = -1 };
			int ret;
			if (!data) return -EINVAL;
			ret = mv100_avc_parse(c->check, data, vb2_get_plane_payload(b, 0),
					      &slice, &n, &no_ref);
			if (ret < 0 || n > VDH_MAX_NAL) {
				atomic_inc(&vdh.avc_refusals);
				return -EOPNOTSUPP;
			}
		}
	} else {
		u32 size = capture_bytes(c);
		if (vb2_plane_size(b, 0) < size)
			return -EINVAL;
		vb2_set_plane_payload(b, 0, 0);
	}
	to_vb2_v4l2_buffer(b)->field = V4L2_FIELD_NONE;
	return 0;
}
static int validate_output(struct vb2_buffer *b)
{
	to_vb2_v4l2_buffer(b)->field = V4L2_FIELD_NONE;
	return 0;
}
static void queue(struct vb2_buffer *b)
{
	struct context *c = b->vb2_queue->drv_priv;
	if (c->avc && V4L2_TYPE_IS_OUTPUT(b->type) && !c->announced) {
		const u8 *slice = NULL, *bytes = vb2_plane_vaddr(b, 0);
		size_t len = 0;
		int ret = bytes ? mv100_avc_parse(c->avc, bytes, vb2_get_plane_payload(b, 0),
						  &slice, &len, &c->stream) : -EINVAL;
		if (ret < 0) {
			vb2_queue_error(b->vb2_queue);
			v4l2_m2m_buf_done(to_vb2_v4l2_buffer(b), VB2_BUF_STATE_ERROR);
			return;
		}
		if (c->avc->has_sps) {
			struct v4l2_event ev = { .type = V4L2_EVENT_SOURCE_CHANGE,
				.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION };
			c->width = c->avc->width; c->height = c->avc->height;
			c->announced = true;
			v4l2_event_queue_fh(&c->fh, &ev);
		}
		if (!ret) {
			v4l2_m2m_buf_done(to_vb2_v4l2_buffer(b), VB2_BUF_STATE_DONE);
			return;
		}
	}
	if (c->avc && !V4L2_TYPE_IS_OUTPUT(b->type) && v4l2_m2m_dst_buf_is_last(c->fh.m2m_ctx)) {
		vb2_set_plane_payload(b, 0, 0);
		v4l2_m2m_last_buffer_done(c->fh.m2m_ctx, to_vb2_v4l2_buffer(b));
		return;
	}
	v4l2_m2m_buf_queue(c->fh.m2m_ctx, to_vb2_v4l2_buffer(b));
}
static void return_buffers(struct vb2_queue *q, enum vb2_buffer_state state)
{
	struct context *c = q->drv_priv;
	struct vb2_v4l2_buffer *buf;
	for (;;) {
		buf = V4L2_TYPE_IS_OUTPUT(q->type) ? v4l2_m2m_src_buf_remove(c->fh.m2m_ctx) :
						     v4l2_m2m_dst_buf_remove(c->fh.m2m_ctx);
		if (!buf)
			break;
		v4l2_m2m_buf_done(buf, state);
	}
}
static int start(struct vb2_queue *q, unsigned int count)
{
	struct context *c = q->drv_priv;
	if (c->avc && !V4L2_TYPE_IS_OUTPUT(q->type) && !c->announced) {
		return_buffers(q, VB2_BUF_STATE_QUEUED);
		return -EINVAL;
	}
	if (c->avc)
		v4l2_m2m_update_start_streaming_state(c->fh.m2m_ctx, q);
	if ((c->avc && !V4L2_TYPE_IS_OUTPUT(q->type)) || (!c->avc && V4L2_TYPE_IS_OUTPUT(q->type))) {
		int err = vdh.owner ? -EBUSY : vdh_start_stream(&vdh.hw, c->width, c->height);
		if (err) {
			return_buffers(q, VB2_BUF_STATE_QUEUED);
			return err;
		}
		vdh.owner = c;
		memset(&c->stream, 0, sizeof(c->stream));
		c->stream.last = -1;
		c->sequence = c->output_sequence = 0;
	}
	return 0;
}
static void stop(struct vb2_queue *q)
{
	struct context *c = q->drv_priv;
	mutex_lock(&c->display_lock);
	release_display(c, 0, VB2_BUF_STATE_ERROR);
	mutex_unlock(&c->display_lock);
	if (c->avc) v4l2_m2m_update_stop_streaming_state(c->fh.m2m_ctx, q);
	if (V4L2_TYPE_IS_OUTPUT(q->type) && c->avc) {
		c->announced = false;
		memset(c->avc, 0, 2 * sizeof(*c->avc));
	}
	if (vdh.owner == c && (c->avc || V4L2_TYPE_IS_OUTPUT(q->type))) {
		vdh_stop_stream(&vdh.hw);
		vdh.owner = NULL;
	}
	return_buffers(q, VB2_BUF_STATE_ERROR);
}
static void req_complete(struct vb2_buffer *b)
{
	struct context *c = b->vb2_queue->drv_priv;
	v4l2_ctrl_request_complete(b->req_obj.req, &c->controls);
}
static const struct vb2_ops buf_ops = {
	.buf_out_validate = validate_output, .queue_setup = queue_setup, .buf_prepare = prepare, .buf_queue = queue,
	.start_streaming = start, .stop_streaming = stop,
	.wait_prepare = vb2_ops_wait_prepare, .wait_finish = vb2_ops_wait_finish,
	.buf_request_complete = req_complete,
};
static int setup_queue(struct context *c, struct vb2_queue *q, bool src)
{
	q->type = src ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = c;
	q->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	q->mem_ops = &vb2_vmalloc_memops;
	q->ops = &buf_ops;
	q->dev = &vdh.pdev->dev;
	q->lock = &vdh.mutex;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	q->requires_requests = q->supports_requests = src && !c->avc;
	return vb2_queue_init(q);
}
static int setup_queues(void *ctx, struct vb2_queue *out, struct vb2_queue *cap)
{
	int ret = setup_queue(ctx, out, true);
	return ret ? ret : setup_queue(ctx, cap, false);
}
static int decode_access_unit(struct context *c, struct vb2_v4l2_buffer *src,
			      struct vb2_v4l2_buffer *dst)
{
	const u8 *data = vb2_plane_vaddr(&src->vb2_buf, 0);
	u8 *out = vb2_plane_vaddr(&dst->vb2_buf, 0);
	struct vdh_picture p = {
		.stateful = true, .sps = &c->avc->sps, .pps = &c->avc->pps, .decode = &c->avc->dec,
		.width = c->width, .height = c->height, .out = out,
		.timestamp = src->vb2_buf.timestamp,
	};
	int ret;
	if (vdh.owner != c || !vdh.hw.mem)
		return -EPIPE;
	if (!p.out || !data || !out || vb2_plane_size(&dst->vb2_buf, 0) < capture_bytes(c))
		return -EINVAL;
	ret = mv100_avc_parse(c->avc, data, vb2_get_plane_payload(&src->vb2_buf, 0),
			      &p.data, &p.bytes, &c->stream);
	if (ret <= 0)
		return ret ? ret : -ENODATA;
	return vdh_picture(&vdh.hw, &c->stream, &p);
}
static void run_work(struct work_struct *w)
{
	struct context *c = container_of(w, struct context, work);
	struct vb2_v4l2_buffer *src = v4l2_m2m_next_src_buf(c->fh.m2m_ctx);
	struct vb2_v4l2_buffer *dst = v4l2_m2m_dst_buf_remove(c->fh.m2m_ctx);
	struct media_request *req = src->vb2_buf.req_obj.req;
	enum vb2_buffer_state state = VB2_BUF_STATE_ERROR;
	int ret = c->avc ? decode_access_unit(c, src, dst) :
		v4l2_ctrl_request_setup(req, &c->controls);
	if (!c->avc && !ret) {
		struct vdh_picture p = {
			.sps = c->sps->p_cur.p_h264_sps, .pps = c->pps->p_cur.p_h264_pps,
			.decode = c->decode->p_cur.p_h264_decode_params,
			.data = vb2_plane_vaddr(&src->vb2_buf, 0),
			.bytes = vb2_get_plane_payload(&src->vb2_buf, 0),
			.out = vb2_plane_vaddr(&dst->vb2_buf, 0),
			.width = c->width, .height = c->height, .timestamp = src->vb2_buf.timestamp,
		};
		if (c->codec == V4L2_PIX_FMT_HEVC_SLICE) {
			p.hsps = c->hsps->p_cur.p_hevc_sps;
			p.hpps = c->hpps->p_cur.p_hevc_pps;
			p.hdec = c->hdec->p_cur.p;
			p.hsl = c->hsl->p_cur.p_hevc_slice_params;
			ret = vdh_hevc_picture(&vdh.hw, &c->stream, &p);
		} else {
			ret = vdh_picture(&vdh.hw, &c->stream, &p);
		}
	}
	mutex_lock(&c->display_lock);
	v4l2_m2m_src_buf_remove(c->fh.m2m_ctx);
	if (c->avc && ret == -ENODATA) {
		/* SPS/PPS/SEI can arrive as independent buffers. They produce no frame. */
		if (v4l2_m2m_is_last_draining_src_buf(c->fh.m2m_ctx, src)) {
			release_display(c, 0, VB2_BUF_STATE_DONE);
			vb2_set_plane_payload(&dst->vb2_buf, 0, 0);
			v4l2_m2m_last_buffer_done(c->fh.m2m_ctx, dst);
		} else {
			v4l2_m2m_buf_queue(c->fh.m2m_ctx, dst);
		}
		v4l2_m2m_buf_done(src, VB2_BUF_STATE_DONE);
		mutex_unlock(&c->display_lock);
		v4l2_m2m_job_finish(vdh.m2m, c->fh.m2m_ctx);
		return;
	}
	if (ret) {
		if (c->avc)
			dev_dbg_ratelimited(vdh.hw.dev, "AVC buffer %u length %lu status %d\n",
					     c->sequence, (unsigned long)vb2_get_plane_payload(&src->vb2_buf, 0), ret);
		if (c->avc) atomic_inc(&vdh.avc_refusals);
		vdh.hw.errors++;
		memset(&c->stream, 0, sizeof(c->stream));
		c->stream.last = -1;
		vb2_set_plane_payload(&dst->vb2_buf, 0, 0);
		dev_dbg(vdh.hw.dev, "request rejected: %d\n", ret);
	} else {
		state = VB2_BUF_STATE_DONE;
		vb2_set_plane_payload(&dst->vb2_buf, 0, capture_bytes(c));
	}
	v4l2_m2m_buf_copy_metadata(src, dst, true);
	dst->sequence = src->sequence = c->sequence++;
	if (!c->avc)
		v4l2_ctrl_request_complete(req, &c->controls);
	if (c->avc && !ret) {
		unsigned int pos = c->pending_count;
		if (c->stream.output_idr) {
			release_display(c, 0, VB2_BUF_STATE_DONE);
			pos = 0;
		}
		while (pos && c->pending[pos-1].poc > c->stream.output_poc) {
			c->pending[pos] = c->pending[pos-1];
			pos--;
		}
		c->pending[pos].buffer = dst;
		c->pending[pos].poc = c->stream.output_poc;
		c->pending_count++;
		if (v4l2_m2m_is_last_draining_src_buf(c->fh.m2m_ctx, src)) {
			const struct v4l2_event ev = { .type = V4L2_EVENT_EOS };
			c->pending[c->pending_count-1].buffer->flags |= V4L2_BUF_FLAG_LAST;
			release_display(c, 0, state);
			v4l2_m2m_mark_stopped(c->fh.m2m_ctx);
			v4l2_event_queue_fh(&c->fh, &ev);
		} else {
			release_display(c, c->avc->reorder, state);
		}
	} else {
		v4l2_m2m_buf_done(dst, state);
	}
	v4l2_m2m_buf_done(src, state);
	mutex_unlock(&c->display_lock);
	v4l2_m2m_job_finish(vdh.m2m, c->fh.m2m_ctx);
}
static void run(void *ctx)
{
	struct context *c = ctx;
	schedule_work(&c->work);
}
static int validate_request(struct media_request *req)
{
	if (vb2_request_buffer_cnt(req) != 1)
		return -EINVAL;
	return vb2_request_validate(req);
}
static const struct v4l2_m2m_ops m2m_ops = { .device_run = run };
static const struct media_device_ops media_ops = {
	.req_validate = validate_request, .req_queue = v4l2_m2m_request_queue,
};
static struct v4l2_ctrl *compound(struct context *c, u32 id)
{
	const struct v4l2_ctrl_config cfg = { .id = id };
	return v4l2_ctrl_new_custom(&c->controls, &cfg, NULL);
}
static int open_video(struct file *file)
{
	struct context *c = kzalloc(sizeof(*c), GFP_KERNEL);
	int ret;
	if (!c)
		return -ENOMEM;
	mutex_init(&c->display_lock);
	v4l2_fh_init(&c->fh, video_devdata(file));
	if (web_node(file)) {
		c->avc = kcalloc(2, sizeof(*c->avc), GFP_KERNEL);
		if (!c->avc) { v4l2_fh_exit(&c->fh); kfree(c); return -ENOMEM; }
		memset(&c->stream, 0, sizeof(c->stream));
		c->stream.last = -1;
		c->check = c->avc + 1;
	}
	c->input_size = VDH_MAX_NAL;
	v4l2_ctrl_handler_init(&c->controls, 15);
	c->sps = compound(c, V4L2_CID_STATELESS_H264_SPS);
	c->pps = compound(c, V4L2_CID_STATELESS_H264_PPS);
	c->decode = compound(c, V4L2_CID_STATELESS_H264_DECODE_PARAMS);
	compound(c, V4L2_CID_STATELESS_H264_SCALING_MATRIX);
	v4l2_ctrl_new_std_menu(&c->controls, NULL, V4L2_CID_STATELESS_H264_DECODE_MODE,
		V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED, BIT(0), V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED);
	v4l2_ctrl_new_std_menu(&c->controls, NULL, V4L2_CID_STATELESS_H264_START_CODE,
		V4L2_STATELESS_H264_START_CODE_ANNEX_B, BIT(0), V4L2_STATELESS_H264_START_CODE_ANNEX_B);
	v4l2_ctrl_new_std_menu(&c->controls, NULL, V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		vdh.hw.h264_cabac_ready ? V4L2_MPEG_VIDEO_H264_PROFILE_HIGH : V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE,
		BIT(V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED), V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE);
	if (vdh.hw.hevc_ready) {
		const struct v4l2_ctrl_config slice_cfg = { .id = V4L2_CID_STATELESS_HEVC_SLICE_PARAMS,
			.dims = {1}, .flags = V4L2_CTRL_FLAG_DYNAMIC_ARRAY };
		c->hsps = compound(c, V4L2_CID_STATELESS_HEVC_SPS);
		c->hpps = compound(c, V4L2_CID_STATELESS_HEVC_PPS);
		c->hdec = compound(c, V4L2_CID_STATELESS_HEVC_DECODE_PARAMS);
		c->hsl = v4l2_ctrl_new_custom(&c->controls, &slice_cfg, NULL);
		v4l2_ctrl_new_std_menu(&c->controls, NULL, V4L2_CID_STATELESS_HEVC_DECODE_MODE,
			V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED, BIT(0), V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED);
		v4l2_ctrl_new_std_menu(&c->controls, NULL, V4L2_CID_STATELESS_HEVC_START_CODE,
			V4L2_STATELESS_HEVC_START_CODE_NONE, 0, V4L2_STATELESS_HEVC_START_CODE_NONE);
		v4l2_ctrl_new_std_menu(&c->controls, NULL, V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
			V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN, 0, V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN);
	}
	ret = c->controls.error;
	if (ret)
		goto fail;
	c->fh.ctrl_handler = &c->controls;
	c->width = c->height = 64;
	c->codec = c->avc ? V4L2_PIX_FMT_H264 : V4L2_PIX_FMT_H264_SLICE;
	INIT_WORK(&c->work, run_work);
	c->fh.m2m_ctx = v4l2_m2m_ctx_init(vdh.m2m, c, setup_queues);
	if (IS_ERR(c->fh.m2m_ctx)) {
		ret = PTR_ERR(c->fh.m2m_ctx);
		goto fail;
	}
	file->private_data = &c->fh;
	v4l2_fh_add(&c->fh);
	return 0;
fail:
	v4l2_ctrl_handler_free(&c->controls);
	v4l2_fh_exit(&c->fh);
	kfree(c->avc);
	kfree(c);
	return ret;
}
static int close_video(struct file *file)
{
	struct context *c = context(file->private_data);
	mutex_lock(&vdh.mutex);
	v4l2_m2m_ctx_release(c->fh.m2m_ctx);
	mutex_unlock(&vdh.mutex);
	cancel_work_sync(&c->work);
	v4l2_fh_del(&c->fh);
	v4l2_ctrl_handler_free(&c->controls);
	v4l2_fh_exit(&c->fh);
	kfree(c->avc);
	kfree(c);
	return 0;
}
static const struct v4l2_file_operations fops = {
	.owner = THIS_MODULE, .open = open_video, .release = close_video,
	.poll = v4l2_m2m_fop_poll, .mmap = v4l2_m2m_fop_mmap, .unlocked_ioctl = video_ioctl2,
};
static int __init vdh_init(void)
{
	struct platform_device_info info = { .name = NAME, .id = -1, .dma_mask = DMA_BIT_MASK(32) };
	int ret;
	if (!of_machine_is_compatible("hisilicon,hi3798mv100"))
		return -ENODEV;
	vdh.pdev = platform_device_register_full(&info);
	if (IS_ERR(vdh.pdev))
		return PTR_ERR(vdh.pdev);
	ret = vdh_hw_init(&vdh.pdev->dev, &vdh.hw);
	if (ret)
		goto fail_dev;
	ret = devm_device_add_group(&vdh.pdev->dev, &vdh_stats);
	if (ret)
		goto fail_dev;
	mutex_init(&vdh.mutex);
	vdh.media.dev = &vdh.pdev->dev;
	strscpy(vdh.media.driver_name, NAME, sizeof(vdh.media.driver_name));
	strscpy(vdh.media.model, NAME, sizeof(vdh.media.model));
	strscpy(vdh.media.bus_info, "platform:" NAME, sizeof(vdh.media.bus_info));
	media_device_init(&vdh.media);
	vdh.media.ops = &media_ops;
	vdh.v4l2.mdev = &vdh.media;
	strscpy(vdh.v4l2.name, NAME, sizeof(vdh.v4l2.name));
	ret = v4l2_device_register(&vdh.pdev->dev, &vdh.v4l2);
	if (ret)
		goto fail_media;
	vdh.m2m = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(vdh.m2m)) {
		ret = PTR_ERR(vdh.m2m);
		goto fail_v4l;
	}
	strscpy(vdh.video.name, NAME, sizeof(vdh.video.name));
	vdh.video.v4l2_dev = &vdh.v4l2;
	vdh.video.lock = &vdh.mutex;
	vdh.video.fops = &fops;
	vdh.video.ioctl_ops = &io;
	vdh.video.release = video_device_release_empty;
	vdh.video.vfl_dir = VFL_DIR_M2M;
	vdh.video.device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	vdh.web = vdh.video;
	strscpy(vdh.web.name, "mv100-vdh-avc", sizeof(vdh.web.name));
	ret = video_register_device(&vdh.web, VFL_TYPE_VIDEO, -1);
	if (ret) goto fail_m2m;
	ret = video_register_device(&vdh.video, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto fail_web;
	ret = v4l2_m2m_register_media_controller(vdh.m2m, &vdh.video, MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret)
		goto fail_video;
	ret = media_device_register(&vdh.media);
	if (ret)
		goto fail_controller;
	vdh.debug = debugfs_create_dir(NAME, NULL);
	debugfs_create_ulong("decoded_frames", 0444, vdh.debug, &vdh.hw.frames);
	debugfs_create_ulong("rejected_frames", 0444, vdh.debug, &vdh.hw.errors);
	return 0;
fail_controller:
	v4l2_m2m_unregister_media_controller(vdh.m2m);
fail_video:
	video_unregister_device(&vdh.video);
fail_web:
	video_unregister_device(&vdh.web);
fail_m2m:
	v4l2_m2m_release(vdh.m2m);
fail_v4l:
	v4l2_device_unregister(&vdh.v4l2);
fail_media:
	media_device_cleanup(&vdh.media);
fail_dev:
	platform_device_unregister(vdh.pdev);
	return ret;
}
static void __exit vdh_exit(void)
{
	debugfs_remove_recursive(vdh.debug);
	media_device_unregister(&vdh.media);
	v4l2_m2m_unregister_media_controller(vdh.m2m);
	video_unregister_device(&vdh.video);
	video_unregister_device(&vdh.web);
	v4l2_m2m_release(vdh.m2m);
	v4l2_device_unregister(&vdh.v4l2);
	media_device_cleanup(&vdh.media);
	platform_device_unregister(vdh.pdev);
}
module_init(vdh_init);
module_exit(vdh_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi3798MV100 VDH H.264 and HEVC V4L2 Request decoder");
