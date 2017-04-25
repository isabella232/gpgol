/* serpent.c - Implementation of the Serpent encryption algorithm.
 * Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
 *
 * This file is part of Libgcrypt.
 *
 * Libgcrypt is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser general Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * Libgcrypt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

/* 
   Requirements:

   Big endian machines need to define WORDS_BIGENDIAN

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif 

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "serpent.h"

/* Serpent is a 128 bit block cipher.  */
#define BLOCKSIZE 16

/* Number of rounds per Serpent encrypt/decrypt operation.  */
#define ROUNDS 32

/* Magic number, used during generating of the subkeys.  */
#define PHI 0x9E3779B9

/* Serpent works on 128 bit blocks.  */
typedef uint32_t serpent_block_t[4];

/* Serpent key, provided by the user.  If the original key is shorter
   than 256 bits, it is padded.  */
typedef uint32_t serpent_key_t[8];

/* The key schedule consists of 33 128 bit subkeys.  */
typedef uint32_t serpent_subkeys_t[ROUNDS + 1][4];

/* A Serpent context.  Note that the public API uses symenc_t
   instead.  */
struct symenc_context_s
{
  serpent_subkeys_t keys;	/* Generated subkeys. */

  union {
    uint32_t dummy;             /* Force alignment.  */
    struct {
      unsigned char iv[BLOCKSIZE];	/* The IV. */
      unsigned int unused;              /* Unused bytes in the IV. */
    } cfb;
  } u;
};
typedef struct symenc_context_s serpent_context_t;


/* A prototype.  */
static const char *serpent_test (void);
      
#define rol(x,n) ( ((x) << (n)) | ((x) >> (32-(n))) )
#define ror(x,n) ( ((x) >> (n)) | ((x) << (32-(n))) )

#define byte_swap_32(x) \
  (0 \
   | (((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) \
   | (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))

/* These are the S-Boxes of Serpent.  They are copied from Serpents
   reference implementation (the optimized one, contained in
   `floppy2') and are therefore:

     Copyright (C) 1998 Ross Anderson, Eli Biham, Lars Knudsen.

  To quote the Serpent homepage
  (http://www.cl.cam.ac.uk/~rja14/serpent.html):

  "Serpent is now completely in the public domain, and we impose no
   restrictions on its use.  This was announced on the 21st August at
   the First AES Candidate Conference. The optimised implementations
   in the submission package are now under the GNU PUBLIC LICENSE
   (GPL), although some comments in the code still say otherwise. You
   are welcome to use Serpent for any application."  */

#define SBOX0(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t05, t06, t07, t08, t09; \
    uint32_t t11, t12, t13, t14, t15, t17, t01; \
    t01 = b   ^ c  ; \
    t02 = a   | d  ; \
    t03 = a   ^ b  ; \
    z   = t02 ^ t01; \
    t05 = c   | z  ; \
    t06 = a   ^ d  ; \
    t07 = b   | c  ; \
    t08 = d   & t05; \
    t09 = t03 & t07; \
    y   = t09 ^ t08; \
    t11 = t09 & y  ; \
    t12 = c   ^ d  ; \
    t13 = t07 ^ t11; \
    t14 = b   & t06; \
    t15 = t06 ^ t13; \
    w   =     ~ t15; \
    t17 = w   ^ t14; \
    x   = t12 ^ t17; \
  }

#define SBOX0_INVERSE(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t06, t08, t09, t10; \
    uint32_t t12, t13, t14, t15, t17, t18, t01; \
    t01 = c   ^ d  ; \
    t02 = a   | b  ; \
    t03 = b   | c  ; \
    t04 = c   & t01; \
    t05 = t02 ^ t01; \
    t06 = a   | t04; \
    y   =     ~ t05; \
    t08 = b   ^ d  ; \
    t09 = t03 & t08; \
    t10 = d   | y  ; \
    x   = t09 ^ t06; \
    t12 = a   | t05; \
    t13 = x   ^ t12; \
    t14 = t03 ^ t10; \
    t15 = a   ^ c  ; \
    z   = t14 ^ t13; \
    t17 = t05 & t13; \
    t18 = t14 | t17; \
    w   = t15 ^ t18; \
  }

#define SBOX1(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t06, t07, t08; \
    uint32_t t10, t11, t12, t13, t16, t17, t01; \
    t01 = a   | d  ; \
    t02 = c   ^ d  ; \
    t03 =     ~ b  ; \
    t04 = a   ^ c  ; \
    t05 = a   | t03; \
    t06 = d   & t04; \
    t07 = t01 & t02; \
    t08 = b   | t06; \
    y   = t02 ^ t05; \
    t10 = t07 ^ t08; \
    t11 = t01 ^ t10; \
    t12 = y   ^ t11; \
    t13 = b   & d  ; \
    z   =     ~ t10; \
    x   = t13 ^ t12; \
    t16 = t10 | x  ; \
    t17 = t05 & t16; \
    w   = c   ^ t17; \
  }

