/*
    Copyright (C) 2017 Daniel Schultz

    This file is part of FLINT.

    FLINT is free software: you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.  See <http://www.gnu.org/licenses/>.
*/

#include <gmp.h>
#include <stdlib.h>
#include "flint.h"
#include "fmpz.h"
#include "fmpz_mpoly.h"
#include "longlong.h"

/*
   Set polyq to the quotient poly2 by poly3 discarding remainder (with notional
   remainder coeffs reduced modulo the leading coeff of poly3), and return
   the length of the quotient. This version of the function assumes the
   exponent vectors all fit in a single word. The exponent vectors are
   assumed to have fields with the given number of bits. Assumes input polys
   are nonzero. Implements "Polynomial division using dynamic arrays, heaps
   and packed exponents" by Michael Monagan and Roman Pearce [1], except that
   we use a heap with smallest exponent at head. Note that if a < b then
   (n - b) < (n - b) where n is the maximum value a and b can take. The word
   "maxn" is set to an exponent vector whose fields are all set to such a
   value n. This allows division from left to right with a heap with smallest
   exponent at the head. Quotient poly is written in reverse order.
   [1] http://www.cecm.sfu.ca/~rpearcea/sdmp/sdmp_paper.pdf 
*/
slong _fmpz_mpoly_div_monagan_pearce1(fmpz ** polyq, ulong ** expq,
                slong * allocq, const fmpz * poly2, const ulong * exp2,
            slong len2, const fmpz * poly3, const ulong * exp3, slong len3,
                                                      slong bits, ulong maskhi)
{
    slong i, j, k, s;
    slong next_loc, heap_len = 2;
    mpoly_heap1_s * heap;
    mpoly_heap_t * chain;
    slong * store, * store_base;
    mpoly_heap_t * x;
    fmpz * p1 = *polyq;
    ulong * e1 = *expq;
    slong * hind;
    ulong mask, exp;
    fmpz_t r, acc_lg;
    ulong acc_sm[3];
    int lt_divides, small;
    slong bits2, bits3;
    ulong lc_norm, lc_abs, lc_sign, lc_n, lc_i;
    TMP_INIT;

    TMP_START;

    fmpz_init(acc_lg);
    fmpz_init(r);

   /* whether intermediate computations q - a*b will fit in three words */
   bits2 = _fmpz_vec_max_bits(poly2, len2);
   bits3 = _fmpz_vec_max_bits(poly3, len3);
   /* allow one bit for sign, one bit for subtraction */
   small = FLINT_ABS(bits2) <= (FLINT_ABS(bits3) + FLINT_BIT_COUNT(len3) +
           FLINT_BITS - 2) && FLINT_ABS(bits3) <= FLINT_BITS - 2;

    /* whether intermediate computations q - a*b will fit in three words */
    bits2 = _fmpz_vec_max_bits(poly2, len2);
    bits3 = _fmpz_vec_max_bits(poly3, len3);
    /* allow one bit for sign, one bit for subtraction */
    small = FLINT_ABS(bits2) <= (FLINT_ABS(bits3) + FLINT_BIT_COUNT(len3) + FLINT_BITS - 2)
         && FLINT_ABS(bits3) <= FLINT_BITS - 2;

    /* alloc array of heap nodes which can be chained together */
    next_loc = len3 + 4;   /* something bigger than heap can ever be */
    heap = (mpoly_heap1_s *) TMP_ALLOC((len3 + 1)*sizeof(mpoly_heap1_s));
    chain = (mpoly_heap_t *) TMP_ALLOC(len3*sizeof(mpoly_heap_t));
    store = store_base = (slong *) TMP_ALLOC(2*len3*sizeof(mpoly_heap_t *));

    /* space for flagged heap indicies */
    hind = (slong *) TMP_ALLOC(len3*sizeof(slong));
    for (i = 0; i < len3; i++)
        hind[i] = 1;

    /* mask with high bit set in each field of exponent vector */
    mask = 0;
    for (i = 0; i < FLINT_BITS/bits; i++)
        mask = (mask << bits) + (UWORD(1) << (bits - 1));

    /* quotient poly indices start at -1 */
    k = -WORD(1);

    /* see description of divisor heap division in paper */
    s = len3;
   
    /* insert (-1, 0, exp2[0]) into heap */
    x = chain + 0;
    x->i = -WORD(1);
    x->j = 0;
    x->next = NULL;
    HEAP_ASSIGN(heap[1], exp2[0], x);

    /* precompute leading cofficient info assuming "small" case */
    lc_abs = FLINT_ABS(poly3[0]);
    lc_sign = FLINT_SIGN_EXT(poly3[0]);
    count_leading_zeros(lc_norm, lc_abs);
    lc_n = lc_abs << lc_norm;
    invert_limb(lc_i, lc_n);

    while (heap_len > 1)
    {
        exp = heap[1].exp;

        if (mpoly_monomial_overflows1(exp, mask))
            goto exp_overflow;

        k++;
        _fmpz_mpoly_fit_length(&p1, &e1, allocq, k + 1, 1);

        lt_divides = mpoly_monomial_divides1(e1 + k, exp, exp3[0], mask);

        /* take nodes from heap with exponent matching exp */
        if (small)
        {
            acc_sm[0] = acc_sm[1] = acc_sm[2] = 0;
            do
            {
                x = _mpoly_heap_pop1(heap, &heap_len, maskhi);
                do
                {
                    *store++ = x->i;
                    *store++ = x->j;
                    if (x->i != -WORD(1))
                        hind[x->i] |= WORD(1);

                    if (x->i == -WORD(1))
                        _fmpz_mpoly_add_uiuiui_fmpz(acc_sm, poly2 + x->j);
                    else
                        _fmpz_mpoly_submul_uiuiui_fmpz(acc_sm, poly3[x->i], p1[x->j]);
                } while ((x = x->next) != NULL);
            } while (heap_len > 1 && heap[1].exp == exp);
        } else
        {
            fmpz_zero(acc_lg);  
            do
            {
                x = _mpoly_heap_pop1(heap, &heap_len, maskhi);
                do
                {
                    *store++ = x->i;
                    *store++ = x->j;
                    if (x->i != -WORD(1))
                        hind[x->i] |= WORD(1);

                    if (x->i == -WORD(1))
                        fmpz_add(acc_lg, acc_lg, poly2 + x->j);
                    else
                        fmpz_submul(acc_lg, poly3 + x->i, p1 + x->j);
                } while ((x = x->next) != NULL);
            } while (heap_len > 1 && heap[1].exp == exp);
        }

        /* process nodes taken from the heap */
        while (store > store_base)
        {
            j = *--store;
            i = *--store;

            if (i == -WORD(1))
            {
                /* take next dividend term */
                if (j + 1 < len2)
                {
                    x = chain + 0;
                    x->i = i;
                    x->j = j + 1;
                    x->next = NULL;
                    _mpoly_heap_insert1(heap, exp2[x->j], x,
                                                 &next_loc, &heap_len, maskhi);
                }
            } else
            {
                /* should we go right? */
                if (  (i + 1 < len3)
                   && (hind[i + 1] == 2*j + 1)
                   )
                {
                    x = chain + i + 1;
                    x->i = i + 1;
                    x->j = j;
                    x->next = NULL;
                    hind[x->i] = 2*(x->j + 1) + 0;
                    _mpoly_heap_insert1(heap, exp3[x->i] + e1[x->j], x,
                                                 &next_loc, &heap_len, maskhi);
                }
                /* should we go up? */
                if (j + 1 == k)
                {
                    s++;
                } else if (  ((hind[i] & 1) == 1)
                          && ((i == 1) || (hind[i - 1] >= 2*(j + 2) + 1))
                          )
                {
                    x = chain + i;
                    x->i = i;
                    x->j = j + 1;
                    x->next = NULL;
                    hind[x->i] = 2*(x->j + 1) + 0;
                    _mpoly_heap_insert1(heap, exp3[x->i] + e1[x->j], x,
                                                 &next_loc, &heap_len, maskhi);
                }
            }
        }

        /* try to divide accumulated term by leading term */
        if (!lt_divides)
        {
            k--;
            continue;
        }
        if (small)
        {
            ulong d0, d1, ds = acc_sm[2];

            /* d1:d0 = abs(acc_sm[1:0]) assuming ds is sign extension of acc_sm[1] */
            sub_ddmmss(d1, d0, acc_sm[1]^ds, acc_sm[0]^ds, ds, ds);
            
            if ((acc_sm[0] | acc_sm[1] | acc_sm[2]) == 0)
            {
                k--;
                continue;
            }

            if (ds == FLINT_SIGN_EXT(acc_sm[1]) && d1 < lc_abs)
            {
                ulong qq, rr, nhi, nlo;
                nhi = (d1 << lc_norm) | (d0 >> (FLINT_BITS - lc_norm));
                nlo = d0 << lc_norm;
                udiv_qrnnd_preinv(qq, rr, nhi, nlo, lc_n, lc_i);
                (void) rr;
                if (qq == 0)
                {
                    k--;
                    continue;
                }
                if ((qq & (WORD(3) << (FLINT_BITS - 2))) == 0)
                {
                    _fmpz_demote(p1 + k);
                    p1[k] = (qq^ds^lc_sign) - (ds^lc_sign);
                } else
                {
                    small = 0;
                    fmpz_set_ui(p1 + k, qq);
                    if (ds != lc_sign)
                        fmpz_neg(p1 + k, p1 + k);
                }
            } else
            {
                small = 0;
                fmpz_set_signed_uiuiui(acc_lg, acc_sm[2], acc_sm[1], acc_sm[0]);
                goto large_lt_divides;
            }
        } else
        {
            if (fmpz_is_zero(acc_lg))
            {
                k--;
                continue;
            }
large_lt_divides:
            fmpz_fdiv_qr(p1 + k, r, acc_lg, poly3 + 0);
            if (fmpz_is_zero(p1 + k))
            {
                k--;
                continue;
            }
        }

        /* put newly generated quotient term back into the heap if neccesary */
        if (s > 1)
        {
            i = 1;
            x = chain + i;
            x->i = i;
            x->j = k;
            x->next = NULL;
            hind[x->i] = 2*(x->j + 1) + 0;
            _mpoly_heap_insert1(heap, exp3[x->i] + e1[x->j], x,
                                                 &next_loc, &heap_len, maskhi);
        }
        s = 1;
    }

    k++;

cleanup:

    fmpz_clear(acc_lg);
    fmpz_clear(r);

    (*polyq) = p1;
    (*expq) = e1;

    TMP_END;

    /* return quotient poly length */
    return k;

exp_overflow:
    for (i = 0; i < k; i++)
        _fmpz_demote(p1 + i);
    k = -WORD(1);
    goto cleanup;
}


