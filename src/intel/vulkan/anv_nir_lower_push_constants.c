/*
 * Copyright © 2015 Intel Corporation
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

#include "anv_nir.h"

struct lower_push_constants_state {
   nir_shader *shader;
   bool is_scalar;
};

static bool
lower_push_constants_block(nir_block *block, void *void_state)
{
   struct lower_push_constants_state *state = void_state;

   nir_foreach_instr(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      /* TODO: Handle indirect push constants */
      if (intrin->intrinsic != nir_intrinsic_load_push_constant)
         continue;

      /* This wont work for vec4 stages. */
      assert(state->is_scalar);

      assert(intrin->const_index[0] % 4 == 0);
      assert(intrin->const_index[1] == 128);

      /* We just turn them into uniform loads with the appropreate offset */
      intrin->intrinsic = nir_intrinsic_load_uniform;
   }

   return true;
}

void
anv_nir_lower_push_constants(nir_shader *shader, bool is_scalar)
{
   struct lower_push_constants_state state = {
      .shader = shader,
      .is_scalar = is_scalar,
   };

   nir_foreach_function(shader, function) {
      if (function->impl)
         nir_foreach_block(function->impl, lower_push_constants_block, &state);
   }

   assert(shader->num_uniforms % 4 == 0);
   if (is_scalar)
      shader->num_uniforms /= 4;
   else
      shader->num_uniforms = DIV_ROUND_UP(shader->num_uniforms, 16);
}
