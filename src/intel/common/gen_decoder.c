/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <expat.h>
#include <inttypes.h>
#include <zlib.h>

#include <util/macros.h>

#include "gen_decoder.h"

#include "genxml/gen6_xml.h"
#include "genxml/gen7_xml.h"
#include "genxml/gen75_xml.h"
#include "genxml/gen8_xml.h"
#include "genxml/gen9_xml.h"

#define XML_BUFFER_SIZE 4096

#define MAKE_GEN(major, minor) ( ((major) << 8) | (minor) )

struct gen_spec {
   uint32_t gen;

   int ncommands;
   struct gen_group *commands[256];
   int nstructs;
   struct gen_group *structs[256];
   int nregisters;
   struct gen_group *registers[256];
   int nenums;
   struct gen_enum *enums[256];
};

struct location {
   const char *filename;
   int line_number;
};

struct parser_context {
   XML_Parser parser;
   int foo;
   struct location loc;
   const char *platform;

   struct gen_group *group;
   struct gen_enum *enoom;

   int nfields;
   struct gen_field *fields[128];

   int nvalues;
   struct gen_value *values[256];

   struct gen_spec *spec;
};

const char *
gen_group_get_name(struct gen_group *group)
{
   return group->name;
}

uint32_t
gen_group_get_opcode(struct gen_group *group)
{
   return group->opcode;
}

struct gen_group *
gen_spec_find_struct(struct gen_spec *spec, const char *name)
{
   for (int i = 0; i < spec->nstructs; i++)
      if (strcmp(spec->structs[i]->name, name) == 0)
         return spec->structs[i];

   return NULL;
}

struct gen_group *
gen_spec_find_register(struct gen_spec *spec, uint32_t offset)
{
   for (int i = 0; i < spec->nregisters; i++)
      if (spec->registers[i]->register_offset == offset)
         return spec->registers[i];

   return NULL;
}

struct gen_enum *
gen_spec_find_enum(struct gen_spec *spec, const char *name)
{
   for (int i = 0; i < spec->nenums; i++)
      if (strcmp(spec->enums[i]->name, name) == 0)
         return spec->enums[i];

   return NULL;
}

uint32_t
gen_spec_get_gen(struct gen_spec *spec)
{
   return spec->gen;
}

static void __attribute__((noreturn))
fail(struct location *loc, const char *msg, ...)
{
   va_list ap;

   va_start(ap, msg);
   fprintf(stderr, "%s:%d: error: ",
           loc->filename, loc->line_number);
   vfprintf(stderr, msg, ap);
   fprintf(stderr, "\n");
   va_end(ap);
   exit(EXIT_FAILURE);
}

static void *
fail_on_null(void *p)
{
   if (p == NULL) {
      fprintf(stderr, "aubinator: out of memory\n");
      exit(EXIT_FAILURE);
   }

   return p;
}

static char *
xstrdup(const char *s)
{
   return fail_on_null(strdup(s));
}

static void *
zalloc(size_t s)
{
   return calloc(s, 1);
}

static void *
xzalloc(size_t s)
{
   return fail_on_null(zalloc(s));
}

static struct gen_group *
create_group(struct parser_context *ctx, const char *name, const char **atts)
{
   struct gen_group *group;

   group = xzalloc(sizeof(*group));
   if (name)
      group->name = xstrdup(name);

   group->spec = ctx->spec;
   group->group_offset = 0;
   group->group_count = 0;

   return group;
}

static struct gen_enum *
create_enum(struct parser_context *ctx, const char *name, const char **atts)
{
   struct gen_enum *e;

   e = xzalloc(sizeof(*e));
   if (name)
      e->name = xstrdup(name);

   e->nvalues = 0;

   return e;
}

static void
get_group_offset_count(struct parser_context *ctx, const char *name,
                       const char **atts, uint32_t *offset, uint32_t *count)
{
   char *p;
   int i;

   for (i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "count") == 0)
         *count = strtoul(atts[i + 1], &p, 0);
      else if (strcmp(atts[i], "start") == 0)
         *offset = strtoul(atts[i + 1], &p, 0);
   }
   return;
}

