/* MapiGPGME.cpp - Mapi support with GPGME
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
#include <mapidefs.h>
#include <mapiutil.h>
#include <time.h>
#include <initguid.h>
#include <mapiguid.h>
#include <atlbase.h>

#include "gpgme.h"
#include "intern.h"
#include "HashTable.h"
#include "MapiGPGME.h"
#include "engine.h"
#include "keycache.h"

/* attachment information */
#define ATT_SIGN(action) ((action) & GPG_ATTACH_SIGN)
#define ATT_ENCR(action) ((action) & GPG_ATTACH_ENCRYPT)
#define ATT_PREFIX ".pgpenc"

/* default extension for attachments */
#define EXT_MSG "pgp"
#define EXT_SIG "sig"

/* memory macros */
#define delete_buf(buf) delete [] (buf)

#define fail_if_null(p) do { if (!(p)) abort (); } while (0)


void 
MapiGPGME::logDebug (const char *fmt, ...)
{
    FILE * logfp;
    va_list a;

    if (enableLogging == false || this->logfile == NULL)
	return;

    logfp = fopen (this->logfile, "a+b");
    if (logfp == NULL)
	return;
    va_start (a, fmt);
    vfprintf (logfp, fmt, a);
    va_end (a);
    fclose (logfp);
}


void
MapiGPGME::clearObject (void)
{
    this->attachRows = NULL;
    this->attachTable = NULL;
    this->defaultKey = NULL;
    this->logfile = NULL;
    this->recipSet = NULL;
    this->parent = NULL;
    this->msg = NULL;
}


void
MapiGPGME::prepareLogging (void)
{
    char *val=NULL;
    load_extension_value ("logFile", &val);
    if (val != NULL && *val != '"' && *val != 0) {
	setLogFile (val);
	setEnableLogging (true);	
	xfree (val);	
    }
}


MapiGPGME::MapiGPGME (LPMESSAGE msg)
{
    clearConfig ();
    clearObject ();
    this->msg = msg;
    this->passCache = new HashTable ();
    op_init ();
    prepareLogging ();
    logDebug ("constructor %p\r\n", msg);
}


MapiGPGME::MapiGPGME (void)
{    
    clearConfig ();
    clearObject ();
    this->passCache = new HashTable ();
    op_init ();
    prepareLogging ();
    logDebug ("constructor null\r\n");
}


void 
MapiGPGME::cleanupTempFiles (void)
{
    HANDLE hd;
    WIN32_FIND_DATA fnd;
    char path[MAX_PATH+32], tmp[MAX_PATH+4];

    GetTempPath (sizeof (path), path);
    if (path[strlen (path)-1] != '\\')
	strcat (path, "\\");
    strcpy (tmp, path);
    strcat (path, "*"ATT_PREFIX"*");
    hd = FindFirstFile (path, &fnd);
    if (hd == INVALID_HANDLE_VALUE)
	return;
    do {
	char *p = (char *)xcalloc (1, strlen (tmp) + strlen (fnd.cFileName) +2);
	sprintf (p, "%s%s", tmp, fnd.cFileName);
	logDebug ("delete tmp %s\r\n", p);
	DeleteFile (p);
	xfree (p);
    } while (FindNextFile (hd, &fnd) == TRUE);
    FindClose (hd);
}

MapiGPGME::~MapiGPGME ()
{
    unsigned i=0;

    logDebug ("destructor %p\r\n", msg);
    op_deinit ();
    if (defaultKey)
	delete_buf (defaultKey);
    if (logfile)
	delete_buf (logfile);
    
    logDebug ("hash entries %d\r\n", passCache->size ());
    for (i = 0; i < passCache->size (); i++) {
	cache_item_t t = (cache_item_t)passCache->get (i);
	if (t != NULL)
	    cache_item_free (t);
    }
    delete passCache;
    freeAttachments ();
    cleanupTempFiles ();
}


int
MapiGPGME::setRTFBody (char *body)
{
    setMessageAccess (MAPI_ACCESS_MODIFY);
    HWND rtf = findMessageWindow (parent);
    if (rtf != NULL) {
	logDebug ("setRTFBody: window handle %p", rtf);
	SetWindowText (rtf, body);
	return TRUE;
    }
    return FALSE;
}


int 
MapiGPGME::setBody (char *body)
{
    /* XXX: handle richtext/html */
    SPropValue sProp; 
    HRESULT hr;
    int rc = TRUE;
    
    if (body == NULL) {
	logDebug ( "setBody with empty buffer\r\n");
	return FALSE;
    }
    rtfSync (body);
    sProp.ulPropTag = PR_BODY;
    sProp.Value.lpszA = body;
    hr = HrSetOneProp (msg, &sProp);
    if (FAILED (hr))
	rc = FALSE;
    logDebug ( "setBody rc=%d '%s'\r\n", rc, body);
    return rc;
}


void
MapiGPGME::rtfSync (char *body)
{
    BOOL bChanged = FALSE;
    SPropValue sProp; 
    HRESULT hr;

    /* Make sure that the Plaintext and the Richtext are in sync */
    sProp.ulPropTag = PR_BODY;
    sProp.Value.lpszA = "";
    hr = HrSetOneProp(msg, &sProp);
    RTFSync(msg, RTF_SYNC_BODY_CHANGED, &bChanged);
    sProp.Value.lpszA = body;
    hr = HrSetOneProp(msg, &sProp);
    RTFSync(msg, RTF_SYNC_BODY_CHANGED, &bChanged);
}