slong _fmpz_mpoly_div_monagan_pearce(fmpz ** polyq,
           ulong ** expq, slong * allocq, const fmpz * poly2,
   const ulong * exp2, slong len2, const fmpz * poly3, const ulong * exp3, 
                   slong len3, slong bits, slong N, ulong maskhi, ulong masklo)
{
    slong i, j, k, s;
    slong next_loc;
    slong heap_len = 2; /* heap zero index unused */
    mpoly_heap_s * heap;
    mpoly_heap_t * chain;
    slong * store, * store_base;
    mpoly_heap_t * x;
    fmpz * p1 = *polyq;
    ulong * e1 = *expq;
    ulong * exp, * exps;
    ulong ** exp_list;
    slong exp_next;
    ulong mask;
    fmpz_t r, acc_lg;
    ulong acc_sm[3];
    slong * hind;
    int lt_divides, small;
    slong bits2, bits3;
    ulong lc_norm, lc_abs, lc_sign, lc_n, lc_i;
    TMP_INIT;

    /* if exponent vectors fit in one word, call specialised version */
    if (N == 1)
        return _fmpz_mpoly_div_monagan_pearce1(polyq, expq, allocq,
                           poly2, exp2, len2, poly3, exp3, len3, bits, maskhi);

    TMP_START;

    fmpz_init(acc_lg);
    fmpz_init(r);

    /* whether intermediate computations q - a*b will fit in three words */
    bits2 = _fmpz_vec_max_bits(poly2, len2);
    bits3 = _fmpz_vec_max_bits(poly3, len3);
    /* allow one bit for sign, one bit for subtraction */
    small = FLINT_ABS(bits2) <= (FLINT_ABS(bits3) + FLINT_BIT_COUNT(len3) + FLINT_BITS - 2)
         && FLINT_ABS(bits3) <= FLINT_BITS - 2;


    /* alloc array of heap nodes which can be chained together */
    next_loc = len3 + 4;   /* something bigger than heap can ever be */
    heap = (mpoly_heap_s *) TMP_ALLOC((len3 + 1)*sizeof(mpoly_heap_s));
    chain = (mpoly_heap_t *) TMP_ALLOC(len3*sizeof(mpoly_heap_t));
    store = store_base = (slong *) TMP_ALLOC(2*len3*sizeof(mpoly_heap_t *));

    /* array of exponent vectors, each of "N" words */
    exps = (ulong *) TMP_ALLOC(len3*N*sizeof(ulong));
    /* list of pointers to available exponent vectors */
    exp_list = (ulong **) TMP_ALLOC(len3*sizeof(ulong *));
    /* space to save copy of current exponent vector */
    exp = (ulong *) TMP_ALLOC(N*sizeof(ulong));
    /* set up list of available exponent vectors */
    exp_next = 0;
    for (i = 0; i < len3; i++)
        exp_list[i] = exps + i*N;

    /* space for flagged heap indicies */
    hind = (slong *) TMP_ALLOC(len3*sizeof(slong));
    for (i = 0; i < len3; i++)
        hind[i] = 1;

    /* mask with high bit set in each word of each field of exponent vector */
    mask = 0;
    for (i = 0; i < FLINT_BITS/bits; i++)
        mask = (mask << bits) + (UWORD(1) << (bits - 1));

    /* quotient and poly index starts at -1 */
    k = -WORD(1);
   
    /* s is the number of terms * (latest quotient) we should put into heap */
    s = len3;
   
    /* insert (-1, 0, exp2[0]) into heap */
    x = chain + 0;
    x->i = -WORD(1);
    x->j = 0;
    x->next = NULL;
    heap[1].next = x;
    heap[1].exp = exp_list[exp_next++];
    mpoly_monomial_set(heap[1].exp, exp2, N);

    /* precompute leading cofficient info assuming "small" case */
    lc_abs = FLINT_ABS(poly3[0]);
    lc_sign = FLINT_SIGN_EXT(poly3[0]);
    count_leading_zeros(lc_norm, lc_abs);
    lc_n = lc_abs << lc_norm;
    invert_limb(lc_i, lc_n);

    while (heap_len > 1)
    {
        mpoly_monomial_set(exp, heap[1].exp, N);

        if (mpoly_monomial_overflows(exp, N, mask))
            goto exp_overflow;
      
        k++;
        _fmpz_mpoly_fit_length(&p1, &e1, allocq, k + 1, N);

        lt_divides = mpoly_monomial_divides(e1 + k*N, exp, exp3, N, mask);

        /* take nodes from heap with exponent matching exp */
        if (small) 
        {
            acc_sm[0] = acc_sm[1] = acc_sm[2] = 0;
            do
            {
                exp_list[--exp_next] = heap[1].exp;
                x = _mpoly_heap_pop(heap, &heap_len, N, maskhi, masklo);
                do
                {
                    *store++ = x->i;
                    *store++ = x->j;
                    if (x->i != -WORD(1))
                        hind[x->i] |= WORD(1);

                    if (x->i == -WORD(1))
                        _fmpz_mpoly_add_uiuiui_fmpz(acc_sm, poly2 + x->j);
                    else
                        _fmpz_mpoly_submul_uiuiui_fmpz(acc_sm, poly3[x->i], p1[x->j]);
                } while ((x = x->next) != NULL);
            } while (heap_len > 1 && mpoly_monomial_equal(heap[1].exp, exp, N));
        } else
        {
            fmpz_zero(acc_lg);
            do
            {
                exp_list[--exp_next] = heap[1].exp;
                x = _mpoly_heap_pop(heap, &heap_len, N, maskhi, masklo);
                do
                {
                    *store++ = x->i;
                    *store++ = x->j;
                    if (x->i != -WORD(1))
                        hind[x->i] |= WORD(1);

                    if (x->i == -WORD(1))
                        fmpz_add(acc_lg, acc_lg, poly2 + x->j);
                    else
                        fmpz_submul(acc_lg, poly3 + x->i, p1 + x->j);
                } while ((x = x->next) != NULL);
            } while (heap_len > 1 && mpoly_monomial_equal(heap[1].exp, exp, N));
        }

        /* process nodes taken from the heap */
        while (store > store_base)
        {
            j = *--store;
            i = *--store;

            if (i == -WORD(1))
            {
                /* take next dividend term */
                if (j + 1 < len2)
                {
                    x = chain + 0;
                    x->i = i;
                    x->j = j + 1;
                    x->next = NULL;
                    mpoly_monomial_set(exp_list[exp_next], exp2 + x->j*N, N);
                    if (!_mpoly_heap_insert(heap, exp_list[exp_next++], x,
                                      &next_loc, &heap_len, N, maskhi, masklo))
                        exp_next--;
                }
            } else
            {
                /* should we go up */
                if (  (i + 1 < len3)
                   && (hind[i + 1] == 2*j + 1)
                   )
                {
                    x = chain + i + 1;
                    x->i = i + 1;
                    x->j = j;
                    x->next = NULL;
                    hind[x->i] = 2*(x->j + 1) + 0;
                    mpoly_monomial_add(exp_list[exp_next], exp3 + x->i*N,
                                                           e1   + x->j*N, N);
                    if (!_mpoly_heap_insert(heap, exp_list[exp_next++], x,
                                      &next_loc, &heap_len, N, maskhi, masklo))
                        exp_next--;
                }
                /* should we go up? */
                if (j + 1 == k)
                {
                    s++;
                } else if (  ((hind[i] & 1) == 1)
                          && ((i == 1) || (hind[i - 1] >= 2*(j + 2) + 1))
                          )
                {
                    x = chain + i;
                    x->i = i;
                    x->j = j + 1;
                    x->next = NULL;
                    hind[x->i] = 2*(x->j + 1) + 0;
                    mpoly_monomial_add(exp_list[exp_next], exp3 + x->i*N,
                                                           e1   + x->j*N, N);
                    if (!_mpoly_heap_insert(heap, exp_list[exp_next++], x,
                                      &next_loc, &heap_len, N, maskhi, masklo))
                        exp_next--;
                }
            }
        }

        /* try to divide accumulated term by leading term */
        if (!lt_divides)
        {
            k--;
            continue;
        }
        if (small)
        {
            ulong d0, d1, ds = acc_sm[2];

            /* d1:d0 = abs(acc_sm[1:0]) assuming ds is sign extension of acc_sm[1] */
            sub_ddmmss(d1, d0, acc_sm[1]^ds, acc_sm[0]^ds, ds, ds);
            
            if ((acc_sm[0] | acc_sm[1] | acc_sm[2]) == 0)
            {
                k--;
                continue;
            }

            if (ds == FLINT_SIGN_EXT(acc_sm[1]) && d1 < lc_abs)
            {
                ulong qq, rr, nhi, nlo;
                nhi = (d1 << lc_norm) | (d0 >> (FLINT_BITS - lc_norm));
                nlo = d0 << lc_norm;
                udiv_qrnnd_preinv(qq, rr, nhi, nlo, lc_n, lc_i);
                (void) rr;
                if (qq == 0)
                {
                    k--;
                    continue;
                }
                if ((qq & (WORD(3) << (FLINT_BITS - 2))) == 0)
                {
                    _fmpz_demote(p1 + k);
                    p1[k] = (qq^ds^lc_sign) - (ds^lc_sign);
                } else
                {
                    small = 0;
                    fmpz_set_ui(p1 + k, qq);
                    if (ds != lc_sign)
                        fmpz_neg(p1 + k, p1 + k);
                }
            } else
            {
                small = 0;
                fmpz_set_signed_uiuiui(acc_lg, acc_sm[2], acc_sm[1], acc_sm[0]);
                goto large_lt_divides;
            }
        } else
        {
            if (fmpz_is_zero(acc_lg))
            {
                k--;
                continue;
            }
large_lt_divides:
            fmpz_fdiv_qr(p1 + k, r, acc_lg, poly3 + 0);
            if (fmpz_is_zero(p1 + k))
            {
                k--;
                continue;
            }
        }

        /* put newly generated quotient term back into the heap if neccesary */
        if (s > 1)
        {
            i = 1;
            x = chain + i;
            x->i = i;
            x->j = k;
            x->next = NULL;
            hind[x->i] = 2*(x->j + 1) + 0;
            mpoly_monomial_add(exp_list[exp_next], exp3 + x->i*N,
                                                   e1   + x->j*N, N);
            if (!_mpoly_heap_insert(heap, exp_list[exp_next++], x,
                                  &next_loc, &heap_len, N, maskhi, masklo))
                exp_next--;
        }
        s = 1;
    }

    k++;

cleanup:

    fmpz_clear(acc_lg);
    fmpz_clear(r);

    (*polyq) = p1;
    (*expq) = e1;

    TMP_END;

    /* return quotient poly length */
    return k;

exp_overflow:
    for (i = 0; i < k; i++)
        _fmpz_demote(p1 + i);
    k = -WORD(1);
    goto cleanup;

}

