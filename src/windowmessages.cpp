#include "windowmessages.h"

#include "util.h"
#include "oomhelp.h"

#include <stdio.h>

#define RESPONDER_CLASS_NAME "GpgOLResponder"

/* Singleton window */
static HWND g_responder_window = NULL;

static int
request_send_mail (LPDISPATCH mailitem)
{
  if (invoke_oom_method (mailitem, "Send", NULL))
    {
      log_debug ("%s:%s: Failed to resend message.",
                 SRCNAME, __func__);
      return -1;
    }
  log_debug ("%s:%s: Message %p sent.",
             SRCNAME, __func__, mailitem);
  return 0;
}

LONG_PTR WINAPI
gpgol_window_proc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  if (message == WM_USER + 1)
    {
      wm_ctx_t *ctx = (wm_ctx_t *) lParam;
      log_debug ("%s:%s: Recieved user msg: %i",
                 SRCNAME, __func__, ctx->wmsg_type);
      switch (ctx->wmsg_type)
        {
          case (REQUEST_SEND_MAIL):
            {
              ctx->err = request_send_mail ((LPDISPATCH) ctx->data);
              break;
            }
          default:
            log_debug ("Unknown msg");
        }
        return 0;
    }

  return DefWindowProc(hWnd, message, wParam, lParam);
}

HWND
create_responder_window ()
{
  size_t cls_name_len = strlen(RESPONDER_CLASS_NAME) + 1;
  char cls_name[cls_name_len];
  if (g_responder_window)
    {
      return g_responder_window;
    }
  /* Create Window wants a mutable string as the first parameter */
  snprintf (cls_name, cls_name_len, "%s", RESPONDER_CLASS_NAME);

  WNDCLASS windowClass;
  windowClass.style = CS_GLOBALCLASS | CS_DBLCLKS;
  windowClass.lpfnWndProc = gpgol_window_proc;
  windowClass.cbClsExtra = 0;
  windowClass.cbWndExtra = 0;
  windowClass.hInstance = (HINSTANCE) GetModuleHandle(NULL);
  windowClass.hIcon = 0;
  windowClass.hCursor = 0;
  windowClass.hbrBackground = 0;
  windowClass.lpszMenuName  = 0;
  windowClass.lpszClassName = cls_name;
  RegisterClass(&windowClass);
  g_responder_window = CreateWindow (cls_name, RESPONDER_CLASS_NAME, 0, 0, 0,
                                     0, 0, 0, (HMENU) 0,
                                     (HINSTANCE) GetModuleHandle(NULL), 0);
  return g_responder_window;
}

int
send_msg_to_ui_thread (wm_ctx_t *ctx)
{
  size_t cls_name_len = strlen(RESPONDER_CLASS_NAME) + 1;
  char cls_name[cls_name_len];
  snprintf (cls_name, cls_name_len, "%s", RESPONDER_CLASS_NAME);

  HWND responder = FindWindow (cls_name, RESPONDER_CLASS_NAME);
  if (!responder)
  {
    log_error ("%s:%s: Failed to find responder window.",
               SRCNAME, __func__);
    return -1;
  }
  SendMessage (responder, WM_USER + 1, 0, (LPARAM) ctx);
  return 0;
}

int
do_in_ui_thread (gpgol_wmsg_type type, void *data)
{
  wm_ctx_t ctx = {NULL, UNKNOWN, 0};
  ctx.wmsg_type = type;
  ctx.data = data;
  if (send_msg_to_ui_thread (&ctx))
    {
      return -1;
    }
  return ctx.err;
}