char* 
MapiGPGME::getBody (void)
{
    HRESULT hr;
    LPSPropValue lpspvFEID = NULL;
    char *body;

    hr = HrGetOneProp ((LPMAPIPROP) msg, PR_BODY, &lpspvFEID);
    if (FAILED (hr))
	return NULL;
    
    body = new char[strlen (lpspvFEID->Value.lpszA)+1];
    fail_if_null (body);
    strcpy (body, lpspvFEID->Value.lpszA);

    MAPIFreeBuffer (lpspvFEID);
    lpspvFEID = NULL;

    return body;
}


void 
MapiGPGME::freeKeyArray (void **key)
{
    gpgme_key_t *buf = (gpgme_key_t *)key;
    int i=0;

    if (buf == NULL)
	return;
    for (i = 0; buf[i] != NULL; i++) {
	gpgme_key_release (buf[i]);
	buf[i] = NULL;
    }
    xfree (buf);
}


int 
MapiGPGME::countRecipients (char **recipients)
{
    for (int i=0; recipients[i] != NULL; i++)
	;
    return i;
}


char** 
MapiGPGME::getRecipients (bool isRootMsg)
{
    HRESULT hr;
    LPMAPITABLE lpRecipientTable = NULL;
    LPSRowSet lpRecipientRows = NULL;
    char **rset = NULL;

    if (!isRootMsg)
	return NULL;
        
    static SizedSPropTagArray (1L, PropRecipientNum) = {1L, {PR_EMAIL_ADDRESS}};

    hr = msg->GetRecipientTable (0, &lpRecipientTable);
    if (SUCCEEDED(hr)) {
	size_t j = 0;
        hr = HrQueryAllRows (lpRecipientTable, 
			     (LPSPropTagArray) &PropRecipientNum,
			     NULL, NULL, 0L, &lpRecipientRows);
	rset = new char*[lpRecipientRows->cRows+1];
	fail_if_null (rset);
        for (j = 0L; j < lpRecipientRows->cRows; j++) {
	    const char *s = lpRecipientRows->aRow[j].lpProps[0].Value.lpszA;
	    rset[j] = new char[strlen (s)+1];
	    fail_if_null (rset[j]);
	    strcpy (rset[j], s);
	    logDebug ( "rset %d: %s\r\n", j, rset[j]);
	}
	rset[j] = NULL;
	if (NULL != lpRecipientTable)
	    lpRecipientTable->Release();
	if (NULL != lpRecipientRows)
	    FreeProws(lpRecipientRows);	
    }

    return rset;
}


void
MapiGPGME::freeUnknownKeys (char **unknown, int n)
{    
    for (int i=0; i < n; i++) {
	if (unknown[i] != NULL)
	    xfree (unknown[i]);
    }
    if (n > 0)
	xfree (unknown);
}

void 
MapiGPGME::freeRecipients (char **recipients)
{
    for (int i=0; recipients[i] != NULL; i++)
	delete_buf (recipients[i]);	
    delete recipients;
}


const char*
MapiGPGME::getPassphrase (const char *keyid)
{
    cache_item_t item = (cache_item_t)passCache->get(keyid);
    if (item != NULL)
	return item->pass;
    return NULL;
}


void
MapiGPGME::storePassphrase (void *itm)
{
    cache_item_t item = (cache_item_t)itm;
    cache_item_t old;
    old = (cache_item_t)passCache->get(item->keyid);
    if (old != NULL)
	cache_item_free (old);
    passCache->put (item->keyid+8, item);
    logDebug ( "put keyid %s = '%s'\r\n", item->keyid+8, "***");
}


int 
MapiGPGME::encrypt (void)
{
    gpgme_key_t *keys=NULL, *keys2=NULL;
    char *body = getBody ();
    char *newBody = NULL;
    char **recipients = getRecipients (true);
    char **unknown = NULL;
    int opts = 0;
    int err = 0;
    size_t all=0;

    /* XXX: if the body is empty, there is a 'access violation' */
    if (body == NULL) {
	freeRecipients (recipients);
	return 0;
    }

    logDebug ( "encrypt\r\n");
    int n = op_lookup_keys (recipients, &keys, &unknown, &all);
    logDebug ( "fnd %d need %d (%p)\r\n", n, all, unknown);
    if (n != countRecipients (recipients)) {
	logDebug ( "recipient_dialog_box2\r\n");
	recipient_dialog_box2 (keys, unknown, all, &keys2, &opts);
	free (keys);
	keys = keys2;
    }

    err = op_encrypt ((void*)keys, body, &newBody);
    if (err)
	MessageBox (NULL, op_strerror (err), "GPG Encryption", MB_ICONERROR|MB_OK);
    else
	setBody (newBody);
    delete_buf (body);
    xfree (newBody);
    freeRecipients (recipients);
    freeUnknownKeys (unknown, n);
    if (!err && hasAttachments ()) {
	logDebug ( "encrypt attachments\r\n");
	recipSet = (void *)keys;
	encryptAttachments (parent);
    }
    freeKeyArray ((void **)keys);
    return err;
}