#define SBOX1_INVERSE(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t06, t07, t08; \
    uint32_t t09, t10, t11, t14, t15, t17, t01; \
    t01 = a   ^ b  ; \
    t02 = b   | d  ; \
    t03 = a   & c  ; \
    t04 = c   ^ t02; \
    t05 = a   | t04; \
    t06 = t01 & t05; \
    t07 = d   | t03; \
    t08 = b   ^ t06; \
    t09 = t07 ^ t06; \
    t10 = t04 | t03; \
    t11 = d   & t08; \
    y   =     ~ t09; \
    x   = t10 ^ t11; \
    t14 = a   | y  ; \
    t15 = t06 ^ x  ; \
    z   = t01 ^ t04; \
    t17 = c   ^ t15; \
    w   = t14 ^ t17; \
  }

#define SBOX2(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t05, t06, t07, t08; \
    uint32_t t09, t10, t12, t13, t14, t01; \
    t01 = a   | c  ; \
    t02 = a   ^ b  ; \
    t03 = d   ^ t01; \
    w   = t02 ^ t03; \
    t05 = c   ^ w  ; \
    t06 = b   ^ t05; \
    t07 = b   | t05; \
    t08 = t01 & t06; \
    t09 = t03 ^ t07; \
    t10 = t02 | t09; \
    x   = t10 ^ t08; \
    t12 = a   | d  ; \
    t13 = t09 ^ x  ; \
    t14 = b   ^ t13; \
    z   =     ~ t09; \
    y   = t12 ^ t14; \
  }

#define SBOX2_INVERSE(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t06, t07, t08, t09; \
    uint32_t t10, t11, t12, t15, t16, t17, t01; \
    t01 = a   ^ d  ; \
    t02 = c   ^ d  ; \
    t03 = a   & c  ; \
    t04 = b   | t02; \
    w   = t01 ^ t04; \
    t06 = a   | c  ; \
    t07 = d   | w  ; \
    t08 =     ~ d  ; \
    t09 = b   & t06; \
    t10 = t08 | t03; \
    t11 = b   & t07; \
    t12 = t06 & t02; \
    z   = t09 ^ t10; \
    x   = t12 ^ t11; \
    t15 = c   & z  ; \
    t16 = w   ^ x  ; \
    t17 = t10 ^ t15; \
    y   = t16 ^ t17; \
  }

#define SBOX3(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t06, t07, t08; \
    uint32_t t09, t10, t11, t13, t14, t15, t01; \
    t01 = a   ^ c  ; \
    t02 = a   | d  ; \
    t03 = a   & d  ; \
    t04 = t01 & t02; \
    t05 = b   | t03; \
    t06 = a   & b  ; \
    t07 = d   ^ t04; \
    t08 = c   | t06; \
    t09 = b   ^ t07; \
    t10 = d   & t05; \
    t11 = t02 ^ t10; \
    z   = t08 ^ t09; \
    t13 = d   | z  ; \
    t14 = a   | t07; \
    t15 = b   & t13; \
    y   = t08 ^ t11; \
    w   = t14 ^ t15; \
    x   = t05 ^ t04; \
  }

#define SBOX3_INVERSE(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t06, t07, t09; \
    uint32_t t11, t12, t13, t14, t16, t01; \
    t01 = c   | d  ; \
    t02 = a   | d  ; \
    t03 = c   ^ t02; \
    t04 = b   ^ t02; \
    t05 = a   ^ d  ; \
    t06 = t04 & t03; \
    t07 = b   & t01; \
    y   = t05 ^ t06; \
    t09 = a   ^ t03; \
    w   = t07 ^ t03; \
    t11 = w   | t05; \
    t12 = t09 & t11; \
    t13 = a   & y  ; \
    t14 = t01 ^ t05; \
    x   = b   ^ t12; \
    t16 = b   | t13; \
    z   = t14 ^ t16; \
  }

#define SBOX4(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t06, t08, t09; \
    uint32_t t10, t11, t12, t13, t14, t15, t16, t01; \
    t01 = a   | b  ; \
    t02 = b   | c  ; \
    t03 = a   ^ t02; \
    t04 = b   ^ d  ; \
    t05 = d   | t03; \
    t06 = d   & t01; \
    z   = t03 ^ t06; \
    t08 = z   & t04; \
    t09 = t04 & t05; \
    t10 = c   ^ t06; \
    t11 = b   & c  ; \
    t12 = t04 ^ t08; \
    t13 = t11 | t03; \
    t14 = t10 ^ t09; \
    t15 = a   & t05; \
    t16 = t11 | t12; \
    y   = t13 ^ t08; \
    x   = t15 ^ t16; \
    w   =     ~ t14; \
  }

