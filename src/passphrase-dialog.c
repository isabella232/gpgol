/* passphrase-dialog.c
 *	Copyright (C) 2004 Timo Schulz
 *	Copyright (C) 2005 g10 Code GmbH
 *
 * This file is part of GPGME Dialogs.
 *
 * GPGME Dialogs is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 
 * of the License, or (at your option) any later version.
 *  
 * GPGME Dialogs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with GPGME Dialogs; if not, write to the Free Software Foundation, 
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 */
#include <windows.h>
#include <time.h>

#include "resource.h"
#include "gpgme.h"
#include "keycache.h"
#include "intern.h"
#include "usermap.h"

static void
add_string_list (HWND hbox, const char **list, int start_idx)
{
    const char * s;
    int i;

    for (i=0; (s=list[i]); i++)
	SendMessage (hbox, CB_ADDSTRING, 0, (LPARAM)(const char *)s);
    SendMessage (hbox, CB_SETCURSEL, (WPARAM) start_idx, 0);
}


static void
set_key_hint (struct decrypt_key_s * dec, HWND dlg, int ctrlid)
{
    const char *s = dec->user_id;
    char *key_hint;
    char stop_char=0;
    size_t i=0;

    if (dec->user_id != NULL) {
	key_hint = (char *)xmalloc (17 + strlen (dec->user_id) + 32);
	if (strchr (s, '<') && strchr (s, '>'))
	    stop_char = '<';
	else if (strchr (s, '(') && strchr (s, ')'))
	    stop_char = '(';
	while (s && *s != stop_char)
	    key_hint[i++] = *s++;
	key_hint[i++] = ' ';
	sprintf (key_hint+i, "(0x%s)", dec->keyid+8);
    }
    else
	key_hint = xstrdup ("No key hint given.");
    SendDlgItemMessage (dlg, ctrlid, CB_ADDSTRING, 0, 
			(LPARAM)(const char *)key_hint);
    SendDlgItemMessage (dlg, ctrlid, CB_SETCURSEL, 0, 0);
    xfree (key_hint);
}


static void
load_recipbox (HWND dlg, int ctlid, gpgme_ctx_t ctx)
{    	
    gpgme_decrypt_result_t res;
    gpgme_recipient_t r;
    void *usermap ;

    if (ctx == NULL)
	return;
    res = gpgme_op_decrypt_result (ctx);
    if (res == NULL || res->recipients == NULL)
	return;
    usermap = new_usermap (res->recipients);
    for (r = res->recipients; r; r = r->next) {
	char *userid = HashTable_get (usermap, r->keyid);
	SendDlgItemMessage (dlg, ctlid, LB_ADDSTRING, 0, 
			    (LPARAM)(const char*)userid);
    }
    free_usermap (usermap);
}


