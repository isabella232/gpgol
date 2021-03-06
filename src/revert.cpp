/* revert.cpp - Convert messages back to the orignal format
 * Copyright (C) 2008 g10 Code GmbH
 * Copyright (C) 2015, 2016 by Bundesamt für Sicherheit in der Informationstechnik
 * Software engineering by Intevation GmbH
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <windows.h>

#include "mymapi.h"
#include "mymapitags.h"
#include "common.h"
#include "oomhelp.h"
#include "mapihelp.h"
#include "mimemaker.h"
#include "mail.h"

/* Helper method for mailitem_revert to add changes on the mapi side
   and save them. */
static int finalize_mapi (LPMESSAGE message)
{
  HRESULT hr;
  SPropTagArray proparray;
  ULONG tag_id;

  if (get_gpgollastdecrypted_tag (message, &tag_id))
    {
      log_error ("%s:%s: can't getlastdecrypted tag",
                 SRCNAME, __func__);
      return -1;
    }
  proparray.cValues = 1;
  proparray.aulPropTag[0] = tag_id;
  hr = message->DeleteProps (&proparray, NULL);
  if (hr)
    {
      log_error ("%s:%s: failed to delete lastdecrypted tag",
                 SRCNAME, __func__);
      return -1;
    }

  return mapi_save_changes (message, FORCE_SAVE);
}

/* Similar to gpgol_message_revert but works on OOM and is
   used by the Ol > 2010 implementation.
   Doing this through OOM was necessary as the MAPI structures
   in the write event are not in sync with the OOM side.
   Trying to revert in the AfterWrite where MAPI is synced
   led to an additional save_changes after the wipe and
   so an additional sync.
   Updating the BODY through MAPI did not appear to work
   at all. Not sure why this is the case.
   Using the property accessor methods instead of
   MAPI properties might also not be necessary.

   Returns 0 on success, -1 on error. On error this
   function might leave plaintext in the mail.    */