/* XXX: I would prefer to use MapiGPGME::passphraseCallback, but member 
        functions have an incompatible calling convention. */
int 
passphraseCallback (void *opaque, const char *uid_hint, 
		    const char *passphrase_info,
		    int last_was_bad, int fd)
{
    MapiGPGME *ctx = (MapiGPGME*)opaque;
    const char *passwd;
    char keyid[16+1];
    DWORD nwritten = 0;
    int i=0;

    while (uid_hint && *uid_hint != ' ')
	keyid[i++] = *uid_hint++;
    keyid[i] = '\0';
    
    passwd = ctx->getPassphrase (keyid+8);
    /*logDebug ( "get keyid %s = '%s'\r\n", keyid+8, "***");*/
    if (passwd != NULL) {
	WriteFile ((HANDLE)fd, passwd, strlen (passwd), &nwritten, NULL);
	WriteFile ((HANDLE)fd, "\n", 1, &nwritten, NULL);
    }

    return 0;
}


int 
MapiGPGME::decrypt (void)
{
    outlgpg_type_t id;
    char *body = getBody ();
    char *newBody = NULL;
    int err;
    int hasAttach = hasAttachments ();

    id = getMessageType (body);
    if (id == GPG_TYPE_CLEARSIG)
	return verify ();

    if (nstorePasswd == 0)
	err = op_decrypt_start (body, &newBody);
    else if (passCache->size () == 0) {
	/* XXX: use the callback to see if a cached passphrase is available. If not
	        call the real passphrase callback and store the passphrase. */
	cache_item_t itm = NULL;
	err = op_decrypt_start_ext (body, &newBody, &itm);
	if (!err)
	    storePassphrase (itm);
    }
    else
	err = op_decrypt_next (passphraseCallback, this, body, &newBody);
    if (err) {
	if (hasAttach && gpg_error ((gpg_err_code_t)err) == gpg_error (GPG_ERR_NO_DATA))
	    ;
	else
	    MessageBox (NULL, op_strerror (err), "GPG Decryption", MB_ICONERROR|MB_OK);
    }
    else
	setRTFBody (newBody);

    if (hasAttach) {
	logDebug ( "decrypt attachments\r\n");
	decryptAttachments (parent);
    }
    delete_buf (body);
    xfree (newBody);
    return err;
}


int
MapiGPGME::sign (void)
{
    char *body = getBody ();
    char *newBody = NULL;
    int hasAttach = hasAttachments ();
    int err = 0;

    if (body == NULL)
	return 0;
    err = op_sign_start (body, &newBody);
    if (err)
	MessageBox (NULL, op_strerror (err), "GPG Sign", MB_ICONERROR|MB_OK);
    else
	setBody (newBody);

    if (hasAttach && autoSignAtt)
	signAttachments (parent);

    delete_buf (body);
    xfree (newBody);
    return err;
}


outlgpg_type_t
MapiGPGME::getMessageType (const char *body)
{
    if (strstr (body, "BEGIN PGP MESSAGE"))
	return GPG_TYPE_MSG;
    if (strstr (body, "BEGIN PGP SIGNED MESSAGE"))
	return GPG_TYPE_CLEARSIG;
    if (strstr (body, "BEGIN PGP SIGNATURE"))
	return GPG_TYPE_SIG;
    if (strstr (body, "BEGIN PGP PUBLIC KEY"))
	return GPG_TYPE_PUBKEY;
    if (strstr (body, "BEGIN PGP PRIVATE KEY"))
	return GPG_TYPE_SECKEY;
    return GPG_TYPE_NONE;
}



int
MapiGPGME::doCmdFile(int action, const char *in, const char *out)
{
    logDebug ( "doCmdFile action=%d in=%s out=%s\r\n", action, in, out);
    if (ATT_SIGN (action) && ATT_ENCR (action))
	return !op_sign_encrypt_file (recipSet, in, out);
    if (ATT_SIGN (action) && !ATT_ENCR (action))
	return !op_sign_file (OP_SIG_NORMAL, in, out);
    if (!ATT_SIGN (action) && ATT_ENCR (action))
	return !op_encrypt_file (recipSet, in, out);
    return !op_decrypt_file (in, out);    
}


int
MapiGPGME::doCmdAttach (int action)
{
    logDebug ( "doCmdAttach action=%d\n", action);
    if (ATT_SIGN (action) && ATT_ENCR (action))
	return signEncrypt ();
    if (ATT_SIGN (action) && !ATT_ENCR (action))
	return sign ();
    if (!ATT_SIGN (action) && ATT_ENCR (action))
	return encrypt ();
    return decrypt ();
}


int
MapiGPGME::doCmd (int doEncrypt, int doSign)
{
    logDebug ( "doCmd doEncrypt=%d doSign=%d\r\n", doEncrypt, doSign);
    if (doEncrypt && doSign)
	return signEncrypt ();
    if (doEncrypt && !doSign)
	return encrypt ();
    if (!doEncrypt && doSign)
	return sign ();
    return -1;
}


