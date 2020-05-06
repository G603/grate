/*
 * Copyright (c) 2012, 2013 Erik Faye-Lund
 * Copyright (c) 2013 Avionic Design GmbH
 * Copyright (c) 2013 Thierry Reding
 * Copyright (c) 2017 Dmitry Osipenko
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "grate.h"
#include "matrix.h"
#include "tgr_3d.xml.h"

#define ANIMATION_SPEED		60.0f

static const char *vertex_shader[] = {
	"attribute vec4 position;\n",
	"attribute vec2 texcoord;\n",
	"varying vec2 vtexcoord;\n",
	"uniform mat4 mvp;\n",
	"\n",
	"void main()\n",
	"{\n",
	"  gl_Position = position * mvp;\n",
	"  vtexcoord = texcoord;\n",
	"}\n"
};

static const char *fragment_shader[] = {
	"precision mediump float;\n",
	"varying vec2 vtexcoord;\n",
	"uniform sampler2D tex;\n",
	"\n",
	"void main()\n",
	"{\n",
	"  gl_FragColor = texture2D(tex, vtexcoord);\n",
	"}\n"
};

static const char *shader_linker =
	"LINK fp20, fp20, NOP, NOP, tram0.zwxx, export1"
;

static const float vertices[] = {
	/* front */
	-0.5f, -0.5f,  0.5f, 1.0f,
	 0.5f, -0.5f,  0.5f, 1.0f,
	 0.5f,  0.5f,  0.5f, 1.0f,
	-0.5f,  0.5f,  0.5f, 1.0f,
	/* back */
	-0.5f, -0.5f, -0.5f, 1.0f,
	 0.5f, -0.5f, -0.5f, 1.0f,
	 0.5f,  0.5f, -0.5f, 1.0f,
	-0.5f,  0.5f, -0.5f, 1.0f,
	/* left */
	-0.5f, -0.5f,  0.5f, 1.0f,
	-0.5f,  0.5f,  0.5f, 1.0f,
	-0.5f,  0.5f, -0.5f, 1.0f,
	-0.5f, -0.5f, -0.5f, 1.0f,
	/* right */
	 0.5f, -0.5f,  0.5f, 1.0f,
	 0.5f,  0.5f,  0.5f, 1.0f,
	 0.5f,  0.5f, -0.5f, 1.0f,
	 0.5f, -0.5f, -0.5f, 1.0f,
	/* top */
	-0.5f,  0.5f,  0.5f, 1.0f,
	 0.5f,  0.5f,  0.5f, 1.0f,
	 0.5f,  0.5f, -0.5f, 1.0f,
	-0.5f,  0.5f, -0.5f, 1.0f,
	/* bottom */
	-0.5f, -0.5f,  0.5f, 1.0f,
	 0.5f, -0.5f,  0.5f, 1.0f,
	 0.5f, -0.5f, -0.5f, 1.0f,
	-0.5f, -0.5f, -0.5f, 1.0f,
};

static const float uv[] = {
	/* front */
	0.0f, 0.0f,
	1.0f, 0.0f,
	1.0f, 1.0f,
	0.0f, 1.0f,
	/* back */
	1.0f, 0.0f,
	0.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,
	/* left */
	0.0f, 0.0f,
	1.0f, 0.0f,
	1.0f, 1.0f,
	0.0f, 1.0f,
	/* right */
	1.0f, 0.0f,
	0.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,
	/* top */
	0.0f, 0.0f,
	1.0f, 0.0f,
	1.0f, 1.0f,
	0.0f, 1.0f,
	/* bottom */
	1.0f, 0.0f,
	0.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,
};

static const unsigned short indices[] = {
	/* front */
	 0,  1,  2,
	 0,  2,  3,
	/* back */
	 6,  5,  4,
	 7,  6,  4,
	/* left */
	 8,  9, 10,
	 8, 10, 11,
	/* right */
	14, 13, 12,
	15, 14, 12,
	/* top */
	16, 17, 18,
	16, 18, 19,
	/* bottom */
	22, 21, 20,
	23, 22, 20,
};

