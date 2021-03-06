/*
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <linux/videodev2.h>
#include <linux/media.h>

#include "cedrus-frame-test.h"

#define SOURCE_SIZE_MAX						(1024 * 1024)

static bool find_format(int video_fd, unsigned int type, unsigned int pixelformat)
{
	struct v4l2_fmtdesc fmtdesc;
	int rc;

	memset(&fmtdesc, 0, sizeof(fmtdesc));
	fmtdesc.type = type;
	fmtdesc.index = 0;

	do {
		rc = ioctl(video_fd, VIDIOC_ENUM_FMT, &fmtdesc);
		if (rc < 0)
			break;

		if (fmtdesc.pixelformat == pixelformat)
			return true;

		fmtdesc.index++;
	} while (rc >= 0);

	return false;
}

static int try_format(int video_fd, unsigned int type, unsigned int width, unsigned int height, unsigned int pixelformat)
{
	struct v4l2_format format;
	int rc;

	memset(&format, 0, sizeof(format));
	format.type = type;
	format.fmt.pix_mp.width = width;
	format.fmt.pix_mp.height = height;
	format.fmt.pix_mp.plane_fmt[0].sizeimage = type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ? SOURCE_SIZE_MAX : 0;
	format.fmt.pix_mp.pixelformat = pixelformat;

	rc = ioctl(video_fd, VIDIOC_TRY_FMT, &format);
	if (rc < 0) {
		fprintf(stderr, "Unable to try format for type %d: %s\n", type, strerror(errno));
		return -1;
	}

	return 0;
}

static int set_format(int video_fd, unsigned int type, unsigned int width, unsigned int height, unsigned int pixelformat)
{
	struct v4l2_format format;
	int rc;

	memset(&format, 0, sizeof(format));
	format.type = type;
	format.fmt.pix_mp.width = width;
	format.fmt.pix_mp.height = height;
	format.fmt.pix_mp.plane_fmt[0].sizeimage = type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ? SOURCE_SIZE_MAX : 0;
	format.fmt.pix_mp.pixelformat = pixelformat;

	rc = ioctl(video_fd, VIDIOC_S_FMT, &format);
	if (rc < 0) {
		fprintf(stderr, "Unable to set format for type %d: %s\n", type, strerror(errno));
		return -1;
	}

	return 0;
}

static int get_format(int video_fd, unsigned int type, unsigned int *width, unsigned int *height, unsigned int *bytesperline, unsigned int *sizes, unsigned int *planes_count)
{
	struct v4l2_format format;
	unsigned int count;
	unsigned int i;
	int rc;

	memset(&format, 0, sizeof(format));
	format.type = type;

	rc = ioctl(video_fd, VIDIOC_G_FMT, &format);
	if (rc < 0) {
		fprintf(stderr, "Unable to get format for type %d: %s\n", type, strerror(errno));
		return -1;
	}

	count = format.fmt.pix_mp.num_planes;

	if (width != NULL)
		*width = format.fmt.pix_mp.width;

	if (height != NULL)
		*height = format.fmt.pix_mp.height;

	if (planes_count != NULL)
		if (*planes_count > 0 && *planes_count < count)
			count = *planes_count;

	if (bytesperline != NULL)
		for (i = 0; i < count; i++)
			bytesperline[i] = format.fmt.pix_mp.plane_fmt[i].bytesperline;

	if (sizes != NULL)
		for (i = 0; i < count; i++)
			sizes[i] = format.fmt.pix_mp.plane_fmt[i].sizeimage;

	if (planes_count != NULL)
		*planes_count = count;

	return 0;
}

static int create_buffers(int video_fd, unsigned int type, unsigned int buffers_count)
{
	struct v4l2_create_buffers buffers;
	int rc;

	memset(&buffers, 0, sizeof(buffers));
	buffers.format.type = type;
	buffers.memory = V4L2_MEMORY_MMAP;
	buffers.count = buffers_count;

	rc = ioctl(video_fd, VIDIOC_G_FMT, &buffers.format);
	if (rc < 0) {
		fprintf(stderr, "Unable to get format for type %d: %s\n", type, strerror(errno));
		return -1;
	}

	rc = ioctl(video_fd, VIDIOC_CREATE_BUFS, &buffers);
	if (rc < 0) {
		fprintf(stderr, "Unable to create buffer for type %d: %s\n", type, strerror(errno));
		return -1;
	}

	return 0;
}

static int request_buffer(int video_fd, unsigned int type, unsigned int index, unsigned int *lengths, unsigned int *offsets, unsigned int buffers_count)
{
	struct v4l2_plane planes[buffers_count];
	struct v4l2_buffer buffer;
	unsigned int i;
	int rc;

	memset(planes, 0, sizeof(planes));
	memset(&buffer, 0, sizeof(buffer));

	buffer.type = type;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.index = index;
	buffer.length = buffers_count;
	buffer.m.planes = planes;

	rc = ioctl(video_fd, VIDIOC_QUERYBUF, &buffer);
	if (rc < 0) {
		fprintf(stderr, "Unable to query buffer: %s\n", strerror(errno));
		return -1;
	}

	if (lengths != NULL)
		for (i = 0; i < buffer.length; i++)
			lengths[i] = buffer.m.planes[i].length;

	if (offsets != NULL)
		for (i = 0; i < buffer.length; i++)
			offsets[i] = buffer.m.planes[i].m.mem_offset;

	return 0;
}

static int queue_buffer(int video_fd, int request_fd, unsigned int type, unsigned int index, unsigned int size, unsigned int buffers_count)
{
	struct v4l2_plane planes[buffers_count];
	struct v4l2_buffer buffer;
	unsigned int i;
	int rc;

	memset(planes, 0, sizeof(planes));
	memset(&buffer, 0, sizeof(buffer));

	buffer.type = type;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.index = index;
	buffer.length = buffers_count;
	buffer.m.planes = planes;

	for (i = 0; i < buffers_count; i++)
		buffer.m.planes[i].bytesused = size;

	if (request_fd >= 0) {
		buffer.flags = V4L2_BUF_FLAG_REQUEST_FD;
		buffer.request_fd = request_fd;
	}

	rc = ioctl(video_fd, VIDIOC_QBUF, &buffer);
	if (rc < 0) {
		fprintf(stderr, "Unable to queue buffer: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int dequeue_buffer(int video_fd, int request_fd, unsigned int type, unsigned int index, unsigned int buffers_count)
{
	struct v4l2_plane planes[buffers_count];
	struct v4l2_buffer buffer;
	int rc;

	memset(planes, 0, sizeof(planes));
	memset(&buffer, 0, sizeof(buffer));

	buffer.type = type;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.index = index;
	buffer.length = buffers_count;
	buffer.m.planes = planes;

	if (request_fd >= 0) {
		buffer.flags = V4L2_BUF_FLAG_REQUEST_FD;
		buffer.request_fd = request_fd;
	}

	rc = ioctl(video_fd, VIDIOC_DQBUF, &buffer);
	if (rc < 0) {
		fprintf(stderr, "Unable to dequeue buffer: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int export_buffer(int video_fd, unsigned int type, unsigned int index, unsigned int flags, int *export_fds, unsigned int export_fds_count)
{
	struct v4l2_exportbuffer exportbuffer;
	unsigned int i;
	int rc;

	for (i = 0; i < export_fds_count; i++) {
		memset(&exportbuffer, 0, sizeof(exportbuffer));
		exportbuffer.type = type;
		exportbuffer.index = index;
		exportbuffer.plane = i;
		exportbuffer.flags = flags;

		rc = ioctl(video_fd, VIDIOC_EXPBUF, &exportbuffer);
		if (rc < 0) {
			fprintf(stderr, "Unable to export buffer: %s\n", strerror(errno));
			return -1;
		}

		export_fds[i] = exportbuffer.fd;
	}

	return 0;
}

static int set_control(int video_fd, int request_fd, unsigned int id, void *data, unsigned int size)
{
	struct v4l2_ext_control control;
	struct v4l2_ext_controls controls;
	int rc;

	memset(&control, 0, sizeof(control));
	memset(&controls, 0, sizeof(controls));

	control.id = id;
	control.ptr = data;
	control.size = size;

	controls.controls = &control;
	controls.count = 1;

	if (request_fd >= 0) {
		controls.which = V4L2_CTRL_WHICH_REQUEST_VAL;
		controls.request_fd = request_fd;
	}

	rc = ioctl(video_fd, VIDIOC_S_EXT_CTRLS, &controls);
	if (rc < 0) {
		fprintf(stderr, "Unable to set control: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int set_stream(int video_fd, unsigned int type, bool enable)
{
	enum v4l2_buf_type buf_type = type;
	int rc;

	rc = ioctl(video_fd, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &buf_type);
	if (rc < 0) {
		fprintf(stderr, "Unable to %sable stream: %s\n", enable ? "en" : "dis", strerror(errno));
		return -1;
	}

	return 0;
}

static int set_format_controls(int video_fd, int request_fd, enum codec_type type, union controls *frame)
{
	struct {
		enum codec_type type;
		char *description;
		unsigned int id;
		void *data;
		unsigned int size;
	} glue[] = {
		{ CODEC_TYPE_MPEG2, "slice parameters", V4L2_CID_MPEG_VIDEO_MPEG2_SLICE_PARAMS, &frame->mpeg2.slice_params, sizeof(frame->mpeg2.slice_params) },
		{ CODEC_TYPE_H264, "decode parameters", V4L2_CID_MPEG_VIDEO_H264_DECODE_PARAM, &frame->h264.decode_param, sizeof(frame->h264.decode_param) },
		{ CODEC_TYPE_H264, "picture parameter set", V4L2_CID_MPEG_VIDEO_H264_PPS, &frame->h264.pps, sizeof(frame->h264.pps) },
		{ CODEC_TYPE_H264, "sequence parameter set", V4L2_CID_MPEG_VIDEO_H264_SPS, &frame->h264.sps, sizeof(frame->h264.sps) },
		{ CODEC_TYPE_H264, "scaling matrix", V4L2_CID_MPEG_VIDEO_H264_SCALING_MATRIX, &frame->h264.scaling_matrix, sizeof(frame->h264.scaling_matrix) },
		{ CODEC_TYPE_H264, "scaling matrix", V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAM, &frame->h264.slice_param, sizeof(frame->h264.slice_param) },
	};
	unsigned int glue_count = sizeof(glue) / sizeof(glue[0]);
	unsigned int i;
	int rc;

	for (i = 0; i < glue_count; i++) {
		if (glue[i].type != type)
			continue;

		rc = set_control(video_fd, request_fd, glue[i].id, glue[i].data, glue[i].size);
		if (rc < 0) {
			fprintf(stderr, "Unable to set %s control\n", glue[i].description);
			return -1;
		}
	}

	return 0;
}

static int codec_source_format(enum codec_type type)
{
	switch (type) {
	case CODEC_TYPE_MPEG2:
		return V4L2_PIX_FMT_MPEG2_SLICE;
	case CODEC_TYPE_H264:
		return V4L2_PIX_FMT_H264_SLICE;
	default:
		fprintf(stderr, "Invalid format type\n");
		return -1;
	}
}

bool video_engine_format_test(int video_fd, unsigned int width, unsigned int height, unsigned int format)
{
	int rc;

	rc = try_format(video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, width, height, format);

	return rc >= 0;
}

int video_engine_start(int video_fd, int media_fd, unsigned int width, unsigned int height, struct format_description *format, enum codec_type type, struct video_buffer **buffers, unsigned int buffers_count)
{
	struct media_request_alloc request_alloc;
	struct video_buffer *buffer;
	unsigned int source_format;
	unsigned int source_length;
	unsigned int source_map_offset;
	unsigned int destination_format;
	void *destination_map[VIDEO_MAX_PLANES];
	unsigned int destination_map_lengths[VIDEO_MAX_PLANES];
	unsigned int destination_map_offsets[VIDEO_MAX_PLANES];
	unsigned int destination_sizes[VIDEO_MAX_PLANES];
	unsigned int destination_bytesperlines[VIDEO_MAX_PLANES];
	unsigned int destination_planes_count;
	unsigned int export_fds_count;
	unsigned int i, j;
	int rc;

	*buffers = malloc(buffers_count * sizeof(**buffers));
	memset(*buffers, 0, buffers_count * sizeof(**buffers));

	source_format = codec_source_format(type);

	rc = set_format(video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, width, height, source_format);
	if (rc < 0) {
		fprintf(stderr, "Unable to set source format\n");
		goto error;
	}

	destination_format = format->v4l2_format;

	rc = set_format(video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, width, height, destination_format);
	if (rc < 0) {
		fprintf(stderr, "Unable to set destination format\n");
		goto error;
	}

	destination_planes_count = 0;

	rc = get_format(video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, NULL, NULL, destination_bytesperlines, destination_sizes, &destination_planes_count);
	if (rc < 0) {
		fprintf(stderr, "Unable to get destination format\n");
		goto error;
	}

	rc = create_buffers(video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, buffers_count);
	if (rc < 0) {
		fprintf(stderr, "Unable to create source buffers\n");
		goto error;
	}

	for (i = 0; i < buffers_count; i++) {
		buffer = &((*buffers)[i]);

		rc = request_buffer(video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, i, &source_length, &source_map_offset, 1);
		if (rc < 0) {
			fprintf(stderr, "Unable to request source buffer\n");
			goto error;
		}

		buffer->source_map = mmap(NULL, source_length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, source_map_offset);
		if (buffer->source_map == MAP_FAILED) {
			fprintf(stderr, "Unable to map source buffer\n");
			goto error;
		}

		buffer->source_data = buffer->source_map;
		buffer->source_size = source_length;
	}

	rc = create_buffers(video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, buffers_count);
	if (rc < 0) {
		fprintf(stderr, "Unable to create destination buffers\n");
		goto error;
	}

	for (i = 0; i < buffers_count; i++) {
		buffer = &((*buffers)[i]);

		rc = request_buffer(video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, i, destination_map_lengths, destination_map_offsets, format->v4l2_buffers_count);
		if (rc < 0) {
			fprintf(stderr, "Unable to request destination buffer\n");
			goto error;
		}

		for (j = 0; j < format->v4l2_buffers_count; j++) {
			destination_map[j] = mmap(NULL, destination_map_lengths[j], PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, destination_map_offsets[j]);
			if (destination_map[j] == MAP_FAILED) {
				fprintf(stderr, "Unable to map destination buffer\n");
				goto error;
			}
		}

		if (format->v4l2_buffers_count == 1) {
			for (j = 0; j < destination_planes_count; j++) {
				buffer->destination_map[j] = j == 0 ? destination_map[0] : NULL;
				buffer->destination_map_lengths[j] = j == 0 ? destination_map_lengths[0] : 0;
				buffer->destination_offsets[j] = j > 0 ? destination_sizes[j-1] : 0;
				buffer->destination_data[j] = (void *) ((unsigned char *) destination_map[0] + buffer->destination_offsets[j]);
				buffer->destination_sizes[j] = destination_sizes[j];
				buffer->destination_bytesperlines[j] = destination_bytesperlines[j];
			}
		} else if (format->v4l2_buffers_count == destination_planes_count) {
			for (j = 0; j < destination_planes_count; j++) {
				buffer->destination_map[j] = destination_map[j];
				buffer->destination_map_lengths[j] = destination_map_lengths[j];
				buffer->destination_offsets[j] = 0;
				buffer->destination_data[j] = destination_map[j];
				buffer->destination_sizes[j] = destination_sizes[j];
				buffer->destination_bytesperlines[j] = destination_bytesperlines[j];
			}
		} else {
			fprintf(stderr, "Unsupported combination of %d buffers with %d planes\n", format->v4l2_buffers_count, destination_planes_count);
			goto error;
		}

		buffer->destination_planes_count = destination_planes_count;
		buffer->destination_buffers_count = format->v4l2_buffers_count;
		export_fds_count = format->v4l2_buffers_count;

		for (j = 0; j < export_fds_count; j++)
			buffer->export_fds[j] = -1;

		rc = export_buffer(video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, i, O_RDONLY, buffer->export_fds, export_fds_count);
		if (rc < 0) {
			fprintf(stderr, "Unable to export destination buffer\n");
			goto error;
		}

		rc = ioctl(media_fd, MEDIA_IOC_REQUEST_ALLOC, &request_alloc);
		if (rc < 0) {
			fprintf(stderr, "Unable to allocate media request: %s\n", strerror(errno));
			goto error;
		}

		buffer->request_fd = request_alloc.fd;
	}

	rc = set_stream(video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, true);
	if (rc < 0) {
		fprintf(stderr, "Unable to enable source stream\n");
		goto error;
	}

	rc = set_stream(video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, true);
	if (rc < 0) {
		fprintf(stderr, "Unable to enable destination stream\n");
		goto error;
	}

	rc = 0;
	goto complete;

error:
	free(*buffers);
	*buffers = NULL;

complete:
	return rc;
}

int video_engine_stop(int video_fd, struct video_buffer *buffers, unsigned int buffers_count)
{
	unsigned int i, j;
	int rc;

	rc = set_stream(video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, false);
	if (rc < 0) {
		fprintf(stderr, "Unable to enable source stream\n");
		return -1;
	}

	rc = set_stream(video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, false);
	if (rc < 0) {
		fprintf(stderr, "Unable to enable destination stream\n");
		return -1;
	}

	for (i = 0; i < buffers_count; i++) {
		munmap(buffers[i].source_data, buffers[i].source_size);

		for (j = 0; j < buffers[i].destination_buffers_count; j++) {
			if (buffers[i].destination_map[j] == NULL)
				break;

			munmap(buffers[i].destination_map[j], buffers[i].destination_map_lengths[j]);

			if (buffers[i].export_fds[j] >= 0)
				close(buffers[i].export_fds[j]);
		}

		for (j = 0; j < buffers[i].destination_buffers_count; j++) {
			if (buffers[i].export_fds[j] < 0)
				break;

			close(buffers[i].export_fds[j]);
		}

		close(buffers[i].request_fd);
	}

	free(buffers);

	return 0;
}

int video_engine_decode(int video_fd, unsigned int index, union controls *frame, enum codec_type type, void *source_data, unsigned int source_size, struct video_buffer *buffers)
{
	struct timeval tv = { 0, 300000 };
	int request_fd = -1;
        fd_set except_fds;
	int rc;

	request_fd = buffers[index].request_fd;

	memcpy(buffers[index].source_data, source_data, source_size);

	rc = set_format_controls(video_fd, request_fd, type, frame);
	if (rc < 0) {
		fprintf(stderr, "Unable to set format controls\n");
		return -1;
	}

	rc = queue_buffer(video_fd, request_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, index, source_size, 1);
	if (rc < 0) {
		fprintf(stderr, "Unable to queue source buffer\n");
		return -1;
	}

	rc = queue_buffer(video_fd, -1, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, index, 0, buffers[index].destination_buffers_count);
	if (rc < 0) {
		fprintf(stderr, "Unable to queue destination buffer\n");
		return -1;
	}

	rc = ioctl(request_fd, MEDIA_REQUEST_IOC_QUEUE, NULL);
	if (rc < 0) {
		fprintf(stderr, "Unable to queue media request: %s\n", strerror(errno));
		return -1;
	}

	FD_ZERO(&except_fds);
	FD_SET(request_fd, &except_fds);

	rc = select(request_fd + 1, NULL, NULL, &except_fds, &tv);
	if (rc == 0) {
		fprintf(stderr, "Timeout when waiting for media request\n");
		return -1;
	} else if (rc < 0) {
		fprintf(stderr, "Unable to select media request: %s\n", strerror(errno));
		return -1;
	}

	rc = dequeue_buffer(video_fd, -1, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, index, 1);
	if (rc < 0) {
		fprintf(stderr, "Unable to dequeue source buffer\n");
		return -1;
	}

	rc = dequeue_buffer(video_fd, -1, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, index, buffers[index].destination_buffers_count);
	if (rc < 0) {
		fprintf(stderr, "Unable to dequeue destination buffer\n");
		return -1;
	}

	rc = ioctl(request_fd, MEDIA_REQUEST_IOC_REINIT, NULL);
	if (rc < 0) {
		fprintf(stderr, "Unable to reinit media request: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}
