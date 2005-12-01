/* pgpmime.c - Try to handle PGP/MIME for Outlook
 *	Copyright (C) 2005 g10 Code GmbH
 *
 * This file is part of GPGol.
 * 
 * GPGol is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * GPGol is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
   EXPLAIN what we are doing here.
*/
   

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <objidl.h> /* For IStream. */

#include <gpgme.h>

#include "mymapi.h"
#include "mymapitags.h"

#include "rfc822parse.h"
#include "intern.h"
#include "util.h"
#include "pgpmime.h"
#include "engine.h"


/* The maximum length of a line we ar able to porcess.  RFC822 alows
   only for 1000 bytes; thus 2000 seems to be a reasonable value. */
#define LINEBUFSIZE 2000

/* The reverse base-64 list used for base-64 decoding. */
static unsigned char const asctobin[256] = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3e, 0xff, 0xff, 0xff, 0x3f, 
  0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 
  0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 
  0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 
  0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 
  0x31, 0x32, 0x33, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff
};




/* The context object we use to track information. */
struct pgpmime_context
{
  HWND hwnd;          /* A window handle to be used for message boxes etc. */
  rfc822parse_t msg;  /* The handle of the RFC822 parser. */

  int preview;        /* Do only decryption and pop up no  message bozes.  */

  int nesting_level;  /* Current MIME nesting level. */
  int in_data;        /* We are currently in data (body or attachment). */

  
  gpgme_data_t body;      /* NULL or a data object used to collect the
                             body part we are going to display later. */
  int collect_body;       /* True if we are collecting the body lines. */
  int collect_attachment; /* True if we are collecting an attachment. */
  int is_qp_encoded;      /* Current part is QP encoded. */
  int is_base64_encoded;  /* Current part is base 64 encoded. */
  int is_utf8;            /* Current part has charset utf-8. */

  int part_counter;       /* Counts the number of processed parts. */
  char *filename;         /* Current filename (malloced) or NULL. */

  LPSTREAM outstream;     /* NULL or a stream to write a part to. */

  /* Helper to keep the state of the base64 decoder. */
  struct 
  {
    int idx;
    unsigned char val;
    int stop_seen;
    int invalid_encoding;
  } base64;

  int line_too_long;  /* Indicates that a received line was too long. */
  int parser_error;   /* Indicates that we encountered a error from
                         the parser. */

  /* Buffer used to constructed complete files. */
  size_t linebufsize;   /* The allocated size of the buffer. */
  size_t linebufpos;    /* The actual write posituion. */  
  char linebuf[1];      /* The buffer. */
};
typedef struct pgpmime_context *pgpmime_context_t;


/* This function is a wrapper around gpgme_data_write to convert the
   data to utf-8 first.  We assume Latin-1 here. */
static int
latin1_data_write (gpgme_data_t data, const char *line, size_t len)
{
  const char *s;
  char *buffer, *p;
  size_t i, n;
  int rc;

  for (s=line, i=0, n=0 ; i < len; s++, i++ ) 
    {
      n++;
      if (*s & 0x80)
        n++;
    }
  buffer = xmalloc (n + 1);
  for (s=line, i=0, p=buffer; i < len; s++, i++ )
    {
      if (*s & 0x80)
        {
          *p++ = 0xc0 | ((*s >> 6) & 3);
          *p++ = 0x80 | (*s & 0x3f);
        }
      else
        *p++ = *s;
    }
  assert (p-buffer == n);
  rc = gpgme_data_write (data, buffer, n);
  xfree (buffer);
  return rc;
}



/* Do in-place decoding of quoted-printable data of LENGTH in BUFFER.
   Returns the new length of the buffer. */
static size_t
qp_decode (char *buffer, size_t length)
{
  char *d, *s;

  for (s=d=buffer; length; length--)
    if (*s == '=' && length > 2 && hexdigitp (s+1) && hexdigitp (s+2))
      {
        s++;
        *(unsigned char*)d++ = xtoi_2 (s);
        s += 2;
        length -= 2;
      }
    else
      *d++ = *s++;
  
  return d - buffer;
}


/* Do in-place decoding of base-64 data of LENGTH in BUFFER.  Returns
   the new length of the buffer. CTX is required to return errors and
   to maintain state of the decoder.  */
