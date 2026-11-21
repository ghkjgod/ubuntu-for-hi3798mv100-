/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MV100_AVC_H
#define MV100_AVC_H
#include "vdh.h"

struct mv100_avc {
	struct v4l2_ctrl_h264_sps sps;
	struct v4l2_ctrl_h264_pps pps;
	struct v4l2_ctrl_h264_decode_params dec;
	unsigned int reorder, width, height, visible_w, visible_h;
	bool has_sps, has_pps;
};

/* Returns one for an access unit containing a picture and zero for headers. */
int mv100_avc_parse(struct mv100_avc *avc, const u8 *data, size_t size,
		    const u8 **slice, size_t *length, const struct vdh_stream *ref);
#endif
