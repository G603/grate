/*
 * Copyright (c) 2012, 2013 Erik Faye-Lund
 * Copyright (c) 2013 Avionic Design GmbH
 * Copyright (c) 2013 Thierry Reding
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

#define _GNU_SOURCE

#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "libgrate-private.h"
#include "host1x.h"
#include "libcgc.h"
#include "grate.h"
#include "asm.h"

void grate_shader_emit(struct host1x_pushbuf *pb, struct grate_shader *shader)
{
	unsigned int i;

	for (i = 0; i < shader->num_words; i++)
		host1x_pushbuf_push(pb, shader->words[i]);
}

struct grate_shader *grate_shader_new(struct grate *grate,
				      enum grate_shader_type type,
				      const char *lines[],
				      unsigned int count)
{
	size_t length = 0, offset = 0, len;
	enum cgc_shader_type shader_type;
	struct cgc_fragment_shader *fs;
	struct cgc_vertex_shader *vs;
	struct grate_shader *shader;
	struct cgc_header *header;
	struct cgc_shader *cgc;
	unsigned int i;
	size_t size;
	char *code;

	switch (type) {
	case GRATE_SHADER_VERTEX:
		shader_type = CGC_SHADER_VERTEX;
		break;

	case GRATE_SHADER_FRAGMENT:
		shader_type = CGC_SHADER_FRAGMENT;
		break;

	default:
		return NULL;
	}

	for (i = 0; i < count; i++)
		length += strlen(lines[i]);

	code = malloc(length + 1);
	if (!code)
		return NULL;

	for (i = 0; i < count; i++) {
		len = strlen(lines[i]);
		strncpy(code + offset, lines[i], len);
		offset += len;
	}

	code[length] = '\0';

	cgc = cgc_compile(shader_type, code, length);
	if (!cgc) {
		free(code);
		return NULL;
	}

	//cgc_shader_dump(cgc, stdout);

	shader = calloc(1, sizeof(*shader));
	if (!shader) {
		free(code);
		return NULL;
	}

	shader->cgc = cgc;

	switch (cgc->type) {
	case CGC_SHADER_VERTEX:
		vs = cgc->stream;
		fprintf(stdout, "DEBUG: vertex shader: %u words\n", vs->unknownec / 4);
		shader->words = cgc->stream + vs->unknowne8 * 4;
		shader->num_words = vs->unknownec / 4;
		break;

	case CGC_SHADER_FRAGMENT:
		header = cgc->binary;

		fs = cgc->binary + header->binary_offset;
		size = header->binary_size - sizeof(*fs);

		fprintf(stdout, "DEBUG: fragment shader: %zu words\n", size / 4);
		shader->num_words = size / 4;
		shader->words = fs->words;
		break;

	default:
		shader->num_words = 0;
		shader->words = NULL;
		break;
	}

	free(code);

	return shader;
}

static void grate_free_asm_shader(struct grate_shader *shader)
{
	free(shader->words);
	free(shader->cgc->symbols);
	free(shader->cgc);
}

void grate_shader_free(struct grate_shader *shader)
{
	if (shader) {
		switch (shader->cgc->type) {
		case CGC_SHADER_VERTEX:
		case CGC_SHADER_FRAGMENT:
			cgc_shader_free(shader->cgc);
			break;

		default:
			grate_free_asm_shader(shader);
			break;
		}
	}

	free(shader);
}

struct grate_program *grate_program_new(struct grate *grate,
					struct grate_shader *vs,
					struct grate_shader *fs,
					struct grate_shader *linker)
{
	struct grate_program *program;

	program = calloc(1, sizeof(*program));
	if (!program)
		return NULL;

	program->vs = vs;
	program->fs = fs;
	program->linker = linker;

	return program;
}

void grate_program_free(struct grate_program *program)
{
	if (program) {
		grate_shader_free(program->fs);
		grate_shader_free(program->vs);
	}

	free(program);
}

static void grate_program_add_attribute(struct grate_program *program,
					struct cgc_symbol *symbol)
{
	struct grate_attribute *attribute;
	size_t size;

	size = (program->num_attributes + 1) * sizeof(*attribute);

	attribute = realloc(program->attributes, size);
	if (!attribute) {
		fprintf(stderr, "ERROR: failed to allocate attribute\n");
		return;
	}

	program->attributes = attribute;

	attribute = &program->attributes[program->num_attributes];
	attribute->position = symbol->location;
	attribute->name = symbol->name;

	program->num_attributes++;
}

static void grate_program_add_uniform(struct grate_program *program,
				      struct cgc_symbol *symbol)
{
	struct grate_uniform *uniform;
	size_t size;

	size = (program->num_uniforms + 1) * sizeof(*uniform);

	uniform = realloc(program->uniforms, size);
	if (!uniform) {
		fprintf(stderr, "ERROR: failed to allocate uniform\n");
		return;
	}

	program->uniforms = uniform;

	uniform = &program->uniforms[program->num_uniforms];
	uniform->position = symbol->location;
	uniform->name = symbol->name;

	program->num_uniforms++;
}

void grate_program_link(struct grate_program *program)
{
	struct cgc_shader *shader;
	unsigned int i;

	if (!program->vs || !program->fs)
		return;

	shader = program->vs->cgc;

	for (i = 0; i < shader->num_symbols; i++) {
		struct cgc_symbol *symbol = &shader->symbols[i];
		uint32_t attr_mask = (1 << symbol->location);

		if (symbol->location == -1)
			continue;

		switch (symbol->kind) {
		case GLSL_KIND_ATTRIBUTE:
			printf("attribute %s @%u", symbol->name,
			       symbol->location);

			if (symbol->input) {
				grate_program_add_attribute(program, symbol);
				program->attributes_mask |= attr_mask << 16;
			} else {
				program->attributes_mask |= attr_mask;
			}

			break;

		case GLSL_KIND_UNIFORM:
			printf("uniform %s @%u", symbol->name,
			       symbol->location);

			grate_program_add_uniform(program, symbol);
			break;

		case GLSL_KIND_CONSTANT:
			printf("constant %s @%u", symbol->name,
			       symbol->location);

			memcpy(&program->uniform[symbol->location * 4],
			       symbol->vector, 16);

			break;

		default:
			break;
		}

		if (symbol->input)
			printf(" (input)");
		else
			printf(" (output)");

		if (!symbol->used)
			printf(" (unused)");

		printf("\n");
	}

	shader = program->fs->cgc;

	for (i = 0; i < shader->num_symbols; i++) {
		struct cgc_symbol *symbol = &shader->symbols[i];

		if (symbol->location == -1)
			continue;

		switch (symbol->kind) {
		case GLSL_KIND_ATTRIBUTE:
			printf("attribute %s @%u", symbol->name,
			       symbol->location);
			break;

		case GLSL_KIND_UNIFORM:
			printf("uniform %s @%u", symbol->name,
			       symbol->location);
			break;

		case GLSL_KIND_CONSTANT:
			printf("constant %s @%u", symbol->name,
			       symbol->location);

			if (strncmp(symbol->name, "asm-constant", 12) == 0)
				program->fs_uniform[symbol->location] = symbol->vector[0];
			break;

		default:
			break;
		}

		if (symbol->input)
			printf(" (input)");
		else
			printf(" (output)");

		if (!symbol->used)
			printf(" (unused)");

		printf("\n");
	}
}

struct grate_shader *grate_shader_parse_vertex_asm(const char *asm_txt)
{
	struct grate_shader *shader;
	struct cgc_symbol *symbols;
	struct cgc_shader *cgc;
	static char *locale;
	int words = 0;
	int err;
	int i;

	locale = strdup( setlocale(LC_ALL, NULL) );
	if (!locale)
		return NULL;

	/* atof() delimiter is locale-dependent! */
	setlocale(LC_ALL, "C");

	vertex_asm_scan_string(asm_txt);
	err = vertex_asmparse();

	/* restore locale */
	setlocale(LC_ALL, locale);
	free(locale);

	if (err != 0)
		return NULL;

	if (asm_vs_instructions_nb == 0) {
		fprintf(stderr, "ERROR: no vertex instructions generated");
		return NULL;
	}

	shader = calloc(1, sizeof(*shader));
	if (!shader)
		return NULL;

	shader->num_words = 2 + 4 * asm_vs_instructions_nb;
	shader->words = malloc(shader->num_words * 4);
	if (!shader->words) {
		free(shader);
		return NULL;
	}

	asm_vs_instructions[asm_vs_instructions_nb - 1].end_of_program = 1;

	shader->words[words++] = HOST1X_OPCODE_IMM(0x205, 0x00);

	shader->words[words++] =
		HOST1X_OPCODE_NONINCR(0x206, asm_vs_instructions_nb * 4);
	for (i = 0; i < asm_vs_instructions_nb; i++) {
		shader->words[words++] = asm_vs_instructions[i].part3;
		shader->words[words++] = asm_vs_instructions[i].part2;
		shader->words[words++] = asm_vs_instructions[i].part1;
		shader->words[words++] = asm_vs_instructions[i].part0;
	}

	cgc = calloc(1, sizeof(*cgc));
	if (!shader) {
		free(shader);
		return NULL;
	}

	shader->cgc = cgc;

	for (i = 0; i < 16; i++) {
		if (!asm_vs_attributes[i].used)
			continue;

		symbols = realloc(cgc->symbols,
			(cgc->num_symbols + 1) * sizeof(struct cgc_symbol));

		if (!symbols) {
			free(cgc->symbols);
			free(shader->cgc);
			free(shader);
			return NULL;
		}

		cgc->symbols = symbols;

		cgc->symbols[cgc->num_symbols].location = i;
		cgc->symbols[cgc->num_symbols].kind = GLSL_KIND_ATTRIBUTE;
		cgc->symbols[cgc->num_symbols].type = GLSL_TYPE_VEC4;
		cgc->symbols[cgc->num_symbols].name = asm_vs_attributes[i].name;
		cgc->symbols[cgc->num_symbols].input = true;
		cgc->symbols[cgc->num_symbols].used = true;

		cgc->num_symbols++;
	}

	for (i = 0; i < 16; i++) {
		if (!asm_vs_exports[i].used)
			continue;

		symbols = realloc(cgc->symbols,
			(cgc->num_symbols + 1) * sizeof(struct cgc_symbol));

		if (!symbols) {
			free(cgc->symbols);
			free(shader->cgc);
			free(shader);
			return NULL;
		}

		cgc->symbols = symbols;

		cgc->symbols[cgc->num_symbols].location = i;
		cgc->symbols[cgc->num_symbols].kind = GLSL_KIND_ATTRIBUTE;
		cgc->symbols[cgc->num_symbols].type = GLSL_TYPE_UNKNOWN;
		cgc->symbols[cgc->num_symbols].name = asm_vs_exports[i].name;
		cgc->symbols[cgc->num_symbols].input = false;
		cgc->symbols[cgc->num_symbols].used = true;

		cgc->num_symbols++;
	}

	for (i = 0; i < 256; i++) {
		if (!asm_vs_constants[i].used)
			continue;

		symbols = realloc(cgc->symbols,
			(cgc->num_symbols + 1) * sizeof(struct cgc_symbol));

		if (!symbols) {
			free(cgc->symbols);
			free(shader->cgc);
			free(shader);
			return NULL;
		}

		cgc->symbols = symbols;

		cgc->symbols[cgc->num_symbols].location = i;
		cgc->symbols[cgc->num_symbols].kind = GLSL_KIND_CONSTANT;
		cgc->symbols[cgc->num_symbols].type = GLSL_TYPE_VEC4;
		cgc->symbols[cgc->num_symbols].name = "asm-constant";
		cgc->symbols[cgc->num_symbols].input = true;
		cgc->symbols[cgc->num_symbols].used = true;
		cgc->symbols[cgc->num_symbols].vector[0] =
					asm_vs_constants[i].vector.x.value;
		cgc->symbols[cgc->num_symbols].vector[1] =
					asm_vs_constants[i].vector.y.value;
		cgc->symbols[cgc->num_symbols].vector[2] =
					asm_vs_constants[i].vector.z.value;
		cgc->symbols[cgc->num_symbols].vector[3] =
					asm_vs_constants[i].vector.w.value;
		cgc->num_symbols++;
	}

	return shader;
}

