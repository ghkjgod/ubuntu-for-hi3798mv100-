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

#define NAME "mv100-vdh"
struct context {
	struct v4l2_fh fh;
	struct v4l2_ctrl_handler controls;
	struct v4l2_ctrl *sps, *pps, *decode;
	struct vdh_stream stream;
	struct work_struct work;
	u32 width, height, sequence, codec;
	struct v4l2_ctrl *hsps, *hpps, *hsl, *hdec;
};
static struct {
	struct platform_device *pdev;
	struct v4l2_device v4l2;
	struct video_device video;
	struct media_device media;
	struct v4l2_m2m_dev *m2m;
	struct mutex mutex;
	struct vdh_hw hw;
	struct dentry *debug;
	struct context *owner;
} vdh;
static ssize_t decoded_frames_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", READ_ONCE(vdh.hw.frames));
}
static ssize_t rejected_frames_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", READ_ONCE(vdh.hw.errors));
}
static DEVICE_ATTR_RO(decoded_frames);
static DEVICE_ATTR_RO(rejected_frames);
static struct attribute *vdh_attributes[] = {
	&dev_attr_decoded_frames.attr, &dev_attr_rejected_frames.attr, NULL,
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
static int enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
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
	return 0;
}
static int try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct context *c = context(fh);
	u32 w = c->width, h = c->height;
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
	if (f->index || (f->pixel_format != V4L2_PIX_FMT_H264_SLICE &&
	    (!vdh.hw.hevc_ready || f->pixel_format != V4L2_PIX_FMT_HEVC_SLICE)))
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
	return 0;
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
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event, .vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};
static int queue_setup(struct vb2_queue *q, unsigned int *n, unsigned int *planes,
		       unsigned int sizes[], struct device *devs[])
{
	struct context *c = q->drv_priv;
	u32 size = V4L2_TYPE_IS_OUTPUT(q->type) ? VDH_MAX_NAL : c->width*c->height*3/2;
	if (*planes && (*planes != 1 || sizes[0] < size))
		return -EINVAL;
	*planes = 1;
	sizes[0] = size;
	return 0;
}
static int prepare(struct vb2_buffer *b)
{
	struct context *c = b->vb2_queue->drv_priv;
	if (V4L2_TYPE_IS_OUTPUT(b->type)) {
		if (b->planes[0].data_offset || vb2_get_plane_payload(b, 0) > VDH_MAX_NAL)
			return -EINVAL;
	} else {
		u32 size = c->width*c->height*3/2;
		if (vb2_plane_size(b, 0) < size)
			return -EINVAL;
		vb2_set_plane_payload(b, 0, size);
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
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		int err = vdh.owner ? -EBUSY : vdh_start_stream(&vdh.hw, c->width, c->height);
		if (err) {
			return_buffers(q, VB2_BUF_STATE_QUEUED);
			return err;
		}
		vdh.owner = c;
		c->stream.last = -1;
		c->sequence = 0;
	}
	return 0;
}
static void stop(struct vb2_queue *q)
{
	if (V4L2_TYPE_IS_OUTPUT(q->type) && vdh.owner == q->drv_priv) {
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
	q->requires_requests = q->supports_requests = src;
	return vb2_queue_init(q);
}
static int setup_queues(void *ctx, struct vb2_queue *out, struct vb2_queue *cap)
{
	int ret = setup_queue(ctx, out, true);
	return ret ? ret : setup_queue(ctx, cap, false);
}
static void run_work(struct work_struct *w)
{
	struct context *c = container_of(w, struct context, work);
	struct vb2_v4l2_buffer *src = v4l2_m2m_src_buf_remove(c->fh.m2m_ctx);
	struct vb2_v4l2_buffer *dst = v4l2_m2m_dst_buf_remove(c->fh.m2m_ctx);
	struct media_request *req = src->vb2_buf.req_obj.req;
	enum vb2_buffer_state state = VB2_BUF_STATE_ERROR;
	int ret = v4l2_ctrl_request_setup(req, &c->controls);
	if (!ret) {
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
	if (ret) {
		vdh.hw.errors++;
		c->stream.last = -1;
		vb2_set_plane_payload(&dst->vb2_buf, 0, 0);
		dev_dbg(vdh.hw.dev, "request rejected: %d\n", ret);
	} else {
		state = VB2_BUF_STATE_DONE;
	}
	v4l2_m2m_buf_copy_metadata(src, dst, true);
	dst->sequence = src->sequence = c->sequence++;
	v4l2_ctrl_request_complete(req, &c->controls);
	v4l2_m2m_buf_done(src, state);
	v4l2_m2m_buf_done(dst, state);
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
	v4l2_fh_init(&c->fh, &vdh.video);
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
	c->codec = V4L2_PIX_FMT_H264_SLICE;
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
	ret = video_register_device(&vdh.video, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto fail_m2m;
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
	v4l2_m2m_release(vdh.m2m);
	v4l2_device_unregister(&vdh.v4l2);
	media_device_cleanup(&vdh.media);
	platform_device_unregister(vdh.pdev);
}
module_init(vdh_init);
module_exit(vdh_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi3798MV100 VDH H.264 and HEVC V4L2 Request decoder");
