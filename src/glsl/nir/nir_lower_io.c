/*
 * Copyright © 2014 Intel Corporation
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
 *
 * Authors:
 *    Connor Abbott (cwabbott0@gmail.com)
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

/*
 * This lowering pass converts references to input/output variables with
 * loads/stores to actual input/output intrinsics.
 */

#include "nir.h"
#include "nir_builder.h"

struct lower_io_state {
   nir_builder builder;
   void *mem_ctx;
   int (*type_size)(const struct glsl_type *type);
};

void
nir_assign_var_locations(struct exec_list *var_list, unsigned *size,
                         int (*type_size)(const struct glsl_type *))
{
   unsigned location = 0;

   foreach_list_typed(nir_variable, var, node, var_list) {
      /*
       * UBO's have their own address spaces, so don't count them towards the
       * number of global uniforms
       */
      if ((var->data.mode == nir_var_uniform || var->data.mode == nir_var_shader_storage) &&
          var->interface_type != NULL)
         continue;

      var->data.driver_location = location;
      location += type_size(var->type);
   }

   *size = location;
}

static bool
deref_has_indirect(nir_deref_var *deref)
{
   for (nir_deref *tail = deref->deref.child; tail; tail = tail->child) {
      if (tail->deref_type == nir_deref_type_array) {
         nir_deref_array *arr = nir_deref_as_array(tail);
         if (arr->deref_array_type == nir_deref_array_type_indirect)
            return true;
      }
   }

   return false;
}

static unsigned
get_io_offset(nir_deref_var *deref, nir_instr *instr, nir_src *indirect,
              struct lower_io_state *state)
{
   bool found_indirect = false;
   unsigned base_offset = 0;

   nir_builder *b = &state->builder;
   b->cursor = nir_before_instr(instr);

   nir_deref *tail = &deref->deref;
   while (tail->child != NULL) {
      const struct glsl_type *parent_type = tail->type;
      tail = tail->child;

      if (tail->deref_type == nir_deref_type_array) {
         nir_deref_array *deref_array = nir_deref_as_array(tail);
         unsigned size = state->type_size(tail->type);

         base_offset += size * deref_array->base_offset;

         if (deref_array->deref_array_type == nir_deref_array_type_indirect) {
            nir_ssa_def *mul =
               nir_imul(b, nir_imm_int(b, size),
                        nir_ssa_for_src(b, deref_array->indirect, 1));

            if (found_indirect) {
               indirect->ssa =
                  nir_iadd(b, nir_ssa_for_src(b, *indirect, 1), mul);
            } else {
               indirect->ssa = mul;
            }
            indirect->is_ssa = true;
            found_indirect = true;
         }
      } else if (tail->deref_type == nir_deref_type_struct) {
         nir_deref_struct *deref_struct = nir_deref_as_struct(tail);

         for (unsigned i = 0; i < deref_struct->index; i++) {
            base_offset +=
               state->type_size(glsl_get_struct_field(parent_type, i));
         }
      }
   }

   return base_offset;
}

static nir_intrinsic_op
load_op(nir_variable_mode mode, bool has_indirect)
{
   nir_intrinsic_op op;
   switch (mode) {
   case nir_var_shader_in:
      op = has_indirect ? nir_intrinsic_load_input_indirect :
                          nir_intrinsic_load_input;
      break;
   case nir_var_uniform:
      op = has_indirect ? nir_intrinsic_load_uniform_indirect :
                          nir_intrinsic_load_uniform;
      break;
   default:
      unreachable("Unknown variable mode");
   }
   return op;
}

static bool
nir_lower_io_block(nir_block *block, void *void_state)
{
   struct lower_io_state *state = void_state;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_var: {
         nir_variable_mode mode = intrin->variables[0]->var->data.mode;
         if (mode != nir_var_shader_in && mode != nir_var_uniform)
            continue;

         bool has_indirect = deref_has_indirect(intrin->variables[0]);

         nir_intrinsic_instr *load =
            nir_intrinsic_instr_create(state->mem_ctx,
                                       load_op(mode, has_indirect));
         load->num_components = intrin->num_components;

         nir_src indirect;
         unsigned offset = get_io_offset(intrin->variables[0],
                                         &intrin->instr, &indirect, state);

         unsigned location = intrin->variables[0]->var->data.driver_location;
         if (mode == nir_var_uniform) {
            load->const_index[0] = location;
            load->const_index[1] = offset;
         } else {
            load->const_index[0] = location + offset;
         }

         if (has_indirect)
            load->src[0] = indirect;

         if (intrin->dest.is_ssa) {
            nir_ssa_dest_init(&load->instr, &load->dest,
                              intrin->num_components, NULL);
            nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                     nir_src_for_ssa(&load->dest.ssa));
         } else {
            nir_dest_copy(&load->dest, &intrin->dest, state->mem_ctx);
         }

         nir_instr_insert_before(&intrin->instr, &load->instr);
         nir_instr_remove(&intrin->instr);
         break;
      }

      case nir_intrinsic_store_var: {
         if (intrin->variables[0]->var->data.mode != nir_var_shader_out)
            continue;

         bool has_indirect = deref_has_indirect(intrin->variables[0]);

         nir_intrinsic_op store_op;
         if (has_indirect) {
            store_op = nir_intrinsic_store_output_indirect;
         } else {
            store_op = nir_intrinsic_store_output;
         }

         nir_intrinsic_instr *store = nir_intrinsic_instr_create(state->mem_ctx,
                                                                 store_op);
         store->num_components = intrin->num_components;

         nir_src indirect;
         unsigned offset = get_io_offset(intrin->variables[0],
                                         &intrin->instr, &indirect, state);
         offset += intrin->variables[0]->var->data.driver_location;

         store->const_index[0] = offset;

         nir_src_copy(&store->src[0], &intrin->src[0], store);

         if (has_indirect)
            store->src[1] = indirect;

         nir_instr_insert_before(&intrin->instr, &store->instr);
         nir_instr_remove(&intrin->instr);
         break;
      }

      default:
         break;
      }
   }

   return true;
}

static void
nir_lower_io_impl(nir_function_impl *impl, int(*type_size)(const struct glsl_type *))
{
   struct lower_io_state state;

   nir_builder_init(&state.builder, impl);
   state.mem_ctx = ralloc_parent(impl);
   state.type_size = type_size;

   nir_foreach_block(impl, nir_lower_io_block, &state);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void
nir_lower_io(nir_shader *shader, int(*type_size)(const struct glsl_type *))
{
   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         nir_lower_io_impl(overload->impl, type_size);
   }
}