const char *grate_shader_disasm_vs(struct grate_shader *shader)
{
	const char *error = NULL;
	char *disassembly = malloc(1);
	const char *instr_txt;
	char *tmp;
	vpe_instr128 instr;
	int i, sz;

	if (!disassembly)
		return NULL;

	disassembly[0] = '\0';

	for (i = 2, sz = 1; i < shader->num_words - 3; i += 4) {
		instr.part0 = *(shader->words + i + 3);
		instr.part1 = *(shader->words + i + 2);
		instr.part2 = *(shader->words + i + 1);
		instr.part3 = *(shader->words + i + 0);

		instr_txt = vpe_vliw_disassemble(&instr);

		sz += strlen(instr_txt) + 2;
		tmp = realloc(disassembly, sz);

		if (!tmp) {
			error = "ERROR: out of memory";
			goto err;
		}

		disassembly = tmp;
		strcat(disassembly, "\n\n");
		strcat(disassembly, instr_txt);
	}

	return disassembly;
err:
	free(disassembly);

	return strdup(error);
}

struct grate_shader *grate_shader_parse_fragment_asm(const char *asm_txt)
{
	struct grate_shader *shader;
	struct cgc_symbol *symbols;
	struct cgc_shader *cgc;
	static char *locale;
	int words = 0;
	int err;
	int i;