#define SBOX4_INVERSE(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t06, t07, t09; \
    uint32_t t10, t11, t12, t13, t15, t01; \
    t01 = b   | d  ; \
    t02 = c   | d  ; \
    t03 = a   & t01; \
    t04 = b   ^ t02; \
    t05 = c   ^ d  ; \
    t06 =     ~ t03; \
    t07 = a   & t04; \
    x   = t05 ^ t07; \
    t09 = x   | t06; \
    t10 = a   ^ t07; \
    t11 = t01 ^ t09; \
    t12 = d   ^ t04; \
    t13 = c   | t10; \
    z   = t03 ^ t12; \
    t15 = a   ^ t04; \
    y   = t11 ^ t13; \
    w   = t15 ^ t09; \
  }

#define SBOX5(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t07, t08, t09; \
    uint32_t t10, t11, t12, t13, t14, t01; \
    t01 = b   ^ d  ; \
    t02 = b   | d  ; \
    t03 = a   & t01; \
    t04 = c   ^ t02; \
    t05 = t03 ^ t04; \
    w   =     ~ t05; \
    t07 = a   ^ t01; \
    t08 = d   | w  ; \
    t09 = b   | t05; \
    t10 = d   ^ t08; \
    t11 = b   | t07; \
    t12 = t03 | w  ; \
    t13 = t07 | t10; \
    t14 = t01 ^ t11; \
    y   = t09 ^ t13; \
    x   = t07 ^ t08; \
    z   = t12 ^ t14; \
  }

#define SBOX5_INVERSE(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t07, t08, t09; \
    uint32_t t10, t12, t13, t15, t16, t01; \
    t01 = a   & d  ; \
    t02 = c   ^ t01; \
    t03 = a   ^ d  ; \
    t04 = b   & t02; \
    t05 = a   & c  ; \
    w   = t03 ^ t04; \
    t07 = a   & w  ; \
    t08 = t01 ^ w  ; \
    t09 = b   | t05; \
    t10 =     ~ b  ; \
    x   = t08 ^ t09; \
    t12 = t10 | t07; \
    t13 = w   | x  ; \
    z   = t02 ^ t12; \
    t15 = t02 ^ t13; \
    t16 = b   ^ d  ; \
    y   = t16 ^ t15; \
  }

#define SBOX6(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t07, t08, t09, t10; \
    uint32_t t11, t12, t13, t15, t17, t18, t01; \
    t01 = a   & d  ; \
    t02 = b   ^ c  ; \
    t03 = a   ^ d  ; \
    t04 = t01 ^ t02; \
    t05 = b   | c  ; \
    x   =     ~ t04; \
    t07 = t03 & t05; \
    t08 = b   & x  ; \
    t09 = a   | c  ; \
    t10 = t07 ^ t08; \
    t11 = b   | d  ; \
    t12 = c   ^ t11; \
    t13 = t09 ^ t10; \
    y   =     ~ t13; \
    t15 = x   & t03; \
    z   = t12 ^ t07; \
    t17 = a   ^ b  ; \
    t18 = y   ^ t15; \
    w   = t17 ^ t18; \
  }

#define SBOX6_INVERSE(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t06, t07, t08, t09; \
    uint32_t t12, t13, t14, t15, t16, t17, t01; \
    t01 = a   ^ c  ; \
    t02 =     ~ c  ; \
    t03 = b   & t01; \
    t04 = b   | t02; \
    t05 = d   | t03; \
    t06 = b   ^ d  ; \
    t07 = a   & t04; \
    t08 = a   | t02; \
    t09 = t07 ^ t05; \
    x   = t06 ^ t08; \
    w   =     ~ t09; \
    t12 = b   & w  ; \
    t13 = t01 & t05; \
    t14 = t01 ^ t12; \
    t15 = t07 ^ t13; \
    t16 = d   | t02; \
    t17 = a   ^ x  ; \
    z   = t17 ^ t15; \
    y   = t16 ^ t14; \
  }

#define SBOX7(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t05, t06, t08, t09, t10; \
    uint32_t t11, t13, t14, t15, t16, t17, t01; \
    t01 = a   & c  ; \
    t02 =     ~ d  ; \
    t03 = a   & t02; \
    t04 = b   | t01; \
    t05 = a   & b  ; \
    t06 = c   ^ t04; \
    z   = t03 ^ t06; \
    t08 = c   | z  ; \
    t09 = d   | t05; \
    t10 = a   ^ t08; \
    t11 = t04 & z  ; \
    x   = t09 ^ t10; \
    t13 = b   ^ x  ; \
    t14 = t01 ^ x  ; \
    t15 = c   ^ t05; \
    t16 = t11 | t13; \
    t17 = t02 | t14; \
    w   = t15 ^ t17; \
    y   = a   ^ t16; \
  }