static void
load_secbox (HWND dlg, int ctlid)
{
    gpgme_key_t sk;
    size_t n=0, doloop=1;
    void *ctx=NULL;

    enum_gpg_seckeys (NULL, &ctx);
    while (doloop) {
	const char *name, *email, *keyid, *algo;
	char *p;

	if (enum_gpg_seckeys (&sk, &ctx))
	    doloop = 0;

	if (gpgme_key_get_ulong_attr (sk, GPGME_ATTR_KEY_REVOKED, NULL, 0) ||
	    gpgme_key_get_ulong_attr (sk, GPGME_ATTR_KEY_EXPIRED, NULL, 0) ||
	    gpgme_key_get_ulong_attr (sk, GPGME_ATTR_KEY_INVALID, NULL, 0))
	    continue;
	
	name = gpgme_key_get_string_attr (sk, GPGME_ATTR_NAME, NULL, 0);
	email = gpgme_key_get_string_attr (sk, GPGME_ATTR_EMAIL, NULL, 0);
	keyid = gpgme_key_get_string_attr (sk, GPGME_ATTR_KEYID, NULL, 0);
	algo = gpgme_key_get_string_attr (sk, GPGME_ATTR_ALGO, NULL, 0);
	if (!email)
	    email = "";
	p = (char *)xcalloc (1, strlen (name) + strlen (email) + 17 + 32);
	if (email && strlen (email))
	    sprintf (p, "%s <%s> (0x%s, %s)", name, email, keyid+8, algo);
	else
	    sprintf (p, "%s (0x%s, %s)", name, keyid+8, algo);
	SendDlgItemMessage (dlg, ctlid, CB_ADDSTRING, 0, 
			    (LPARAM)(const char *) p);
	xfree (p);
    }
    
    ctx = NULL;
    reset_gpg_seckeys (&ctx);
    doloop = 1;
    n = 0;
    while (doloop) {
	if (enum_gpg_seckeys (&sk, &ctx))
	    doloop = 0;
	if (gpgme_key_get_ulong_attr (sk, GPGME_ATTR_KEY_REVOKED, NULL, 0) ||
	    gpgme_key_get_ulong_attr (sk, GPGME_ATTR_KEY_EXPIRED, NULL, 0) ||
	    gpgme_key_get_ulong_attr (sk, GPGME_ATTR_KEY_INVALID, NULL, 0))
	    continue;
	SendDlgItemMessage (dlg, ctlid, CB_SETITEMDATA, n, (LPARAM)(DWORD)sk);
	n++;
    }
    SendDlgItemMessage (dlg, ctlid, CB_SETCURSEL, 0, 0);
    reset_gpg_seckeys (&ctx);
}


static BOOL CALLBACK
decrypt_key_dlg_proc (HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam)
{
    static struct decrypt_key_s * dec;
    static int hide_state = 1;
    size_t n;

    switch (msg) {
    case WM_INITDIALOG:
	dec = (struct decrypt_key_s *)lparam;
	if (dec && dec->use_as_cb) {
	    dec->opts = 0;
	    dec->pass = NULL;
	    set_key_hint (dec, dlg, IDC_DEC_KEYLIST);
	    EnableWindow (GetDlgItem (dlg, IDC_DEC_KEYLIST), FALSE);
	}
	if (dec && dec->last_was_bad)
	    SetDlgItemText (dlg, IDC_DEC_HINT, "Invalid passphrase; please try again...");
	else
	    SetDlgItemText (dlg, IDC_DEC_HINT, "");
	if (dec && !dec->use_as_cb)
	    load_secbox (dlg, IDC_DEC_KEYLIST);
	CheckDlgButton (dlg, IDC_DEC_HIDE, BST_CHECKED);
	center_window (dlg, NULL);
	if (dec->hide_pwd) {
	    ShowWindow (GetDlgItem (dlg, IDC_DEC_HIDE), SW_HIDE);
	    ShowWindow (GetDlgItem (dlg, IDC_DEC_PASS), SW_HIDE);
	    ShowWindow (GetDlgItem (dlg, IDC_DEC_PASSINF), SW_HIDE);
	    /* XXX: make the dialog window smaller */
	}
	else
	    SetFocus (GetDlgItem (dlg, IDC_DEC_PASS));
	SetForegroundWindow (dlg);
	return FALSE;

    case WM_DESTROY:
	hide_state = 1;
	break;

    case WM_SYSCOMMAND:
	if (wparam == SC_CLOSE)
	    EndDialog (dlg, TRUE);
	break;

    case WM_COMMAND:
	switch (HIWORD (wparam)) {
	case BN_CLICKED:
	    if ((int)LOWORD (wparam) == IDC_DEC_HIDE) {
		HWND hwnd;

		hide_state ^= 1;
		hwnd = GetDlgItem (dlg, IDC_DEC_PASS);
		SendMessage (hwnd, EM_SETPASSWORDCHAR, hide_state? '*' : 0, 0);
		SetFocus (hwnd);
	    }
	    break;
	}
	switch (LOWORD (wparam)) {
	case IDOK:
	    n = SendDlgItemMessage (dlg, IDC_DEC_PASS, WM_GETTEXTLENGTH, 0, 0);
	    if (n) {
		dec->pass = (char *)xcalloc (1, n+2);
		GetDlgItemText (dlg, IDC_DEC_PASS, dec->pass, n+1);
	    }
	    if (!dec->use_as_cb) {
		int idx = SendDlgItemMessage (dlg, IDC_DEC_KEYLIST, 
					      CB_GETCURSEL, 0, 0);
		dec->signer = (gpgme_key_t)SendDlgItemMessage (dlg, IDC_DEC_KEYLIST,
							       CB_GETITEMDATA, idx, 0);
		gpgme_key_ref (dec->signer);
	    }
	    EndDialog (dlg, TRUE);
	    break;

	case IDCANCEL:
	    if (dec && dec->use_as_cb && (dec->flags & 0x01)) {
		const char *warn = "If you cancel this dialog, the message will be sent without signing.\n\n"
				   "Do you really want to cancel?";
		n = MessageBox (dlg, warn, "Secret Key Dialog", MB_ICONWARNING|MB_YESNO);
		if (n == IDNO)
		    return FALSE;
	    }
	    dec->opts = OPT_FLAG_CANCEL;
	    dec->pass = NULL;
	    EndDialog (dlg, FALSE);
	    break;
	}
	break;
    }
    return FALSE;
}