static void 
log_key_info (MapiGPGME *g, gpgme_key_t *keys, gpgme_key_t locusr)
{
    if (locusr != NULL)
	g->logDebug ( "locusr:%s:%s\r\n", 
		   gpgme_key_get_string_attr (locusr, GPGME_ATTR_USERID, NULL, 0),
		   gpgme_key_get_string_attr (locusr, GPGME_ATTR_KEYID, NULL, 0));
    else
	g->logDebug ( "locusr:null\r\n");
    gpgme_key_t n;
    int i;

    if (keys == NULL)
	return;
    i=0;
    for (n=keys[0]; keys[i] != NULL; i++)
	g->logDebug ( "recp:%d:%s:%s\r\n", i,
		   gpgme_key_get_string_attr (keys[i], GPGME_ATTR_USERID, NULL, 0),
		   gpgme_key_get_string_attr (keys[i], GPGME_ATTR_KEYID, NULL, 0));
}
	    

int
MapiGPGME::signEncrypt (void)
{
    char *body = getBody ();
    char *newBody = NULL;
    char **recipients = getRecipients (TRUE);
    char **unknown = NULL;
    gpgme_key_t locusr=NULL, *keys = NULL, *keys2 =NULL;	
    const char *s;

    if (body == NULL) {
	freeRecipients (recipients);
	return 0;
    }
    if (signer_dialog_box (&locusr, NULL) == -1)
	return 0;	
    s = gpgme_key_get_string_attr (locusr, GPGME_ATTR_KEYID, NULL, 0);
    logDebug ( "locusr keyid:%s\r\n", s);

    size_t all;
    int n = op_lookup_keys (recipients, &keys, &unknown, &all);
    if (n != countRecipients (recipients)) {
	recipient_dialog_box2 (keys, unknown, all, &keys2, NULL);
	xfree (keys);
	keys = keys2;
    }

    log_key_info (this, keys, locusr);
    int err = op_sign_encrypt ((void *)keys, (void*)locusr, body, &newBody);
    if (err)
	MessageBox (NULL, op_strerror (err), "GPG Sign Encrypt", MB_ICONERROR|MB_OK);
    else
	setBody (newBody);

    delete_buf (body);
    xfree (newBody);
    freeUnknownKeys (unknown, n);
    if (!err && hasAttachments ()) {
	logDebug ( "encrypt attachments");
	recipSet = (void *)keys;
	encryptAttachments (parent);
    }
    freeKeyArray ((void **)keys);
    gpgme_key_release (locusr);
    freeRecipients (recipients);
    return err;
}


int 
MapiGPGME::verify (void)
{
    char *body = getBody ();
    char *newBody = NULL;
    
    int err = op_verify_start (body, &newBody);
    if (err)
	MessageBox (NULL, op_strerror (err), "GPG Verify", MB_ICONERROR|MB_OK);
    else
	setRTFBody (newBody);

    delete_buf (body);
    xfree (newBody);
    return err;
}


void MapiGPGME::setDefaultKey (const char *key)
{
    if (defaultKey) {
	delete_buf (defaultKey);
	defaultKey = NULL;
    }
    defaultKey = new char[strlen (key)+1];
    fail_if_null (defaultKey);
    strcpy (defaultKey, key);
}


char* MapiGPGME::getDefaultKey (void)
{
    return defaultKey;
}


void 
MapiGPGME::setMessage (LPMESSAGE msg)
{
    this->msg = msg;
    logDebug ( "setMessage %p\r\n", msg);
}


void
MapiGPGME::setWindow(HWND hwnd)
{
    this->parent = hwnd;
}


/* We need this to find the mailer window because we directly change the text
   of the window instead of the MAPI object itself. */
HWND
MapiGPGME::findMessageWindow (HWND parent)
{
    HWND child;

    if (parent == NULL)
	return NULL;

    child = GetWindow (parent, GW_CHILD);
    while (child != NULL) {
	char buf[1024+1];
	HWND rtf;

	memset (buf, 0, sizeof (buf));
	GetWindowText (child, buf, sizeof (buf)-1);
	if (getMessageType (buf) != GPG_TYPE_NONE)
	    return child;
	rtf = findMessageWindow (child);
	if (rtf != NULL)
	    return rtf;
	child = GetNextWindow (child, GW_HWNDNEXT);	
    }
    /*logDebug ( "no message window found.\r\n");*/
    return NULL;
}


int
MapiGPGME::streamFromFile (const char *file, LPATTACH att)
{
    HRESULT hr;
    LPSTREAM to = NULL, from = NULL;
    STATSTG statInfo;

    hr = att->OpenProperty (PR_ATTACH_DATA_BIN, &IID_IStream, 0,
	 		      MAPI_CREATE | MAPI_MODIFY, (LPUNKNOWN*) &to);
    if (FAILED (hr))
	return FALSE;

    hr = OpenStreamOnFile (MAPIAllocateBuffer, MAPIFreeBuffer,
			   STGM_READ, (char*)file, NULL, &from);
    if (!SUCCEEDED (hr)) {
	to->Release ();
	logDebug ( "streamFromFile %s failed.\r\n", file);
	return FALSE;
    }
    from->Stat (&statInfo, STATFLAG_NONAME);
    from->CopyTo (to, statInfo.cbSize, NULL, NULL);
    to->Commit (0);
    to->Release ();
    from->Release ();
    logDebug ( "streamFromFile %s succeeded\r\n", file);
    return TRUE;
}


