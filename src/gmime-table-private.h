/* @file gmime-table-private.h
 * @brief character tables fro gmime.
 *
 *  This file was copied from GMime rev. 496313fb
 *
 * Copyright (C) 2000-2014 Jeffrey Stedfast
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

static unsigned short gmime_special_table[256] = {
    1029,1029,1029,1029,1029,1029,1029,1029,1029,3175,1031,1029,1029,1063,1029,1029,
    1029,1029,1029,1029,1029,1029,1029,1029,1029,1029,1029,1029,1029,1029,1029,1029,
    3314,1984,1100,1728,1728,1216,1728,1216,1100,1100,1472,1984,1100,1984,1608,1348,
    1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1100,1100,1100,1284,1100,1092,
    1100,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,
    1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1132,1260,1132,1728,1856,
    1728,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,
    1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1984,1728,1728,1728,1728,1029,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

enum {
    IS_CTRL     = (1 << 0),
    IS_LWSP     = (1 << 1),
    IS_TSPECIAL = (1 << 2),
    IS_SPECIAL  = (1 << 3),
    IS_SPACE    = (1 << 4),
    IS_DSPECIAL = (1 << 5),
    IS_QPSAFE   = (1 << 6),
    IS_ESAFE    = (1 << 7),  /* encoded word safe */
    IS_PSAFE    = (1 << 8),  /* encode word in phrase safe */
    IS_ATTRCHAR = (1 << 9),  /* attribute-char from rfc2184 */

    /* ctype replacements */
    IS_ASCII    = (1 << 10), /* ascii */
    IS_BLANK    = (1 << 11), /* space or tab */
};

#define is_ctrl(x) ((gmime_special_table[(unsigned char)(x)] & IS_CTRL) != 0)
#define is_lwsp(x) ((gmime_special_table[(unsigned char)(x)] & IS_LWSP) != 0)
#define is_tspecial(x) ((gmime_special_table[(unsigned char)(x)] & IS_TSPECIAL) != 0)
#define is_type(x, t) ((gmime_special_table[(unsigned char)(x)] & (t)) != 0)
#define is_ttoken(x) ((gmime_special_table[(unsigned char)(x)] & (IS_TSPECIAL|IS_LWSP|IS_CTRL)) == 0)
#define is_atom(x) ((gmime_special_table[(unsigned char)(x)] & (IS_SPECIAL|IS_SPACE|IS_CTRL)) == 0)
#define is_dtext(x) ((gmime_special_table[(unsigned char)(x)] & IS_DSPECIAL) == 0)
#define is_fieldname(x) ((gmime_special_table[(unsigned char)(x)] & (IS_CTRL|IS_SPACE)) == 0)
#define is_qpsafe(x) ((gmime_special_table[(unsigned char)(x)] & IS_QPSAFE) != 0)
#define is_especial(x) ((gmime_special_table[(unsigned char)(x)] & IS_ESAFE) != 0)
#define is_psafe(x) ((gmime_special_table[(unsigned char)(x)] & IS_PSAFE) != 0)
#define is_attrchar(x) ((gmime_special_table[(unsigned char)(x)] & IS_ATTRCHAR) != 0)

/* ctype replacements */
#define is_ascii(x) ((gmime_special_table[(unsigned char)(x)] & IS_ASCII) != 0)
#define is_blank(x) ((gmime_special_table[(unsigned char)(x)] & IS_BLANK) != 0)

#define CHARS_LWSP " \t\n\r"               /* linear whitespace chars */
#define CHARS_TSPECIAL "()<>@,;:\\\"/[]?="
#define CHARS_SPECIAL "()<>@,;:\\\".[]"
#define CHARS_CSPECIAL "()\\\r"	           /* not in comments */
#define CHARS_DSPECIAL "[]\\\r \t"         /* not in domains */
#define CHARS_ESPECIAL "()<>@,;:\"/[]?.=_" /* encoded word specials (rfc2047 5.1) */
#define CHARS_PSPECIAL "!*+-/=_"           /* encoded phrase specials (rfc2047 5.3) */
#define CHARS_ATTRCHAR "*'% "              /* attribute-char from rfc2184 */

#define GMIME_FOLD_LEN 78
