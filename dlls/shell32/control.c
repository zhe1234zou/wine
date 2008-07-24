/* Control Panel management
 *
 * Copyright 2001 Eric Pouech
 * Copyright 2008 Owen Rudge
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winreg.h"
#include "wine/winbase16.h"
#include "wownt32.h"
#include "wine/debug.h"
#include "cpl.h"
#include "wine/unicode.h"
#include "commctrl.h"

#define NO_SHLWAPI_REG
#include "shlwapi.h"

#include "cpanel.h"
#include "shresdef.h"
#include "shell32_main.h"

#define MAX_STRING_LEN      1024

WINE_DEFAULT_DEBUG_CHANNEL(shlctrl);

CPlApplet*	Control_UnloadApplet(CPlApplet* applet)
{
    unsigned	i;
    CPlApplet*	next;

    for (i = 0; i < applet->count; i++) {
        if (!applet->info[i].dwSize) continue;
        applet->proc(applet->hWnd, CPL_STOP, i, applet->info[i].lData);
    }
    if (applet->proc) applet->proc(applet->hWnd, CPL_EXIT, 0L, 0L);
    FreeLibrary(applet->hModule);
    next = applet->next;
    HeapFree(GetProcessHeap(), 0, applet);
    return next;
}

CPlApplet*	Control_LoadApplet(HWND hWnd, LPCWSTR cmd, CPanel* panel)
{
    CPlApplet*	applet;
    unsigned 	i;
    CPLINFO	info;
    NEWCPLINFOW newinfo;

    if (!(applet = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*applet))))
       return applet;

    applet->hWnd = hWnd;

    if (!(applet->hModule = LoadLibraryW(cmd))) {
        WARN("Cannot load control panel applet %s\n", debugstr_w(cmd));
	goto theError;
    }
    if (!(applet->proc = (APPLET_PROC)GetProcAddress(applet->hModule, "CPlApplet"))) {
        WARN("Not a valid control panel applet %s\n", debugstr_w(cmd));
	goto theError;
    }
    if (!applet->proc(hWnd, CPL_INIT, 0L, 0L)) {
        WARN("Init of applet has failed\n");
	goto theError;
    }
    if ((applet->count = applet->proc(hWnd, CPL_GETCOUNT, 0L, 0L)) == 0) {
        WARN("No subprogram in applet\n");
	goto theError;
    }

    applet = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, applet,
			 sizeof(*applet) + (applet->count - 1) * sizeof(NEWCPLINFOW));

    for (i = 0; i < applet->count; i++) {
       ZeroMemory(&newinfo, sizeof(newinfo));
       newinfo.dwSize = sizeof(NEWCPLINFOA);
       applet->info[i].dwSize = sizeof(NEWCPLINFOW);
       applet->info[i].dwFlags = 0;
       applet->info[i].dwHelpContext = 0;
       applet->info[i].szHelpFile[0] = '\0';
       /* proc is supposed to return a null value upon success for
	* CPL_INQUIRE and CPL_NEWINQUIRE
	* However, real drivers don't seem to behave like this
	* So, use introspection rather than return value
	*/
       applet->proc(hWnd, CPL_INQUIRE, i, (LPARAM)&info);
       applet->info[i].lData = info.lData;
       if (info.idIcon != CPL_DYNAMIC_RES)
	   applet->info[i].hIcon = LoadIconW(applet->hModule,
					     MAKEINTRESOURCEW(info.idIcon));
       if (info.idName != CPL_DYNAMIC_RES)
	   LoadStringW(applet->hModule, info.idName,
		       applet->info[i].szName, sizeof(applet->info[i].szName) / sizeof(WCHAR));
       if (info.idInfo != CPL_DYNAMIC_RES)
	   LoadStringW(applet->hModule, info.idInfo,
		       applet->info[i].szInfo, sizeof(applet->info[i].szInfo) / sizeof(WCHAR));

       if ((info.idIcon == CPL_DYNAMIC_RES) || (info.idName == CPL_DYNAMIC_RES) ||
           (info.idInfo == CPL_DYNAMIC_RES)) {
	   applet->proc(hWnd, CPL_NEWINQUIRE, i, (LPARAM)&newinfo);

	   applet->info[i].dwFlags = newinfo.dwFlags;
	   applet->info[i].dwHelpContext = newinfo.dwHelpContext;
	   applet->info[i].lData = newinfo.lData;
	   if (info.idIcon == CPL_DYNAMIC_RES) {
	       if (!newinfo.hIcon) WARN("couldn't get icon for applet %u\n", i);
	       applet->info[i].hIcon = newinfo.hIcon;
	   }
	   if (newinfo.dwSize == sizeof(NEWCPLINFOW)) {
	       if (info.idName == CPL_DYNAMIC_RES)
	           memcpy(applet->info[i].szName, newinfo.szName, sizeof(newinfo.szName));
	       if (info.idInfo == CPL_DYNAMIC_RES)
	           memcpy(applet->info[i].szInfo, newinfo.szInfo, sizeof(newinfo.szInfo));
	       memcpy(applet->info[i].szHelpFile, newinfo.szHelpFile, sizeof(newinfo.szHelpFile));
	   } else {
	       if (info.idName == CPL_DYNAMIC_RES)
                   MultiByteToWideChar(CP_ACP, 0, ((LPNEWCPLINFOA)&newinfo)->szName,
	                               sizeof(((LPNEWCPLINFOA)&newinfo)->szName) / sizeof(CHAR),
			               applet->info[i].szName,
			               sizeof(applet->info[i].szName) / sizeof(WCHAR));
	       if (info.idInfo == CPL_DYNAMIC_RES)
                   MultiByteToWideChar(CP_ACP, 0, ((LPNEWCPLINFOA)&newinfo)->szInfo,
	                               sizeof(((LPNEWCPLINFOA)&newinfo)->szInfo) / sizeof(CHAR),
			               applet->info[i].szInfo,
			               sizeof(applet->info[i].szInfo) / sizeof(WCHAR));
               MultiByteToWideChar(CP_ACP, 0, ((LPNEWCPLINFOA)&newinfo)->szHelpFile,
	                           sizeof(((LPNEWCPLINFOA)&newinfo)->szHelpFile) / sizeof(CHAR),
			           applet->info[i].szHelpFile,
			           sizeof(applet->info[i].szHelpFile) / sizeof(WCHAR));
           }
       }
    }

    applet->next = panel->first;
    panel->first = applet;

    return applet;

 theError:
    Control_UnloadApplet(applet);
    return NULL;
}