int main(int argc, char *argv[])
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
	struct grate_program *program;
	struct grate_profile *profile;
	struct grate_framebuffer *fb;
	struct grate_shader *vs, *fs, *linker;
	struct grate_options options;
	struct grate *grate;
	struct grate_3d_ctx *ctx;
	struct grate_texture *texture;
	struct host1x_pixelbuffer *pixbuf;
	struct host1x_bo *bo;
	int location, mvp_loc;
	float aspect, elapsed;

	grate_init_data_path(argv[0]);

	if (!grate_parse_command_line(&options, argc, argv))
		return 1;

	grate = grate_init(&options);
	if (!grate)
		return 1;

	fb = grate_framebuffer_create(grate, options.width, options.height,
				      PIX_BUF_FMT_RGBA8888,
				      PIX_BUF_LAYOUT_TILED_16x16,
				      GRATE_DOUBLE_BUFFERED);
	if (!fb) {
		fprintf(stderr, "grate_framebuffer_create() failed\n");
		return 1;
	}

	aspect = options.width / (float)options.height;

	grate_clear_color(grate, 0.0f, 0.0f, 1.0f, 1.0f);
	grate_bind_framebuffer(grate, fb);

	/* Prepare shaders */

	vs = grate_shader_new(grate, GRATE_SHADER_VERTEX, vertex_shader,
			      ARRAY_SIZE(vertex_shader));
	fs = grate_shader_new(grate, GRATE_SHADER_FRAGMENT, fragment_shader,
			      ARRAY_SIZE(fragment_shader));
	linker = grate_shader_parse_linker_asm(shader_linker);

	program = grate_program_new(grate, vs, fs, linker);
	if (!program) {
		fprintf(stderr, "grate_program_new() failed\n");
		return 1;
	}

	grate_program_link(program);

	mvp_loc = grate_get_vertex_uniform_location(program, "mvp");

	/* Setup context */

	ctx = grate_3d_alloc_ctx(grate);

	grate_3d_ctx_bind_program(ctx, program);
	grate_3d_ctx_set_depth_range(ctx, 0.0f, 1.0f);
	grate_3d_ctx_set_dither(ctx, 0x779);
	grate_3d_ctx_set_point_params(ctx, 0x1401);
	grate_3d_ctx_set_point_size(ctx, 1.0f);
	grate_3d_ctx_set_line_params(ctx, 0x2);
	grate_3d_ctx_set_line_width(ctx, 1.0f);
	grate_3d_ctx_set_viewport_bias(ctx, 0.0f, 0.0f, 0.5f);
	grate_3d_ctx_set_viewport_scale(ctx, options.width, options.height, 0.5f);
	grate_3d_ctx_use_guardband(ctx, true);
	grate_3d_ctx_set_front_direction_is_cw(ctx, false);
	grate_3d_ctx_set_cull_face(ctx, GRATE_3D_CTX_CULL_FACE_BACK);
	grate_3d_ctx_set_scissor(ctx, 0, options.width, 0, options.height);
	grate_3d_ctx_set_point_coord_range(ctx, 0.0f, 1.0f, 0.0f, 1.0f);
	grate_3d_ctx_set_polygon_offset(ctx, 0.0f, 0.0f);
	grate_3d_ctx_set_provoking_vtx_last(ctx, true);

	/* Setup vertices attribute */

	location = grate_get_attribute_location(program, "position");
	bo = grate_create_attrib_bo_from_data(grate, vertices);
	grate_3d_ctx_vertex_attrib_float_pointer(ctx, location, 4, bo);
	grate_3d_ctx_enable_vertex_attrib_array(ctx, location);

	/* Setup texcoords attribute */

	location = grate_get_attribute_location(program, "texcoord");
	bo = grate_create_attrib_bo_from_data(grate, uv);
	grate_3d_ctx_vertex_attrib_float_pointer(ctx, location, 2, bo);
	grate_3d_ctx_enable_vertex_attrib_array(ctx, location);

	/* Setup render target */

	grate_3d_ctx_enable_render_target(ctx, 1);

	/* Setup texture */

	texture = grate_create_texture(grate, 300, 300,
				       PIX_BUF_FMT_RGBA8888,
				       PIX_BUF_LAYOUT_LINEAR);
	grate_texture_load(grate, texture, "data/tegra.png");
	grate_texture_set_wrap_s(texture, GRATE_TEXTURE_CLAMP_TO_EDGE);
	grate_texture_set_wrap_t(texture, GRATE_TEXTURE_CLAMP_TO_EDGE);
	grate_texture_set_min_filter(texture, GRATE_TEXTURE_NEAREST);
	grate_texture_set_mag_filter(texture, GRATE_TEXTURE_NEAREST);

	grate_3d_ctx_bind_texture(ctx, 0, texture);

	/* Create indices BO */

	bo = grate_create_attrib_bo_from_data(grate, indices);

	profile = grate_profile_start(grate);

	while (true) {
		struct mat4 mvp, modelview, projection, transform, result;

		grate_clear(grate);

		mat4_perspective(&projection, 60.0f, aspect, 1.0f, 1024.0f);
		mat4_identity(&modelview);

		mat4_rotate_x(&transform, x);
		mat4_multiply(&result, &modelview, &transform);
		mat4_rotate_y(&transform, y);
		mat4_multiply(&modelview, &result, &transform);
		mat4_rotate_z(&transform, z);
		mat4_multiply(&result, &modelview, &transform);
		mat4_translate(&transform, 0.0f, 0.0f, -2.0f);
		mat4_multiply(&modelview, &transform, &result);

		mat4_multiply(&mvp, &projection, &modelview);

		grate_3d_ctx_set_vertex_mat4_uniform(ctx, mvp_loc, &mvp);

		/* Setup render target */
		pixbuf = grate_get_draw_pixbuf(fb);
		grate_3d_ctx_bind_render_target(ctx, 1, pixbuf);

		grate_3d_draw_elements(ctx, TGR3D_PRIMITIVE_TYPE_TRIANGLES,
				       bo, TGR3D_INDEX_MODE_UINT16,
				       ARRAY_SIZE(indices));
		grate_flush(grate);
		grate_swap_buffers(grate);

		if (grate_key_pressed(grate))
			break;

		grate_profile_sample(profile);

		elapsed = grate_profile_time_elapsed(profile);

		x = 0.3f * ANIMATION_SPEED * elapsed;
		y = 0.2f * ANIMATION_SPEED * elapsed;
		z = 0.4f * ANIMATION_SPEED * elapsed;
	}

	grate_profile_finish(profile);
	grate_profile_free(profile);

	grate_exit(grate);
	return 0;
}