int
MapiGPGME::streamOnFile (const char *file, LPATTACH att)
{
    HRESULT hr;
    LPSTREAM from = NULL, to = NULL;
    STATSTG statInfo;

    hr = att->OpenProperty (PR_ATTACH_DATA_BIN, &IID_IStream, 
			    0, 0, (LPUNKNOWN*) &from);
    if (FAILED (hr))
	return FALSE;

    hr = OpenStreamOnFile (MAPIAllocateBuffer, MAPIFreeBuffer,
   		           STGM_CREATE | STGM_READWRITE, (char*) file,
			   NULL, &to);
    if (!SUCCEEDED (hr)) {
	from->Release ();
	logDebug ( "streamOnFile %s failed with %s\r\n", file, 
		    hr == MAPI_E_NO_ACCESS? 
		    "no access" : hr == MAPI_E_NOT_FOUND? "not found" : "unknown");
	return FALSE;
    }
    from->Stat (&statInfo, STATFLAG_NONAME);
    from->CopyTo (to, statInfo.cbSize, NULL, NULL);
    to->Commit (0);
    to->Release ();
    from->Release ();
    logDebug ( "streamOnFile %s succeeded\r\n", file);
    return TRUE;
}


int
MapiGPGME::getMessageFlags (void)
{
    HRESULT hr;
    LPSPropValue propval = NULL;
    int flags = 0;

    hr = HrGetOneProp (msg, PR_MESSAGE_FLAGS, &propval);
    if (FAILED (hr))
	return 0;
    flags = propval->Value.l;
    MAPIFreeBuffer (propval);
    return flags;
}


int
MapiGPGME::getMessageHasAttachments (void)
{
    HRESULT hr;
    LPSPropValue propval = NULL;
    int nattach = 0;

    hr = HrGetOneProp (msg, PR_HASATTACH, &propval);
    if (FAILED (hr))
	return 0;
    nattach = propval->Value.b? 1 : 0;
    MAPIFreeBuffer (propval);
    return nattach;   
}


bool
MapiGPGME::setMessageAccess (int access)
{
    HRESULT hr;
    SPropValue prop;
    prop.ulPropTag = PR_ACCESS;
    prop.Value.l = access;
    hr = HrSetOneProp (msg, &prop);
    return FAILED (hr)? false: true;
}


bool
MapiGPGME::setAttachMethod (LPATTACH obj, int mode)
{
    SPropValue prop;
    HRESULT hr;
    prop.ulPropTag = PR_ATTACH_METHOD;
    prop.Value.ul = mode;
    hr = HrSetOneProp (obj, &prop);
    return FAILED (hr)? true : false;
}


int
MapiGPGME::getAttachMethod (LPATTACH obj)
{
    HRESULT hr;
    LPSPropValue propval = NULL;
    int method = 0;

    hr = HrGetOneProp ((LPMAPIPROP)obj, PR_ATTACH_METHOD, &propval);
    if (FAILED (hr))
	return 0;
    method = propval->Value.ul;
    MAPIFreeBuffer (propval);
    return method;
}

bool
MapiGPGME::setAttachFilename (LPATTACH obj, const char *name, bool islong)
{
    HRESULT hr;
    SPropValue prop;
    prop.ulPropTag = PR_ATTACH_LONG_FILENAME;

    if (!islong)
	prop.ulPropTag = PR_ATTACH_FILENAME;
    prop.Value.lpszA = (char*) name;   
    hr = HrSetOneProp (obj, &prop);
    return FAILED (hr)? false: true;
}


char*
MapiGPGME::getAttachPathname (LPATTACH obj)
{
    LPSPropValue propval;
    HRESULT hr;
    char *path;

    hr = HrGetOneProp ((LPMAPIPROP)obj, PR_ATTACH_LONG_PATHNAME, &propval);
    if (FAILED (hr)) {
	hr = HrGetOneProp ((LPMAPIPROP)obj, PR_ATTACH_PATHNAME, &propval);
	if (SUCCEEDED (hr)) {
	    path = xstrdup (propval[0].Value.lpszA);
	    MAPIFreeBuffer (propval);
	}
	else
	    return NULL;
    }
    else {
	path = xstrdup (propval[0].Value.lpszA);
	MAPIFreeBuffer (propval);
    }
    return path;
}


char*
MapiGPGME::getAttachFilename (LPATTACH obj)
{
    LPSPropValue propval;
    HRESULT hr;
    char *name;

    hr = HrGetOneProp ((LPMAPIPROP)obj, PR_ATTACH_LONG_FILENAME, &propval);
    if (FAILED(hr)) {
	hr = HrGetOneProp ((LPMAPIPROP)obj, PR_ATTACH_FILENAME, &propval);
	if (SUCCEEDED (hr)) {
	    name = xstrdup (propval[0].Value.lpszA);
	    MAPIFreeBuffer (propval);
	}
	else
	    return NULL;
    }
    else {
	name = xstrdup (propval[0].Value.lpszA);
	MAPIFreeBuffer (propval);
    }
    return name;
}


bool
MapiGPGME::checkAttachmentExtension (const char *ext)
{
    if (ext == NULL)
	return false;
    if (*ext == '.')
	ext++;
    logDebug ( "checkAttachmentExtension: %s\r\n", ext);
    if (stricmp (ext, "gpg") == 0 ||
	stricmp (ext, "pgp") == 0 ||
	stricmp (ext, "asc") == 0)
	return true;
    return false;
}