#define IDC_LISTVIEW        1000

#define NUM_COLUMNS            2
#define LISTVIEW_DEFSTYLE   (WS_CHILD | WS_VISIBLE | WS_TABSTOP |\
                             LVS_SORTASCENDING | LVS_AUTOARRANGE | LVS_SINGLESEL)

static BOOL Control_CreateListView (CPanel *panel)
{
    RECT ws;
    WCHAR empty_string[] = {0};
    WCHAR buf[MAX_STRING_LEN];
    LVCOLUMNW lvc;

    /* Create list view */
    GetClientRect(panel->hWnd, &ws);

    panel->hWndListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW,
                          empty_string, LISTVIEW_DEFSTYLE | LVS_ICON,
                          0, 0, ws.right - ws.left, ws.bottom - ws.top,
                          panel->hWnd, (HMENU) IDC_LISTVIEW, panel->hInst, NULL);

    if (!panel->hWndListView)
        return FALSE;

    /* Create image lists for list view */
    panel->hImageListSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), ILC_MASK, 1, 1);
    panel->hImageListLarge = ImageList_Create(GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON), ILC_MASK, 1, 1);

    (void) ListView_SetImageList(panel->hWndListView, panel->hImageListSmall, LVSIL_SMALL);
    (void) ListView_SetImageList(panel->hWndListView, panel->hImageListLarge, LVSIL_NORMAL);

    /* Create columns for list view */
    lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM | LVCF_WIDTH;
    lvc.pszText = buf;
    lvc.fmt = LVCFMT_LEFT;

    /* Name column */
    lvc.iSubItem = 0;
    lvc.cx = (ws.right - ws.left) / 3;
    LoadStringW(shell32_hInstance, IDS_CPANEL_NAME, buf, sizeof(buf) / sizeof(buf[0]));

    if (ListView_InsertColumnW(panel->hWndListView, 0, &lvc) == -1)
        return FALSE;

    /* Description column */
    lvc.iSubItem = 1;
    lvc.cx = ((ws.right - ws.left) / 3) * 2;
    LoadStringW(shell32_hInstance, IDS_CPANEL_DESCRIPTION, buf, sizeof(buf) /
        sizeof(buf[0]));

    if (ListView_InsertColumnW(panel->hWndListView, 1, &lvc) == -1)
        return FALSE;

    return(TRUE);
}

