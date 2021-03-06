/*
    Copyright (C) 2018 Daniel Schultz

    This file is part of FLINT.

    FLINT is free software: you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.  See <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include "fmpq_mpoly.h"
#include "ulong_extras.h"

int
main(void)
{
    int i, j, k, result;
    FLINT_TEST_INIT(state);

    flint_printf("get/set_monomial_ui....");
    fflush(stdout);

    /* check ui */
    for (i = 0; i < 1000 * flint_test_multiplier(); i++)
    {
        fmpq_mpoly_ctx_t ctx;
        fmpq_mpoly_t f;
        slong len, coeff_bits, exp_bits, index;

        fmpq_mpoly_ctx_init_rand(ctx, state, 20);
        fmpq_mpoly_init(f, ctx);

        len = n_randint(state, 50);
        exp_bits = n_randint(state, 100) + 1;
        coeff_bits = n_randint(state, 100);

        fmpq_mpoly_randtest_bits(f, state, len, coeff_bits, exp_bits, ctx);

        for (j = 0; j < 10; j++)
        {
            ulong * exp1 = (ulong *) flint_malloc(ctx->zctx->minfo->nvars*sizeof(ulong));
            ulong * exp2 = (ulong *) flint_malloc(ctx->zctx->minfo->nvars*sizeof(ulong));

            for (k = 0; k < ctx->zctx->minfo->nvars; k++)
            {
                slong bits = n_randint(state, FLINT_BITS) + 1;
                exp1[k] = n_randbits(state, bits);
            }

            index = n_randint(state, f->zpoly->length + 1);

            fmpq_mpoly_set_monomial_ui(f, index, exp1, ctx);

            if (!mpoly_monomials_valid_test(f->zpoly->exps, f->zpoly->length, f->zpoly->bits, ctx->zctx->minfo))
                flint_throw(FLINT_ERROR, "Polynomial exponents invalid");

            if (mpoly_monomials_overflow_test(f->zpoly->exps, f->zpoly->length, f->zpoly->bits, ctx->zctx->minfo))
                flint_throw(FLINT_ERROR, "Polynomial exponents overflow");

            fmpq_mpoly_get_monomial_ui(exp2, f, index, ctx);

            result = 1;
            for (k = 0; k < ctx->zctx->minfo->nvars; k++)
                result &= exp1[k] == exp2[k];

            if (!result)
            {
                printf("FAIL\ncheck ui, i = %d, j = %d\n", i, j);
                flint_abort();
            }

            flint_free(exp1);
            flint_free(exp2);
        }

        fmpq_mpoly_clear(f, ctx);  
    }

    FLINT_TEST_CLEANUP(state);
    
    flint_printf("PASS\n");
    return 0;
}