static void
get_register_offset(const char **atts, uint32_t *offset)
{
   char *p;
   int i;

   for (i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "num") == 0)
         *offset = strtoul(atts[i + 1], &p, 0);
   }
   return;
}

static void
get_start_end_pos(int *start, int *end)
{
   /* start value has to be mod with 32 as we need the relative
    * start position in the first DWord. For the end position, add
    * the length of the field to the start position to get the
    * relative postion in the 64 bit address.
    */
   if (*end - *start > 32) {
      int len = *end - *start;
      *start = *start % 32;
      *end = *start + len;
   } else {
      *start = *start % 32;
      *end = *end % 32;
   }

   return;
}

static inline uint64_t
mask(int start, int end)
{
   uint64_t v;

   v = ~0ULL >> (63 - end + start);

   return v << start;
}

static inline uint64_t
field(uint64_t value, int start, int end)
{
   get_start_end_pos(&start, &end);
   return (value & mask(start, end)) >> (start);
}

static inline uint64_t
field_address(uint64_t value, int start, int end)
{
   /* no need to right shift for address/offset */
   get_start_end_pos(&start, &end);
   return (value & mask(start, end));
}

static struct gen_type
string_to_type(struct parser_context *ctx, const char *s)
{
   int i, f;
   struct gen_group *g;
   struct gen_enum *e;

   if (strcmp(s, "int") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_INT };
   else if (strcmp(s, "uint") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_UINT };
   else if (strcmp(s, "bool") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_BOOL };
   else if (strcmp(s, "float") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_FLOAT };
   else if (strcmp(s, "address") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_ADDRESS };
   else if (strcmp(s, "offset") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_OFFSET };
   else if (sscanf(s, "u%d.%d", &i, &f) == 2)
      return (struct gen_type) { .kind = GEN_TYPE_UFIXED, .i = i, .f = f };
   else if (sscanf(s, "s%d.%d", &i, &f) == 2)
      return (struct gen_type) { .kind = GEN_TYPE_SFIXED, .i = i, .f = f };
   else if (g = gen_spec_find_struct(ctx->spec, s), g != NULL)
      return (struct gen_type) { .kind = GEN_TYPE_STRUCT, .gen_struct = g };
   else if (e = gen_spec_find_enum(ctx->spec, s), e != NULL)
      return (struct gen_type) { .kind = GEN_TYPE_ENUM, .gen_enum = e };
   else if (strcmp(s, "mbo") == 0)
      return (struct gen_type) { .kind = GEN_TYPE_MBO };
   else
      fail(&ctx->loc, "invalid type: %s", s);
}

static struct gen_field *
create_field(struct parser_context *ctx, const char **atts)
{
   struct gen_field *field;
   char *p;
   int i;

   field = xzalloc(sizeof(*field));

   for (i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0)
         field->name = xstrdup(atts[i + 1]);
      else if (strcmp(atts[i], "start") == 0)
         field->start = ctx->group->group_offset+strtoul(atts[i + 1], &p, 0);
      else if (strcmp(atts[i], "end") == 0) {
         field->end = ctx->group->group_offset+strtoul(atts[i + 1], &p, 0);
         if (ctx->group->group_offset)
            ctx->group->group_offset = field->end+1;
      } else if (strcmp(atts[i], "type") == 0)
         field->type = string_to_type(ctx, atts[i + 1]);
      else if (strcmp(atts[i], "default") == 0 &&
               field->start >= 16 && field->end <= 31) {
         field->has_default = true;
         field->default_value = strtoul(atts[i + 1], &p, 0);
      }
   }

   return field;
}

static struct gen_value *
create_value(struct parser_context *ctx, const char **atts)
{
   struct gen_value *value = xzalloc(sizeof(*value));

   for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0)
         value->name = xstrdup(atts[i + 1]);
      else if (strcmp(atts[i], "value") == 0)
         value->value = strtoul(atts[i + 1], NULL, 0);
   }

   return value;
}