static void 	 Control_WndProc_Create(HWND hWnd, const CREATESTRUCTW* cs)
{
   CPanel*	panel = (CPanel*)cs->lpCreateParams;
   HMENU hMenu, hSubMenu;
   CPlApplet*	applet;
   MENUITEMINFOW mii;
   int menucount, i, index;
   CPlItem *item;
   LVITEMW lvItem;
   INITCOMMONCONTROLSEX icex;

   SetWindowLongPtrW(hWnd, 0, (LONG_PTR)panel);
   panel->hWnd = hWnd;

   /* Initialise common control DLL */
   icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
   icex.dwICC = ICC_LISTVIEW_CLASSES;
   InitCommonControlsEx(&icex);

   /* create the list view */
   if (!Control_CreateListView(panel))
       return;

   hMenu = LoadMenuW(shell32_hInstance, MAKEINTRESOURCEW(MENU_CPANEL));

   /* insert menu items for applets */
   hSubMenu = GetSubMenu(hMenu, 0);
   menucount = 0;

   for (applet = panel->first; applet; applet = applet->next) {
      for (i = 0; i < applet->count; i++) {
         if (!applet->info[i].dwSize)
            continue;

         /* set up a CPlItem for this particular subprogram */
         item = HeapAlloc(GetProcessHeap(), 0, sizeof(CPlItem));

         if (!item)
            continue;

         item->applet = (CPlApplet *) applet;
         item->id = i;

         mii.cbSize = sizeof(MENUITEMINFOW);
         mii.fMask = MIIM_ID | MIIM_STRING | MIIM_DATA;
         mii.dwTypeData = applet->info[i].szName;
         mii.cch = sizeof(applet->info[i].szName) / sizeof(applet->info[i].szName[0]);
         mii.wID = IDM_CPANEL_APPLET_BASE + menucount;
         mii.dwItemData = (DWORD) item;

         if (InsertMenuItemW(hSubMenu, menucount, TRUE, &mii)) {
            /* add the list view item */
            index = ImageList_AddIcon(panel->hImageListLarge, applet->info[i].hIcon);
            ImageList_AddIcon(panel->hImageListSmall, applet->info[i].hIcon);

            lvItem.mask = LVIF_IMAGE | LVIF_TEXT | LVIF_PARAM;
            lvItem.iItem = menucount;
            lvItem.iSubItem = 0;
            lvItem.pszText = applet->info[i].szName;
            lvItem.iImage = index;
            lvItem.lParam = (LPARAM) item;

            ListView_InsertItemW(panel->hWndListView, &lvItem);

            /* add the description */
            ListView_SetItemTextW(panel->hWndListView, menucount, 1,
                applet->info[i].szInfo);

            /* update menu bar, increment count */
            DrawMenuBar(hWnd);
            menucount++;
         }
      }
   }

   panel->total_subprogs = menucount;

   /* check the "large items" icon in the View menu */
   hSubMenu = GetSubMenu(hMenu, 1);
   CheckMenuRadioItem(hSubMenu, FCIDM_SHVIEW_BIGICON, FCIDM_SHVIEW_REPORTVIEW,
      FCIDM_SHVIEW_BIGICON, MF_BYCOMMAND);

   SetMenu(hWnd, hMenu);
}

static void Control_FreeCPlItems(HWND hWnd, CPanel *panel)
{
    HMENU hMenu, hSubMenu;
    MENUITEMINFOW mii;
    int i;

    /* get the File menu */
    hMenu = GetMenu(hWnd);

    if (!hMenu)
        return;

    hSubMenu = GetSubMenu(hMenu, 0);

    if (!hSubMenu)
        return;

    /* loop and free the item data */
    for (i = IDM_CPANEL_APPLET_BASE; i <= IDM_CPANEL_APPLET_BASE + panel->total_subprogs; i++)
    {
        mii.cbSize = sizeof(MENUITEMINFOW);
        mii.fMask = MIIM_DATA;

        if (!GetMenuItemInfoW(hSubMenu, i, FALSE, &mii))
            continue;

        HeapFree(GetProcessHeap(), 0, (LPVOID) mii.dwItemData);
    }
}