#define SBOX7_INVERSE(a, b, c, d, w, x, y, z) \
  { \
    uint32_t t02, t03, t04, t06, t07, t08, t09; \
    uint32_t t10, t11, t13, t14, t15, t16, t01; \
    t01 = a   & b  ; \
    t02 = a   | b  ; \
    t03 = c   | t01; \
    t04 = d   & t02; \
    z   = t03 ^ t04; \
    t06 = b   ^ t04; \
    t07 = d   ^ z  ; \
    t08 =     ~ t07; \
    t09 = t06 | t08; \
    t10 = b   ^ d  ; \
    t11 = a   | d  ; \
    x   = a   ^ t09; \
    t13 = c   ^ t06; \
    t14 = c   & t11; \
    t15 = d   | x  ; \
    t16 = t01 | t10; \
    w   = t13 ^ t15; \
    y   = t14 ^ t16; \
  }

/* XOR BLOCK1 into BLOCK0.  */
#define BLOCK_XOR(block0, block1) \
  {                               \
    block0[0] ^= block1[0];       \
    block0[1] ^= block1[1];       \
    block0[2] ^= block1[2];       \
    block0[3] ^= block1[3];       \
  }

/* Copy BLOCK_SRC to BLOCK_DST.  */
#define BLOCK_COPY(block_dst, block_src) \
  {                                      \
    block_dst[0] = block_src[0];         \
    block_dst[1] = block_src[1];         \
    block_dst[2] = block_src[2];         \
    block_dst[3] = block_src[3];         \
  }

/* Apply SBOX number WHICH to to the block found in ARRAY0 at index
   INDEX, writing the output to the block found in ARRAY1 at index
   INDEX.  */
#define SBOX(which, array0, array1, index)            \
  SBOX##which (array0[index + 0], array0[index + 1],  \
               array0[index + 2], array0[index + 3],  \
               array1[index + 0], array1[index + 1],  \
               array1[index + 2], array1[index + 3]);

/* Apply inverse SBOX number WHICH to to the block found in ARRAY0 at
   index INDEX, writing the output to the block found in ARRAY1 at
   index INDEX.  */
#define SBOX_INVERSE(which, array0, array1, index)              \
  SBOX##which##_INVERSE (array0[index + 0], array0[index + 1],  \
                         array0[index + 2], array0[index + 3],  \
                         array1[index + 0], array1[index + 1],  \
                         array1[index + 2], array1[index + 3]);

/* Apply the linear transformation to BLOCK.  */
#define LINEAR_TRANSFORMATION(block)                  \
  {                                                   \
    block[0] = rol (block[0], 13);                    \
    block[2] = rol (block[2], 3);                     \
    block[1] = block[1] ^ block[0] ^ block[2];        \
    block[3] = block[3] ^ block[2] ^ (block[0] << 3); \
    block[1] = rol (block[1], 1);                     \
    block[3] = rol (block[3], 7);                     \
    block[0] = block[0] ^ block[1] ^ block[3];        \
    block[2] = block[2] ^ block[3] ^ (block[1] << 7); \
    block[0] = rol (block[0], 5);                     \
    block[2] = rol (block[2], 22);                    \
  }

/* Apply the inverse linear transformation to BLOCK.  */
#define LINEAR_TRANSFORMATION_INVERSE(block)          \
  {                                                   \
    block[2] = ror (block[2], 22);                    \
    block[0] = ror (block[0] , 5);                    \
    block[2] = block[2] ^ block[3] ^ (block[1] << 7); \
    block[0] = block[0] ^ block[1] ^ block[3];        \
    block[3] = ror (block[3], 7);                     \
    block[1] = ror (block[1], 1);                     \
    block[3] = block[3] ^ block[2] ^ (block[0] << 3); \
    block[1] = block[1] ^ block[0] ^ block[2];        \
    block[2] = ror (block[2], 3);                     \
    block[0] = ror (block[0], 13);                    \
  }

/* Apply a Serpent round to BLOCK, using the SBOX number WHICH and the
   subkeys contained in SUBKEYS.  Use BLOCK_TMP as temporary storage.
   This macro increments `round'.  */
#define ROUND(which, subkeys, block, block_tmp) \
  {                                             \
    BLOCK_XOR (block, subkeys[round]);          \
    round++;                                    \
    SBOX (which, block, block_tmp, 0);          \
    LINEAR_TRANSFORMATION (block_tmp);          \
    BLOCK_COPY (block, block_tmp);              \
  }

/* Apply the last Serpent round to BLOCK, using the SBOX number WHICH
   and the subkeys contained in SUBKEYS.  Use BLOCK_TMP as temporary
   storage.  The result will be stored in BLOCK_TMP.  This macro
   increments `round'.  */
#define ROUND_LAST(which, subkeys, block, block_tmp) \
  {                                                  \
    BLOCK_XOR (block, subkeys[round]);               \
    round++;                                         \
    SBOX (which, block, block_tmp, 0);               \
    BLOCK_XOR (block_tmp, subkeys[round]);           \
    round++;                                         \
  }