EXTERN_C LONG __stdcall
gpgol_mailitem_revert (LPDISPATCH mailitem)
{
  LPDISPATCH attachments = NULL;
  LPMESSAGE message = NULL;
  char *item_str;
  char *msgcls = NULL;
  int i;
  int count = 0;
  LONG result = -1;
  msgtype_t msgtype;
  int body_restored = 0;
  LPDISPATCH *to_delete = NULL;
  int del_cnt = 0;
  LPDISPATCH to_restore = NULL;
  int mosstmpl_found = 0;
  int is_smime = 0;
  Mail *mail = NULL;

  /* Check whether we need to care about this message.  */
  msgcls = get_pa_string (mailitem, PR_MESSAGE_CLASS_W_DASL);
  log_debug ("%s:%s: message class is `%s'\n",
             SRCNAME, __func__, msgcls? msgcls:"[none]");
  if (!msgcls)
    {
      return -1;
    }
  if ( !( !strncmp (msgcls, "IPM.Note.GpgOL", 14)
          && (!msgcls[14] || msgcls[14] == '.') ) )
    {
      xfree (msgcls);
      log_error ("%s:%s: Message processed but not our class. Bug.",
                 SRCNAME, __func__);
      return -1;
    }

  mail = Mail::getMailForItem (mailitem);
  if (!mail)
    {
      xfree (msgcls);
      log_error ("%s:%s: No mail object for mailitem. Bug.",
                 SRCNAME, __func__);
      return -1;
    }
  is_smime = mail->isSMIME_m ();

  message = get_oom_base_message (mailitem);
  attachments = get_oom_object (mailitem, "Attachments");

  if (!message)
    {
      log_error ("%s:%s: No message object.",
                 SRCNAME, __func__);
      goto done;
    }

  if (!attachments)
    {
      log_error ("%s:%s: No attachments object.",
                 SRCNAME, __func__);
      goto done;
    }
  msgtype = mapi_get_message_type (message);

  if (msgtype != MSGTYPE_GPGOL_PGP_MESSAGE &&
      msgtype != MSGTYPE_GPGOL_MULTIPART_ENCRYPTED &&
      msgtype != MSGTYPE_GPGOL_MULTIPART_SIGNED &&
      msgtype != MSGTYPE_GPGOL_OPAQUE_ENCRYPTED &&
      msgtype != MSGTYPE_GPGOL_OPAQUE_SIGNED)
    {
      log_error ("%s:%s: Revert not supported for msgtype: %i",
                 SRCNAME, __func__, msgtype);
      goto done;
    }


  count = get_oom_int (attachments, "Count");
  to_delete = (LPDISPATCH*) xmalloc (count * sizeof (LPDISPATCH));

  /* Yes the items start at 1! */
  for (i = 1; i <= count; i++)
    {
      LPDISPATCH attachment;
      attachtype_t att_type;

      if (gpgrt_asprintf (&item_str, "Item(%i)", i) == -1)
        {
          log_error ("%s:%s: Error: %i", SRCNAME, __func__, __LINE__);
          goto done;
        }

      memdbg_alloc (item_str);
      attachment = get_oom_object (attachments, item_str);
      xfree (item_str);
      if (!attachment)
        {
          log_error ("%s:%s: Error: %i", SRCNAME, __func__, __LINE__);
          goto done;
        }

      if (get_pa_int (attachment, GPGOL_ATTACHTYPE_DASL, (int*) &att_type))
        {
          att_type = ATTACHTYPE_FROMMOSS;
        }

      switch (att_type)
        {
          case ATTACHTYPE_PGPBODY:
            {
              /* Restore Body */
              char *body = get_pa_string (attachment, PR_ATTACH_DATA_BIN_DASL);
              if (!body)
                {
                  log_error ("%s:%s: Error: %i", SRCNAME, __func__, __LINE__);
                  gpgol_release (attachment);
                  goto done;
                }
              log_debug ("%s:%s: Restoring pgp-body.",
                         SRCNAME, __func__);
              if (put_oom_string (mailitem, "Body", body))
                {
                  log_error ("%s:%s: Error: %i", SRCNAME, __func__, __LINE__);
                  xfree (body);
                  gpgol_release (attachment);
                  goto done;
                }
              body_restored = 1;
              xfree (body);
              to_delete[del_cnt++] = attachment;
              break;
            } /* No break we also want to delete that. */
          case ATTACHTYPE_MOSS:
            {
              char *mime_tag = get_pa_string (attachment,
                                              PR_ATTACH_MIME_TAG_DASL);
              if (!mime_tag)
                {
                  log_error ("%s:%s: Error: %i", SRCNAME, __func__, __LINE__);
                }
              else if (msgtype == MSGTYPE_GPGOL_MULTIPART_ENCRYPTED &&
                       !strcmp (mime_tag, "application/octet-stream"))
                {
                  /* This is the body attachment of a multipart encrypted
                     message. Rebuild the message. */
                  to_restore = attachment;
                  to_delete[del_cnt++] = attachment;
                }
              else if (msgtype == MSGTYPE_GPGOL_MULTIPART_SIGNED &&
                       mime_tag && !strcmp (mime_tag, "multipart/signed"))
                {
                  /* This is the MIME formatted MOSS attachment of a multipart
                     signed message. Rebuild the MIME structure from that.
                     This means treating it as a MOSSTMPL */
                  mosstmpl_found = 1;
                }
              else if (is_smime)
                {
                  /* Same here. No restoration but just rebuilding from the
                     attachment. */
                  mosstmpl_found = 1;
                }
              else
                {
                  log_oom ("%s:%s: Skipping attachment with tag: %s", SRCNAME,
                           __func__, mime_tag);
                  to_delete[del_cnt++] = attachment;
                }
              xfree (mime_tag);
              break;
            }
          case ATTACHTYPE_FROMMOSS:
          case ATTACHTYPE_FROMMOSS_DEC:
            {
              to_delete[del_cnt++] = attachment;
              break;
            }
          case ATTACHTYPE_MOSSTEMPL:
            /* This is a newly created attachment containing a MIME structure
               other clients could handle */
            {
              if (mosstmpl_found)
                {
                  log_error ("More then one mosstempl.");
                  goto done;
                }
              mosstmpl_found = 1;
              break;
            }
          default:
            to_delete[del_cnt++] = attachment;
        }
    }

  if (to_restore && !mosstmpl_found)
    {
      log_debug ("%s:%s: Restoring from MOSS.", SRCNAME, __func__);
      if (restore_msg_from_moss (message, to_restore, msgtype,
                                 msgcls))
        {
          log_error ("%s:%s: Error: %i", SRCNAME, __func__,
                     __LINE__);
        }
      else
        {
          to_restore = NULL;
        }
    }
  if (to_restore || mosstmpl_found)
    {
      HRESULT hr;
      SPropValue prop;
      /* Message was either restored or the only attachment is the
         mosstmplate in which case we need to activate the
         MultipartSigned magic.*/
      prop.ulPropTag = PR_MESSAGE_CLASS_A;
      if (is_smime)
        {
#if 0
          /* FIXME this does not appear to work somehow. */
          if (opt.enable_smime)
            {
              prop.Value.lpszA =
                (char*) "IPM.Note.InfoPathForm.GpgOL.SMIME.MultipartSigned";
              hr = HrSetOneProp (message, &prop);
            }
          else
#endif
            {
              ULONG tag;
              if (msgtype == MSGTYPE_GPGOL_MULTIPART_SIGNED)
                prop.Value.lpszA = (char*) "IPM.Note.SMIME.MultipartSigned";
              else
                prop.Value.lpszA = (char*) "IPM.Note.SMIME";
              hr = HrSetOneProp (message, &prop);

              if (!get_gpgolmsgclass_tag (message, &tag))
                {
                  SPropTagArray proparray;
                  proparray.cValues = 1;
                  proparray.aulPropTag[0] = tag;
                  hr = message->DeleteProps (&proparray, NULL);
                  if (hr)
                    {
                      log_error ("%s:%s: deleteprops smime failed: hr=%#lx\n",
                                 SRCNAME, __func__, hr);

                    }
                }
            }
        }
      else if (msgtype == MSGTYPE_GPGOL_MULTIPART_SIGNED)
        {
          prop.Value.lpszA =
            (char*) "IPM.Note.SMIME.MultipartSigned";
          hr = HrSetOneProp (message, &prop);
        }
      else
        {
          prop.Value.lpszA =
            (char*) "IPM.Note.SMIME";
          hr = HrSetOneProp (message, &prop);
        }
      if (hr)
        {
          log_error ("%s:%s: error setting the message class: hr=%#lx\n",
                     SRCNAME, __func__, hr);
          goto done;
        }

      /* Backup the real message class */
      if (!is_smime || opt.enable_smime)
        {
          if (mapi_set_gpgol_msg_class (message, msgcls))
            {
              log_error ("%s:%s: Error: %i", SRCNAME, __func__, __LINE__);
              goto done;
            }
        }
      else if (is_smime && !opt.enable_smime)
        {
          /* SMIME is disabled remove our categories. */
          mail->removeCategories_o ();
        }
    }

  result = 0;
done:

  /* Do the deletion body wipe even on error. */

  for (i = 0; i < del_cnt; i++)
    {
      LPDISPATCH attachment = to_delete[i];

      if (attachment == to_restore)
        {
          /* If restoring failed to restore is still set. In that case
             do not delete the MOSS attachment to avoid data loss. */
          continue;
        }
      /* Delete the attachments that are marked to delete */
      if (invoke_oom_method (attachment, "Delete", NULL))
        {
          log_error ("%s:%s: Error: %i", SRCNAME, __func__, __LINE__);
          result = -1;
        }
    }
  if (!body_restored && put_oom_string (mailitem, "Body", ""))
    {
      log_error ("%s:%s: Error: %i", SRCNAME, __func__, __LINE__);
      result = -1;
    }

  for (i = 0; i < del_cnt; i++)
    {
      gpgol_release (to_delete[i]);
    }

  xfree (to_delete);
  gpgol_release (attachments);
  xfree (msgcls);

  if (!result && finalize_mapi (message))
    {
      log_error ("%s:%s: Finalize failed.",
                 SRCNAME, __func__);
      result = -1;
    }

  gpgol_release (message);

  return result;
}