static void
start_element(void *data, const char *element_name, const char **atts)
{
   struct parser_context *ctx = data;
   int i;
   const char *name = NULL;
   const char *gen = NULL;

   ctx->loc.line_number = XML_GetCurrentLineNumber(ctx->parser);

   for (i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0)
         name = atts[i + 1];
      else if (strcmp(atts[i], "gen") == 0)
         gen = atts[i + 1];
   }

   if (strcmp(element_name, "genxml") == 0) {
      if (name == NULL)
         fail(&ctx->loc, "no platform name given");
      if (gen == NULL)
         fail(&ctx->loc, "no gen given");

      ctx->platform = xstrdup(name);
      int major, minor;
      int n = sscanf(gen, "%d.%d", &major, &minor);
      if (n == 0)
         fail(&ctx->loc, "invalid gen given: %s", gen);
      if (n == 1)
         minor = 0;

      ctx->spec->gen = MAKE_GEN(major, minor);
   } else if (strcmp(element_name, "instruction") == 0 ||
              strcmp(element_name, "struct") == 0) {
      ctx->group = create_group(ctx, name, atts);
   } else if (strcmp(element_name, "register") == 0) {
      ctx->group = create_group(ctx, name, atts);
      get_register_offset(atts, &ctx->group->register_offset);
   } else if (strcmp(element_name, "group") == 0) {
      get_group_offset_count(ctx, name, atts, &ctx->group->group_offset,
                             &ctx->group->group_count);
   } else if (strcmp(element_name, "field") == 0) {
      do {
         ctx->fields[ctx->nfields++] = create_field(ctx, atts);
         if (ctx->group->group_count)
            ctx->group->group_count--;
      } while (ctx->group->group_count > 0);
   } else if (strcmp(element_name, "enum") == 0) {
      ctx->enoom = create_enum(ctx, name, atts);
   } else if (strcmp(element_name, "value") == 0) {
      ctx->values[ctx->nvalues++] = create_value(ctx, atts);
   }
}

static void
end_element(void *data, const char *name)
{
   struct parser_context *ctx = data;
   struct gen_spec *spec = ctx->spec;

   if (strcmp(name, "instruction") == 0 ||
      strcmp(name, "struct") == 0 ||
      strcmp(name, "register") == 0) {
      size_t size = ctx->nfields * sizeof(ctx->fields[0]);
      struct gen_group *group = ctx->group;

      group->fields = xzalloc(size);
      group->nfields = ctx->nfields;
      memcpy(group->fields, ctx->fields, size);
      ctx->nfields = 0;
      ctx->group = NULL;

      for (int i = 0; i < group->nfields; i++) {
         if (group->fields[i]->start >= 16 &&
            group->fields[i]->end <= 31 &&
            group->fields[i]->has_default) {
            group->opcode_mask |=
               mask(group->fields[i]->start % 32, group->fields[i]->end % 32);
            group->opcode |=
               group->fields[i]->default_value << group->fields[i]->start;
         }
      }

      if (strcmp(name, "instruction") == 0)
         spec->commands[spec->ncommands++] = group;
      else if (strcmp(name, "struct") == 0)
         spec->structs[spec->nstructs++] = group;
      else if (strcmp(name, "register") == 0)
         spec->registers[spec->nregisters++] = group;
   } else if (strcmp(name, "group") == 0) {
      ctx->group->group_offset = 0;
      ctx->group->group_count = 0;
   } else if (strcmp(name, "field") == 0) {
      assert(ctx->nfields > 0);
      struct gen_field *field = ctx->fields[ctx->nfields - 1];
      size_t size = ctx->nvalues * sizeof(ctx->values[0]);
      field->inline_enum.values = xzalloc(size);
      field->inline_enum.nvalues = ctx->nvalues;
      memcpy(field->inline_enum.values, ctx->values, size);
      ctx->nvalues = 0;
   } else if (strcmp(name, "enum") == 0) {
      struct gen_enum *e = ctx->enoom;
      size_t size = ctx->nvalues * sizeof(ctx->values[0]);
      e->values = xzalloc(size);
      e->nvalues = ctx->nvalues;
      memcpy(e->values, ctx->values, size);
      ctx->nvalues = 0;
      ctx->enoom = NULL;
      spec->enums[spec->nenums++] = e;
   }
}