	locale = strdup( setlocale(LC_ALL, NULL) );
	if (!locale)
		return NULL;

	/* atof() delimiter is locale-dependent! */
	setlocale(LC_ALL, "C");

	fragment_asm_scan_string(asm_txt);
	err = fragment_asmparse();

	/* restore locale */
	setlocale(LC_ALL, locale);
	free(locale);

	if (err != 0)
		return NULL;

	if (asm_fs_instructions_nb == 0) {
		fprintf(stderr, "ERROR: no fragment instructions generated");
		return NULL;
	}

	shader = calloc(1, sizeof(*shader));
	if (!shader)
		return NULL;

	/* PSEQ + MFU SCHED + TEX + ALU SCHED + ALU COMPLEMENT + DW */
	shader->num_words  = 6 + asm_fs_instructions_nb * 6 + 1;
	/* + MFU */
	shader->num_words += 1 + asm_mfu_instructions_nb * 2;
	/* + ALU */
	shader->num_words += 1 + asm_alu_instructions_nb * 8;
	/* + ALU buffer size + (some config?) */
	shader->num_words += 4;
	/* + PSEQ/DW exec number */
	shader->num_words += 2;

	shader->words = malloc(shader->num_words * 4);
	if (!shader->words) {
		free(shader);
		return NULL;
	}