/* Apply an inverse Serpent round to BLOCK, using the SBOX number
   WHICH and the subkeys contained in SUBKEYS.  Use BLOCK_TMP as
   temporary storage.  This macro increments `round'.  */
#define ROUND_INVERSE(which, subkey, block, block_tmp) \
  {                                                    \
    LINEAR_TRANSFORMATION_INVERSE (block);             \
    SBOX_INVERSE (which, block, block_tmp, 0);         \
    BLOCK_XOR (block_tmp, subkey[round]);              \
    round--;                                           \
    BLOCK_COPY (block, block_tmp);                     \
  }

/* Apply the first Serpent round to BLOCK, using the SBOX number WHICH
   and the subkeys contained in SUBKEYS.  Use BLOCK_TMP as temporary
   storage.  The result will be stored in BLOCK_TMP.  This macro
   increments `round'.  */
#define ROUND_FIRST_INVERSE(which, subkeys, block, block_tmp) \
  {                                                           \
    BLOCK_XOR (block, subkeys[round]);                        \
    round--;                                                  \
    SBOX_INVERSE (which, block, block_tmp, 0);                \
    BLOCK_XOR (block_tmp, subkeys[round]);                    \
    round--;                                                  \
  }

/* Convert the user provided key KEY of KEY_LENGTH bytes into the
   internally used format.  */
static void
serpent_key_prepare (const unsigned char *key, unsigned int key_length,
		     serpent_key_t key_prepared)
{
  unsigned int i;

  /* Copy key.  */
  for (i = 0; i < key_length / 4; i++)
    {
#ifdef WORDS_BIGENDIAN
      key_prepared[i] = byte_swap_32 (((uint32_t *) key)[i]);
#else
      key_prepared[i] = ((uint32_t *) key)[i];
#endif
    }

  if (i < 8)
    {
      /* Key must be padded according to the Serpent
	 specification.  */
      key_prepared[i] = 0x00000001;

      for (i++; i < 8; i++)
	key_prepared[i] = 0;
    }
}

/* Derive the 33 subkeys from KEY and store them in SUBKEYS.  */
static void
serpent_subkeys_generate (serpent_key_t key, serpent_subkeys_t subkeys)
{
  uint32_t w_real[140];		/* The `prekey'.  */
  uint32_t k[132];
  uint32_t *w = &w_real[8];
  int i, j;

  /* Initialize with key values.  */
  for (i = 0; i < 8; i++)
    w[i - 8] = key[i];

  /* Expand to intermediate key using the affine recurrence.  */
  for (i = 0; i < 132; i++)
    w[i] = rol (w[i - 8] ^ w[i - 5] ^ w[i - 3] ^ w[i - 1] ^ PHI ^ i, 11);

  /* Calculate subkeys via S-Boxes, in bitslice mode.  */
  SBOX (3, w, k,   0);
  SBOX (2, w, k,   4);
  SBOX (1, w, k,   8);
  SBOX (0, w, k,  12);
  SBOX (7, w, k,  16);
  SBOX (6, w, k,  20);
  SBOX (5, w, k,  24);
  SBOX (4, w, k,  28);
  SBOX (3, w, k,  32);
  SBOX (2, w, k,  36);
  SBOX (1, w, k,  40);
  SBOX (0, w, k,  44);
  SBOX (7, w, k,  48);
  SBOX (6, w, k,  52);
  SBOX (5, w, k,  56);
  SBOX (4, w, k,  60);
  SBOX (3, w, k,  64);
  SBOX (2, w, k,  68);
  SBOX (1, w, k,  72);
  SBOX (0, w, k,  76);
  SBOX (7, w, k,  80);
  SBOX (6, w, k,  84);
  SBOX (5, w, k,  88);
  SBOX (4, w, k,  92);
  SBOX (3, w, k,  96);
  SBOX (2, w, k, 100);
  SBOX (1, w, k, 104);
  SBOX (0, w, k, 108);
  SBOX (7, w, k, 112);
  SBOX (6, w, k, 116);
  SBOX (5, w, k, 120);
  SBOX (4, w, k, 124);
  SBOX (3, w, k, 128);

  /* Renumber subkeys.  */
  for (i = 0; i < ROUNDS + 1; i++)
    for (j = 0; j < 4; j++)
      subkeys[i][j] = k[4 * i + j];
}

/* Initialize CONTEXT with the key KEY of KEY_LENGTH bits.  */
static void
serpent_setkey_internal (serpent_context_t *context,
			 const unsigned char *key, unsigned int key_length)
{
  serpent_key_t key_prepared;

  serpent_key_prepare (key, key_length, key_prepared);
  serpent_subkeys_generate (key_prepared, context->keys);
}