const char*
MapiGPGME::getAttachmentExtension (const char *fname)
{
    static char ext[4];
    char *p;

    p = strrchr (fname, '.');
    if (p != NULL) {
	/* XXX: what if the extension is < 3 chars */
	strncpy (ext, p, 4);
	if (checkAttachmentExtension (ext))
	    return ext;
    }
    return EXT_MSG;
}


const char*
MapiGPGME::getPGPExtension (int action)
{
    if (ATT_SIGN (action))
	return EXT_SIG;
    return EXT_MSG;
}


bool 
MapiGPGME::setXHeader (const char *name, const char *val)
{  
    USES_CONVERSION;
    LPMDB lpMdb = NULL;
    HRESULT hr = NULL;  
    LPSPropTagArray pProps = NULL;
    SPropValue pv;
    MAPINAMEID mnid[1];	
    // {00020386-0000-0000-C000-000000000046}  ->  GUID For X-Headers	
    GUID guid = {0x00020386, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x46} };

    mnid[0].lpguid = &guid;
    mnid[0].ulKind = MNID_STRING;
    mnid[0].Kind.lpwstrName = A2W (name);

    hr = msg->GetIDsFromNames (1, (LPMAPINAMEID*)mnid, MAPI_CREATE, &pProps);
    if (FAILED (hr))
	return false;
    
    pv.ulPropTag = (pProps->aulPropTag[0] & 0xFFFF0000) | PT_STRING8;
    pv.Value.lpszA = (char *)val;
    hr = HrSetOneProp(msg, &pv);	
    if (!SUCCEEDED (hr))
	return false;

    return true;
}


char*
MapiGPGME::getXHeader (const char *name)
{
    /* XXX: PR_TRANSPORT_HEADERS is not available in my MSDN. */
    return NULL;
}


void
MapiGPGME::freeAttachments (void)
{
    if (attachTable != NULL) {
        attachTable->Release ();
	attachTable = NULL;
    }
    if (attachRows != NULL) {
        FreeProws (attachRows);
	attachRows = NULL;
    }
}


int
MapiGPGME::getAttachments (void)
{
    static SizedSPropTagArray (1L, PropAttNum) = {1L, {PR_ATTACH_NUM}};
    HRESULT hr;    
   
    hr = msg->GetAttachmentTable (0, &attachTable);
    if (FAILED (hr))
	return FALSE;

    hr = HrQueryAllRows (attachTable, (LPSPropTagArray) &PropAttNum,
			 NULL, NULL, 0L, &attachRows);
    if (FAILED (hr)) {
	freeAttachments ();
	return FALSE;
    }
    return TRUE;
}


LPATTACH
MapiGPGME::openAttachment (int pos)
{
    HRESULT hr;
    LPATTACH att = NULL;
    
    hr = msg->OpenAttach (pos, NULL, MAPI_BEST_ACCESS, &att);	
    if (SUCCEEDED (hr))
	return att;
    return NULL;
}


void
MapiGPGME::releaseAttachment (LPATTACH att)
{
    att->Release ();
}



char*
MapiGPGME::generateTempname (const char *name)
{
    char temp[MAX_PATH+2];
    char *p;

    GetTempPath (sizeof (temp)-1, temp);
    if (temp[strlen (temp)-1] != '\\')
	strcat (temp, "\\");
    p = (char *)xcalloc (1, strlen (temp) + strlen (name) + 16);
    sprintf (p, "%s%s", temp, name);
    return p;
}


bool
MapiGPGME::signAttachment (const char *datfile)
{
    char *sigfile;
    LPATTACH newatt;
    int pos=0, err=0;

    sigfile = (char *)xcalloc (1,strlen (datfile)+5);
    strcpy (sigfile, datfile);
    strcat (sigfile, ".asc");

    newatt = createAttachment (pos);
    setAttachMethod (newatt, ATTACH_BY_VALUE);
    setAttachFilename (newatt, sigfile, false);

    if (nstorePasswd == 0)
	err = op_sign_file (OP_SIG_DETACH, datfile, sigfile);
    else if (passCache->size () == 0) {
	cache_item_t itm=NULL;
	err = op_sign_file_ext (OP_SIG_DETACH, datfile, sigfile, &itm);
	if (!err)
	    storePassphrase (itm);
    }
    else
	err = op_sign_file_next (passphraseCallback, this, OP_SIG_DETACH, datfile, sigfile);

    if (streamFromFile (sigfile, newatt)) {
	logDebug ("signAttachment: commit changes.\r\n");
	newatt->SaveChanges (FORCE_SAVE);
    }
    releaseAttachment (newatt);
    xfree (sigfile);

    return (!err)? true : false;
}

/* XXX: find a way to see if the attachment is already secured. This could be
        done by watching at the extension or checking the first lines. */