	/* PSEQ */
	shader->words[words++] =
			HOST1X_OPCODE_NONINCR(0x541, asm_fs_instructions_nb);
	for (i = 0; i < asm_fs_instructions_nb; i++)
		shader->words[words++] = asm_pseq_instructions[i].data;

	shader->words[words++] = HOST1X_OPCODE_IMM(0x500, 0x0);

	/* MFU SCHED */
	shader->words[words++] =
			HOST1X_OPCODE_NONINCR(0x601, asm_fs_instructions_nb);
	for (i = 0; i < asm_fs_instructions_nb; i++)
		shader->words[words++] = asm_mfu_sched[i].data;

	/* MFU */
	shader->words[words++] =
			HOST1X_OPCODE_NONINCR(0x604, asm_mfu_instructions_nb * 2);
	for (i = 0; i < asm_mfu_instructions_nb; i++) {
		shader->words[words++] = asm_mfu_instructions[i].part1;
		shader->words[words++] = asm_mfu_instructions[i].part0;
	}

	/* TEX */
	shader->words[words++] =
			HOST1X_OPCODE_NONINCR(0x701, asm_fs_instructions_nb);
	for (i = 0; i < asm_fs_instructions_nb; i++)
		shader->words[words++] = asm_tex_instructions[i].data;