static BOOL CALLBACK
decrypt_key_ext_dlg_proc (HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam)
{
    static struct decrypt_key_s * dec;
    static int hide_state = 1;
    size_t n;

    switch (msg) {
    case WM_INITDIALOG:
	dec = (struct decrypt_key_s *)lparam;
	if (dec != NULL) {
	    dec->opts = 0;
	    dec->pass = NULL;
	    set_key_hint (dec, dlg, IDC_DECEXT_KEYLIST);
	    EnableWindow (GetDlgItem (dlg, IDC_DECEXT_KEYLIST), FALSE);
	}
	if (dec && dec->last_was_bad)
	    SetDlgItemText (dlg, IDC_DECEXT_HINT, "Invalid passphrase; please try again...");
	else
	    SetDlgItemText (dlg, IDC_DECEXT_HINT, "");
	if (dec != NULL)
	    load_recipbox (dlg, IDC_DECEXT_RSET, (gpgme_ctx_t)dec->ctx);
	CheckDlgButton (dlg, IDC_DECEXT_HIDE, BST_CHECKED);
	center_window (dlg, NULL);
	SetFocus (GetDlgItem (dlg, IDC_DECEXT_PASS));
	SetForegroundWindow (dlg);
	return FALSE;

    case WM_DESTROY:
	hide_state = 1;
	break;

    case WM_SYSCOMMAND:
	if (wparam == SC_CLOSE)
	    EndDialog (dlg, TRUE);
	break;

    case WM_COMMAND:
	switch (HIWORD (wparam)) {
	case BN_CLICKED:
	    if ((int)LOWORD (wparam) == IDC_DECEXT_HIDE) {
		HWND hwnd;

		hide_state ^= 1;
		hwnd = GetDlgItem (dlg, IDC_DECEXT_PASS);
		SendMessage (hwnd, EM_SETPASSWORDCHAR, hide_state? '*' : 0, 0);
		SetFocus (hwnd);
	    }
	    break;
	}
	switch (LOWORD (wparam)) {
	case IDOK:
	    n = SendDlgItemMessage (dlg, IDC_DECEXT_PASS, WM_GETTEXTLENGTH, 0, 0);
	    if (n) {
		dec->pass = (char *)xcalloc( 1, n+2 );
		GetDlgItemText( dlg, IDC_DECEXT_PASS, dec->pass, n+1 );
	    }
	    EndDialog (dlg, TRUE);
	    break;

	case IDCANCEL:
	    if (dec && dec->use_as_cb && (dec->flags & 0x01)) {
		const char *warn = "If you cancel this dialog, the message will be sent without signing.\n"
				   "Do you really want to cancel?";
		n = MessageBox (dlg, warn, "Secret Key Dialog", MB_ICONWARNING|MB_YESNO);
		if (n == IDNO)
		    return FALSE;
	    }
	    dec->opts = OPT_FLAG_CANCEL;
	    dec->pass = NULL;
	    EndDialog (dlg, FALSE);
	    break;
	}
	break;
    }
    return FALSE;
}