/* Initialize CTX with the key KEY of KEY_LENGTH bytes.  */
static int
serpent_setkey (serpent_context_t *context,
		const void *key, unsigned int key_length)
{
  static const char *serpent_test_ret;
  static int serpent_init_done;
  int ret = 0;
  
  if (!serpent_init_done)
    {
      /* Execute a self-test the first time, Serpent is used.  */
      serpent_test_ret = serpent_test ();
#ifdef TEST
      if (serpent_test_ret)
	fprintf (stderr, "Serpent test failure: %s\n", serpent_test_ret);
#endif /*TEST*/
      serpent_init_done = 1;
    }

  if (serpent_test_ret)
    ret = -1;
  else
    serpent_setkey_internal (context, key, key_length);

  return ret;
}


static void
serpent_encrypt_internal (serpent_context_t *context,
			  const serpent_block_t input, serpent_block_t output)
{
  serpent_block_t b, b_next;
  int round = 0;

#ifdef WORDS_BIGENDIAN
  b[0] = byte_swap_32 (input[0]);
  b[1] = byte_swap_32 (input[1]);
  b[2] = byte_swap_32 (input[2]);
  b[3] = byte_swap_32 (input[3]);
#else
  b[0] = input[0];
  b[1] = input[1];
  b[2] = input[2];
  b[3] = input[3];
#endif

  ROUND (0, context->keys, b, b_next);
  ROUND (1, context->keys, b, b_next);
  ROUND (2, context->keys, b, b_next);
  ROUND (3, context->keys, b, b_next);
  ROUND (4, context->keys, b, b_next);
  ROUND (5, context->keys, b, b_next);
  ROUND (6, context->keys, b, b_next);
  ROUND (7, context->keys, b, b_next);
  ROUND (0, context->keys, b, b_next);
  ROUND (1, context->keys, b, b_next);
  ROUND (2, context->keys, b, b_next);
  ROUND (3, context->keys, b, b_next);
  ROUND (4, context->keys, b, b_next);
  ROUND (5, context->keys, b, b_next);
  ROUND (6, context->keys, b, b_next);
  ROUND (7, context->keys, b, b_next);
  ROUND (0, context->keys, b, b_next);
  ROUND (1, context->keys, b, b_next);
  ROUND (2, context->keys, b, b_next);
  ROUND (3, context->keys, b, b_next);
  ROUND (4, context->keys, b, b_next);
  ROUND (5, context->keys, b, b_next);
  ROUND (6, context->keys, b, b_next);
  ROUND (7, context->keys, b, b_next);
  ROUND (0, context->keys, b, b_next);
  ROUND (1, context->keys, b, b_next);
  ROUND (2, context->keys, b, b_next);
  ROUND (3, context->keys, b, b_next);
  ROUND (4, context->keys, b, b_next);
  ROUND (5, context->keys, b, b_next);
  ROUND (6, context->keys, b, b_next);

  ROUND_LAST (7, context->keys, b, b_next);

#ifdef WORDS_BIGENDIAN
  output[0] = byte_swap_32 (b_next[0]);
  output[1] = byte_swap_32 (b_next[1]);
  output[2] = byte_swap_32 (b_next[2]);
  output[3] = byte_swap_32 (b_next[3]);
#else
  output[0] = b_next[0];
  output[1] = b_next[1];
  output[2] = b_next[2];
  output[3] = b_next[3];
#endif
}


#ifdef TEST /* We only use CFB mode, thus tehre is no actual need for
               the decryption fucntion. */