static size_t
base64_decode (pgpmime_context_t ctx, char *buffer, size_t length)
{
  int idx = ctx->base64.idx;
  unsigned char val = ctx->base64.val;
  int c;
  char *d, *s;

  if (ctx->base64.stop_seen)
    return 0;

  for (s=d=buffer; length; length--, s++)
    {
      if (*s == '\n' || *s == ' ' || *s == '\r' || *s == '\t')
        continue;
      if (*s == '=')
        { 
          /* Pad character: stop */
          if (idx == 1)
            *d++ = val; 
          ctx->base64.stop_seen = 1;
          break;
        }

      if ((c = asctobin[*(unsigned char *)s]) == 255) 
        {
          if (!ctx->base64.invalid_encoding)
            log_debug ("%s: invalid base64 character %02X at pos %d skipped\n",
                       __func__, *(unsigned char*)s, (int)(s-buffer));
          ctx->base64.invalid_encoding = 1;
          continue;
        }

      switch (idx) 
        {
        case 0: 
          val = c << 2;
          break;
        case 1: 
          val |= (c>>4)&3;
          *d++ = val;
          val = (c<<4)&0xf0;
          break;
        case 2: 
          val |= (c>>2)&15;
          *d++ = val;
          val = (c<<6)&0xc0;
          break;
        case 3: 
          val |= c&0x3f;
          *d++ = val;
          break;
        }
      idx = (idx+1) % 4;
    }

  
  ctx->base64.idx = idx;
  ctx->base64.val = val;
  return d - buffer;
}



/* Print the message event EVENT. */
static void
debug_message_event (pgpmime_context_t ctx, rfc822parse_event_t event)
{
  const char *s;

  switch (event)
    {
    case RFC822PARSE_OPEN: s= "Open"; break;
    case RFC822PARSE_CLOSE: s= "Close"; break;
    case RFC822PARSE_CANCEL: s= "Cancel"; break;
    case RFC822PARSE_T2BODY: s= "T2Body"; break;
    case RFC822PARSE_FINISH: s= "Finish"; break;
    case RFC822PARSE_RCVD_SEEN: s= "Rcvd_Seen"; break;
    case RFC822PARSE_LEVEL_DOWN: s= "Level_Down"; break;
    case RFC822PARSE_LEVEL_UP: s= "Level_Up"; break;
    case RFC822PARSE_BOUNDARY: s= "Boundary"; break;
    case RFC822PARSE_LAST_BOUNDARY: s= "Last_Boundary"; break;
    case RFC822PARSE_BEGIN_HEADER: s= "Begin_Header"; break;
    case RFC822PARSE_PREAMBLE: s= "Preamble"; break;
    case RFC822PARSE_EPILOGUE: s= "Epilogue"; break;
    default: s= "[unknown event]"; break;
    }
  log_debug ("%s: ctx=%p, rfc822 event %s\n", SRCNAME, ctx, s);
}




/* This routine gets called by the RFC822 parser for all kind of
   events.  OPAQUE carries in our case a pgpmime context.  Should
   return 0 on success or -1 and as well as seeting errno on
   failure. */