void fmpz_mpoly_div_monagan_pearce(fmpz_mpoly_t q, const fmpz_mpoly_t poly2, 
                          const fmpz_mpoly_t poly3, const fmpz_mpoly_ctx_t ctx)
{
   slong exp_bits, N, lenq = 0;
   ulong * exp2 = poly2->exps, * exp3 = poly3->exps;
   ulong maskhi, masklo;
   int free2 = 0, free3 = 0;
   fmpz_mpoly_t temp1;
   fmpz_mpoly_struct * tq;

   /* check divisor is nonzero */
   if (poly3->length == 0)
      flint_throw(FLINT_DIVZERO, "Divide by zero in fmpz_mpoly_div_monagan_pearce");

   /* dividend zero, write out quotient */
   if (poly2->length == 0)
   {
      fmpz_mpoly_zero(q, ctx);
 
      return;
   }

   /* maximum bits in quotient exps and inputs is max for poly2 and poly3 */
   exp_bits = FLINT_MAX(poly2->bits, poly3->bits);

   masks_from_bits_ord(maskhi, masklo, exp_bits, ctx->ord);
   N = words_per_exp(ctx->n, exp_bits);

   /* ensure input exponents packed to same size as output exponents */
   if (exp_bits > poly2->bits)
   {
      free2 = 1;
      exp2 = (ulong *) flint_malloc(N*poly2->length*sizeof(ulong));
      mpoly_unpack_monomials(exp2, exp_bits, poly2->exps, poly2->bits,
                                                        poly2->length, ctx->n);
   }

   if (exp_bits > poly3->bits)
   {
      free3 = 1;
      exp3 = (ulong *) flint_malloc(N*poly3->length*sizeof(ulong));
      mpoly_unpack_monomials(exp3, exp_bits, poly3->exps, poly3->bits,
                                                        poly3->length, ctx->n);
   }

   /* check divisor leading monomial is at most that of the dividend */
   if (mpoly_monomial_lt(exp3, exp2, N, maskhi, masklo))
   {
      fmpz_mpoly_zero(q, ctx);

      goto cleanup3;
   }

   /* take care of aliasing */
   if (q == poly2 || q == poly3)
   {
      fmpz_mpoly_init2(temp1, FLINT_MAX(poly2->length/poly3->length + 1, 1),
                                                                          ctx);
      fmpz_mpoly_fit_bits(temp1, exp_bits, ctx);
      temp1->bits = exp_bits;

      tq = temp1;
   } else
   {
      fmpz_mpoly_fit_length(q, FLINT_MAX(poly2->length/poly3->length + 1, 1),
                                                                          ctx);
      fmpz_mpoly_fit_bits(q, exp_bits, ctx);
      q->bits = exp_bits;

      tq = q;
   }

   /* do division with remainder */
   while ((lenq = _fmpz_mpoly_div_monagan_pearce(&tq->coeffs, &tq->exps,
                         &tq->alloc, poly2->coeffs, exp2, poly2->length, 
                                     poly3->coeffs, exp3, poly3->length,
                                      exp_bits, N, maskhi, masklo)) == -WORD(1)
            && exp_bits < FLINT_BITS)
   {
      ulong * old_exp2 = exp2, * old_exp3 = exp3;
      slong old_exp_bits = exp_bits;

      exp_bits = mpoly_optimize_bits(exp_bits + 1, ctx->n);

      masks_from_bits_ord(maskhi, masklo, exp_bits, ctx->ord);
      N = words_per_exp(ctx->n, exp_bits);

      exp2 = (ulong *) flint_malloc(N*poly2->length*sizeof(ulong));
      mpoly_unpack_monomials(exp2, exp_bits, old_exp2, old_exp_bits,
                                                        poly2->length, ctx->n);

      exp3 = (ulong *) flint_malloc(N*poly3->length*sizeof(ulong));
      mpoly_unpack_monomials(exp3, exp_bits, old_exp3, old_exp_bits,
                                                        poly3->length, ctx->n);

      if (free2)
         flint_free(old_exp2);

      if (free3)
         flint_free(old_exp3);

      free2 = free3 = 1; 

      fmpz_mpoly_fit_bits(tq, exp_bits, ctx);
      tq->bits = exp_bits;
   }

   if (lenq == -WORD(1))
      flint_throw(FLINT_EXPOF,
                      "Exponent overflow in fmpz_mpoly_div_monagan_pearce");

   /* take care of aliasing */
   if (q == poly2 || q == poly3)
   {
      fmpz_mpoly_swap(temp1, q, ctx);

      fmpz_mpoly_clear(temp1, ctx);
   } 

   _fmpz_mpoly_set_length(q, lenq, ctx);

cleanup3:

   if (free2)
      flint_free(exp2);

   if (free3)
      flint_free(exp3);
}