int
MapiGPGME::processAttachment (LPATTACH *attm, HWND hwnd, int pos, int action)
{    
    LPATTACH att = *attm;
    int method = getAttachMethod (att);
    BOOL success = TRUE;
    HRESULT hr;

    /* XXX: sign-only code is still not very intuitive. */

    if (action == GPG_ATTACH_NONE)
	return FALSE;
    if (action == GPG_ATTACH_DECRYPT && !saveDecryptedAtt)
	return TRUE;

    switch (method) {
    case ATTACH_EMBEDDED_MSG:
	LPMESSAGE emb;

	/* we do not support to sign these kind of attachments. */
	if (action == GPG_ATTACH_SIGN)
	    return TRUE;
	hr = att->OpenProperty (PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, 
			        MAPI_MODIFY, (LPUNKNOWN*) &emb);
	if (FAILED (hr))
	    return FALSE;
	setWindow (hwnd);
	setMessage (emb);
	if (doCmdAttach (action))
	    success = FALSE;
	emb->SaveChanges (FORCE_SAVE);
	att->SaveChanges (FORCE_SAVE);
	emb->Release ();
	break;

    case ATTACH_BY_VALUE:
	char *inname;
	char *outname;
	char *tmp;

	tmp = getAttachFilename (att);
	inname =  generateTempname (tmp);
	logDebug ( "enc inname: '%s'\r\n", inname);
	if (action != GPG_ATTACH_DECRYPT) {
	    char *tmp2 = (char *)xcalloc (1, strlen (inname) 
					     + strlen (ATT_PREFIX) + 4 + 1);
	    sprintf (tmp2, "%s"ATT_PREFIX".%s", tmp, getPGPExtension (action));
	    outname = generateTempname (tmp2);
	    xfree (tmp2);
	    logDebug ( "enc outname: '%s'\r\n", outname);
	}
	else {
	    if (checkAttachmentExtension (strrchr (tmp, '.')) == false) {
		logDebug ( "%s: no pgp extension found.\r\n", tmp);
		xfree (tmp);
		xfree (inname);
		return TRUE;
	    }
	    char *tmp2 = (char*)xcalloc (1, strlen (tmp) + 4);
	    strcpy (tmp2, tmp);
	    tmp2[strlen (tmp2) - 4] = '\0';
	    outname = generateTempname (tmp2);
	    xfree (tmp2);
	    logDebug ("dec outname: '%s'\r\n", outname);
	}
	success = FALSE;
	/* if we are in sign-only mode, just create a detached signature
	   for each attachment but do not alter the attachment data itself. */
	if (action != GPG_ATTACH_SIGN && streamOnFile (inname, att)) {
	    if (doCmdFile (action, inname, outname))
		success = TRUE;
	    else
		logDebug ( "doCmdFile failed\r\n");
	}
	if ((action == GPG_ATTACH_ENCRYPT || action == GPG_ATTACH_SIGN) 
	    && autoSignAtt)
	    signAttachment (inname);

	/*DeleteFile (inname);*/
	/* XXX: the file does not seemed to be closed. */
	xfree (inname);
	xfree (tmp);
	
	if (action != GPG_ATTACH_SIGN)
	    deleteAttachment (pos);

	if (action == GPG_ATTACH_ENCRYPT) {
	    LPATTACH newatt;
	    *attm = newatt = createAttachment (pos);
	    setAttachMethod (newatt, ATTACH_BY_VALUE);
	    setAttachFilename (newatt, outname, false);

	    if (streamFromFile (outname, newatt)) {
		logDebug ( "commit changes.\r\n");	    
		newatt->SaveChanges (FORCE_SAVE);
	    }
	}
	else if (success && action == GPG_ATTACH_DECRYPT) {
	    success = saveDecryptedAttachment (NULL, outname);
	    logDebug ("saveDecryptedAttachment ec=%d\r\n", success);
	}
	DeleteFile (outname);
	xfree (outname);
	releaseAttachment (att);
	break;

    case ATTACH_BY_REF_ONLY:
	break;

    case ATTACH_OLE:
	break;

    }

    return success;
}


int 
MapiGPGME::decryptAttachments (HWND hwnd)
{
    int n;

    if (!getAttachments ())
	return FALSE;
    n = countAttachments ();
    logDebug ( "dec: mail has %d attachments\r\n", n);
    if (!n)
	return TRUE;
    for (int i=0; i < n; i++) {
	LPATTACH amsg = openAttachment (i);
	if (!amsg)
	    continue;
	processAttachment (&amsg, hwnd, i, GPG_ATTACH_DECRYPT);
    }
    freeAttachments ();
    return 0;
}


int
MapiGPGME::signAttachments (HWND hwnd)
{
    if (!getAttachments ())
	return FALSE;
    int n = countAttachments ();
    logDebug ("sig: mail has %d attachments\r\n", n);
    if (!n)
	return TRUE;
    for (int i=0; i < n; i++) {
	LPATTACH amsg = openAttachment (i);
	if (!amsg)
	    continue;
	processAttachment (&amsg, hwnd, i, GPG_ATTACH_SIGN);
	releaseAttachment (amsg);
    }
    freeAttachments ();
    return 0;
}


int
MapiGPGME::encryptAttachments (HWND hwnd)
{    
    int n;

    if (!getAttachments ())
	return FALSE;
    n = countAttachments ();
    logDebug ("enc: mail has %d attachments\r\n", n);
    if (!n)
	return TRUE;
    for (int i=0; i < n; i++) {
	LPATTACH amsg = openAttachment (i);
	if (amsg == NULL)
	    continue;
	processAttachment (&amsg, hwnd, i, GPG_ATTACH_ENCRYPT);
	releaseAttachment (amsg);	
    }
    freeAttachments ();
    return 0;
}


bool 
MapiGPGME::saveDecryptedAttachment (HWND root, const char *srcname)
				     