	/* ALU SCHED */
	shader->words[words++] =
			HOST1X_OPCODE_NONINCR(0x801, asm_fs_instructions_nb);
	for (i = 0; i < asm_fs_instructions_nb; i++)
		shader->words[words++] = asm_alu_sched[i].data;

	/* ALU */
	shader->words[words++] =
			HOST1X_OPCODE_NONINCR(0x804, asm_alu_instructions_nb * 8);
	for (i = 0; i < asm_alu_instructions_nb; i++) {
		shader->words[words++] = asm_alu_instructions[i].part1;
		shader->words[words++] = asm_alu_instructions[i].part0;
		shader->words[words++] = asm_alu_instructions[i].part3;
		shader->words[words++] = asm_alu_instructions[i].part2;
		shader->words[words++] = asm_alu_instructions[i].part5;
		shader->words[words++] = asm_alu_instructions[i].part4;
		shader->words[words++] = asm_alu_instructions[i].part7;
		shader->words[words++] = asm_alu_instructions[i].part6;
	}

	/* ALU COMPLEMENT */
	shader->words[words++] =
			HOST1X_OPCODE_NONINCR(0x806, asm_fs_instructions_nb);
	for (i = 0; i < asm_fs_instructions_nb; i++)
		shader->words[words++] = asm_alu_instructions[i].complement;

	/* DW */
	shader->words[words++] =
			HOST1X_OPCODE_NONINCR(0x901, asm_fs_instructions_nb);
	for (i = 0; i < asm_fs_instructions_nb; i++)
		shader->words[words++] = asm_dw_instructions[i].data;

	/* ALU buffer size */
	shader->words[words++] = HOST1X_OPCODE_NONINCR(0xe20, 1);
	shader->words[words++] =
		(0x12B / (asm_alu_buffer_size * 4)) << 24 | (asm_alu_buffer_size - 1);

	shader->words[words++] = HOST1X_OPCODE_NONINCR(0x501, 1);
	shader->words[words++] = 0x003212CF;

	/* PSEQ/DW exec number */
	shader->words[words++] = HOST1X_OPCODE_NONINCR(0x546, 1);
	shader->words[words++] = asm_pseq_to_dw_exec_nb << 6;

	cgc = calloc(1, sizeof(*cgc));
	if (!shader) {
		free(shader);
		return NULL;
	}

	shader->cgc = cgc;

	for (i = 0; i < 32; i++) {
		if (asm_fs_constants[i] == 0)
			continue;

		symbols = realloc(cgc->symbols,
			(cgc->num_symbols + 1) * sizeof(struct cgc_symbol));

		if (!symbols) {
			free(cgc->symbols);
			free(shader->cgc);
			free(shader);
			return NULL;
		}

		cgc->symbols = symbols;

		cgc->symbols[cgc->num_symbols].location = i;
		cgc->symbols[cgc->num_symbols].kind = GLSL_KIND_CONSTANT;
		cgc->symbols[cgc->num_symbols].type = GLSL_TYPE_UNKNOWN;
		cgc->symbols[cgc->num_symbols].name = "asm-constant";
		cgc->symbols[cgc->num_symbols].input = true;
		cgc->symbols[cgc->num_symbols].used = true;
		cgc->symbols[cgc->num_symbols].vector[0] = asm_fs_constants[i];
		cgc->num_symbols++;
	}

	return shader;
}