static void
character_data(void *data, const XML_Char *s, int len)
{
}

static int
devinfo_to_gen(const struct gen_device_info *devinfo)
{
   int value = 10 * devinfo->gen;

   if (devinfo->is_baytrail || devinfo->is_haswell)
      value += 5;

   return value;
}

static const struct {
   int gen;
   const uint8_t *data;
   size_t data_length;
} gen_data[] = {
   { .gen = 60, .data = gen6_xml, .data_length = sizeof(gen6_xml) },
   { .gen = 70, .data = gen7_xml, .data_length = sizeof(gen7_xml) },
   { .gen = 75, .data = gen75_xml, .data_length = sizeof(gen75_xml) },
   { .gen = 80, .data = gen8_xml, .data_length = sizeof(gen8_xml) },
   { .gen = 90, .data = gen9_xml, .data_length = sizeof(gen9_xml) }
};

static const uint8_t *
devinfo_to_xml_data(const struct gen_device_info *devinfo,
                    uint32_t *data_length)
{
   int i, gen = devinfo_to_gen(devinfo);

   for (i = 0; i < ARRAY_SIZE(gen_data); i++) {
      if (gen_data[i].gen == gen) {
         *data_length = gen_data[i].data_length;
         return gen_data[i].data;
      }
   }

   unreachable("Unknown generation");
   return NULL;
}

static uint32_t zlib_inflate(const void *compressed_data,
                             uint32_t compressed_len,
                             void **out_ptr)
{
   struct z_stream_s zstream;
   void *out;

   memset(&zstream, 0, sizeof(zstream));

   zstream.next_in = (unsigned char *)compressed_data;
   zstream.avail_in = compressed_len;

   if (inflateInit(&zstream) != Z_OK)
      return 0;

   out = malloc(4096);
   zstream.next_out = out;
   zstream.avail_out = 4096;

   do {
      switch (inflate(&zstream, Z_SYNC_FLUSH)) {
      case Z_STREAM_END:
         goto end;
      case Z_OK:
         break;
      default:
         inflateEnd(&zstream);
         return 0;
      }

      if (zstream.avail_out)
         break;

      out = realloc(out, 2*zstream.total_out);
      if (out == NULL) {
         inflateEnd(&zstream);
         return 0;
      }

      zstream.next_out = (unsigned char *)out + zstream.total_out;
      zstream.avail_out = zstream.total_out;
   } while (1);
 end:
   inflateEnd(&zstream);
   *out_ptr = out;
   return zstream.total_out;
}

struct gen_spec *
gen_spec_load(const struct gen_device_info *devinfo)
{
   struct parser_context ctx;
   void *buf;
   const void *zlib_data;
   void *text_data;
   uint32_t zlib_length = 0, text_length;

   memset(&ctx, 0, sizeof ctx);
   ctx.parser = XML_ParserCreate(NULL);
   XML_SetUserData(ctx.parser, &ctx);
   if (ctx.parser == NULL) {
      fprintf(stderr, "failed to create parser\n");
      return NULL;
   }

   XML_SetElementHandler(ctx.parser, start_element, end_element);
   XML_SetCharacterDataHandler(ctx.parser, character_data);

   ctx.spec = xzalloc(sizeof(*ctx.spec));

   zlib_data = devinfo_to_xml_data(devinfo, &zlib_length);
   text_length = zlib_inflate(zlib_data, zlib_length, &text_data);

   buf = XML_GetBuffer(ctx.parser, text_length);
   memcpy(buf, text_data, text_length);

   if (XML_ParseBuffer(ctx.parser, text_length, true) == 0) {
      fprintf(stderr,
              "Error parsing XML at line %ld col %ld byte %ld/%u: %s\n",
              XML_GetCurrentLineNumber(ctx.parser),
              XML_GetCurrentColumnNumber(ctx.parser),
              XML_GetCurrentByteIndex(ctx.parser), text_length,
              XML_ErrorString(XML_GetErrorCode(ctx.parser)));
      XML_ParserFree(ctx.parser);
      free(text_data);
      return NULL;
   }

   XML_ParserFree(ctx.parser);
   free(text_data);

   return ctx.spec;
}

