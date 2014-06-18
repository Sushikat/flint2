/*=============================================================================

    This file is part of FLINT.

    FLINT is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    FLINT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FLINT; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

=============================================================================*/
/******************************************************************************

    Copyright (C) 2014 Abhinav Baid

******************************************************************************/

#include "fmpz_mat.h"
#include "fmpq_mat.h"

int
fmpz_mat_is_reduced(const fmpz_mat_t A, double delta, double eta)
{
    slong i, j, k, d = A->r, n = A->c;
    fmpq_mat_t r, mu;
    fmpq *s;
    mpq_t deltax, etax;
    fmpq_t deltaq, etaq, tmp;

    if (d == 0 || d == 1)
        return 1;

    fmpq_mat_init(r, d, d);
    fmpq_mat_init(mu, d, d);

    s = _fmpq_vec_init(d);

    mpq_init(deltax);
    mpq_init(etax);

    fmpq_init(deltaq);
    fmpq_init(etaq);
    fmpq_init(tmp);

    mpq_set_d(deltax, delta);
    mpq_set_d(etax, eta);
    fmpq_set_mpq(deltaq, deltax);
    fmpq_set_mpq(etaq, etax);
    mpq_clears(deltax, etax, '\0');

    _fmpz_vec_dot(fmpq_mat_entry_num(r, 0, 0), A->rows[0], A->rows[0], n);

    for (i = 1; i < d; i++)
    {
        _fmpz_vec_dot(fmpq_numref(s), A->rows[i], A->rows[i], n);
        fmpz_one(fmpq_denref(s));
        for (j = 0; j <= i - 1; j++)
        {
            _fmpz_vec_dot(fmpq_mat_entry_num(r, i, j), A->rows[i], A->rows[j],
                          n);
            for (k = 0; k <= j - 1; k++)
            {
                fmpq_submul(fmpq_mat_entry(r, i, j), fmpq_mat_entry(mu, j, k),
                            fmpq_mat_entry(r, i, k));
            }
            fmpq_div(fmpq_mat_entry(mu, i, j), fmpq_mat_entry(r, i, j),
                     fmpq_mat_entry(r, j, j));
            fmpq_abs(tmp, fmpq_mat_entry(mu, i, j));
            if (fmpq_cmp(tmp, etaq) > 0)    /* check size reduction */
            {
                fmpq_mat_clear(r);
                fmpq_mat_clear(mu);
                fmpq_clear(deltaq);
                fmpq_clear(etaq);
                fmpq_clear(tmp);
                _fmpq_vec_clear(s, d);
                return 0;
            }
            fmpq_set(s + j + 1, s + j);
            fmpq_submul(s + j + 1, fmpq_mat_entry(mu, i, j),
                        fmpq_mat_entry(r, i, j));
        }
        fmpq_set(fmpq_mat_entry(r, i, i), s + i);
        fmpq_mul(tmp, deltaq, fmpq_mat_entry(r, i - 1, i - 1));
        if (fmpq_cmp(tmp, s + i - 1) > 0)   /* check Lovasz condition */
        {
            fmpq_mat_clear(r);
            fmpq_mat_clear(mu);
            fmpq_clear(deltaq);
            fmpq_clear(etaq);
            fmpq_clear(tmp);
            _fmpq_vec_clear(s, d);
            return 0;
        }
    }
    fmpq_mat_clear(r);
    fmpq_mat_clear(mu);
    fmpq_clear(deltaq);
    fmpq_clear(etaq);
    fmpq_clear(tmp);
    _fmpq_vec_clear(s, d);
    return 1;
}