const char *grate_shader_disasm_fs(struct grate_shader *shader)
{
	const char *error = NULL;
	const char *instr_txt;
	char *disassembly = NULL;
	char *tmp;
	pseq_instr pseq_instructions[64];
	mfu_instr mfu_instructions[64];
	tex_instr tex_instructions[64];
	alu_instr alu_instructions[64];
	dw_instr dw_instructions[64];
	instr_sched mfu_sched[64];
	instr_sched alu_sched[64];
	unsigned alu_buffer_size = 1;
	unsigned pseq_to_dw_exec_nb = 1;
	int pseq_nb           = 0;
	int mfu_sched_nb      = 0;
	int mfu_nb            = 0;
	int tex_nb            = 0;
	int alu_sched_nb      = 0;
	int alu_nb            = 0;
	int alu_complement_nb = 0;
	int dw_nb             = 0;
	int i, k, sz;
	int ret;

	memset(pseq_instructions, 0, sizeof(pseq_instructions));
	memset(mfu_instructions, 0, sizeof(mfu_instructions));
	memset(mfu_instructions, 0, sizeof(mfu_instructions));
	memset(tex_instructions, 0, sizeof(pseq_instructions));
	memset(alu_instructions, 0, sizeof(alu_instructions));
	memset(dw_instructions, 0, sizeof(dw_instructions));
	memset(mfu_sched, 0, sizeof(mfu_sched));
	memset(alu_sched, 0, sizeof(alu_sched));

	for (i = 0; i < shader->num_words; i++) {
		uint32_t host1x_command = shader->words[i];
		unsigned int host1x_opcode = host1x_command >> 28;
		unsigned int offset = (host1x_command >> 16) & 0xfff;
		unsigned int count = host1x_command & 0xffff;
		unsigned int mask = count;

		switch (host1x_opcode) {
		case 0: /* SETCL */
			break;
		case 1: /* INCR */
			if (offset <= 0x546 && offset + count > 0x546)
				pseq_to_dw_exec_nb = (shader->words[i + 1 + 0x546 - offset] >> 6) & 0x7f;

			if (offset <= 0xe20 && offset + count > 0xe20)
				alu_buffer_size = (shader->words[i + 1 + 0xe20 - offset] & 0x3) + 1;

			i += count;
			break;
		case 2: /* NONINCR */
			switch (offset) {
			case 0x541:
				if (pseq_nb + count >= 64) {
					error = "ERROR: over 64 PSEQ instructions";
					goto err;
				}

				for (k = 0; k < count; k++)
					pseq_instructions[pseq_nb++].data =
							shader->words[i + 1 + k];
				break;
			case 0x546:
				if (count != 0)
					pseq_to_dw_exec_nb = (shader->words[i + count] >> 6) & 0x7f;
				break;
			case 0x601:
				if (mfu_sched_nb + count >= 64) {
					error = "ERROR: over 64 MFU schedule entries";
					goto err;
				}

				for (k = 0; k < count; k++)
					mfu_sched[mfu_sched_nb++].data =
							shader->words[i + 1 + k];
				break;
			case 0x604:
				if (mfu_nb + count >= 64 * 2) {
					error = "ERROR: over 64 MFU instructions";
					goto err;
				}

				for (k = 0; k < count; k++, mfu_nb++) {
					switch (mfu_nb % 2) {
					case 0:
						mfu_instructions[mfu_nb / 2].part1 =
							shader->words[i + 1 + k];
						break;
					case 1:
						mfu_instructions[mfu_nb / 2].part0 =
							shader->words[i + 1 + k];
						break;
					}
				}
				break;
			case 0x701:
				if (tex_nb + count >= 64) {
					error = "ERROR: over 64 TEX instructions";
					goto err;
				}

				for (k = 0; k < count; k++)
					tex_instructions[tex_nb++].data =
							shader->words[i + 1 + k];
				break;
			case 0x801:
				if (alu_sched_nb + count >= 64) {
					error = "ERROR: over 64 ALU schedule entries";
					goto err;
				}

				for (k = 0; k < count; k++)
					alu_sched[alu_sched_nb++].data =
							shader->words[i + 1 + k];
				break;
			case 0x804:
				if (alu_nb + count >= 64 * 8) {
					error = "ERROR: over 64 ALU instructions";
					goto err;
				}

				for (k = 0; k < count; k++, alu_nb++) {
					switch (alu_nb % 8) {
					case 0:
						alu_instructions[alu_nb / 8].part1 =
							shader->words[i + 1 + k];
						break;
					case 1:
						alu_instructions[alu_nb / 8].part0 =
							shader->words[i + 1 + k];
						break;
					case 2:
						alu_instructions[alu_nb / 8].part3 =
							shader->words[i + 1 + k];
						break;
					case 3:
						alu_instructions[alu_nb / 8].part2 =
							shader->words[i + 1 + k];
						break;
					case 4:
						alu_instructions[alu_nb / 8].part5 =
							shader->words[i + 1 + k];
						break;
					case 5:
						alu_instructions[alu_nb / 8].part4 =
							shader->words[i + 1 + k];
						break;
					case 6:
						alu_instructions[alu_nb / 8].part7 =
							shader->words[i + 1 + k];
						break;
					case 7:
						alu_instructions[alu_nb / 8].part6 =
							shader->words[i + 1 + k];
						break;
					}
				}
				break;
			case 0x806:
				if (alu_complement_nb + count >= 64) {
					error = "ERROR: over 64 ALU complement entries";
					goto err;
				}

				for (k = 0; k < count; k++)
					alu_instructions[alu_complement_nb++].complement =
							shader->words[i + 1 + k];
				break;
			case 0x901:
				if (dw_nb + count >= 64) {
					error = "ERROR: over 64 DW instructions";
					goto err;
				}

				for (k = 0; k < count; k++)
					dw_instructions[dw_nb++].data =
							shader->words[i + 1 + k];
				break;
			case 0xe20:
				if (count != 0)
					alu_buffer_size = (shader->words[i + count] & 0x3) + 1;
				break;
			}
			i += count;
			break;
		case 3: /* MASK */
			for (count = 0; count < 16; count++) {
				if (mask & (1 << count)) {
					if (offset + count == 0x546)
						pseq_to_dw_exec_nb = (shader->words[i + 1] >> 6) & 0x7f;

					if (offset + count == 0xe20)
						alu_buffer_size = (shader->words[i + 1] & 0x3) + 1;

					i++;
				}
			}
			break;
		case 4: /* IMM */
			break;
		case 5: /* EXTEND */
			break;
		default:
			error = "ERROR: host1x command stream is invalid";
			goto err;
		}
	}

	if (pseq_nb != mfu_sched_nb)
		goto malformed;

	if (pseq_nb != tex_nb)
		goto malformed;

	if (pseq_nb != alu_sched_nb)
		goto malformed;

	if (pseq_nb != dw_nb)
		goto malformed;

	ret = asprintf(&disassembly, "\nalu_buffer_size = %u\npseq_to_dw_exec_nb = %u",
		       alu_buffer_size, pseq_to_dw_exec_nb);
	if (ret == -1) {
		error = "ERROR: out of memory";
		goto err;
	}

	for (i = 0, sz = strlen(disassembly) + 1; i < pseq_nb; i++) {
		pseq_instr *pseq = &pseq_instructions[i];
		mfu_instr  *mfu  = &mfu_instructions[mfu_sched[i].address];
		tex_instr  *tex  = &tex_instructions[i];
		alu_instr  *alu  = &alu_instructions[alu_sched[i].address];
		dw_instr   *dw   = &dw_instructions[i];

		instr_txt = fragment_pipeline_disassemble(
			pseq,
			mfu, mfu_sched[i].instructions_nb,
			tex,
			alu, alu_sched[i].instructions_nb,
			dw);

		sz += strlen(instr_txt) + 2;
		tmp = realloc(disassembly, sz);

		if (!tmp) {
			error = "ERROR: out of memory";
			goto err;
		}

		disassembly = tmp;
		strcat(disassembly, "\n\n");
		strcat(disassembly, instr_txt);
	}

	return disassembly;

malformed:
	error = "ERROR: fragment shader instructions stream is malformed";
err:
	free(disassembly);

	return strdup(error);
}