static int
message_cb (void *opaque, rfc822parse_event_t event, rfc822parse_t msg)
{
  pgpmime_context_t ctx = opaque;
  HRESULT hr;

  debug_message_event (ctx, event);
  if (event == RFC822PARSE_T2BODY)
    {
      rfc822parse_field_t field;
      const char *s1, *s2;
      size_t off;
      char *p;
      int is_text = 0;

      ctx->is_utf8 = 0;
      field = rfc822parse_parse_field (msg, "Content-Type", -1);
      if (field)
        {
          s1 = rfc822parse_query_media_type (field, &s2);
          if (s1)
            {
              log_debug ("%s: ctx=%p, media `%s' `%s'\n",
                         SRCNAME, ctx, s1, s2);

              if (!strcmp (s1, "multipart"))
                {
                  /* We don't care about the top level multipart layer
                     but wait until it comes to the actual parts which
                     then will get stored as attachments.

                     For now encapsulated signed or encrypted
                     containers are not processed in a special way as
                     they should. */
                }
              else if (!strcmp (s1, "text"))
                {
                  is_text = 1;
                }
              else /* Other type. */
                {
                  if (!ctx->preview)
                    ctx->collect_attachment = 1;
                }

            }

          s1 = rfc822parse_query_parameter (field, "charset", 0);
          if (s1 && !strcmp (s1, "utf-8"))
            ctx->is_utf8 = 1;

          rfc822parse_release_field (field);
        }
      else
        {
          /* No content-type at all indicates text/plain. */
          is_text = 1;
        }
      ctx->in_data = 1;

      /* Need to figure out the encoding. */
      ctx->is_qp_encoded = 0;
      ctx->is_base64_encoded = 0;
      p = rfc822parse_get_field (msg, "Content-Transfer-Encoding", -1, &off);
      if (p)
        {
          if (!stricmp (p+off, "quoted-printable"))
            ctx->is_qp_encoded = 1;
          else if (!stricmp (p+off, "base64"))
            {
              ctx->is_base64_encoded = 1;
              ctx->base64.idx = 0;
              ctx->base64.val = 0;
              ctx->base64.stop_seen = 0;
              ctx->base64.invalid_encoding = 0;
            }
          free (p);
        }

      /* If this is a text part, decide whether we treat it as our body. */
      if (is_text)
        {
          /* If this is the first text part at all we will
             start to collect it and use it later as the
             regular body.  An initialized ctx->BODY is an
             indication that this is not the first text part -
             we treat such a part like any other
             attachment. */
          if (!ctx->body)
            {
              if (!gpgme_data_new (&ctx->body))
                ctx->collect_body = 1;
            }
          else if (!ctx->preview)
            ctx->collect_attachment = 1;
        }

      /* Now that if we have an attachment prepare for writing it out. */
      if (ctx->collect_attachment)
        {
          p = NULL;
          field = rfc822parse_parse_field (msg, "Content-Disposition", -1);
          if (field)
            {
              s1 = rfc822parse_query_parameter (field, "filename", 0);
              if (s1)
                p = xstrdup (s1);
              rfc822parse_release_field (field);
            }
          if (!p)
            {
              p = xmalloc (50);
              snprintf (p, 49, "unnamed-%d.dat", ctx->part_counter);
            }
          if (ctx->outstream)
            {
              IStream_Release (ctx->outstream);
              ctx->outstream = NULL;
            }
        tryagain:
          xfree (ctx->filename);
          ctx->filename = ctx->preview? NULL:get_save_filename (ctx->hwnd, p);
          if (!ctx->filename)
            ctx->collect_attachment = 0; /* User does not want to save it. */
          else
            {
              hr = OpenStreamOnFile (MAPIAllocateBuffer, MAPIFreeBuffer,
                                     (STGM_CREATE | STGM_READWRITE),
                                     ctx->filename, NULL, &ctx->outstream); 
              if (FAILED (hr)) 
                {
                  log_error ("%s:%s: can't create file `%s': hr=%#lx\n",
                             SRCNAME, __func__, ctx->filename, hr); 
                  MessageBox (ctx->hwnd, _("Error creating file\n"
                                           "Please select another one"),
                              _("I/O-Error"), MB_ICONERROR|MB_OK);
                  goto tryagain;
                }
              log_debug ("%s:%s: writing attachment to `%s'\n",
                         SRCNAME, __func__, ctx->filename); 
            }
          xfree (p);
        }
    }
  else if (event == RFC822PARSE_LEVEL_DOWN)
    {
      ctx->nesting_level++;
    }
  else if (event == RFC822PARSE_LEVEL_UP)
    {
      if (ctx->nesting_level)
        ctx->nesting_level--;
      else 
        {
          log_error ("%s: ctx=%p, invalid structure: bad nesting level\n",
                     SRCNAME, ctx);
          ctx->parser_error = 1;
        }
    }
  else if (event == RFC822PARSE_BOUNDARY || event == RFC822PARSE_LAST_BOUNDARY)
    {
      ctx->in_data = 0;
      ctx->collect_body = 0;
      ctx->collect_attachment = 0;
      xfree (ctx->filename);
      ctx->filename = NULL;
      if (ctx->outstream)
        {
          IStream_Commit (ctx->outstream, 0);
          IStream_Release (ctx->outstream);
          ctx->outstream = NULL;
        }
    }
  else if (event == RFC822PARSE_BEGIN_HEADER)
    {
      ctx->part_counter++;
    }

  return 0;
}