/* Display a signer dialog which contains all secret keys, useable
   for signing data. The key is returned in r_key. The password in
   r_passwd. */
int 
signer_dialog_box (gpgme_key_t *r_key, char **r_passwd)
{
    struct decrypt_key_s hd;
    int rc = 0;

    memset(&hd, 0, sizeof (hd));
    hd.hide_pwd = 1;
    DialogBoxParam (glob_hinst, (LPCTSTR)IDD_DEC, GetDesktopWindow (),
		    decrypt_key_dlg_proc, (LPARAM)&hd);
    if (hd.signer) {
	if (r_passwd)
	    *r_passwd = xstrdup (hd.pass);
	else {	    
	    xfree (hd.pass);
	    hd.pass = NULL;
	}
	*r_key = hd.signer;
    }
    if (hd.opts & OPT_FLAG_CANCEL)
	rc = -1;
    memset (&hd, 0, sizeof (hd));    
    return rc;
}


/* GPGME passphrase callback function. It starts the decryption dialog
   to request the passphrase from the user. */
int
passphrase_callback_box (void *opaque, const char *uid_hint, 
			 const char *pass_info,
			 int prev_was_bad, int fd)
{
    struct decrypt_key_s *hd = (struct decrypt_key_s *)opaque;
    DWORD nwritten = 0;

    if (hd->opts & OPT_FLAG_CANCEL) {
	WriteFile ((HANDLE)fd, "\n", 1, &nwritten, NULL);
	CloseHandle ((HANDLE)fd);
	return -1;
    }
    if (prev_was_bad) {
	xfree (hd->pass);
	hd->pass = NULL;
    }

    if (hd && uid_hint && !hd->pass) {
	const char * s = uid_hint;
	size_t i=0;
	
	while (s && *s != ' ')
	    hd->keyid[i++] = *s++;
	hd->keyid[i] = '\0'; s++;
	if (hd->user_id) {
	    xfree (hd->user_id);
	    hd->user_id = NULL;
	}
	hd->user_id = (char *)xcalloc (1, strlen (s) + 2);
	strcpy (hd->user_id, s);

	hd->last_was_bad = prev_was_bad? 1: 0;
	hd->use_as_cb = 1;
	if (hd->flags & 0x01)
	    DialogBoxParam (glob_hinst, (LPCSTR)IDD_DEC, GetDesktopWindow (),
			    decrypt_key_dlg_proc, (LPARAM)hd);
	else
	    DialogBoxParam (glob_hinst, (LPCTSTR)IDD_DEC_EXT, GetDesktopWindow (),
			    decrypt_key_ext_dlg_proc, 
			    (LPARAM)hd);
    }
    if (hd->pass != NULL) {
	WriteFile ((HANDLE)fd, hd->pass, strlen (hd->pass), &nwritten, NULL);
	WriteFile ((HANDLE)fd, "\n", 1, &nwritten, NULL);
    }
    else
	WriteFile((HANDLE)fd, "\n", 1, &nwritten, NULL);
    return 0;
}


/* Release the context which was used in the passphrase callback. */
void
free_decrypt_key (struct decrypt_key_s * ctx)
{
    if (!ctx)
	return;
    if (ctx->pass) {
	xfree (ctx->pass);
	ctx->pass = NULL;
    }
    if (ctx->user_id) {
	xfree (ctx->user_id);
	ctx->user_id = NULL;
    }
    xfree(ctx);
}