/* msgcache.h - Interface to the message cache.
 * Copyright (C) 2005 g10 Code GmbH
 *
 * This file is part of GpgOL.
 * 
 * GpgOL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * GpgOL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MSGCACHE_H
#define MSGCACHE_H

#ifdef __cplusplus
extern "C" {
#if 0
}
#endif
#endif

/* Initialize the message cache subsystem. */
int initialize_msgcache (void);

/* Put BODY into tye cace, derive the key from MESSAGE.  TRANSFER
   controls whether the cache will snatch ownership of body. */
void msgcache_put (char *body, int transfer, LPMESSAGE message);

/* Return the plaintext stored under a KEY of length KEYLEN or NULL
   if none was found. */
const char *msgcache_get (const void *key, size_t keylen, void **refhandle);

/* Return the plaintext stored for message MESSAGE or NULL if none was
   found. */
const char *msgcache_get_from_mapi (LPMESSAGE message, void **refhandle);

/* Release access to a value returned by msgcache_get.  REFHANDLE is
   the value as stored in the pointer variable by msgcache_get. */
void msgcache_unref (void *refhandle);


#ifdef __cplusplus
}
#endif
#endif /*MSGCACHE_H*/