/* This handler is called by GPGME with the decrypted plaintext. */
static ssize_t
plaintext_handler (void *handle, const void *buffer, size_t size)
{
  pgpmime_context_t ctx = handle;
  const char *s;
  size_t nleft, pos, len;

  s = buffer;
  pos = ctx->linebufpos;
  nleft = size;
  for (; nleft ; nleft--, s++)
    {
      if (pos >= ctx->linebufsize)
        {
          log_error ("%s: ctx=%p, rfc822 parser failed: line too long\n",
                     SRCNAME, ctx);
          ctx->line_too_long = 1;
          return 0; /* Error. */
        }
      if (*s != '\n')
        ctx->linebuf[pos++] = *s;
      else
        { /* Got a complete line. Remove the last CR */
          if (pos && ctx->linebuf[pos-1] == '\r')
            pos--;

          if (rfc822parse_insert (ctx->msg, ctx->linebuf, pos))
            {
              log_error ("%s: ctx=%p, rfc822 parser failed: %s\n",
                         SRCNAME, ctx, strerror (errno));
              ctx->parser_error = 1;
              return 0; /* Error. */
            }

          if (ctx->in_data && ctx->collect_body && ctx->body)
            {
              /* We are inside the body of the message.  Save it away
                 to a gpgme data object.  Note that this gets only
                 used for the first text part. */
              if (ctx->collect_body == 1)  /* Need to skip the first line. */
                ctx->collect_body = 2;
              else
                {
                  if (ctx->is_qp_encoded)
                    len = qp_decode (ctx->linebuf, pos);
                  else if (ctx->is_base64_encoded)
                    len = base64_decode (ctx, ctx->linebuf, pos);
                  else
                    len = pos;
                  if (len)
                    {
                      if (ctx->is_utf8)
                        gpgme_data_write (ctx->body, ctx->linebuf, len);
                      else
                        latin1_data_write (ctx->body, ctx->linebuf, len);
                    }
                  if (!ctx->is_base64_encoded)
                    gpgme_data_write (ctx->body, "\r\n", 2);
                }
            }
          else if (ctx->in_data && ctx->collect_attachment)
            {
              /* We are inside of an attachment part.  Write it out. */
              if (ctx->collect_attachment == 1)  /* Skip the first line. */
                ctx->collect_attachment = 2;
              else if (ctx->outstream)
                {
                  HRESULT hr = 0;

                  if (ctx->is_qp_encoded)
                    len = qp_decode (ctx->linebuf, pos);
                  else if (ctx->is_base64_encoded)
                    len = base64_decode (ctx, ctx->linebuf, pos);
                  else
                    len = pos;
                  if (len)
                    hr = IStream_Write (ctx->outstream, ctx->linebuf,
                                        len, NULL);
                  if (!hr && !ctx->is_base64_encoded)
                    hr = IStream_Write (ctx->outstream, "\r\n", 2, NULL);
                  if (hr)
                    {
                      log_debug ("%s:%s: Write failed: hr=%#lx",
                                 SRCNAME, __func__, hr);
                      if (!ctx->preview)
                        MessageBox (ctx->hwnd, _("Error writing file"),
                                    _("I/O-Error"), MB_ICONERROR|MB_OK);
                      ctx->parser_error = 1;
                      return 0; /* Error. */
                    }
                }
            }
          
          /* Continue with next line. */
          pos = 0;
        }
    }
  ctx->linebufpos = pos;

  return size;
}


/* Decrypt the PGP/MIME INSTREAM (i.e the second part of the
   multipart/mixed) and allow saving of all attachments. On success a
   newly allocated body will be stored at BODY.  If ATTESTATION is not
   NULL a text with the result of the signature verification will get
   printed to it.  HWND is the window to be used for message box and
   such.  In PREVIEW_MODE no verification will be done, no messages
   saved and no messages boxes will pop up. */
int
pgpmime_decrypt (LPSTREAM instream, int ttl, char **body,
                 gpgme_data_t attestation, HWND hwnd, int preview_mode)
{
  gpg_error_t err;
  struct gpgme_data_cbs cbs;
  gpgme_data_t plaintext;
  pgpmime_context_t ctx;

  *body = NULL;

  memset (&cbs, 0, sizeof cbs);
  cbs.write = plaintext_handler;

  ctx = xcalloc (1, sizeof *ctx + LINEBUFSIZE);
  ctx->linebufsize = LINEBUFSIZE;
  ctx->hwnd = hwnd;
  ctx->preview = preview_mode;

  ctx->msg = rfc822parse_open (message_cb, ctx);
  if (!ctx->msg)
    {
      err = gpg_error_from_errno (errno);
      log_error ("failed to open the RFC822 parser: %s", strerror (errno));
      goto leave;
    }

  err = gpgme_data_new_from_cbs (&plaintext, &cbs, ctx);
  if (err)
    goto leave;

  err = op_decrypt_stream_to_gpgme (instream, plaintext, ttl,
                                    _("[PGP/MIME message]"), attestation,
                                    preview_mode);
  if (!err && (ctx->parser_error || ctx->line_too_long))
    err = gpg_error (GPG_ERR_GENERAL);

  if (!err)
    {
      if (ctx->body)
        {
          /* Return the buffer but first make sure it is a string. */
          if (gpgme_data_write (ctx->body, "", 1) == 1)
            {
              *body = gpgme_data_release_and_get_mem (ctx->body, NULL);
              ctx->body = NULL; 
            }
        }
      else
        *body = xstrdup (_("[PGP/MIME message without plain text body]"));
    }

 leave:
  if (plaintext)
    gpgme_data_release (plaintext);
  if (ctx)
    {
      if (ctx->outstream)
        {
          IStream_Revert (ctx->outstream);
          IStream_Release (ctx->outstream);
        }
      rfc822parse_close (ctx->msg);
      if (ctx->body)
        gpgme_data_release (ctx->body);
      xfree (ctx->filename);
      xfree (ctx);
    }
  return err;
}