struct gen_spec *
gen_spec_load_from_path(const struct gen_device_info *devinfo,
                        const char *path)
{
   struct parser_context ctx;
   size_t len, filename_len = strlen(path) + 20;
   char *filename = malloc(filename_len);
   void *buf;
   FILE *input;

   len = snprintf(filename, filename_len, "%s/gen%i.xml",
                  path, devinfo_to_gen(devinfo));
   assert(len < filename_len);

   input = fopen(filename, "r");
   if (input == NULL) {
      fprintf(stderr, "failed to open xml description\n");
      free(filename);
      return NULL;
   }

   memset(&ctx, 0, sizeof ctx);
   ctx.parser = XML_ParserCreate(NULL);
   XML_SetUserData(ctx.parser, &ctx);
   if (ctx.parser == NULL) {
      fprintf(stderr, "failed to create parser\n");
      fclose(input);
      free(filename);
      return NULL;
   }

   XML_SetElementHandler(ctx.parser, start_element, end_element);
   XML_SetCharacterDataHandler(ctx.parser, character_data);
   ctx.loc.filename = filename;
   ctx.spec = xzalloc(sizeof(*ctx.spec));

   do {
      buf = XML_GetBuffer(ctx.parser, XML_BUFFER_SIZE);
      len = fread(buf, 1, XML_BUFFER_SIZE, input);
      if (len < 0) {
         fprintf(stderr, "fread: %m\n");
         fclose(input);
         free(filename);
         return NULL;
      }
      if (XML_ParseBuffer(ctx.parser, len, len == 0) == 0) {
         fprintf(stderr,
                 "Error parsing XML at line %ld col %ld: %s\n",
                 XML_GetCurrentLineNumber(ctx.parser),
                 XML_GetCurrentColumnNumber(ctx.parser),
                 XML_ErrorString(XML_GetErrorCode(ctx.parser)));
         fclose(input);
         free(filename);
         return NULL;
      }
   } while (len > 0);

   XML_ParserFree(ctx.parser);

   fclose(input);
   free(filename);

   return ctx.spec;
}

struct gen_group *
gen_spec_find_instruction(struct gen_spec *spec, const uint32_t *p)
{
   for (int i = 0; i < spec->ncommands; i++) {
      uint32_t opcode = *p & spec->commands[i]->opcode_mask;
      if (opcode == spec->commands[i]->opcode)
         return spec->commands[i];
   }

   return NULL;
}

int
gen_group_get_length(struct gen_group *group, const uint32_t *p)
{
   uint32_t h = p[0];
   uint32_t type = field(h, 29, 31);

   switch (type) {
   case 0: /* MI */ {
      uint32_t opcode = field(h, 23, 28);
      if (opcode < 16)
         return 1;
      else
         return field(h, 0, 7) + 2;
      break;
   }

   case 3: /* Render */ {
      uint32_t subtype = field(h, 27, 28);
      switch (subtype) {
      case 0:
         return field(h, 0, 7) + 2;
      case 1:
         return 1;
      case 2:
         return 2;
      case 3:
         return field(h, 0, 7) + 2;
      }
   }
   }

   unreachable("bad opcode");
}

void
gen_field_iterator_init(struct gen_field_iterator *iter,
                        struct gen_group *group,
                        const uint32_t *p,
                        bool print_colors)
{
   iter->group = group;
   iter->p = p;
   iter->i = 0;
   iter->print_colors = print_colors;
}

static const char *
gen_get_enum_name(struct gen_enum *e, uint64_t value)
{
   for (int i = 0; i < e->nvalues; i++) {
      if (e->values[i]->value == value) {
         return e->values[i]->name;
      }
   }
   return NULL;
}