static void
serpent_decrypt_internal (serpent_context_t *context,
			  const serpent_block_t input, serpent_block_t output)
{
  serpent_block_t b, b_next;
  int round = ROUNDS;

#ifdef WORDS_BIGENDIAN
  b_next[0] = byte_swap_32 (input[0]);
  b_next[1] = byte_swap_32 (input[1]);
  b_next[2] = byte_swap_32 (input[2]);
  b_next[3] = byte_swap_32 (input[3]);
#else
  b_next[0] = input[0];
  b_next[1] = input[1];
  b_next[2] = input[2];
  b_next[3] = input[3];
#endif

  ROUND_FIRST_INVERSE (7, context->keys, b_next, b);

  ROUND_INVERSE (6, context->keys, b, b_next);
  ROUND_INVERSE (5, context->keys, b, b_next);
  ROUND_INVERSE (4, context->keys, b, b_next);
  ROUND_INVERSE (3, context->keys, b, b_next);
  ROUND_INVERSE (2, context->keys, b, b_next);
  ROUND_INVERSE (1, context->keys, b, b_next);
  ROUND_INVERSE (0, context->keys, b, b_next);
  ROUND_INVERSE (7, context->keys, b, b_next);
  ROUND_INVERSE (6, context->keys, b, b_next);
  ROUND_INVERSE (5, context->keys, b, b_next);
  ROUND_INVERSE (4, context->keys, b, b_next);
  ROUND_INVERSE (3, context->keys, b, b_next);
  ROUND_INVERSE (2, context->keys, b, b_next);
  ROUND_INVERSE (1, context->keys, b, b_next);
  ROUND_INVERSE (0, context->keys, b, b_next);
  ROUND_INVERSE (7, context->keys, b, b_next);
  ROUND_INVERSE (6, context->keys, b, b_next);
  ROUND_INVERSE (5, context->keys, b, b_next);
  ROUND_INVERSE (4, context->keys, b, b_next);
  ROUND_INVERSE (3, context->keys, b, b_next);
  ROUND_INVERSE (2, context->keys, b, b_next);
  ROUND_INVERSE (1, context->keys, b, b_next);
  ROUND_INVERSE (0, context->keys, b, b_next);
  ROUND_INVERSE (7, context->keys, b, b_next);
  ROUND_INVERSE (6, context->keys, b, b_next);
  ROUND_INVERSE (5, context->keys, b, b_next);
  ROUND_INVERSE (4, context->keys, b, b_next);
  ROUND_INVERSE (3, context->keys, b, b_next);
  ROUND_INVERSE (2, context->keys, b, b_next);
  ROUND_INVERSE (1, context->keys, b, b_next);
  ROUND_INVERSE (0, context->keys, b, b_next);


#ifdef WORDS_BIGENDIAN
  output[0] = byte_swap_32 (b_next[0]);
  output[1] = byte_swap_32 (b_next[1]);
  output[2] = byte_swap_32 (b_next[2]);
  output[3] = byte_swap_32 (b_next[3]);
#else
  output[0] = b_next[0];
  output[1] = b_next[1];
  output[2] = b_next[2];
  output[3] = b_next[3];
#endif
}
#endif /*TEST*/


static const char *
serpent_test (void)
{
  serpent_context_t context;
  unsigned char scratch[16];
  unsigned int i;

  static struct test
  {
    int key_length;
    unsigned char key[32];
    unsigned char text_plain[16];
    unsigned char text_cipher[16];
  } test_data[] =
    {
      {
	16,
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
	"\xD2\x9D\x57\x6F\xCE\xA3\xA3\xA7\xED\x90\x99\xF2\x92\x73\xD7\x8E",
	"\xB2\x28\x8B\x96\x8A\xE8\xB0\x86\x48\xD1\xCE\x96\x06\xFD\x99\x2D"
      },
      {
	0
      }
    };

  for (i = 0; test_data[i].key_length; i++)
    {
      serpent_setkey_internal (&context, test_data[i].key,
                               test_data[i].key_length);
      serpent_encrypt_internal (&context,
				(const uint32_t *) test_data[i].text_plain,
				(uint32_t *) scratch);

      if (memcmp (scratch, test_data[i].text_cipher, sizeof (serpent_block_t)))
        return "Serpent test encryption failed";

#ifdef TEST
      serpent_decrypt_internal (&context,
                                (const uint32_t *) test_data[i].text_cipher,
                                (uint32_t *) scratch);
      if (memcmp (scratch, test_data[i].text_plain, sizeof (serpent_block_t)))
        return "Serpent test decryption failed.";
#endif /*TEST*/
    }

  return NULL;
}

/* Open a new cipher instance with KEY and IV.  On error the fucntion
   returns NULL, otherwise a handle which needs to be provided to the
   other functions. */
symenc_t
symenc_open (const void *key, size_t keylen, const void *iv, size_t ivlen)
{
  symenc_t ctx;

  if (!key || keylen != 16 || !iv || ivlen != BLOCKSIZE)
    return NULL;
  ctx = calloc (1, sizeof *ctx);
  if (!ctx)
    return NULL;
  if (serpent_setkey (ctx, key, keylen))
    {
      free (ctx);
      return NULL;
    }
  memcpy (ctx->u.cfb.iv, iv, BLOCKSIZE);
  return ctx;
}

/* Close a Serpent encryption context.  */
void
symenc_close (symenc_t ctx)
{
  if (ctx)
    {
      free (ctx);
    }
}