{
    char filter[] = "All Files (*.*)|*.*||";
    char fname[MAX_PATH+1];
    char *p;
    OPENFILENAME ofn;

    for (size_t i=0; i< strlen (filter); i++)  {
	if (filter[i] == '|')
	    filter[i] = '\0';
    }

    memset (fname, 0, sizeof (fname));
    p = strstr (srcname, ATT_PREFIX);
    if (!p)
	strncpy (fname, srcname, MAX_PATH);
    else {
	strncpy (fname, srcname, (p-srcname));
	strcat (fname, srcname+(p-srcname)+strlen (ATT_PREFIX));	
    }

    memset (&ofn, 0, sizeof (ofn));
    ofn.lStructSize = sizeof (ofn);
    ofn.hwndOwner = root;
    ofn.lpstrFile = fname;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.Flags |= OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
    ofn.lpstrTitle = "GPG - Save decrypted attachments";
    ofn.lpstrFilter = filter;

    if (GetSaveFileName (&ofn)) {
	logDebug ("copy %s -> %s\r\n", srcname, fname);
	return CopyFile (srcname, fname, FALSE) == 0? false : true;
    }
    return true;
}


int
MapiGPGME::startKeyManager (void)
{
    return start_key_manager ();
}


void
MapiGPGME::startConfigDialog (HWND parent)
{
    config_dialog_box (parent);
}


int
MapiGPGME::readOptions (void)
{
    char *val=NULL;

    load_extension_value ("autoSignAttachments", &val);
    autoSignAtt = val == NULL || *val != '1' ? 0 : 1;
    xfree (val); val =NULL;

    load_extension_value ("saveDecryptedAttachments", &val);
    saveDecryptedAtt = val == NULL || *val != '1'? 0 : 1;
    xfree (val); val =NULL;

    load_extension_value ("encryptDefault", &val);
    doEncrypt = val == NULL || *val != '1'? 0 : 1;
    xfree (val); val=NULL;

    load_extension_value ("signDefault", &val);
    doSign = val == NULL || *val != '1'? 0 : 1;
    xfree (val); val = NULL;

    load_extension_value ("addDefaultKey", &val);
    encryptDefault = val == NULL || *val != '1' ? 0 : 1;
    xfree (val); val = NULL;

    load_extension_value ("storePasswdTime", &val);
    nstorePasswd = val == NULL || *val == '0'? 0 : atol (val);
    xfree (val); val = NULL;

    load_extension_value ("encodingFormat", &val);
    encFormat = val == NULL? GPG_FMT_CLASSIC  : atol (val);
    xfree (val); val = NULL;

    load_extension_value ("logFile", &val);
    if (val == NULL ||*val == '"' || *val == 0)
	logfile = NULL;
    else {
	setLogFile (val);
	setEnableLogging (true);
    }
    xfree (val); val=NULL;

    load_extension_value ("defaultKey", &val);
    if (val == NULL || *val == '"') {
	encryptDefault = 0;
	defaultKey = NULL;
    }
    else {
	setDefaultKey (val);
	encryptDefault = 1;
    }

    xfree (val); val=NULL;

    return 0;
}


void
MapiGPGME::displayError (HWND root, const char *title)
{	
    char buf[256];
    
    FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError (), 
		   MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), 
		   buf, sizeof (buf)-1, NULL);
    MessageBox (root, buf, title, MB_OK|MB_ICONERROR);
}


int
MapiGPGME::writeOptions (void)
{
    struct conf {
	const char *name;
	bool value;
    };
    struct conf opt[] = {
	{"encryptDefault", doEncrypt},
	{"signDefault", doSign},
	{"addDefaultKey", encryptDefault},
	{"saveDecryptedAttachments", saveDecryptedAtt},
	{"autoSignAttachments", autoSignAtt},
	{NULL, 0}
    };
    char buf[32];

    for (int i=0; opt[i].name != NULL; i++) {
	int rc = store_extension_value (opt[i].name, opt[i].value? "1": "0");
	if (rc)
	    displayError (NULL, "Save options in the registry");
	/* XXX: also show the name of the value */
    }

    if (logfile != NULL)
	store_extension_value ("logFile", logfile);
    if (defaultKey != NULL)
	store_extension_value ("defaultKey", defaultKey);
    
    sprintf (buf, "%d", nstorePasswd);
    store_extension_value ("storePasswdTime", buf);
    
    sprintf (buf, "%d", encFormat);
    store_extension_value ("encodingFormat", buf);

    return 0;
}


int 
MapiGPGME::attachPublicKey (const char *keyid)
{
    /* @untested@ */
    const char *patt[1];
    char *keyfile;
    int err, pos = 0;
    LPATTACH newatt;

    keyfile = generateTempname (keyid);
    patt[0] = xstrdup (keyid);
    err = op_export_keys (patt, keyfile);

    newatt = createAttachment (pos);
    setAttachMethod (newatt, ATTACH_BY_VALUE);
    setAttachFilename (newatt, keyfile, false);
    /* XXX: set proper RFC3156 MIME types. */

    if (streamFromFile (keyfile, newatt)) {
	logDebug ("attachPublicKey: commit changes.\r\n");
	newatt->SaveChanges (FORCE_SAVE);
    }
    releaseAttachment (newatt);
    xfree (keyfile);
    xfree ((void *)patt[0]);
    return err;
}


    



    
    