struct grate_shader *grate_shader_parse_linker_asm(const char *asm_txt)
{
	struct grate_shader *shader;
	struct cgc_shader *cgc;
	int words = 0;
	int err;
	int i;

	linker_asm_scan_string(asm_txt);
	err = linker_asmparse();

	if (err != 0)
		return NULL;

	shader = calloc(1, sizeof(*shader));
	if (!shader)
		return NULL;

	shader->num_words = 1 + asm_linker_instructions_nb * 2 + 2;
	shader->words = malloc(shader->num_words * 4);
	if (!shader->words) {
		free(shader);
		return NULL;
	}

	shader->words[words++] =
		HOST1X_OPCODE_INCR(0x300, asm_linker_instructions_nb * 2);
	for (i = 0; i < asm_linker_instructions_nb; i++) {
		shader->words[words++] = asm_linker_instructions[i].first;
		shader->words[words++] = asm_linker_instructions[i].latter;
	}

	shader->words[words++] = HOST1X_OPCODE_NONINCR(0xe21, 1);
	shader->words[words]  = asm_linker_used_tram_rows_nb << 8;
	shader->words[words] |= 64 / asm_linker_used_tram_rows_nb;

	cgc = calloc(1, sizeof(*cgc));
	if (!shader) {
		free(shader);
		return NULL;
	}