void
symenc_cfb_encrypt (symenc_t ctx,
                    void *buffer_out, const void *buffer_in, size_t nbytes)
{
  const unsigned char *inbuf  = buffer_in;
  unsigned char *outbuf = buffer_out;
  unsigned char *ivp;
  int i;

  assert (ctx->u.cfb.unused <= BLOCKSIZE);

  /* If the input is short enough to be entirely encoded by the
     remaining XOR mask, XOR the input with the IV and store input
     into IV.  */
  if (nbytes <= ctx->u.cfb.unused )
    {
      for (ivp = ctx->u.cfb.iv + BLOCKSIZE - ctx->u.cfb.unused;
           nbytes; nbytes--, ctx->u.cfb.unused--)
        *outbuf++ = (*ivp++ ^= *inbuf++);
      return; /* Ready.  */
    }

  /* If there are still some bytes left in the XOR mask, XOR the input
     with the IV and store input into IV.  */
  if (ctx->u.cfb.unused)
    {
      nbytes -= ctx->u.cfb.unused;
      for (ivp = ctx->u.cfb.iv + BLOCKSIZE - ctx->u.cfb.unused;
           ctx->u.cfb.unused; ctx->u.cfb.unused--)
        *outbuf++ = (*ivp++ ^= *inbuf++);
    }

  /* Now process complete blocks. */
  while (nbytes >= BLOCKSIZE) 
    {
      serpent_encrypt_internal (ctx, (const uint32_t*)ctx->u.cfb.iv, 
                                (uint32_t*)ctx->u.cfb.iv);
      for (ivp = ctx->u.cfb.iv, i=0; i < BLOCKSIZE; i++ )
        *outbuf++ = (*ivp++ ^= *inbuf++);
      nbytes -= BLOCKSIZE;
    }

  /* Process any remaining bytes from the last block.  */
  if (nbytes) 
    { 
      serpent_encrypt_internal (ctx, (const uint32_t*)ctx->u.cfb.iv, 
                                (uint32_t*)ctx->u.cfb.iv);
      ctx->u.cfb.unused = BLOCKSIZE - nbytes;
      for (ivp = ctx->u.cfb.iv; nbytes; nbytes--)
        *outbuf++ = (*ivp++ ^= *inbuf++);
    }
}


void
symenc_cfb_decrypt (symenc_t ctx,
                    void *buffer_out, const void *buffer_in, size_t nbytes)
{
  const unsigned char *inbuf  = buffer_in;
  unsigned char *outbuf = buffer_out;
  unsigned char *ivp;
  uint32_t temp;
  int i;

  assert (ctx->u.cfb.unused <= BLOCKSIZE);

  /* If the input is short enough to be entirely decoded by the
     remaining XOR mask, XOR the input with the IV and store input
     into IV.  */
  if (nbytes <= ctx->u.cfb.unused) 
    {
      for (ivp = ctx->u.cfb.iv + BLOCKSIZE - ctx->u.cfb.unused;
           nbytes; nbytes--, ctx->u.cfb.unused--) 
        {
          temp = *inbuf++;
          *outbuf++ = (*ivp ^ temp);
          *ivp++ = temp;
	}
      return; /* Ready.  */
    }

  /* If there are still some bytes left in the XOR mask, XOR the input
     with the IV and store input into IV.  */
  if (ctx->u.cfb.unused)
    {
      nbytes -= ctx->u.cfb.unused;
      for (ivp = ctx->u.cfb.iv + BLOCKSIZE - ctx->u.cfb.unused;
           ctx->u.cfb.unused; ctx->u.cfb.unused--)
        {
          temp = *inbuf++;
          *outbuf++ = (*ivp ^ temp);
          *ivp++ = temp;
	}
    }

  /* Now process complete blocks.  */
  while (nbytes >= BLOCKSIZE)
    {
      serpent_encrypt_internal (ctx, (const uint32_t*)ctx->u.cfb.iv, 
                                (uint32_t*)ctx->u.cfb.iv);
      for (ivp = ctx->u.cfb.iv, i=0; i < BLOCKSIZE; i++)
        {
          temp = *inbuf++;
          *outbuf++ = (*ivp ^ temp);
          *ivp++ = temp;
	}
      nbytes -= BLOCKSIZE;
    }

  /* Process any remaining bytes from the last block.  */
  if (nbytes)
    {
      serpent_encrypt_internal (ctx, (const uint32_t*)ctx->u.cfb.iv, 
                                (uint32_t*)ctx->u.cfb.iv);
      ctx->u.cfb.unused = BLOCKSIZE - nbytes;
      for (ivp = ctx->u.cfb.iv; nbytes; nbytes--)
        {
          temp = *inbuf++;
          *outbuf++ = (*ivp ^ temp);
          *ivp++ = temp;
	}
    }
}



#ifdef TEST
int 
main (int argc, char** argv)
{
  int decmode;
  symenc_t ctx;
  char buf[255];
  size_t n;

  if (argc) { argc--, argv++; }

  decmode = (argc && !strcmp (*argv, "--decrypt"));

  ctx = symenc_open ("1234567890123456", 16, "abcdefghijklmnop", 16);
  if (!ctx)
    abort ();
  
  while ( (n=fread (buf, 1, 255, stdin)) )
    {
      if (decmode)
        symenc_cfb_decrypt (ctx, buf, buf, n);
      else
        symenc_cfb_encrypt (ctx, buf, buf, n);
      if (fwrite (buf, 1, n, stdout) != n)
        abort ();
    }

  symenc_close (ctx);
  return 0;
}

#endif /*TEST*/



/*
Local Variables:
compile-command: "gcc -Wall -g -O2 -DTEST -o serpent serpent.c"
End:
*/