static void Control_UpdateListViewStyle(CPanel *panel, UINT style, UINT id)
{
    HMENU hMenu, hSubMenu;

    SetWindowLongW(panel->hWndListView, GWL_STYLE, LISTVIEW_DEFSTYLE | style);

    /* update the menu */
    hMenu = GetMenu(panel->hWnd);
    hSubMenu = GetSubMenu(hMenu, 1);

    CheckMenuRadioItem(hSubMenu, FCIDM_SHVIEW_BIGICON, FCIDM_SHVIEW_REPORTVIEW,
        id, MF_BYCOMMAND);
}

static CPlItem* Control_GetCPlItem_From_MenuID(HWND hWnd, UINT id)
{
    HMENU hMenu, hSubMenu;
    MENUITEMINFOW mii;

    /* retrieve the CPlItem structure from the menu item data */
    hMenu = GetMenu(hWnd);

    if (!hMenu)
        return NULL;

    hSubMenu = GetSubMenu(hMenu, 0);

    if (!hSubMenu)
        return NULL;

    mii.cbSize = sizeof(MENUITEMINFOW);
    mii.fMask = MIIM_DATA;

    if (!GetMenuItemInfoW(hSubMenu, id, FALSE, &mii))
        return NULL;

    return (CPlItem *) mii.dwItemData;
}

static CPlItem* Control_GetCPlItem_From_ListView(CPanel *panel)
{
    LVITEMW lvItem;
    int selitem;

    selitem = SendMessageW(panel->hWndListView, LVM_GETNEXTITEM, -1, LVNI_FOCUSED
        | LVNI_SELECTED);

    if (selitem != -1)
    {
        lvItem.iItem = selitem;
        lvItem.mask = LVIF_PARAM;

        if (SendMessageW(panel->hWndListView, LVM_GETITEMW, 0, (LPARAM) &lvItem))
            return (CPlItem *) lvItem.lParam;
    }

    return NULL;
}

static LRESULT WINAPI	Control_WndProc(HWND hWnd, UINT wMsg,
					WPARAM lParam1, LPARAM lParam2)
{
   CPanel*	panel = (CPanel*)GetWindowLongPtrW(hWnd, 0);

   if (panel || wMsg == WM_CREATE) {
      switch (wMsg) {
      case WM_CREATE:
	 Control_WndProc_Create(hWnd, (CREATESTRUCTW*)lParam2);
	 return 0;
      case WM_DESTROY:
         {
	    CPlApplet*	applet = panel->first;
	    while (applet)
	       applet = Control_UnloadApplet(applet);
         }
         Control_FreeCPlItems(hWnd, panel);
         PostQuitMessage(0);
	 break;
      case WM_COMMAND:
         switch (LOWORD(lParam1))
         {
             case IDM_CPANEL_EXIT:
                 SendMessageW(hWnd, WM_CLOSE, 0, 0);
                 return 0;

             case IDM_CPANEL_ABOUT:
                 {
                     WCHAR appName[MAX_STRING_LEN];

                     LoadStringW(shell32_hInstance, IDS_CPANEL_TITLE, appName,
                         sizeof(appName) / sizeof(appName[0]));
                     ShellAboutW(hWnd, appName, NULL, NULL);

                     return 0;
                 }

             case FCIDM_SHVIEW_BIGICON:
                 Control_UpdateListViewStyle(panel, LVS_ICON, FCIDM_SHVIEW_BIGICON);
                 return 0;

             case FCIDM_SHVIEW_SMALLICON:
                 Control_UpdateListViewStyle(panel, LVS_SMALLICON, FCIDM_SHVIEW_SMALLICON);
                 return 0;

             case FCIDM_SHVIEW_LISTVIEW:
                 Control_UpdateListViewStyle(panel, LVS_LIST, FCIDM_SHVIEW_LISTVIEW);
                 return 0;

             case FCIDM_SHVIEW_REPORTVIEW:
                 Control_UpdateListViewStyle(panel, LVS_REPORT, FCIDM_SHVIEW_REPORTVIEW);
                 return 0;

             default:
                 /* check if this is an applet */
                 if ((LOWORD(lParam1) >= IDM_CPANEL_APPLET_BASE) &&
                     (LOWORD(lParam1) <= IDM_CPANEL_APPLET_BASE + panel->total_subprogs))
                 {
                     CPlItem *item = Control_GetCPlItem_From_MenuID(hWnd, LOWORD(lParam1));

                     /* execute the applet if item is valid */
                     if (item)
                         item->applet->proc(item->applet->hWnd, CPL_DBLCLK, item->id,
                             item->applet->info[item->id].lData);

                     return 0;
                 }

                 break;
         }

         break;

      case WM_NOTIFY:
      {
          LPNMHDR nmh = (LPNMHDR) lParam2;

          switch (nmh->idFrom)
          {
              case IDC_LISTVIEW:
                  switch (nmh->code)
                  {
                      case NM_RETURN:
                      case NM_DBLCLK:
                      {
                          CPlItem *item = Control_GetCPlItem_From_ListView(panel);

                          /* execute the applet if item is valid */
                          if (item)
                              item->applet->proc(item->applet->hWnd, CPL_DBLCLK,
                                  item->id, item->applet->info[item->id].lData);

                          return 0;
                      }
                  }

                  break;
          }

          break;
      }

      case WM_SIZE:
      {
          RECT rect;

          GetClientRect(hWnd, &rect);
          MoveWindow(panel->hWndListView, 0, 0, rect.right, rect.bottom, TRUE);

          return 0;
      }
     }
   }

   return DefWindowProcW(hWnd, wMsg, lParam1, lParam2);
}