	shader->cgc = cgc;

	return shader;
}

const char *grate_shader_disasm_linker(struct grate_shader *shader)
{
	const char *error = NULL;
	const char *instr_txt;
	char *disassembly = malloc(1);
	char *tmp;
	link_instr linker_instructions[32];
	int linker_instr_nb = 0;
	int i, k, sz;

	if (!disassembly)
		return NULL;

	disassembly[0] = '\0';

	memset(linker_instructions, 0, sizeof(linker_instructions));

	for (i = 0; i < shader->num_words; i++) {
		uint32_t host1x_command = shader->words[i];
		unsigned int host1x_opcode = host1x_command >> 28;
		unsigned int offset = (host1x_command >> 16) & 0xfff;
		unsigned int count = host1x_command & 0xffff;
		unsigned int mask = count;

		switch (host1x_opcode) {
		case 0: /* SETCL */
			break;
		case 1: /* INCR */
			if (offset >= 0x300 && offset <= 0x33f) {
				linker_instr_nb = offset - 0x300;

				if (linker_instr_nb + count >= 64) {
					error = "ERROR: over 32 LINKER instructions";
					goto err;
				}

				for (k = 0; k < count; k++, linker_instr_nb++) {
					switch (linker_instr_nb % 2) {
					case 0:
						linker_instructions[linker_instr_nb / 2].first =
							shader->words[i + 1 + k];
						break;
					case 1:
						linker_instructions[linker_instr_nb / 2].latter =
							shader->words[i + 1 + k];
						break;
					}
				}
			}
			i += count;
			break;
		case 2: /* NONINCR */
			i += count;
			break;
		case 3: /* MASK */
			for (count = 0; count < 16; count++) {
				if (mask & (1 << count))
					i++;
			}
			break;
		case 4: /* IMM */
			break;
		case 5: /* EXTEND */
			break;
		default:
			error = "ERROR: host1x command stream is invalid";
			goto err;
		}
	}

	for (i = 0, sz = 1; i < 32; i++) {
		if (linker_instructions[i].data == 0)
			continue;

		instr_txt = linker_instruction_disassemble(&linker_instructions[i]);

		sz += strlen(instr_txt) + 1;
		tmp = realloc(disassembly, sz);

		if (!tmp) {
			error = "ERROR: out of memory";
			goto err;
		}

		disassembly = tmp;
		strcat(disassembly, "\n");
		strcat(disassembly, instr_txt);
	}

	return disassembly;
err:
	free(disassembly);

	return strdup(error);
}