bool
gen_field_iterator_next(struct gen_field_iterator *iter)
{
   struct gen_field *f;
   union {
      uint64_t qw;
      float f;
   } v;

   if (iter->i == iter->group->nfields)
      return false;

   f = iter->group->fields[iter->i++];
   iter->name = f->name;
   iter->dword = f->start / 32;
   iter->struct_desc = NULL;

   if ((f->end - f->start) > 32)
      v.qw = ((uint64_t) iter->p[iter->dword+1] << 32) | iter->p[iter->dword];
   else
      v.qw = iter->p[iter->dword];

   const char *enum_name = NULL;

   switch (f->type.kind) {
   case GEN_TYPE_UNKNOWN:
   case GEN_TYPE_INT: {
      uint64_t value = field(v.qw, f->start, f->end);
      snprintf(iter->value, sizeof(iter->value),
               "%"PRId64, value);
      enum_name = gen_get_enum_name(&f->inline_enum, value);
      break;
   }
   case GEN_TYPE_UINT: {
      uint64_t value = field(v.qw, f->start, f->end);
      snprintf(iter->value, sizeof(iter->value),
               "%"PRIu64, value);
      enum_name = gen_get_enum_name(&f->inline_enum, value);
      break;
   }
   case GEN_TYPE_BOOL: {
      const char *true_string =
         iter->print_colors ? "\e[0;35mtrue\e[0m" : "true";
      snprintf(iter->value, sizeof(iter->value),
               "%s", field(v.qw, f->start, f->end) ? true_string : "false");
      break;
   }
   case GEN_TYPE_FLOAT:
      snprintf(iter->value, sizeof(iter->value), "%f", v.f);
      break;
   case GEN_TYPE_ADDRESS:
   case GEN_TYPE_OFFSET:
      snprintf(iter->value, sizeof(iter->value),
               "0x%08"PRIx64, field_address(v.qw, f->start, f->end));
      break;
   case GEN_TYPE_STRUCT:
      snprintf(iter->value, sizeof(iter->value),
               "<struct %s>", f->type.gen_struct->name);
      iter->struct_desc =
         gen_spec_find_struct(iter->group->spec, f->type.gen_struct->name);
      break;
   case GEN_TYPE_UFIXED:
      snprintf(iter->value, sizeof(iter->value),
               "%f", (float) field(v.qw, f->start, f->end) / (1 << f->type.f));
      break;
   case GEN_TYPE_SFIXED:
      /* FIXME: Sign extend extracted field. */
      snprintf(iter->value, sizeof(iter->value), "%s", "foo");
      break;
   case GEN_TYPE_MBO:
       break;
   case GEN_TYPE_ENUM: {
      uint64_t value = field(v.qw, f->start, f->end);
      snprintf(iter->value, sizeof(iter->value),
               "%"PRId64, value);
      enum_name = gen_get_enum_name(f->type.gen_enum, value);
      break;
   }
   }

   if (enum_name) {
      int length = strlen(iter->value);
      snprintf(iter->value + length, sizeof(iter->value) - length,
               " (%s)", enum_name);
   }

   return true;
}

static void
print_dword_header(FILE *outfile,
                   struct gen_field_iterator *iter, uint64_t offset)
{
   fprintf(outfile, "0x%08"PRIx64":  0x%08x : Dword %d\n",
           offset + 4 * iter->dword, iter->p[iter->dword], iter->dword);
}

void
gen_print_group(FILE *outfile, struct gen_group *group,
                uint64_t offset, const uint32_t *p,
                int starting_dword, bool color)
{
   struct gen_field_iterator iter;
   int last_dword = 0;

   gen_field_iterator_init(&iter, group, p, color);
   while (gen_field_iterator_next(&iter)) {
      if (last_dword != iter.dword) {
         print_dword_header(outfile, &iter, offset);
         last_dword = iter.dword;
      }
      if (iter.dword >= starting_dword) {
         fprintf(outfile, "    %s: %s\n", iter.name, iter.value);
         if (iter.struct_desc) {
            uint64_t struct_offset = offset + 4 * iter.dword;
            print_dword_header(outfile, &iter, struct_offset);
            gen_print_group(outfile, iter.struct_desc, struct_offset,
                            &p[iter.dword], 0, color);
         }
      }
   }
}