static void    Control_DoInterface(CPanel* panel, HWND hWnd, HINSTANCE hInst)
{
    WNDCLASSW	wc;
    MSG		msg;
    WCHAR appName[MAX_STRING_LEN];
    const WCHAR className[] = {'S','h','e','l','l','_','C','o','n','t','r','o',
        'l','_','W','n','d','C','l','a','s','s',0};

    LoadStringW(shell32_hInstance, IDS_CPANEL_TITLE, appName, sizeof(appName) / sizeof(appName[0]));

    wc.style = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc = Control_WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = sizeof(CPlApplet*);
    wc.hInstance = panel->hInst = hInst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursorW( 0, (LPWSTR)IDC_ARROW );
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = className;

    if (!RegisterClassW(&wc)) return;

    CreateWindowExW(0, wc.lpszClassName, appName,
		    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		    CW_USEDEFAULT, CW_USEDEFAULT,
		    CW_USEDEFAULT, CW_USEDEFAULT,
		    hWnd, NULL, hInst, panel);
    if (!panel->hWnd) return;

    while (GetMessageW(&msg, panel->hWnd, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void Control_RegisterRegistryApplets(HWND hWnd, CPanel *panel, HKEY hkey_root, LPCWSTR szRepPath)
{
    WCHAR name[MAX_PATH];
    WCHAR value[MAX_PATH];
    HKEY hkey;

    if (RegOpenKeyW(hkey_root, szRepPath, &hkey) == ERROR_SUCCESS)
    {
        int idx = 0;

        for(;; ++idx)
        {
            DWORD nameLen = MAX_PATH;
            DWORD valueLen = MAX_PATH;

            if (RegEnumValueW(hkey, idx, name, &nameLen, NULL, NULL, (LPBYTE)value, &valueLen) != ERROR_SUCCESS)
                break;

            Control_LoadApplet(hWnd, value, panel);
        }
        RegCloseKey(hkey);
    }
}

static	void	Control_DoWindow(CPanel* panel, HWND hWnd, HINSTANCE hInst)
{
    HANDLE		h;
    WIN32_FIND_DATAW	fd;
    WCHAR		buffer[MAX_PATH];
    static const WCHAR wszAllCpl[] = {'*','.','c','p','l',0};
    static const WCHAR wszRegPath[] = {'S','O','F','T','W','A','R','E','\\','M','i','c','r','o','s','o','f','t',
            '\\','W','i','n','d','o','w','s','\\','C','u','r','r','e','n','t','V','e','r','s','i','o','n',
            '\\','C','o','n','t','r','o','l',' ','P','a','n','e','l','\\','C','p','l','s',0};
    WCHAR *p;

    /* first add .cpl files in the system directory */
    GetSystemDirectoryW( buffer, MAX_PATH );
    p = buffer + strlenW(buffer);
    *p++ = '\\';
    lstrcpyW(p, wszAllCpl);

    if ((h = FindFirstFileW(buffer, &fd)) != INVALID_HANDLE_VALUE) {
        do {
	   lstrcpyW(p, fd.cFileName);
	   Control_LoadApplet(hWnd, buffer, panel);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
    }

    /* now check for cpls in the registry */
    Control_RegisterRegistryApplets(hWnd, panel, HKEY_LOCAL_MACHINE, wszRegPath);
    Control_RegisterRegistryApplets(hWnd, panel, HKEY_CURRENT_USER, wszRegPath);

    Control_DoInterface(panel, hWnd, hInst);
}

static	void	Control_DoLaunch(CPanel* panel, HWND hWnd, LPCWSTR wszCmd)
   /* forms to parse:
    *	foo.cpl,@sp,str
    *	foo.cpl,@sp
    *	foo.cpl,,str
    *	foo.cpl @sp
    *	foo.cpl str
    *   "a path\foo.cpl"
    */
{
    LPWSTR	buffer;
    LPWSTR	beg = NULL;
    LPWSTR	end;
    WCHAR	ch;
    LPWSTR       ptr;
    signed 	sp = -1;
    LPWSTR	extraPmtsBuf = NULL;
    LPWSTR	extraPmts = NULL;
    int        quoted = 0;

    buffer = HeapAlloc(GetProcessHeap(), 0, (lstrlenW(wszCmd) + 1) * sizeof(*wszCmd));
    if (!buffer) return;

    end = lstrcpyW(buffer, wszCmd);

    for (;;) {
        ch = *end;
        if (ch == '"') quoted = !quoted;
        if (!quoted && (ch == ' ' || ch == ',' || ch == '\0')) {
            *end = '\0';
            if (beg) {
                if (*beg == '@') {
                    sp = atoiW(beg + 1);
                } else if (*beg == '\0') {
                    sp = -1;
                } else {
                    extraPmtsBuf = beg;
                }
            }
            if (ch == '\0') break;
            beg = end + 1;
            if (ch == ' ') while (end[1] == ' ') end++;
        }
        end++;
    }
    while ((ptr = StrChrW(buffer, '"')))
	memmove(ptr, ptr+1, lstrlenW(ptr)*sizeof(WCHAR));

    /* now check for any quotes in extraPmtsBuf and remove */
    if (extraPmtsBuf != NULL)
    {
        beg = end = extraPmtsBuf;
        quoted = 0;

        for (;;) {
            ch = *end;
            if (ch == '"') quoted = !quoted;
            if (!quoted && (ch == ' ' || ch == ',' || ch == '\0')) {
                *end = '\0';
                if (beg) {
                    if (*beg != '\0') {
                        extraPmts = beg;
                    }
                }
                if (ch == '\0') break;
		    beg = end + 1;
                if (ch == ' ') while (end[1] == ' ') end++;
            }
            end++;
        }

        while ((ptr = StrChrW(extraPmts, '"')))
            memmove(ptr, ptr+1, lstrlenW(ptr)*sizeof(WCHAR));

        if (extraPmts == NULL)
            extraPmts = extraPmtsBuf;
    }

    TRACE("cmd %s, extra %s, sp %d\n", debugstr_w(buffer), debugstr_w(extraPmts), sp);

    Control_LoadApplet(hWnd, buffer, panel);

    if (panel->first) {
        CPlApplet* applet = panel->first;

        assert(applet && applet->next == NULL);

        /* we've been given a textual parameter (or none at all) */
        if (sp == -1) {
            while ((++sp) != applet->count) {
                if (applet->info[sp].dwSize) {
                    TRACE("sp %d, name %s\n", sp, debugstr_w(applet->info[sp].szName));

                    if (StrCmpIW(extraPmts, applet->info[sp].szName) == 0)
                        break;
                }
            }
        }

        if (sp >= applet->count) {
            WARN("Out of bounds (%u >= %u), setting to 0\n", sp, applet->count);
            sp = 0;
        }

        if (applet->info[sp].dwSize) {
            if (!applet->proc(applet->hWnd, CPL_STARTWPARMSA, sp, (LPARAM)extraPmts))
                applet->proc(applet->hWnd, CPL_DBLCLK, sp, applet->info[sp].lData);
        }

        Control_UnloadApplet(applet);
    }

    HeapFree(GetProcessHeap(), 0, buffer);
}

/*************************************************************************
 * Control_RunDLLW			[SHELL32.@]
 *
 */
void WINAPI Control_RunDLLW(HWND hWnd, HINSTANCE hInst, LPCWSTR cmd, DWORD nCmdShow)
{
    CPanel	panel;

    TRACE("(%p, %p, %s, 0x%08x)\n",
	  hWnd, hInst, debugstr_w(cmd), nCmdShow);

    memset(&panel, 0, sizeof(panel));

    if (!cmd || !*cmd) {
        Control_DoWindow(&panel, hWnd, hInst);
    } else {
        Control_DoLaunch(&panel, hWnd, cmd);
    }
}

/*************************************************************************
 * Control_RunDLLA			[SHELL32.@]
 *
 */
void WINAPI Control_RunDLLA(HWND hWnd, HINSTANCE hInst, LPCSTR cmd, DWORD nCmdShow)
{
    DWORD len = MultiByteToWideChar(CP_ACP, 0, cmd, -1, NULL, 0 );
    LPWSTR wszCmd = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (wszCmd && MultiByteToWideChar(CP_ACP, 0, cmd, -1, wszCmd, len ))
    {
        Control_RunDLLW(hWnd, hInst, wszCmd, nCmdShow);
    }
    HeapFree(GetProcessHeap(), 0, wszCmd);
}

/*************************************************************************
 * Control_FillCache_RunDLLW			[SHELL32.@]
 *
 */
HRESULT WINAPI Control_FillCache_RunDLLW(HWND hWnd, HANDLE hModule, DWORD w, DWORD x)
{
    FIXME("%p %p 0x%08x 0x%08x stub\n", hWnd, hModule, w, x);
    return 0;
}

/*************************************************************************
 * Control_FillCache_RunDLLA			[SHELL32.@]
 *
 */
HRESULT WINAPI Control_FillCache_RunDLLA(HWND hWnd, HANDLE hModule, DWORD w, DWORD x)
{
    return Control_FillCache_RunDLLW(hWnd, hModule, w, x);
}


/*************************************************************************
 * RunDLL_CallEntry16				[SHELL32.122]
 * the name is probably wrong
 */
void WINAPI RunDLL_CallEntry16( DWORD proc, HWND hwnd, HINSTANCE inst,
                                LPCSTR cmdline, INT cmdshow )
{
    WORD args[5];
    SEGPTR cmdline_seg;

    TRACE( "proc %x hwnd %p inst %p cmdline %s cmdshow %d\n",
           proc, hwnd, inst, debugstr_a(cmdline), cmdshow );

    cmdline_seg = MapLS( cmdline );
    args[4] = HWND_16(hwnd);
    args[3] = MapHModuleLS(inst);
    args[2] = SELECTOROF(cmdline_seg);
    args[1] = OFFSETOF(cmdline_seg);
    args[0] = cmdshow;
    WOWCallback16Ex( proc, WCB16_PASCAL, sizeof(args), args, NULL );
    UnMapLS( cmdline_seg );
}

/*************************************************************************
 * CallCPLEntry16				[SHELL32.166]
 *
 * called by desk.cpl on "Advanced" with:
 * hMod("DeskCp16.Dll"), pFunc("CplApplet"), 0, 1, 0xc, 0
 *
 */
DWORD WINAPI CallCPLEntry16(HMODULE hMod, FARPROC pFunc, DWORD dw3, DWORD dw4, DWORD dw5, DWORD dw6)
{
    FIXME("(%p, %p, %08x, %08x, %08x, %08x): stub.\n", hMod, pFunc, dw3, dw4, dw5, dw6);
    return 0x0deadbee;
}
