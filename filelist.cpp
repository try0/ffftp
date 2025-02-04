﻿/*=============================================================================
*
*								ファイル一覧
*
===============================================================================
/ Copyright (C) 1997-2007 Sota. All rights reserved.
/
/ Redistribution and use in source and binary forms, with or without 
/ modification, are permitted provided that the following conditions 
/ are met:
/
/  1. Redistributions of source code must retain the above copyright 
/     notice, this list of conditions and the following disclaimer.
/  2. Redistributions in binary form must reproduce the above copyright 
/     notice, this list of conditions and the following disclaimer in the 
/     documentation and/or other materials provided with the distribution.
/
/ THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
/ IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
/ OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
/ IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, 
/ INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
/ BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF 
/ USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
/ ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
/ (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
/ THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/============================================================================*/

#include "common.h"
#include <sys/stat.h>
#include "OleDragDrop.h"

#define BUF_SIZE		256
#define CF_CNT 2
#define WM_DRAGDROP		(WM_APP + 100)
#define WM_GETDATA		(WM_APP + 101)
#define WM_DRAGOVER		(WM_APP + 102)


/*===== プロトタイプ =====*/

static LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK RemoteWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT FileListCommonWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static void DispFileList2View(HWND hWnd, std::vector<FILELIST>& files);
// ファイルアイコン表示対応
//static void AddListView(HWND hWnd, int Pos, char *Name, int Type, LONGLONG Size, FILETIME *Time, int Attr, char *Owner, int Link, int InfoExist);
static void AddListView(HWND hWnd, int Pos, char *Name, int Type, LONGLONG Size, FILETIME *Time, int Attr, char *Owner, int Link, int InfoExist, int ImageId);
static int GetImageIndex(int Win, int Pos);
static int MakeRemoteTree1(char *Path, char *Cur, std::vector<FILELIST>& Base, int *CancelCheckWork);
static int MakeRemoteTree2(char *Path, char *Cur, std::vector<FILELIST>& Base, int *CancelCheckWork);
static void CopyTmpListToFileList(std::vector<FILELIST>& Base, std::vector<FILELIST> const& List);
static std::optional<std::vector<std::variant<FILELIST, std::string>>> GetListLine(int Num);
static int MakeDirPath(char *Str, int ListType, char *Path, char *Dir);
static int MakeLocalTree(const char *Path, std::vector<FILELIST>& Base);
static void AddFileList(FILELIST const& Pkt, std::vector<FILELIST>& Base);
static int AnalyzeFileInfo(char *Str);
static int CheckUnixType(char *Str, char *Tmp, int Add1, int Add2, int Day);
static int CheckHHMMformat(char *Str);
static int CheckYYMMDDformat(char *Str, char Sym, int Dig3);
static int CheckYYYYMMDDformat(char *Str, char Sym);
// Windows Server 2008 R2
static int CheckMMDDYYYYformat(char *Str, char Sym);
static int ResolveFileInfo(char *Str, int ListType, char *Fname, LONGLONG *Size, FILETIME *Time, int *Attr, char *Owner, int *Link, int *InfoExist);
static int FindField(char *Str, char *Buf, int Num, int ToLast);
// MLSD対応
static int FindField2(char *Str, char *Buf, char Separator, int Num, int ToLast);
static void GetMonth(char *Str, WORD *Month, WORD *Day);
static int GetYearMonthDay(char *Str, WORD *Year, WORD *Month, WORD *Day);
static int GetHourAndMinute(char *Str, WORD *Hour, WORD *Minute);
static int GetVMSdate(char *Str, WORD *Year, WORD *Month, WORD *Day);
static int AskFilterStr(const char *Fname, int Type);
static int atoi_n(const char *Str, int Len);

/*===== 外部参照 =====*/

extern int SepaWidth;
extern int RemoteWidth;
extern int ListHeight;
extern char FilterStr[FILTER_EXT_LEN+1];
// 外部アプリケーションへドロップ後にローカル側のファイル一覧に作業フォルダが表示されるバグ対策
extern int SuppressRefresh;
// ローカル側自動更新
extern HANDLE ChangeNotification;
// 特定の操作を行うと異常終了するバグ修正
extern int CancelFlg;

/* 設定値 */
extern int LocalWidth;
extern int LocalTabWidth[4];
extern int RemoteTabWidth[6];
extern char UserMailAdrs[USER_MAIL_LEN+1];
extern HFONT ListFont;
extern int ListType;
extern int FindMode;
extern int DotFile;
extern int DispDrives;
extern int MoveMode;
// ファイルアイコン表示対応
extern int DispFileIcon;
// タイムスタンプのバグ修正
extern int DispTimeSeconds;
// ファイルの属性を数字で表示
extern int DispPermissionsNumber;
extern HOSTDATA CurHost;

/*===== ローカルなワーク =====*/

static HWND hWndListLocal = NULL;
static HWND hWndListRemote = NULL;

static WNDPROC LocalProcPtr;
static WNDPROC RemoteProcPtr;

static HIMAGELIST ListImg = NULL;
// ファイルアイコン表示対応
static HIMAGELIST ListImgFileIcon = NULL;

static char FindStr[40+1] = { "*" };		/* 検索文字列 */

static int Dragging = NO;
// 特定の操作を行うと異常終了するバグ修正
static POINT DropPoint;

static int StratusMode;			/* 0=ファイル, 1=ディレクトリ, 2=リンク */


// リモートファイルリスト (2007.9.3 yutaka)
static std::vector<FILELIST> remoteFileListBase;
static std::vector<FILELIST> remoteFileListBaseNoExpand;
static char remoteFileDir[FMAX_PATH + 1];


/*----- ファイルリストウインドウを作成する ------------------------------------
*
*	Parameter
*		HWND hWnd : 親ウインドウのウインドウハンドル
*		HINSTANCE hInst : インスタンスハンドル
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL
*----------------------------------------------------------------------------*/

int MakeListWin(HWND hWnd, HINSTANCE hInst)
{
	int Sts;
	LV_COLUMN LvCol;
	long Tmp;

	// 変数が未初期化のバグ修正
	memset(&LvCol, 0, sizeof(LV_COLUMN));

	/*===== ローカル側のリストビュー =====*/

	hWndListLocal = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr, WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS, 0, AskToolWinHeight() * 2, LocalWidth, ListHeight, GetMainHwnd(), 0, hInst, nullptr);

	if(hWndListLocal != NULL)
	{
		LocalProcPtr = (WNDPROC)SetWindowLongPtrW(hWndListLocal, GWLP_WNDPROC, (LONG_PTR)LocalWndProc);

		Tmp = (long)SendMessage(hWndListLocal, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
		Tmp |= LVS_EX_FULLROWSELECT;
		SendMessage(hWndListLocal, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)Tmp);

		if(ListFont != NULL)
			SendMessage(hWndListLocal, WM_SETFONT, (WPARAM)ListFont, MAKELPARAM(TRUE, 0));

		ListImg = ImageList_LoadImageW(hInst, MAKEINTRESOURCEW(dirattr_bmp), 16, 9, RGB(255,0,0), IMAGE_BITMAP, 0);
		SendMessage(hWndListLocal, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImg);
		ShowWindow(hWndListLocal, SW_SHOW);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		LvCol.cx = LocalTabWidth[0];
		LvCol.pszText = MSGJPN038;
		LvCol.iSubItem = 0;
		SendMessage(hWndListLocal, LVM_INSERTCOLUMN, 0, (LPARAM)&LvCol);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		LvCol.cx = LocalTabWidth[1];
		LvCol.pszText = MSGJPN039;
		LvCol.iSubItem = 1;
		SendMessage(hWndListLocal, LVM_INSERTCOLUMN, 1, (LPARAM)&LvCol);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
		LvCol.fmt = LVCFMT_RIGHT;
		LvCol.cx = LocalTabWidth[2];
		LvCol.pszText = MSGJPN040;
		LvCol.iSubItem = 2;
		SendMessage(hWndListLocal, LVM_INSERTCOLUMN, 2, (LPARAM)&LvCol);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		LvCol.cx = LocalTabWidth[3];
		LvCol.pszText = MSGJPN041;
		LvCol.iSubItem = 3;
		SendMessage(hWndListLocal, LVM_INSERTCOLUMN, 3, (LPARAM)&LvCol);
	}

	/*===== ホスト側のリストビュー =====*/

	hWndListRemote = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr, WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS, LocalWidth + SepaWidth, AskToolWinHeight() * 2, RemoteWidth, ListHeight, GetMainHwnd(), 0, hInst, nullptr);

	if(hWndListRemote != NULL)
	{
		RemoteProcPtr = (WNDPROC)SetWindowLongPtrW(hWndListRemote, GWLP_WNDPROC, (LONG_PTR)RemoteWndProc);

		Tmp = (long)SendMessage(hWndListRemote, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
		Tmp |= LVS_EX_FULLROWSELECT;
		SendMessage(hWndListRemote, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)Tmp);

		if(ListFont != NULL)
			SendMessage(hWndListRemote, WM_SETFONT, (WPARAM)ListFont, MAKELPARAM(TRUE, 0));

		SendMessage(hWndListRemote, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImg);
		ShowWindow(hWndListRemote, SW_SHOW);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		LvCol.cx = RemoteTabWidth[0];
		LvCol.pszText = MSGJPN042;
		LvCol.iSubItem = 0;
		SendMessage(hWndListRemote, LVM_INSERTCOLUMN, 0, (LPARAM)&LvCol);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		LvCol.cx = RemoteTabWidth[1];
		LvCol.pszText = MSGJPN043;
		LvCol.iSubItem = 1;
		SendMessage(hWndListRemote, LVM_INSERTCOLUMN, 1, (LPARAM)&LvCol);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
		LvCol.fmt = LVCFMT_RIGHT;
		LvCol.cx = RemoteTabWidth[2];
		LvCol.pszText = MSGJPN044;
		LvCol.iSubItem = 2;
		SendMessage(hWndListRemote, LVM_INSERTCOLUMN, 2, (LPARAM)&LvCol);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		LvCol.cx = RemoteTabWidth[3];
		LvCol.pszText = MSGJPN045;
		LvCol.iSubItem = 3;
		SendMessage(hWndListRemote, LVM_INSERTCOLUMN, 3, (LPARAM)&LvCol);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		LvCol.cx = RemoteTabWidth[4];
		LvCol.pszText = MSGJPN046;
		LvCol.iSubItem = 4;
		SendMessage(hWndListRemote, LVM_INSERTCOLUMN, 4, (LPARAM)&LvCol);

		LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		LvCol.cx = RemoteTabWidth[5];
		LvCol.pszText = MSGJPN047;
		LvCol.iSubItem = 5;
		SendMessage(hWndListRemote, LVM_INSERTCOLUMN, 5, (LPARAM)&LvCol);
	}

	Sts = FFFTP_SUCCESS;
	if((hWndListLocal == NULL) ||
	   (hWndListRemote == NULL))
	{
		Sts = FFFTP_FAIL;
	}
	return(Sts);
}


/*----- ファイルリストウインドウを削除 ----------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void DeleteListWin(void)
{
//	if(ListImg != NULL)
//		ImageList_Destroy(ListImg);
	if(hWndListLocal != NULL)
		DestroyWindow(hWndListLocal);
	if(hWndListRemote != NULL)
		DestroyWindow(hWndListRemote);
	return;
}


/*----- ローカル側のファイルリストのウインドウハンドルを返す ------------------
*
*	Parameter
*		なし
*
*	Return Value
*		HWND ウインドウハンドル
*----------------------------------------------------------------------------*/

HWND GetLocalHwnd(void)
{
	return(hWndListLocal);
}


/*----- ホスト側のファイルリストのウインドウハンドルを返す --------------------
*
*	Parameter
*		なし
*
*	Return Value
*		HWND ウインドウハンドル
*----------------------------------------------------------------------------*/

HWND GetRemoteHwnd(void)
{
	return(hWndListRemote);
}


/*----- ローカル側のファイルリストウインドウのメッセージ処理 ------------------
*
*	Parameter
*		HWND hWnd : ウインドウハンドル
*		UINT message  : メッセージ番号
*		WPARAM wParam : メッセージの WPARAM 引数
*		LPARAM lParam : メッセージの LPARAM 引数
*
*	Return Value
*		メッセージに対応する戻り値
*----------------------------------------------------------------------------*/

static LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return(FileListCommonWndProc(hWnd, message, wParam, lParam));
}


/*----- ホスト側のファイルリストウインドウのメッセージ処理 --------------------
*
*	Parameter
*		HWND hWnd : ウインドウハンドル
*		UINT message  : メッセージ番号
*		WPARAM wParam : メッセージの WPARAM 引数
*		LPARAM lParam : メッセージの LPARAM 引数
*
*	Return Value
*		メッセージに対応する戻り値
*----------------------------------------------------------------------------*/

static LRESULT CALLBACK RemoteWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return(FileListCommonWndProc(hWnd, message, wParam, lParam));
}


static void doTransferRemoteFile(void)
{
	char LocDir[FMAX_PATH+1];

	// すでにリモートから転送済みなら何もしない。(2007.9.3 yutaka)
	if (!empty(remoteFileListBase))
		return;

	// 特定の操作を行うと異常終了するバグ修正
	while(1)
	{
		MSG msg;
		if(PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		else if(AskTransferNow() == NO)
			break;
		Sleep(10);
	}

	std::vector<FILELIST> FileListBase;
	MakeSelectedFileList(WIN_REMOTE, YES, NO, FileListBase, &CancelFlg);
	std::vector<FILELIST> FileListBaseNoExpand;
	MakeSelectedFileList(WIN_REMOTE, NO, NO, FileListBaseNoExpand, &CancelFlg);

	// set temporary folder
	AskLocalCurDir(LocDir, FMAX_PATH);

	auto tmp = tempDirectory() / L"file";
	if (auto const created = !fs::create_directory(tmp); !created) {
		// 既存のファイルを削除する
		for (auto const& f : FileListBase)
			fs::remove(tmp / fs::u8path(f.File));
	}

	// 外部アプリケーションへドロップ後にローカル側のファイル一覧に作業フォルダが表示されるバグ対策
	SuppressRefresh = 1;

	// ダウンロード先をテンポラリに設定
	SetLocalDirHist(tmp.u8string().c_str());

	// FFFTPにダウンロード要求を出し、ダウンロードの完了を待つ。
	PostMessageW(GetMainHwnd(), WM_COMMAND, MAKEWPARAM(MENU_DOWNLOAD, 0), 0);

	// 特定の操作を行うと異常終了するバグ修正
	while(1)
	{
		MSG msg;

		if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);

		} else {
			// 転送スレッドが動き出したら抜ける。
			if (AskTransferNow() == YES)
				break;
		}

		Sleep(10);
	}

	// 特定の操作を行うと異常終了するバグ修正
	while(1)
	{
		MSG msg;
		if(PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		else if(AskTransferNow() == NO)
			break;
		Sleep(10);
	}

	// ダウンロード先を元に戻す
	SetLocalDirHist(LocDir);
	SetCurrentDirAsDirHist();

	// 外部アプリケーションへドロップ後にローカル側のファイル一覧に作業フォルダが表示されるバグ対策
	SuppressRefresh = 0;
	GetLocalDirForWnd();

	remoteFileListBase = std::move(FileListBase);
	remoteFileListBaseNoExpand = std::move(FileListBaseNoExpand);
	strncpy_s(remoteFileDir, sizeof(remoteFileDir), tmp.u8string().c_str(), _TRUNCATE);
}


int isDirectory(char *fn)
{
	struct _stat buf;

	if (_stat(fn, &buf) == 0) {
		if (buf.st_mode & _S_IFDIR) { // is directory
			return 1;
		}
	}
	return 0;
}

// テンポラリのファイルおよびフォルダを削除する。
void doDeleteRemoteFile(void)
{
	if (!empty(remoteFileListBase)) {
		MoveFileToTrashCan(remoteFileDir);
		remoteFileListBase.clear();
	}

	remoteFileListBaseNoExpand.clear();
}


// yutaka
// cf. http://www.nakka.com/lib/
/* ドロップファイルの作成 */
static HDROP CreateDropFileMem(char** FileName, int cnt) {
	std::vector<std::wstring> wFileNames(cnt);
	for (int i = 0; i < cnt; i++)
		wFileNames[i] = u8(FileName[i]);
	auto extra = std::reduce(begin(wFileNames), end(wFileNames), 0, [](auto l, auto const& r) { return l + size_as<int>(r) + 1; }) + 1;
	auto drop = (HDROP)GlobalAlloc(GHND, sizeof DROPFILES + extra * sizeof(wchar_t));
	if (drop) {
		auto dropfiles = reinterpret_cast<DROPFILES*>(GlobalLock(drop));
		*dropfiles = { sizeof DROPFILES, {}, false, true };
		/* 構造体の後ろにファイル名のリストをコピーする。(ファイル名\0ファイル名\0ファイル名\0\0) */
		auto ptr = reinterpret_cast<wchar_t*>(dropfiles + 1);
		for (auto const& wFileName : wFileNames) {
			wcscpy(ptr, wFileName.c_str());
			ptr += size(wFileName) + 1;
		}
		*ptr = L'\0';
		GlobalUnlock(drop);
	}
	return drop;
}


// OLE D&Dを開始する 
// (2007.8.30 yutaka)
static void doDragDrop(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	POINT pt;

	// テンポラリをきれいにする (2007.9.3 yutaka)
	doDeleteRemoteFile();

	/* ドラッグ&ドロップの開始 */
	CLIPFORMAT clipFormat = CF_HDROP;
	OleDragDrop::DoDragDrop(hWnd, WM_GETDATA, WM_DRAGOVER, &clipFormat, 1, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK);

	// ドロップ先のアプリに WM_LBUTTONUP を飛ばす。
	// 特定の操作を行うと異常終了するバグ修正
//	GetCursorPos(&pt);
	pt = DropPoint;
	ScreenToClient(hWnd, &pt);
	PostMessageW(hWnd,WM_LBUTTONUP,0,MAKELPARAM(pt.x,pt.y));
	// ドロップ先が他プロセスかつカーソルが自プロセスのドロップ可能なウィンドウ上にある場合の対策
	EnableWindow(GetMainHwnd(), TRUE);
}



/*----- ファイル一覧ウインドウの共通メッセージ処理 ----------------------------
*
*	Parameter
*		HWND hWnd : ウインドウハンドル
*		UINT message  : メッセージ番号
*		WPARAM wParam : メッセージの WPARAM 引数
*		LPARAM lParam : メッセージの LPARAM 引数
*
*	Return Value
*		メッセージに対応する戻り値
*----------------------------------------------------------------------------*/

static LRESULT FileListCommonWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	POINT Point;
	HWND hWndPnt;
	HWND hWndParent;
	static HCURSOR hCsrDrg;
	static HCURSOR hCsrNoDrg;
	static POINT DragPoint;
	static HWND hWndDragStart;
	static int RemoteDropFileIndex = -1;
	int Win;
	HWND hWndDst;
	WNDPROC ProcPtr;
	HWND hWndHistEdit;
	// 特定の操作を行うと異常終了するバグ修正
	static int DragFirstTime = NO;

	Win = WIN_LOCAL;
	hWndDst = hWndListRemote;
	ProcPtr = LocalProcPtr;
	hWndHistEdit = GetLocalHistEditHwnd();
	if(hWnd == hWndListRemote)
	{
		Win = WIN_REMOTE;
		hWndDst = hWndListLocal;
		ProcPtr = RemoteProcPtr;
		hWndHistEdit = GetRemoteHistEditHwnd();
	}

	switch (message)
	{
		case WM_SYSKEYDOWN:
			if (wParam == 'D') {	// Alt+D
				SetFocus(hWndHistEdit);
				break;
			}
			EraseListViewTips();
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_KEYDOWN:
			if(wParam == 0x09)
			{
				SetFocus(hWndDst);
				break;
			}
			EraseListViewTips();
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_SETFOCUS :
			SetFocusHwnd(hWnd);
			MakeButtonsFocus();
			DispCurrentWindow(Win);
			DispSelectedSpace();
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_KILLFOCUS :
			EraseListViewTips();
			MakeButtonsFocus();
			DispCurrentWindow(-1);
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_DROPFILES :
			if (AskUserOpeDisabled() != YES)
				if (Dragging != YES) {		// ドラッグ中は処理しない。ドラッグ後にWM_LBUTTONDOWNが飛んでくるため、そこで処理する。
					if (hWnd == hWndListRemote) {
						if (AskConnecting() == YES)
							UploadDragProc(wParam);
					} else if (hWnd == hWndListLocal)
						ChangeDirDropFileProc(wParam);
				}
			DragFinish((HDROP)wParam);
			return 0;

		case WM_LBUTTONDOWN :
			// 特定の操作を行うと異常終了するバグ修正
			if(AskUserOpeDisabled() == YES)
				break;
			if(Dragging == YES)
				break;
			DragFirstTime = NO;
			GetCursorPos(&DropPoint);
			EraseListViewTips();
			SetFocus(hWnd);
			DragPoint.x = LOWORD(lParam);
			DragPoint.y = HIWORD(lParam);
			hWndDragStart = hWnd;
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
			break;

		case WM_LBUTTONUP :
			// 特定の操作を行うと異常終了するバグ修正
			if(AskUserOpeDisabled() == YES)
				break;
			if(Dragging == YES)
			{
				Dragging = NO;
				ReleaseCapture();
				hCsrDrg = LoadCursor(NULL, IDC_ARROW);
				SetCursor(hCsrDrg);

				Point.x = (long)(short)LOWORD(lParam);
				Point.y = (long)(short)HIWORD(lParam);
				ClientToScreen(hWnd, &Point);
				hWndPnt = WindowFromPoint(Point);
				if(hWndPnt == hWndDst)  // local <-> remote 
				{
					if(hWndPnt == hWndListRemote) {
						PostMessageW(GetMainHwnd(), WM_COMMAND, MAKEWPARAM(MENU_UPLOAD, 0), 0);
					} else if(hWndPnt == hWndListLocal) {
						PostMessageW(GetMainHwnd(), WM_COMMAND, MAKEWPARAM(MENU_DOWNLOAD, 0), 0);
					}
				} else { // 同一ウィンドウ内の場合 (yutaka)
					if (hWndDragStart == hWndListRemote && hWndPnt == hWndListRemote) {
						// remote <-> remoteの場合は、サーバでのファイルの移動を行う。(2007.9.5 yutaka)
						if (RemoteDropFileIndex != -1) {
							ListView_SetItemState(hWnd, RemoteDropFileIndex, 0, LVIS_DROPHILITED);
							MoveRemoteFileProc(RemoteDropFileIndex);
						}

					}

				}
			}
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_DRAGDROP:  
			// OLE D&Dを開始する (yutaka)
			// 特定の操作を行うと異常終了するバグ修正
//			doDragDrop(hWnd, message, wParam, lParam);
			if(DragFirstTime == NO)
				doDragDrop(hWnd, message, wParam, lParam);
			DragFirstTime = YES;
			return (TRUE);
			break;

		case WM_GETDATA:  // ファイルのパスをD&D先のアプリへ返す (yutaka)
			switch(wParam)
			{
			case CF_HDROP:		/* ファイル */
				{
					char **FileNameList;
					int filelen;
					std::vector<FILELIST> FileListBase, FileListBaseNoExpand;
					char LocDir[FMAX_PATH+1];
					char *PathDir = nullptr;

					// 特定の操作を行うと異常終了するバグ修正
					GetCursorPos(&DropPoint);
					hWndPnt = WindowFromPoint(DropPoint);
					hWndParent = GetParent(hWndPnt);
					DisableUserOpe();
					CancelFlg = NO;

					// ローカル側で選ばれているファイルをFileListBaseに登録
					if (hWndDragStart == hWndListLocal) {
						AskLocalCurDir(LocDir, FMAX_PATH);
						PathDir = LocDir;

						if(hWndPnt != hWndListRemote && hWndPnt != hWndListLocal && hWndParent != hWndListRemote && hWndParent != hWndListLocal)
							MakeSelectedFileList(WIN_LOCAL, NO, NO, FileListBase, &CancelFlg);			
						FileListBaseNoExpand = FileListBase;

					} else if (hWndDragStart == hWndListRemote) {
						if (hWndPnt == hWndListRemote || hWndPnt == hWndListLocal || hWndParent == hWndListRemote || hWndParent == hWndListLocal) {
						} else {
							// 選択されているリモートファイルのリストアップ
							// このタイミングでリモートからローカルの一時フォルダへダウンロードする
							// (2007.8.31 yutaka)
							doTransferRemoteFile();
							PathDir = remoteFileDir;
							FileListBase = remoteFileListBase;
							FileListBaseNoExpand = remoteFileListBaseNoExpand;
						}

					} 

					auto const& pf =
#if defined(HAVE_TANDEM)
						empty(FileListBaseNoExpand) ? FileListBase :
#endif
						FileListBaseNoExpand;
					// 特定の操作を行うと異常終了するバグ修正
					if (!empty(pf)) {
						Dragging = NO;
						ReleaseCapture();
						hCsrDrg = LoadCursor(NULL, IDC_ARROW);
						SetCursor(hCsrDrg);
						// ドロップ先が他プロセスかつカーソルが自プロセスのドロップ可能なウィンドウ上にある場合の対策
						EnableWindow(GetMainHwnd(), FALSE);
					}
					EnableUserOpe();
					// ドロップ先が他プロセスかつカーソルが自プロセスのドロップ可能なウィンドウ上にある場合の対策
					if (!empty(pf))
						EnableWindow(GetMainHwnd(), FALSE);
					int filenum = size_as<int>(pf);
					// ファイルが未選択の場合は何もしない。(yutaka)
					if (filenum <= 0) {
						*((HANDLE *)lParam) = NULL;
						return (FALSE);
					}
					
					/* ファイル名の配列を作成する */
					FileNameList = (char **)GlobalAlloc(GPTR,sizeof(char *) * filenum);
					if(FileNameList == NULL){
						abort();
					}
					int j = 0;
					for (auto const& f : FileListBaseNoExpand) {
						filelen = (int)strlen(PathDir) + 1 + (int)strlen(f.File) + 1;
						FileNameList[j] = (char *)GlobalAlloc(GPTR, filelen);
						strncpy_s(FileNameList[j], filelen, PathDir, _TRUNCATE);
						strncat_s(FileNameList[j], filelen, "\\", _TRUNCATE);
						strncat_s(FileNameList[j], filelen, f.File, _TRUNCATE);
						j++;
					}
					
					/* ドロップファイルリストの作成 */
					/* NTの場合はUNICODEになるようにする */
					*((HANDLE *)lParam) = CreateDropFileMem(FileNameList, filenum);

					/* ファイル名の配列を解放する */
					for (int i = 0; i < filenum ; i++)
						GlobalFree(FileNameList[i]);
					GlobalFree(FileNameList);
					return (TRUE);
				}
				break;

			default:
				*((HANDLE *)lParam) = NULL;
				break;
			}

			break;

		case WM_DRAGOVER:
			{
				LVHITTESTINFO hi;
				int Node, index;
				static int prev_index = -1;

				// 同一ウィンドウ内でのD&Dはリモート側のみ
				if (Win != WIN_REMOTE)
					break;

				if(MoveMode == MOVE_DISABLE)
					break;

				memset(&hi, 0, sizeof(hi));

				GetCursorPos(&Point);
				hWndPnt = WindowFromPoint(Point);
				ScreenToClient(hWnd, &Point);

				hi.pt = Point;

				// 以前の選択を消す
				ListView_SetItemState(hWnd, prev_index, 0, LVIS_DROPHILITED);
				RemoteDropFileIndex = -1;

				if ((hWndPnt == hWndListRemote) && (ListView_HitTest(hWnd, &hi) != -1)) {
					if (hi.flags == LVHT_ONITEMLABEL) { // The position is over a list-view item's text.
					
						index = hi.iItem;
						prev_index = index;
						Node = GetNodeType(Win, index);
						if (Node == NODE_DIR) {
							ListView_SetItemState(hWnd, index, LVIS_DROPHILITED, LVIS_DROPHILITED);
							RemoteDropFileIndex = index;
						}
					}
				} 

			}
			break;

		case WM_RBUTTONDOWN :
			// 特定の操作を行うと異常終了するバグ修正
			if(AskUserOpeDisabled() == YES)
				break;
			/* ここでファイルを選ぶ */
			CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

			EraseListViewTips();
			SetFocus(hWnd);
			if(hWnd == hWndListRemote)
				ShowPopupMenu(WIN_REMOTE, 0);
			else if(hWnd == hWndListLocal)
				ShowPopupMenu(WIN_LOCAL, 0);
			break;

		case WM_LBUTTONDBLCLK :
			// 特定の操作を行うと異常終了するバグ修正
			if(AskUserOpeDisabled() == YES)
				break;
			DoubleClickProc(Win, NO, -1);
			break;

		case WM_MOUSEMOVE :
			// 特定の操作を行うと異常終了するバグ修正
			if(AskUserOpeDisabled() == YES)
				break;
			if(wParam == MK_LBUTTON)
			{
				if((Dragging == NO) && 
				   (hWnd == hWndDragStart) &&
				   (AskConnecting() == YES) &&
				   (SendMessage(hWnd, LVM_GETSELECTEDCOUNT, 0, 0) > 0) &&
				   ((abs((short)LOWORD(lParam) - DragPoint.x) > 5) ||
					(abs((short)HIWORD(lParam) - DragPoint.y) > 5)))
				{
					SetCapture(hWnd);
					Dragging = YES;
					hCsrDrg = LoadCursor(GetFtpInst(), MAKEINTRESOURCE(drag_csr));
					hCsrNoDrg = LoadCursor(GetFtpInst(), MAKEINTRESOURCE(nodrop_csr));
					SetCursor(hCsrDrg);
				}
				else if(Dragging == YES)
				{
					Point.x = (long)(short)LOWORD(lParam);
					Point.y = (long)(short)HIWORD(lParam);
					ClientToScreen(hWnd, &Point);
					hWndPnt = WindowFromPoint(Point);
					if((hWndPnt == hWndListRemote) || (hWndPnt == hWndListLocal))
						SetCursor(hCsrDrg);
					else {
						// マウスポインタの×表示をやめる (yutaka)
#if 0
						SetCursor(hCsrNoDrg);
#endif
					}

					// OLE D&Dの開始を指示する
					PostMessageW(hWnd, WM_DRAGDROP, MAKEWPARAM(wParam, lParam), 0);

				}
				else
					return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
			}
			else
			{
				CheckTipsDisplay(hWnd, lParam);
				return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
			}
			break;

		case WM_MOUSEWHEEL :
			// デッドロック対策
			if(AskUserOpeDisabled() == YES)
				break;
			if(Dragging == NO)
			{
				short zDelta = (short)HIWORD(wParam);

				EraseListViewTips();
				Point.x = (short)LOWORD(lParam);
				Point.y = (short)HIWORD(lParam);
				hWndPnt = WindowFromPoint(Point);

				if((wParam & MAKEWPARAM(MK_SHIFT, 0)) && 
				   ((hWndPnt == hWndListRemote) ||
					(hWndPnt == hWndListLocal) || 
					(hWndPnt == GetTaskWnd())))
				{
					PostMessageW(hWndPnt, WM_VSCROLL, zDelta > 0 ? MAKEWPARAM(SB_PAGEUP, 0) : MAKEWPARAM(SB_PAGEDOWN, 0), 0);
				}
				else if(hWndPnt == hWnd)
					return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
				else if((hWndPnt == hWndDst) || (hWndPnt == GetTaskWnd()))
					PostMessageW(hWndPnt, message, wParam, lParam);
			}
			break;

		default :
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
	}
	return(0L);
}


/*----- ファイルリストのタブ幅を取得する --------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void GetListTabWidth(void)
{
	LV_COLUMN LvCol;
	int i;

	// 変数が未初期化のバグ修正
	memset(&LvCol, 0, sizeof(LV_COLUMN));

	for(i = 0; i <= 3; i++)
	{
		LvCol.mask = LVCF_WIDTH;
		if(SendMessage(hWndListLocal, LVM_GETCOLUMN, i, (LPARAM)&LvCol) == TRUE)
			LocalTabWidth[i] = LvCol.cx;
	}

	for(i = 0; i <= 5; i++)
	{
		LvCol.mask = LVCF_WIDTH;
		if(SendMessage(hWndListRemote, LVM_GETCOLUMN, i, (LPARAM)&LvCol) == TRUE)
			RemoteTabWidth[i] = LvCol.cx;
	}
	return;
}


/*----- ファイル一覧方法にしたがってリストビューを設定する --------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void SetListViewType(void)
{
	// 64ビット対応
//	long lStyle;
	LONG_PTR lStyle;

	switch(ListType)
	{
		case LVS_LIST :
			lStyle = GetWindowLongPtrW(GetLocalHwnd(), GWL_STYLE);
			SetWindowLongPtrW(GetLocalHwnd(), GWL_STYLE, lStyle & ~LVS_REPORT | LVS_LIST);

			lStyle = GetWindowLongPtrW(GetRemoteHwnd(), GWL_STYLE);
			SetWindowLongPtrW(GetRemoteHwnd(), GWL_STYLE, lStyle & ~LVS_REPORT | LVS_LIST);
			break;

		default :
			lStyle = GetWindowLongPtrW(GetLocalHwnd(), GWL_STYLE);
			SetWindowLongPtrW(GetLocalHwnd(), GWL_STYLE, lStyle & ~LVS_LIST | LVS_REPORT);

			lStyle = GetWindowLongPtrW(GetRemoteHwnd(), GWL_STYLE);
			SetWindowLongPtrW(GetRemoteHwnd(), GWL_STYLE, lStyle & ~LVS_LIST | LVS_REPORT);
			break;
	}
	return;
}


// ホスト側のファイル一覧ウインドウにファイル名をセット
void GetRemoteDirForWnd(int Mode, int *CancelCheckWork) {
	if (AskConnecting() == YES) {
		DisableUserOpe();

		char Buf[FMAX_PATH+1];
		AskRemoteCurDir(Buf, FMAX_PATH);
		SetRemoteDirHist(Buf);

		if (Mode == CACHE_LASTREAD || DoDirListCmdSkt("", "", 0, CancelCheckWork) == FTP_COMPLETE) {
			if (auto lines = GetListLine(0)) {
				std::vector<FILELIST> files;
				for (auto& line : *lines)
					std::visit([&files](auto&& arg) {
						if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, FILELIST>)
							if (arg.Node != NODE_NONE && AskFilterStr(arg.File, arg.Node) == YES && (DotFile == YES || arg.File[0] != '.'))
								files.emplace_back(arg);
					}, line);
				DispFileList2View(GetRemoteHwnd(), files);

				// 先頭のアイテムを選択
				ListView_SetItemState(GetRemoteHwnd(), 0, LVIS_FOCUSED, LVIS_FOCUSED);
			} else {
				SetTaskMsg(MSGJPN048);
				SendMessage(GetRemoteHwnd(), LVM_DELETEALLITEMS, 0, 0);
			}
		} else {
#if defined(HAVE_OPENVMS)
			/* OpenVMSの場合空ディレクトリ移動の時に出るので、メッセージだけ出さない
			 * ようにする(VIEWはクリアして良い) */
			if (AskHostType() != HTYPE_VMS)
#endif
				SetTaskMsg(MSGJPN049);
			SendMessage(GetRemoteHwnd(), LVM_DELETEALLITEMS, 0, 0);
		}
		EnableUserOpe();
	}
}


// ローカル側のファイル一覧ウインドウにファイル名をセット
void RefreshIconImageList(std::vector<FILELIST>& files)
{
	HBITMAP hBitmap;
	
	if(DispFileIcon == YES)
	{
		SendMessage(hWndListLocal, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)NULL);
		ShowWindow(hWndListLocal, SW_SHOW);
		SendMessage(hWndListRemote, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)NULL);
		ShowWindow(hWndListRemote, SW_SHOW);
		ImageList_Destroy(ListImgFileIcon);
		ListImgFileIcon = ImageList_Create(16, 16, ILC_MASK | ILC_COLOR32, 0, 1);
		hBitmap = LoadBitmapW(GetFtpInst(), MAKEINTRESOURCEW(dirattr16_bmp));
		ImageList_AddMasked(ListImgFileIcon, hBitmap, RGB(255, 0, 0));
		DeleteObject(hBitmap);
		int ImageId = 0;
		for (auto& file : files) {
			file.ImageId = -1;
			auto fullpath = fs::u8path(file.File);
			if (file.Node != NODE_DRIVE)
				fullpath = fs::current_path() / fullpath;
			if (SHFILEINFOW fi; SHGetFileInfoW(fullpath.c_str(), 0, &fi, sizeof(SHFILEINFOW), SHGFI_SMALLICON | SHGFI_ICON)) {
				if (ImageList_AddIcon(ListImgFileIcon, fi.hIcon) >= 0)
					file.ImageId = ImageId++;
				DestroyIcon(fi.hIcon);
			}
		}
		SendMessage(hWndListLocal, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImgFileIcon);
		ShowWindow(hWndListLocal, SW_SHOW);
		SendMessage(hWndListRemote, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImgFileIcon);
		ShowWindow(hWndListRemote, SW_SHOW);
	}
	else
	{
		SendMessage(hWndListLocal, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImg);
		ShowWindow(hWndListLocal, SW_SHOW);
		SendMessage(hWndListRemote, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImg);
		ShowWindow(hWndListRemote, SW_SHOW);
	}
}

void GetLocalDirForWnd(void)
{
	char Scan[FMAX_PATH+1];
	std::vector<FILELIST> files;

	DoLocalPWD(Scan);
	SetLocalDirHist(Scan);
	DispLocalFreeSpace(Scan);

	// ローカル側自動更新
	if(ChangeNotification != INVALID_HANDLE_VALUE)
		FindCloseChangeNotification(ChangeNotification);
	ChangeNotification = FindFirstChangeNotificationW(fs::u8path(Scan).c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);

	/* ディレクトリ／ファイル */
	FindFile(fs::u8path(Scan) / L"*", [&files](WIN32_FIND_DATAW const& data) {
		if (DotFile != YES && data.cFileName[0] == L'.')
			return true;
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			files.emplace_back(u8(data.cFileName).c_str(), NODE_DIR, NO, MakeLongLong(data.nFileSizeHigh, data.nFileSizeLow), 0, data.ftLastWriteTime, FINFO_ALL);
		else if (AskFilterStr(u8(data.cFileName).c_str(), NODE_FILE) == YES)
			files.emplace_back(u8(data.cFileName).c_str(), NODE_FILE, NO, MakeLongLong(data.nFileSizeHigh, data.nFileSizeLow), 0, data.ftLastWriteTime, FINFO_ALL);
		return true;
	});

	/* ドライブ */
	if (DispDrives)
		GetDrives([&files](const wchar_t drive[]) { files.emplace_back(u8(drive).c_str(), NODE_DRIVE, NO, 0, 0, FILETIME{}, FINFO_ALL); });

	// ファイルアイコン表示対応
	RefreshIconImageList(files);
	DispFileList2View(GetLocalHwnd(), files);

	// 先頭のアイテムを選択
	ListView_SetItemState(GetLocalHwnd(), 0, LVIS_FOCUSED, LVIS_FOCUSED);

	return;
}


// ファイル一覧用リストの内容をファイル一覧ウインドウにセット
static void DispFileList2View(HWND hWnd, std::vector<FILELIST>& files) {
	std::sort(begin(files), end(files), [hWnd](FILELIST& l, FILELIST& r) {
		if (l.Node != r.Node)
			return l.Node < r.Node;
		auto Sort = AskSortType(hWnd == GetRemoteHwnd() ? l.Node == NODE_DIR ? ITEM_RDIR : ITEM_RFILE : l.Node == NODE_DIR ? ITEM_LDIR : ITEM_LFILE);
		auto test = [ascent = (Sort & SORT_GET_ORD) == SORT_ASCENT](auto r) { return ascent ? r < 0 : r > 0; };
		LONGLONG Cmp = 0;
		if ((Sort & SORT_MASK_ORD) == SORT_EXT && test(Cmp = _mbsicmp((const unsigned char*)GetFileExt(l.File), (const unsigned char*)GetFileExt(r.File))))
			return true;
#if defined(HAVE_TANDEM)
		if (AskHostType() == HTYPE_TANDEM && (Sort & SORT_MASK_ORD) == SORT_EXT && test(Cmp = l.Attr - r.Attr))
			return true;
#endif
		if ((Sort & SORT_MASK_ORD) == SORT_SIZE && test(Cmp = l.Size - r.Size))
			return true;
		if ((Sort & SORT_MASK_ORD) == SORT_DATE && test(Cmp = CompareFileTime(&l.Time, &r.Time)))
			return true;
		if ((Sort & SORT_MASK_ORD) == SORT_NAME || Cmp == 0)
			if (test(_mbsicmp((const unsigned char*)l.File, (const unsigned char*)r.File)))
				return true;
		return false;
	});

	SendMessage(hWnd, WM_SETREDRAW, (WPARAM)FALSE, 0);
	SendMessage(hWnd, LVM_DELETEALLITEMS, 0, 0);

	for (auto& file : files)
		AddListView(hWnd, -1, file.File, file.Node, file.Size, &file.Time, file.Attr, file.Owner, file.Link, file.InfoExist, file.ImageId);

	SendMessage(hWnd, WM_SETREDRAW, (WPARAM)TRUE, 0);
	UpdateWindow(hWnd);

	DispSelectedSpace();
}


/*----- ファイル一覧ウインドウ（リストビュー）に追加 --------------------------
*
*	Parameter
*		HWND hWnd : ウインドウハンドル
*		int Pos : 挿入位置
*		char *Name : 名前
*		int Type : タイプ (NIDE_xxxx)
*		LONGLONG Size : サイズ
*		FILETIME *Time : 日付
*		int Attr : 属性
*		char Owner : オーナ名
*		int Link : リンクかどうか
*		int InfoExist : 情報があるかどうか (FINFO_xxx)
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

// ファイルアイコン表示対応
//static void AddListView(HWND hWnd, int Pos, char *Name, int Type, LONGLONG Size, FILETIME *Time, int Attr, char *Owner, int Link, int InfoExist)
static void AddListView(HWND hWnd, int Pos, char *Name, int Type, LONGLONG Size, FILETIME *Time, int Attr, char *Owner, int Link, int InfoExist, int ImageId)
{
	LV_ITEM LvItem;
	char Tmp[20];

	if(Pos == -1)
		Pos = (int)SendMessage(hWnd, LVM_GETITEMCOUNT, 0, 0);

	// 変数が未初期化のバグ修正
	memset(&LvItem, 0, sizeof(LV_ITEM));
	/* アイコン/ファイル名 */
	LvItem.mask = LVIF_TEXT | LVIF_IMAGE;
	LvItem.iItem = Pos;
	LvItem.iSubItem = 0;
	LvItem.pszText = Name;
	if((Type == NODE_FILE) && (AskTransferTypeAssoc(Name, TYPE_X) == TYPE_I))
		Type = 3;
	if(Link == NO)
		LvItem.iImage = Type;
	else
		LvItem.iImage = 4;
	// ファイルアイコン表示対応
	if(DispFileIcon == YES && hWnd == GetLocalHwnd())
		LvItem.iImage = ImageId + 5;
	LvItem.iItem = (int)SendMessage(hWnd, LVM_INSERTITEM, 0, (LPARAM)&LvItem);

	/* 日付/時刻 */
	// タイムスタンプのバグ修正
//	FileTime2TimeString(Time, Tmp, DISPFORM_LEGACY, InfoExist);
	FileTime2TimeString(Time, Tmp, DISPFORM_LEGACY, InfoExist, DispTimeSeconds);
	LvItem.mask = LVIF_TEXT;
	LvItem.iItem = Pos;
	LvItem.iSubItem = 1;
	LvItem.pszText = Tmp;
	LvItem.iItem = (int)SendMessage(hWnd, LVM_SETITEM, 0, (LPARAM)&LvItem);

	/* サイズ */
	if(Type == NODE_DIR)
		strcpy(Tmp, "<DIR>");
	else if(Type == NODE_DRIVE)
		strcpy(Tmp, "<DRIVE>");
	else if(Size >= 0)
		MakeNumString(Size, Tmp, TRUE);
	else
		strcpy(Tmp, "");
	LvItem.mask = LVIF_TEXT;
	LvItem.iItem = Pos;
	LvItem.iSubItem = 2;
	LvItem.pszText = Tmp;
	LvItem.iItem = (int)SendMessage(hWnd, LVM_SETITEM, 0, (LPARAM)&LvItem);

	/* 拡張子 */
	LvItem.mask = LVIF_TEXT;
	LvItem.iItem = Pos;
	LvItem.iSubItem = 3;
#if defined(HAVE_TANDEM)
	if (AskHostType() == HTYPE_TANDEM) {
		_itoa_s(Attr, Tmp, sizeof(Tmp), 10);
		LvItem.pszText = Tmp;
	} else
#endif
	LvItem.pszText = GetFileExt(Name);
	LvItem.iItem = (int)SendMessage(hWnd, LVM_SETITEM, 0, (LPARAM)&LvItem);

	if(hWnd == GetRemoteHwnd())
	{
		/* 属性 */
		strcpy(Tmp, "");
#if defined(HAVE_TANDEM)
		if((InfoExist & FINFO_ATTR) && (AskHostType() != HTYPE_TANDEM))
#else
		if(InfoExist & FINFO_ATTR)
#endif
			// ファイルの属性を数字で表示
//			AttrValue2String(Attr, Tmp);
			AttrValue2String(Attr, Tmp, DispPermissionsNumber);
		LvItem.mask = LVIF_TEXT;
		LvItem.iItem = Pos;
		LvItem.iSubItem = 4;
		LvItem.pszText = Tmp;
		LvItem.iItem = (int)SendMessage(hWnd, LVM_SETITEM, 0, (LPARAM)&LvItem);

		/* オーナ名 */
		LvItem.mask = LVIF_TEXT;
		LvItem.iItem = Pos;
		LvItem.iSubItem = 5;
		LvItem.pszText = Owner;
		LvItem.iItem = (int)SendMessage(hWnd, LVM_SETITEM, 0, (LPARAM)&LvItem);
	}
	return;
}


/*----- ファイル名一覧ウインドウをソートし直す --------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void ReSortDispList(int Win, int *CancelCheckWork)
{
	if(Win == WIN_REMOTE)
		GetRemoteDirForWnd(CACHE_LASTREAD, CancelCheckWork);
	else
		GetLocalDirForWnd();
	return;
}


// ワイルドカードにマッチするかどうかを返す
bool CheckFname(std::wstring str, std::wstring const& regexp) {
	// VAX VMSの時は ; 以降は無視する
	if (AskHostType() == HTYPE_VMS)
		if (auto pos = str.find(L';'); pos != std::wstring::npos)
			str.resize(pos);
	return PathMatchSpecW(str.c_str(), regexp.c_str());
}


// ファイル一覧ウインドウのファイルを選択する
void SelectFileInList(HWND hWnd, int Type, std::vector<FILELIST> const& Base) {
	static bool IgnoreNew = false;
	static bool IgnoreOld = false;
	static bool IgnoreExist = false;
	struct Select {
		using result_t = bool;
		INT_PTR OnInit(HWND hDlg) {
			SendDlgItemMessageW(hDlg, SEL_FNAME, EM_LIMITTEXT, 40, 0);
			SendDlgItemMessageW(hDlg, SEL_FNAME, WM_SETTEXT, 0, (LPARAM)u8(FindStr).c_str());
			SendDlgItemMessageW(hDlg, SEL_REGEXP, BM_SETCHECK, FindMode, 0);
			SendDlgItemMessageW(hDlg, SEL_NOOLD, BM_SETCHECK, IgnoreOld ? BST_CHECKED : BST_UNCHECKED, 0);
			SendDlgItemMessageW(hDlg, SEL_NONEW, BM_SETCHECK, IgnoreNew ? BST_CHECKED : BST_UNCHECKED, 0);
			SendDlgItemMessageW(hDlg, SEL_NOEXIST, BM_SETCHECK, IgnoreExist ? BST_CHECKED : BST_UNCHECKED, 0);
			return TRUE;
		}
		void OnCommand(HWND hDlg, WORD id) {
			switch (id) {
			case IDOK:
				strcpy(FindStr, u8(GetText(hDlg, SEL_FNAME)).c_str());
				FindMode = (int)SendDlgItemMessageW(hDlg, SEL_REGEXP, BM_GETCHECK, 0, 0);
				IgnoreOld = SendDlgItemMessageW(hDlg, SEL_NOOLD, BM_GETCHECK, 0, 0) == BST_CHECKED;
				IgnoreNew = SendDlgItemMessageW(hDlg, SEL_NONEW, BM_GETCHECK, 0, 0) == BST_CHECKED;
				IgnoreExist = SendDlgItemMessageW(hDlg, SEL_NOEXIST, BM_GETCHECK, 0, 0) == BST_CHECKED;
				EndDialog(hDlg, true);
				break;
			case IDCANCEL:
				EndDialog(hDlg, false);
				break;
			case IDHELP:
				ShowHelp(IDH_HELP_TOPIC_0000061);
				break;
			}
		}
	};
	int Win = WIN_LOCAL, WinDst = WIN_REMOTE;
	if (hWnd == GetRemoteHwnd())
		std::swap(Win, WinDst);
	if (Type == SELECT_ALL) {
		LVITEMW item{ 0, 0, 0, GetSelectedCount(Win) <= 1 ? LVIS_SELECTED : 0u, LVIS_SELECTED };
		for (int i = 0, Num = GetItemCount(Win); i < Num; i++)
			if (GetNodeType(Win, i) != NODE_DRIVE)
				SendMessageW(hWnd, LVM_SETITEMSTATE, i, (LPARAM)&item);
		return;
	}
	if (Type == SELECT_REGEXP) {
		if (!Dialog(GetFtpInst(), Win == WIN_LOCAL ? sel_local_dlg : sel_remote_dlg, hWnd, Select{}))
			return;
		try {
			std::variant<std::wstring, std::wregex> pattern;
			if (FindMode == 0)
				pattern = u8(FindStr);
			else
				pattern = std::wregex{ u8(FindStr), std::regex_constants::icase };
			int CsrPos = -1;
			for (int i = 0, Num = GetItemCount(Win); i < Num; i++) {
				char Name[FMAX_PATH + 1];
				GetNodeName(Win, i, Name, FMAX_PATH);
				int Find = FindNameNode(WinDst, Name);
				UINT state = 0;
				if (GetNodeType(Win, i) != NODE_DRIVE) {
					auto matched = std::visit([wName = u8(Name)](auto&& pattern) {
						using t = std::decay_t<decltype(pattern)>;
						if constexpr (std::is_same_v<t, std::wstring>)
							return CheckFname(wName, pattern);
						else if constexpr (std::is_same_v<t, std::wregex>)
							return std::regex_match(wName, pattern);
						else
							static_assert(false_v<t>, "not supported variant type.");
					}, pattern);
					if (matched) {
						state = LVIS_SELECTED;
						if (Find >= 0) {
							if (IgnoreExist)
								state = 0;
							else {
								FILETIME Time1, Time2;
								GetNodeTime(Win, i, &Time1);
								GetNodeTime(WinDst, Find, &Time2);
								if (IgnoreNew && CompareFileTime(&Time1, &Time2) > 0 || IgnoreOld && CompareFileTime(&Time1, &Time2) < 0)
									state = 0;
							}
						}
					}
					if (state != 0 && CsrPos == -1)
						CsrPos = i;
				}
				LVITEMW item{ 0, 0, 0, state, LVIS_SELECTED };
				SendMessageW(hWnd, LVM_SETITEMSTATE, i, (LPARAM)&item);
			}
			if (CsrPos != -1) {
				LVITEMW item{ 0, 0, 0, LVIS_FOCUSED, LVIS_FOCUSED };
				SendMessageW(hWnd, LVM_SETITEMSTATE, CsrPos, (LPARAM)&item);
				SendMessageW(hWnd, LVM_ENSUREVISIBLE, CsrPos, (LPARAM)TRUE);
			}
		}
		catch (std::regex_error&) {}
		return;
	}
	if (Type == SELECT_LIST) {
		for (int i = 0, Num = GetItemCount(Win); i < Num; i++) {
			char Name[FMAX_PATH + 1];
			GetNodeName(Win, i, Name, FMAX_PATH);
			LVITEMW item{ 0, 0, 0, SearchFileList(Name, Base, COMP_STRICT) != NULL ? LVIS_SELECTED : 0u, LVIS_SELECTED };
			SendMessageW(hWnd, LVM_SETITEMSTATE, i, (LPARAM)&item);
		}
		return;
	}
}


// ファイル一覧ウインドウのファイルを検索する
void FindFileInList(HWND hWnd, int Type) {
	static std::variant<std::wstring, std::wregex> pattern;
	int Win = hWnd == GetRemoteHwnd() ? WIN_REMOTE : WIN_LOCAL;
	switch (Type) {
	case FIND_FIRST:
		if (!InputDialog(find_dlg, hWnd, Win == WIN_LOCAL ? MSGJPN050 : MSGJPN051, FindStr, 40 + 1, &FindMode))
			return;
		try {
			if (FindMode == 0)
				pattern = u8(FindStr);
			else
				pattern = std::wregex{ u8(FindStr), std::regex_constants::icase };
		}
		catch (std::regex_error&) {
			return;
		}
		[[fallthrough]];
	case FIND_NEXT:
		for (int i = GetCurrentItem(Win) + 1, Num = GetItemCount(Win); i < Num; i++) {
			char Name[FMAX_PATH + 1];
			GetNodeName(Win, i, Name, FMAX_PATH);
			auto match = std::visit([wName = u8(Name)](auto&& pattern) {
				using t = std::decay_t<decltype(pattern)>;
				if constexpr (std::is_same_v<t, std::wstring>)
					return CheckFname(wName, pattern);
				else if constexpr (std::is_same_v<t, std::wregex>)
					return std::regex_match(wName, pattern);
				else
					static_assert(false_v<t>, "not supported variant type.");
			}, pattern);
			if (match) {
				LVITEMW item{ 0, 0, 0, LVIS_FOCUSED, LVIS_FOCUSED };
				SendMessageW(hWnd, LVM_SETITEMSTATE, i, (LPARAM)&item);
				SendMessageW(hWnd, LVM_ENSUREVISIBLE, i, (LPARAM)TRUE);
				break;
			}
		}
		break;
	}
}


/*----- カーソル位置のアイテム番号を返す --------------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*
*	Return Value
*		int アイテム番号
*----------------------------------------------------------------------------*/

int GetCurrentItem(int Win)
{
	HWND hWnd;
	int Ret;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	if((Ret = (int)SendMessage(hWnd, LVM_GETNEXTITEM, -1, MAKELPARAM(LVNI_ALL | LVNI_FOCUSED, 0))) == -1)
		Ret = 0;

	return(Ret);
}


/*----- アイテム数を返す ------------------------------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*
*	Return Value
*		int アイテム数
*----------------------------------------------------------------------------*/

int GetItemCount(int Win)
{
	HWND hWnd;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	return (int)(SendMessage(hWnd, LVM_GETITEMCOUNT, 0, 0));
}


/*----- 選択されているアイテム数を返す ----------------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*
*	Return Value
*		int 選択されているアイテム数
*----------------------------------------------------------------------------*/

int GetSelectedCount(int Win)
{
	HWND hWnd;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	return (int)(SendMessage(hWnd, LVM_GETSELECTEDCOUNT, 0, 0));
}


/*----- 選択されている最初のアイテム番号を返す --------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*		int All : 選ばれていないものを含める
*
*	Return Value
*		int アイテム番号
*			-1 = 選択されていない
*----------------------------------------------------------------------------*/

int GetFirstSelected(int Win, int All)
{
	HWND hWnd;
	int Ope;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	Ope = LVNI_SELECTED;
	if(All == YES)
		Ope = LVNI_ALL;

	return (int)(SendMessage(hWnd, LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)MAKELPARAM(Ope, 0)));
}


/*----- 選択されている次のアイテム番号を返す ----------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*		int Pos : 今のアイテム番号
*		int All : 選ばれていないものも含める
*
*	Return Value
*		int アイテム番号
*			-1 = 選択されていない
*----------------------------------------------------------------------------*/

int GetNextSelected(int Win, int Pos, int All)
{
	HWND hWnd;
	int Ope;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	Ope = LVNI_SELECTED;
	if(All == YES)
		Ope = LVNI_ALL;

	return (int)(SendMessage(hWnd, LVM_GETNEXTITEM, (WPARAM)Pos, (LPARAM)MAKELPARAM(Ope, 0)));
}


// ローカル側自動更新
int GetHotSelected(int Win, char *Fname)
{
	HWND hWnd;
	int Pos;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	Pos = (int)SendMessage(hWnd, LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)MAKELPARAM(LVNI_FOCUSED, 0));
	if(Pos != -1)
		GetNodeName(Win, Pos, Fname, FMAX_PATH);

	return Pos;
}

int SetHotSelected(int Win, char *Fname)
{
	HWND hWnd;
	int i;
	int Num;
	char Name[FMAX_PATH+1];
	LV_ITEM LvItem;
	int Pos;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	Num = GetItemCount(Win);
	memset(&LvItem, 0, sizeof(LV_ITEM));
	Pos = -1;
	for(i = 0; i < Num; i++)
	{
		LvItem.state = 0;
		GetNodeName(Win, i, Name, FMAX_PATH);
		if(_mbscmp((const unsigned char*)Fname, (const unsigned char*)Name) == 0)
		{
			Pos = i;
			LvItem.state = LVIS_FOCUSED;
		}
		LvItem.mask = LVIF_STATE;
		LvItem.iItem = i;
		LvItem.stateMask = LVIS_FOCUSED;
		LvItem.iSubItem = 0;
		SendMessage(hWnd, LVM_SETITEMSTATE, i, (LPARAM)&LvItem);
	}

	return Pos;
}

/*----- 指定された名前のアイテムを探す ----------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		char *Name : 名前
*
*	Return Value
*		int アイテム番号
*			-1=見つからなかった
*----------------------------------------------------------------------------*/

int FindNameNode(int Win, char *Name)
{
	LV_FINDINFO FindInfo;
	HWND hWnd;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	// 変数が未初期化のバグ修正
	memset(&FindInfo, 0, sizeof(LV_FINDINFO));
	FindInfo.flags = LVFI_STRING;
	FindInfo.psz = Name;
	return (int)(SendMessage(hWnd, LVM_FINDITEM, -1, (LPARAM)&FindInfo));
}


/*----- 指定位置のアイテムの名前を返す ----------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*		char *Buf : 名前を返すバッファ
*		int Max : バッファのサイズ
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void GetNodeName(int Win, int Pos, char *Buf, int Max)
{
	HWND hWnd;
	LV_ITEM LvItem;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	// 変数が未初期化のバグ修正
	memset(&LvItem, 0, sizeof(LV_ITEM));
	LvItem.mask = LVIF_TEXT;
	LvItem.iItem = Pos;
	LvItem.iSubItem = 0;
	LvItem.pszText = Buf;
	LvItem.cchTextMax = Max;
	SendMessage(hWnd, LVM_GETITEM, 0, (LPARAM)&LvItem);
	return;
}


/*----- 指定位置のアイテムの日付を返す ----------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*		FILETIME *Buf : 日付を返すバッファ
*
*	Return Value
*		int ステータス
*			YES/NO=日付情報がなかった
*----------------------------------------------------------------------------*/

int GetNodeTime(int Win, int Pos, FILETIME *Buf)
{
	HWND hWnd;
	LV_ITEM LvItem;
	char Tmp[20];
	int Ret;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	// 変数が未初期化のバグ修正
	memset(&LvItem, 0, sizeof(LV_ITEM));
	LvItem.mask = LVIF_TEXT;
	LvItem.iItem = Pos;
	LvItem.iSubItem = 1;
	LvItem.pszText = Tmp;
	LvItem.cchTextMax = 20;
	SendMessage(hWnd, LVM_GETITEM, 0, (LPARAM)&LvItem);
	Ret = TimeString2FileTime(Tmp, Buf);
	return(Ret);
}


/*----- 指定位置のアイテムのサイズを返す --------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*		int *Buf : サイズを返すワーク
*
*	Return Value
*		int ステータス
*			YES/NO=サイズ情報がなかった
*----------------------------------------------------------------------------*/

int GetNodeSize(int Win, int Pos, LONGLONG *Buf)
{
	HWND hWnd;
	LV_ITEM LvItem;
	char Tmp[40];
	int Ret;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	// 変数が未初期化のバグ修正
	memset(&LvItem, 0, sizeof(LV_ITEM));
	LvItem.mask = LVIF_TEXT;
	LvItem.iItem = Pos;
	LvItem.iSubItem = 2;
	LvItem.pszText = Tmp;
	LvItem.cchTextMax = 20;
	SendMessage(hWnd, LVM_GETITEM, 0, (LPARAM)&LvItem);
	*Buf = -1;
	Ret = NO;
#if defined(HAVE_TANDEM)
	if(AskHostType() == HTYPE_TANDEM) {
		RemoveComma(Tmp);
		*Buf = _atoi64(Tmp);
		Ret = YES;
	} else
#endif
	if(strlen(Tmp) > 0)
	{
		RemoveComma(Tmp);
		*Buf = _atoi64(Tmp);
		Ret = YES;
	}
	return(Ret);
}


/*----- 指定位置のアイテムの属性を返す ----------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*		int *Buf : 属性を返すワーク
*
*	Return Value
*		int ステータス
*			YES/NO=サイズ情報がなかった
*----------------------------------------------------------------------------*/

int GetNodeAttr(int Win, int Pos, int *Buf)
{
	LV_ITEM LvItem;
	char Tmp[20];
	int Ret;

	*Buf = 0;
	Ret = NO;
	if(Win == WIN_REMOTE)
	{
		// 変数が未初期化のバグ修正
		memset(&LvItem, 0, sizeof(LV_ITEM));
		LvItem.mask = LVIF_TEXT;
		LvItem.iItem = Pos;
#if defined(HAVE_TANDEM)
		if(AskHostType() == HTYPE_TANDEM)
			LvItem.iSubItem = 3;
		else
#endif
		LvItem.iSubItem = 4;
		LvItem.pszText = Tmp;
		LvItem.cchTextMax = 20;
		SendMessage(GetRemoteHwnd(), LVM_GETITEM, 0, (LPARAM)&LvItem);
		if(strlen(Tmp) > 0)
		{
#if defined(HAVE_TANDEM)
			if(AskHostType() == HTYPE_TANDEM)
				*Buf = atoi(Tmp);
			else
#endif
			*Buf = AttrString2Value(Tmp);
			Ret = YES;
		}
	}
	return(Ret);
}


/*----- 指定位置のアイテムのタイプを返す --------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*
*	Return Value
*		int タイプ (NODE_xxx)
*----------------------------------------------------------------------------*/

int GetNodeType(int Win, int Pos)
{
	char Tmp[20];
	LV_ITEM LvItem;
	int Ret;
	HWND hWnd;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	// 変数が未初期化のバグ修正
	memset(&LvItem, 0, sizeof(LV_ITEM));
	LvItem.mask = LVIF_TEXT;
	LvItem.iItem = Pos;
	LvItem.iSubItem = 2;
	LvItem.pszText = Tmp;
	LvItem.cchTextMax = 20;
	SendMessage(hWnd, LVM_GETITEM, 0, (LPARAM)&LvItem);

	if(strcmp(Tmp, "<DIR>") == 0)
		Ret = NODE_DIR;
	else if(strcmp(Tmp, "<DRIVE>") == 0)
		Ret = NODE_DRIVE;
	else
		Ret = NODE_FILE;

	return(Ret);
}


/*----- 指定位置のアイテムのイメージ番号を返す ----------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*
*	Return Value
*		int イメージ番号
*			4 Symlink
*----------------------------------------------------------------------------*/
static int GetImageIndex(int Win, int Pos)
{
	HWND hWnd;
	LV_ITEM LvItem;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	// 変数が未初期化のバグ修正
	memset(&LvItem, 0, sizeof(LV_ITEM));
	LvItem.mask = LVIF_IMAGE;
	LvItem.iItem = Pos;
	LvItem.iSubItem = 0;
	SendMessage(hWnd, LVM_GETITEM, 0, (LPARAM)&LvItem);
	return LvItem.iImage;
}


/*----- 指定位置のアイテムのオーナ名を返す ------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*		char *Buf : オーナ名を返すバッファ
*		int Max : バッファのサイズ
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void GetNodeOwner(int Win, int Pos, char *Buf, int Max)
{
	LV_ITEM LvItem;

	strcpy(Buf, "");
	if(Win == WIN_REMOTE)
	{
		// 変数が未初期化のバグ修正
		memset(&LvItem, 0, sizeof(LV_ITEM));
		LvItem.mask = LVIF_TEXT;
		LvItem.iItem = Pos;
		LvItem.iSubItem = 5;
		LvItem.pszText = Buf;
		LvItem.cchTextMax = Max;
		SendMessage(GetRemoteHwnd(), LVM_GETITEM, 0, (LPARAM)&LvItem);
	}
	return;
}


/*----- ホスト側のファイル一覧ウインドウをクリア ------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void EraseRemoteDirForWnd(void)
{
	SendMessage(GetRemoteHwnd(), LVM_DELETEALLITEMS, 0, 0);
	SendMessage(GetRemoteHistHwnd(), CB_RESETCONTENT, 0, 0);
	return;
}


/*----- 選択されているファイルの総サイズを返す --------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*
*	Return Value
*		double サイズ
*----------------------------------------------------------------------------*/

double GetSelectedTotalSize(int Win)
{
	double Ret;
	LONGLONG Size;
	int Pos;

	Ret = 0;
	if(GetSelectedCount(Win) > 0)
	{
		Pos = GetFirstSelected(Win, NO);
		while(Pos != -1)
		{
			GetNodeSize(Win, Pos, &Size);
			if(Size >= 0)
				Ret += Size;
			Pos = GetNextSelected(Win, Pos, NO);
		}
	}
	return(Ret);
}



/*===================================================================

===================================================================*/



/*----- ファイル一覧で選ばれているファイルをリストに登録する ------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Expand : サブディレクトリを展開する (YES/NO)
*		int All : 選ばれていないものもすべて登録する (YES/NO)
*		FILELIST **Base : ファイルリストの先頭
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

int MakeSelectedFileList(int Win, int Expand, int All, std::vector<FILELIST>& Base, int *CancelCheckWork) {
	int Sts;
	int Pos;
	char Name[FMAX_PATH+1];
	char Cur[FMAX_PATH+1];
	FILELIST Pkt;
	int Node;
	int Ignore;

	// ファイル一覧バグ修正
	Sts = FFFTP_SUCCESS;
	if((All == YES) || (GetSelectedCount(Win) > 0))
	{
		/*===== カレントディレクトリのファイル =====*/

		Pos = GetFirstSelected(Win, All);
		while(Pos != -1)
		{
			Node = GetNodeType(Win, Pos);
			if((Node == NODE_FILE) ||
			   ((Expand == NO) && (Node == NODE_DIR)))
			{
				// 変数が未初期化のバグ修正
				memset(&Pkt, 0, sizeof(FILELIST));

				Pkt.InfoExist = 0;
				GetNodeName(Win, Pos, Pkt.File, FMAX_PATH);
				if(GetNodeSize(Win, Pos, &Pkt.Size) == YES)
					Pkt.InfoExist |= FINFO_SIZE;
				if(GetNodeAttr(Win, Pos, &Pkt.Attr) == YES)
					Pkt.InfoExist |= FINFO_ATTR;
				if(GetNodeTime(Win, Pos, &Pkt.Time) == YES)
					Pkt.InfoExist |= (FINFO_TIME | FINFO_DATE);
				Pkt.Node = Node;

				Ignore = NO;
				if((DispIgnoreHide == YES) && (Win == WIN_LOCAL))
				{
					AskLocalCurDir(Cur, FMAX_PATH);
					if (auto attr = GetFileAttributesW((fs::u8path(Cur) / fs::u8path(Pkt.File)).c_str()); attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN))
						Ignore = YES;
				}

				if(Ignore == NO)
					AddFileList(Pkt, Base);
			}
			Pos = GetNextSelected(Win, Pos, All);
		}

		if(Expand == YES)
		{
			/*===== ディレクトリツリー =====*/

			Pos = GetFirstSelected(Win, All);
			while(Pos != -1)
			{
				if(GetNodeType(Win, Pos) == NODE_DIR)
				{
					// 変数が未初期化のバグ修正
					memset(&Pkt, 0, sizeof(FILELIST));

					GetNodeName(Win, Pos, Name, FMAX_PATH);
					strcpy(Pkt.File, Name);
					ReplaceAll(Pkt.File, '\\', '/');
//8/26

					Ignore = NO;
					if((DispIgnoreHide == YES) && (Win == WIN_LOCAL))
					{
						AskLocalCurDir(Cur, FMAX_PATH);
						if (auto attr = GetFileAttributesW((fs::u8path(Cur) / fs::u8path(Pkt.File)).c_str()); attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN))
							Ignore = YES;
					}

					if(Ignore == NO)
					{
//						Pkt.Node = NODE_DIR;
						if(GetImageIndex(Win, Pos) == 4) // symlink
							Pkt.Node = NODE_FILE;
						else
							Pkt.Node = NODE_DIR;
						Pkt.Attr = 0;
						Pkt.Size = 0;
						memset(&Pkt.Time, 0, sizeof(FILETIME));
						AddFileList(Pkt, Base);

						if(GetImageIndex(Win, Pos) != 4) { // symlink
							if(Win == WIN_LOCAL)
							// ファイル一覧バグ修正
//								MakeLocalTree(Name, Base);
							{
								if(MakeLocalTree(Name, Base) == FFFTP_FAIL)
									Sts = FFFTP_FAIL;
							}
							else
							{
								AskRemoteCurDir(Cur, FMAX_PATH);

								if((AskListCmdMode() == NO) &&
								   (AskUseNLST_R() == YES))
								// ファイル一覧バグ修正
//									MakeRemoteTree1(Name, Cur, Base, CancelCheckWork);
								{
									if(MakeRemoteTree1(Name, Cur, Base, CancelCheckWork) == FFFTP_FAIL)
										Sts = FFFTP_FAIL;
								}
								else
								// ファイル一覧バグ修正
//									MakeRemoteTree2(Name, Cur, Base, CancelCheckWork);
								{
									if(MakeRemoteTree2(Name, Cur, Base, CancelCheckWork) == FFFTP_FAIL)
										Sts = FFFTP_FAIL;
								}
							}
						}
					}
				}
				Pos = GetNextSelected(Win, Pos, All);
			}
		}
	}
	// ファイル一覧バグ修正
//	return;
	return(Sts);
}


static inline fs::path DragFile(HDROP hdrop, UINT index) {
	auto const length1 = DragQueryFileW(hdrop, index, nullptr, 0);
	std::wstring buffer(length1, L'\0');
	auto const length2 = DragQueryFileW(hdrop, index, data(buffer), length1 + 1);
	assert(length1 == length2);
	return std::move(buffer);
}

// Drag&Dropされたファイルをリストに登録する
void MakeDroppedFileList(WPARAM wParam, char* Cur, std::vector<FILELIST>& Base) {
	int count = DragQueryFileW((HDROP)wParam, 0xFFFFFFFF, NULL, 0);

	auto const baseDirectory = DragFile((HDROP)wParam, 0).parent_path();
	strncpy(Cur, baseDirectory.u8string().c_str(), FMAX_PATH);

	std::vector<fs::path> directories;
	for (int i = 0; i < count; i++) {
		auto const path = DragFile((HDROP)wParam, i);
		WIN32_FILE_ATTRIBUTE_DATA attr;
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr))
			continue;
		if (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			directories.emplace_back(path);
		else {
			FILELIST Pkt{};
			Pkt.Node = NODE_FILE;
			strcpy(Pkt.File, path.filename().u8string().c_str());
			if (SYSTEMTIME TmpStime; FileTimeToSystemTime(&attr.ftLastWriteTime, &TmpStime)) {
				if (DispTimeSeconds == NO)
					TmpStime.wSecond = 0;
				TmpStime.wMilliseconds = 0;
				SystemTimeToFileTime(&TmpStime, &Pkt.Time);
			}
#if defined(HAVE_TANDEM)
			Pkt.Size = LONGLONG(attr.nFileSizeHigh) << 32 | attr.nFileSizeLow;
			Pkt.InfoExist = FINFO_TIME | FINFO_DATE | FINFO_SIZE;
#endif
			AddFileList(Pkt, Base);
		}
	}

	auto const saved = fs::current_path();
	std::error_code ec;
	fs::current_path(baseDirectory, ec);
	for (auto const& path : directories) {
		FILELIST Pkt{};
		Pkt.Node = NODE_DIR;
		strcpy(Pkt.File, path.filename().u8string().c_str());
		AddFileList(Pkt, Base);
		MakeLocalTree(Pkt.File, Base);
	}
	fs::current_path(saved);
}


// Drag&Dropされたファイルがあるフォルダを取得する
void MakeDroppedDir(WPARAM wParam, char* Cur) {
	strncpy(Cur, DragFile((HDROP)wParam, 0).parent_path().u8string().c_str(), FMAX_PATH);
}


/*----- ホスト側のサブディレクトリ以下のファイルをリストに登録する（１）-------
*
*	Parameter
*		char *Path : パス名
*		char *Cur : カレントディレクトリ
*		FILELIST **Base : ファイルリストの先頭
*
*	Return Value
*		なし
*
*	Note
*		NLST -alLR を使う
*----------------------------------------------------------------------------*/

static int MakeRemoteTree1(char *Path, char *Cur, std::vector<FILELIST>& Base, int *CancelCheckWork) {
	int Ret;
	int Sts;

	// ファイル一覧バグ修正
	Ret = FFFTP_FAIL;
	if(DoCWD(Path, NO, NO, NO) == FTP_COMPLETE)
	{
		/* サブフォルダも含めたリストを取得 */
		Sts = DoDirListCmdSkt("R", "", 999, CancelCheckWork);	/* NLST -alLR*/
		DoCWD(Cur, NO, NO, NO);

		if(Sts == FTP_COMPLETE)
		{
			AddRemoteTreeToFileList(999, Path, RDIR_NLST, Base);
			Ret = FFFTP_SUCCESS;
		}
	}
	return(Ret);
}


/*----- ホスト側のサブディレクトリ以下のファイルをリストに登録する（２）-------
*
*	Parameter
*		char *Path : パス名
*		char *Cur : カレントディレクトリ
*		FILELIST **Base : ファイルリストの先頭
*
*	Return Value
*		なし
*
*	Note
*		各フォルダに移動してリストを取得
*----------------------------------------------------------------------------*/

static int MakeRemoteTree2(char *Path, char *Cur, std::vector<FILELIST>& Base, int *CancelCheckWork) {
	int Ret;
	int Sts;

	// ファイル一覧バグ修正
	Ret = FFFTP_FAIL;
	/* VAX VMS は CWD xxx/yyy という指定ができないので	*/
	/* CWD xxx, Cwd yyy と複数に分ける					*/
	if(AskHostType() != HTYPE_VMS)
		Sts = DoCWD(Path, NO, NO, NO);
	else
	{
#if defined(HAVE_OPENVMS)
		/* OpenVMSの場合、ディレクトリ移動時は"HOGE.DIR;1"を"HOGE"にする */
		ReformVMSDirName(Path, TRUE);
#endif
		Sts = DoCWDStepByStep(Path, Cur);
	}

	if(Sts == FTP_COMPLETE)
	{
		Sts = DoDirListCmdSkt("", "", 999, CancelCheckWork);		/* NLST -alL*/
		DoCWD(Cur, NO, NO, NO);

		if(Sts == FTP_COMPLETE)
		{
			std::vector<FILELIST> CurList;
			AddRemoteTreeToFileList(999, Path, RDIR_CWD, CurList);
			CopyTmpListToFileList(Base, CurList);

			// ファイル一覧バグ修正
			Ret = FFFTP_SUCCESS;

			for (auto const& f : CurList)
				if (f.Node == NODE_DIR) {
					FILELIST Pkt{};
					/* まずディレクトリ名をセット */
					strcpy(Pkt.File, f.File);
					Pkt.Link = f.Link;
					if (Pkt.Link == YES)
						Pkt.Node = NODE_FILE;
					else
						Pkt.Node = NODE_DIR;
					Pkt.Size = 0;
					Pkt.Attr = 0;
					memset(&Pkt.Time, 0, sizeof(FILETIME));
					AddFileList(Pkt, Base);

					if (Pkt.Link == NO && MakeRemoteTree2(const_cast<char*>(f.File), Cur, Base, CancelCheckWork) == FFFTP_FAIL)
						Ret = FFFTP_FAIL;
				}
		}
	}
	return(Ret);
}


/*----- ファイルリストの内容を別のファイルリストにコピー ----------------------
*
*	Parameter
*		FILELIST **Base : コピー先
*		FILELIST *List : コピー元
*
*	Return Value
*		なし
*
*	Note
*		コピーするのはファイルの情報だけ
*		ディレクトリの情報はコピーしない
*----------------------------------------------------------------------------*/

static void CopyTmpListToFileList(std::vector<FILELIST>& Base, std::vector<FILELIST> const& List) {
	for (auto& f : List)
		if (f.Node == NODE_FILE)
			AddFileList(f, Base);
}


// ホスト側のファイル情報をファイルリストに登録
void AddRemoteTreeToFileList(int Num, char *Path, int IncDir, std::vector<FILELIST>& Base) {
	char Dir[FMAX_PATH+1];
	strcpy(Dir, Path);
	if (auto lines = GetListLine(Num))
		for (auto& line : *lines)
			std::visit([&Path, IncDir, &Base, &Dir](auto&& arg) {
				if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, FILELIST>) {
					if (AskFilterStr(arg.File, arg.Node) == YES && (arg.Node == NODE_FILE || IncDir == RDIR_CWD && arg.Node == NODE_DIR)) {
						FILELIST Pkt{ Dir, arg.Node, arg.Link, arg.Size, arg.Attr, arg.Time, arg.InfoExist };
						if (0 < strlen(Pkt.File))
							SetSlashTail(Pkt.File);
						strcat(Pkt.File, arg.File);
						AddFileList(Pkt, Base);
					}
				} else {
					static_assert(std::is_same_v<std::decay_t<decltype(arg)>, std::string>);
					if (MakeDirPath(data(arg), LIST_UNKNOWN, Path, Dir) == FFFTP_SUCCESS && IncDir == RDIR_NLST)
						AddFileList({ Dir, NODE_DIR }, Base);
				}
			}, line);
}


// ファイル一覧情報の１行を取得
static std::optional<std::vector<std::variant<FILELIST, std::string>>> GetListLine(int Num) {
	std::ifstream is{ MakeCacheFileName(Num), std::ios::binary };
	if (!is)
		return {};
	std::vector<std::variant<FILELIST, std::string>> lines;
	for (std::string line; getline(is, line);) {
		/* VAX VMSではファイル情報が複数行にわかれている	*/
		/* それを１行にまとめる							*/
		if (AskHostType() == HTYPE_VMS) {
			if (std::find(begin(line), end(line), ';') == end(line))	/* ファイル名以外の行 */
				continue;
			for (std::string tmp; std::find(begin(line), end(line), ')') == end(line) && getline(is, tmp);)
				line += tmp;
		}
		line.erase(std::remove(begin(line), end(line), '\r'), end(line));
		std::replace(begin(line), end(line), '\b', ' ');
		if (auto list = AnalyzeFileInfo(data(line)); list != LIST_UNKNOWN) {
			char buf[FMAX_PATH + 1];
			LONGLONG size;
			FILETIME time;
			int attr;
			char owner[OWNER_NAME_LEN + 1];
			int link;
			int infoExist;
			auto node = ResolveFileInfo(data(line), list, buf, &size, &time, &attr, owner, &link, &infoExist);
			lines.emplace_back(std::in_place_type<FILELIST>, buf, (char)node, (char)link, size, attr, time, owner, (char)infoExist);
		} else
			lines.emplace_back(line);
	}
	if (CurHost.NameKanjiCode == KANJI_AUTO) {
		CodeDetector cd;
		for (auto& line : lines)
			std::visit([&cd](auto&& arg) {
				if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, FILELIST>)
					if (arg.Node != NODE_NONE && 0 < strlen(arg.File))
						cd.Test(arg.File);
			}, line);
		CurHost.CurNameKanjiCode = cd.result();
	} else
		CurHost.CurNameKanjiCode = CurHost.NameKanjiCode;
	for (auto& line : lines)
		std::visit([](auto&& arg) {
			if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, FILELIST>)
				if (arg.Node != NODE_NONE && 0 < strlen(arg.File)) {
					auto file = ConvertFrom(arg.File, CurHost.CurNameKanjiCode);
					if (auto last = file.back(); last == '/' || last == '\\')
						file.resize(file.size() - 1);
					if (empty(file) || file == "."sv || file == ".."sv)
						arg.Node = NODE_NONE;
					strcpy(arg.File, file.c_str());
				}
		}, line);
	return lines;
}


/*----- サブディレクトリ情報の解析 --------------------------------------------
*
*	Parameter
*		char *Str : ファイル情報（１行）
*		int ListType : リストのタイプ
*		char *Path : 先頭からのパス名
*		char *Dir : ディレクトリ名
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL=ディレクトリ情報でない
*----------------------------------------------------------------------------*/

static int MakeDirPath(char *Str, int ListType, char *Path, char *Dir)
{
	int Sts;

	Sts = FFFTP_FAIL;
	switch(ListType)
	{
		case LIST_ACOS :
		case LIST_ACOS_4 :
			break;

		default:
			if(*(Str + strlen(Str) - 1) == ':')		/* 最後が : ならサブディレクトリ */
			{
				if(strcmp(Str, ".:") != 0)
				{
					if((strncmp(Str, "./", 2) == 0) ||
					   (strncmp(Str, ".\\", 2) == 0))
					{
						Str += 2;
					}

					if(strlen(Str) > 1)
					{
						strcpy(Dir, Path);
						SetSlashTail(Dir);
						strcat(Dir, Str);
						*(Dir + strlen(Dir) - 1) = NUL;

						// 文字化け対策
//						ChangeFnameRemote2Local(Dir, FMAX_PATH);

						ReplaceAll(Dir, '\\', '/');
					}
				}
				Sts = FFFTP_SUCCESS;
			}
			break;
	}
	return(Sts);
}


// ローカル側のサブディレクトリ以下のファイルをリストに登録する
static int MakeLocalTree(const char *Path, std::vector<FILELIST>& Base) {
	auto const path = fs::u8path(Path);
	auto const src = path / L"*";
	FindFile(src, [&path, &Base](WIN32_FIND_DATAW const& data) {
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || AskFilterStr(u8(data.cFileName).c_str(), NODE_FILE) != YES)
			return true;
		FILELIST Pkt{};
		auto const src = (path / data.cFileName).u8string();
		strcpy(Pkt.File, src.c_str());
		ReplaceAll(Pkt.File, '\\', '/');
		Pkt.Node = NODE_FILE;
		Pkt.Size = MakeLongLong(data.nFileSizeHigh, data.nFileSizeLow);
		if (SYSTEMTIME TmpStime; FileTimeToSystemTime(&data.ftLastWriteTime, &TmpStime)) {
			if (DispTimeSeconds == NO)
				TmpStime.wSecond = 0;
			TmpStime.wMilliseconds = 0;
			SystemTimeToFileTime(&TmpStime, &Pkt.Time);
		}
		AddFileList(Pkt, Base);
		return true;
	});

	auto result = FindFile(src, [&path, &Base](WIN32_FIND_DATAW const& data) {
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			return true;
		FILELIST Pkt{};
		auto const src = (path / data.cFileName).u8string();
		strcpy(Pkt.File, src.c_str());
		ReplaceAll(Pkt.File, '\\', '/');
		Pkt.Node = NODE_DIR;
		AddFileList(Pkt, Base);
		return MakeLocalTree(src.c_str(), Base) == FFFTP_SUCCESS;
	});
	return result ? FFFTP_SUCCESS : FFFTP_FAIL;
}


/*----- ファイルリストに情報を登録する ----------------------------------------
*
*	Parameter
*		FILELIST *Pkt : 登録するファイル情報
*		FILELIST **Base : ファイルリストの先頭
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

static void AddFileList(FILELIST const& Pkt, std::vector<FILELIST>& Base) {
	DoPrintf("FileList : NODE=%d : %s", Pkt.Node, Pkt.File);
	/* リストの重複を取り除く */
	if (std::any_of(begin(Base), end(Base), [name = Pkt.File](auto const& f) { return strcmp(name, f.File) == 0; })) {
		DoPrintf(" --> Duplicate!!");
		return;
	}
	Base.emplace_back(Pkt);
}


/*----- ファイルリストに指定のファイルがあるかチェック ------------------------
*
*	Parameter
*		char *Fname : ファイル名
*		FILELIST *Base : ファイルリストの先頭
*		int Caps : 大文字/小文字の区別モード (COMP_xxx)
*
*	Return Value
*		FILELIST *見つかったファイルリストのデータ
*			NULL=見つからない
*----------------------------------------------------------------------------*/

const FILELIST* SearchFileList(const char* Fname, std::vector<FILELIST> const& Base, int Caps) {
	for (auto p = data(Base), end = data(Base) + size(Base); p != end; ++p)
		if (Caps == COMP_STRICT) {
			if (_mbscmp((const unsigned char*)Fname, (const unsigned char*)p->File) == 0)
				return p;
		} else {
			if (_mbsicmp((const unsigned char*)Fname, (const unsigned char*)p->File) == 0) {
				if (Caps == COMP_IGNORE)
					return p;
				char Tmp[FMAX_PATH + 1];
				strcpy(Tmp, p->File);
				_mbslwr((unsigned char*)Tmp);
				if (_mbscmp((const unsigned char*)Tmp, (const unsigned char*)p->File) == 0)
					return p;
			}
		}
	return nullptr;
}


/*----- ファイル情報からリストタイプを求める ----------------------------------
*
*	Parameter
*		char *Str : ファイル情報（１行）
*
*	Return Value
*		int リストタイプ (LIST_xxx)
*----------------------------------------------------------------------------*/

static int AnalyzeFileInfo(char *Str)
{
	int Ret;
	char Tmp[FMAX_PATH+1];
	int Add1;
	int TmpInt;
	int Flag1;
	WORD Month;
	WORD Day;

//DoPrintf("LIST : %s", Str);

	Ret = LIST_UNKNOWN;
	Flag1 = AskHostType();
	if(Flag1 == HTYPE_ACOS)
		Ret = LIST_ACOS;
	else if(Flag1 == HTYPE_ACOS_4)
		Ret = LIST_ACOS_4;
	else if(Flag1 == HTYPE_VMS)
		Ret = LIST_VMS;
	else if(Flag1 == HTYPE_IRMX)
		Ret = LIST_IRMX;
	else if(Flag1 == HTYPE_STRATUS)
		Ret = LIST_STRATUS;
	else if(Flag1 == HTYPE_AGILENT)
		Ret = LIST_AGILENT;
	else if(Flag1 == HTYPE_SHIBASOKU)
		Ret = LIST_SHIBASOKU;
	else
	{
		// MLSD対応
		if(FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS)
		{
			_strlwr(Tmp);
			if(strstr(Tmp, "type=") != NULL)
			{
				if(FindField2(Str, Tmp, ';', 1, NO) == FFFTP_SUCCESS && FindField2(Str, Tmp, '=', 1, NO) == FFFTP_SUCCESS)
				{
					Ret = LIST_MLSD;
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_UNIX_10, LIST_UNIX_20, LIST_UNIX_12, LIST_UNIX_22, LIST_UNIX_50, LIST_UNIX_60 */
		/* MELCOM80 */

		// MLSD対応
//		if(FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS)
		if(Ret == LIST_UNKNOWN && FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS)
		{
			/* MELCOM80は "d rwxrwxrwx" のようにスペースが空いている */
			Flag1 = NO;
			if((strlen(Tmp) == 1) && (strchr("-dDlL", Tmp[0]) != NULL))
			{
				if(FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS)
				{
					if((strlen(Tmp) == 9) ||
					   ((strlen(Tmp) > 9) && (IsDigit(Tmp[9]) != 0)))
					{
						memmove(Str+1, Str+2, strlen(Str+2)+1);
						FindField(Str, Tmp, 0, NO);
						Flag1 = YES;
					}
				}
			}

			// バグ修正
//			if(strlen(Tmp) >= 10)
			if((strlen(Tmp) >= 10) && (strchr("+-dfl", Tmp[0]) != NULL))
			{
				Add1 = 0;
				if((strlen(Tmp) > 10) && (IsDigit(Tmp[10]) != 0))
				{
					/* こういう時 */
					/*   drwxr-xr-x1234  owner group  1024  Nov 6 14:21 Linux/    */
					Add1 = -1;
				}

////////////
// LIST_UNIX_60 support
				if(FindField(Str, Tmp, 7+Add1, NO) == FFFTP_SUCCESS)
				{
					GetMonth(Tmp, &Month, &Day);
					if(Month != 0)
					{
						Ret = CheckUnixType(Str, Tmp, Add1, 2, Day);
					}
				}
///////////

////////////
// LIST_UNIX_12 support
				if((Ret == LIST_UNKNOWN) &&
				   (FindField(Str, Tmp, 6+Add1, NO) == FFFTP_SUCCESS))
				{
					GetMonth(Tmp, &Month, &Day);
					if(Month != 0)
					{
						Ret = CheckUnixType(Str, Tmp, Add1, 0, Day);
					}
				}
//////////////////

////////////
// LIST_UNIX_70 support
				if((Ret == LIST_UNKNOWN) &&
				   (FindField(Str, Tmp, 6+Add1, NO) == FFFTP_SUCCESS))
				{
					GetMonth(Tmp, &Month, &Day);
					if(Month != 0)
					{
						Ret = CheckUnixType(Str, Tmp, Add1, 1, Day);
					}
				}
///////////

				if((Ret == LIST_UNKNOWN) &&
				   (FindField(Str, Tmp, 5+Add1, NO) == FFFTP_SUCCESS))
				{
					GetMonth(Tmp, &Month, &Day);
					if(Month != 0)
					{
						Ret = CheckUnixType(Str, Tmp, Add1, 0, Day);
					}
				}

				if((Ret == LIST_UNKNOWN) &&
				   (FindField(Str, Tmp, 4+Add1, NO) == FFFTP_SUCCESS))
				{
					GetMonth(Tmp, &Month, &Day);
					if(Month != 0)
					{
						Ret = CheckUnixType(Str, Tmp, Add1, -1, Day);
					}
				}

				if((Ret == LIST_UNKNOWN) &&
				   (FindField(Str, Tmp, 3+Add1, NO) == FFFTP_SUCCESS))
				{
					GetMonth(Tmp, &Month, &Day);
					if(Month != 0)
					{
						Ret = CheckUnixType(Str, Tmp, Add1, -2, Day);
					}
				}

				// linux-ftpd
				if((Ret == LIST_UNKNOWN) &&
				   (FindField(Str, Tmp, 7+Add1, NO) == FFFTP_SUCCESS))
				{
					if((FindField(Str, Tmp, 5, NO) == FFFTP_SUCCESS) &&
					   (CheckYYYYMMDDformat(Tmp, NUL) != 0))
					{
						if((FindField(Str, Tmp, 6, NO) == FFFTP_SUCCESS) &&
						   (CheckHHMMformat(Tmp) == YES))
						{
							Ret = LIST_UNIX_16;
						}
					}
				}

				if((Ret != LIST_UNKNOWN) && (Flag1 == YES))
					Ret |= LIST_MELCOM;

				// uClinux
				if((Ret == LIST_UNKNOWN) &&
				   (FindField(Str, Tmp, 5+Add1, NO) == FFFTP_SUCCESS))
				{
					Ret = LIST_UNIX_17;
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_AS400 */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
			   (CheckYYMMDDformat(Tmp, NUL, NO) != 0))
			{
				if((FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS) &&
				   (CheckYYMMDDformat(Tmp, NUL, NO) != 0))
				{
					if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) &&
					   (IsDigit(Tmp[0]) != 0))
					{
						if(FindField(Str, Tmp, 5, NO) == FFFTP_SUCCESS)
						{
							Ret = LIST_AS400;
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_M1800 */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 5, NO) == FFFTP_SUCCESS) &&
			   (CheckYYMMDDformat(Tmp, '*', NO) != 0))
			{
				if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
				   ((IsDigit(Tmp[0]) != 0) || (StrAllSameChar(Tmp, '*') == YES)))
				{
					if((FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS) &&
					   ((IsDigit(Tmp[0]) != 0) || (StrAllSameChar(Tmp, '*') == YES)))
					{
						if((FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS) &&
						   (strlen(Tmp) == 4))
						{
							if(FindField(Str, Tmp, 6, NO) == FFFTP_SUCCESS)
							{
								Ret = LIST_M1800;
							}
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_GP6000 */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) &&
			   (CheckYYMMDDformat(Tmp, NUL, NO) != 0))
			{
				if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
				   (CheckYYMMDDformat(Tmp, NUL, NO) != 0))
				{
					if((FindField(Str, Tmp, 5, NO) == FFFTP_SUCCESS) &&
					   (IsDigit(Tmp[0]) != 0))
					{
						if(FindField(Str, Tmp, 6, NO) == FFFTP_SUCCESS)
						{
							Ret = LIST_GP6000;
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_DOS_1, LIST_DOS_2 */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) &&
			   (CheckHHMMformat(Tmp) == YES))
			{
				if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
				   ((Tmp[0] == '<') || (IsDigit(Tmp[0]) != 0)))
				{
					if(FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS)
					{
						if((FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS) &&
						   (CheckYYMMDDformat(Tmp, NUL, YES) != 0))
						{
							TmpInt = atoi(Tmp);
							if((TmpInt >= 1) && (TmpInt <= 12))
								Ret = LIST_DOS_2;
							else
								Ret = LIST_DOS_1;
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_DOS_3 */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS) &&
			   (CheckHHMMformat(Tmp) == YES))
			{
				if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) &&
				   ((Tmp[0] == '<') || (IsDigit(Tmp[0]) != 0)))
				{
					if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
					   (CheckYYMMDDformat(Tmp, NUL, YES) != 0))
					{
						Ret = LIST_DOS_3;
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_DOS_4 */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS) &&
			   (CheckYYYYMMDDformat(Tmp, NUL) == YES))
			{
				if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) &&
				   (CheckYYMMDDformat(Tmp, NUL, NO) != 0))
				{
					if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
					   ((Tmp[0] == '<') || (IsDigit(Tmp[0]) != 0)))
					{
						if(FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS)
						{
							Ret = LIST_DOS_4;
						}
					}
				}
			}
		}

		// Windows Server 2008 R2
		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) &&
			   (CheckHHMMformat(Tmp) == YES))
			{
				if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
				   ((Tmp[0] == '<') || (IsDigit(Tmp[0]) != 0)))
				{
					if(FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS)
					{
						if((FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS) &&
						   (CheckMMDDYYYYformat(Tmp, NUL) != 0))
						{
							Ret = LIST_DOS_5;
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_CHAMELEON */

		if(Ret == LIST_UNKNOWN)
		{
			if(FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS)
			{
				GetMonth(Tmp, &Month, &Day);
				if((Month != 0) && (Day == 0))
				{
					if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) &&
					   ((Tmp[0] == '<') || (IsDigit(Tmp[0]) != 0)))
					{
						if((FindField(Str, Tmp, 5, NO) == FFFTP_SUCCESS) &&
						   (CheckHHMMformat(Tmp) == YES))
						{
							Ret = LIST_CHAMELEON;
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_OS2 */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS) &&
			   (CheckHHMMformat(Tmp) == YES))
			{
				if((FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS) &&
				   (IsDigit(Tmp[0]) != 0))
				{
					if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
					   (CheckYYMMDDformat(Tmp, NUL, YES) != 0))
					{
						if(FindField(Str, Tmp, 4, NO) == FFFTP_SUCCESS)
						{
							Ret = LIST_OS2;
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_OS7 */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS) &&
			   (strlen(Tmp) == 10))
			{
				if((FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS) &&
				   (CheckYYMMDDformat(Tmp, NUL, NO) != 0))
				{
					if((FindField(Str, Tmp, 4, NO) == FFFTP_SUCCESS) &&
					   (CheckYYMMDDformat(Tmp, NUL, NO) != 0))
					{
						if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
						   (IsDigit(Tmp[0]) != 0))
						{
							if(FindField(Str, Tmp, 5, NO) == FFFTP_SUCCESS)
							{
								Ret = LIST_OS7_2;
							}
						}
					}
				}
				else if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) &&
						(CheckYYMMDDformat(Tmp, NUL, NO) != 0))
				{
					if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
					   (CheckYYMMDDformat(Tmp, NUL, NO) != 0))
					{
						if(FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS)
						{
							Ret = LIST_OS7_1;
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_ALLIED */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 0, NO) == FFFTP_SUCCESS) &&
			   ((Tmp[0] == '<') || (IsDigit(Tmp[0]) != 0)))
			{
				if((FindField(Str, Tmp, 5, NO) == FFFTP_SUCCESS) &&
				   (CheckHHMMformat(Tmp) == YES))
				{
					if(FindField(Str, Tmp, 3, NO) == FFFTP_SUCCESS)
					{
						GetMonth(Tmp, &Month, &Day);
						if(Month != 0)
						{
							if((FindField(Str, Tmp, 6, NO) == FFFTP_SUCCESS) &&
							   (IsDigit(Tmp[0]) != 0))
							{
								Ret = LIST_ALLIED;
							}
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_OS9 */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) &&
			   (CheckYYMMDDformat(Tmp, NUL, NO) != 0))
			{
				if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
				   (IsDigit(Tmp[0]) != 0) && (strlen(Tmp) == 4))
				{
					if((FindField(Str, Tmp, 5, NO) == FFFTP_SUCCESS) &&
					   (IsDigit(Tmp[0]) != 0))
					{
						if(FindField(Str, Tmp, 6, NO) == FFFTP_SUCCESS)
						{
							Ret = LIST_OS9;
						}
					}
				}
			}
		}

		/* 以下のフォーマットをチェック */
		/* LIST_IBM */

		if(Ret == LIST_UNKNOWN)
		{
			if((FindField(Str, Tmp, 2, NO) == FFFTP_SUCCESS) &&
			   (CheckYYYYMMDDformat(Tmp, NUL) == YES))
			{
				if((FindField(Str, Tmp, 1, NO) == FFFTP_SUCCESS) && IsDigit(Tmp[0]))
				{
					if((FindField(Str, Tmp, 7, NO) == FFFTP_SUCCESS) && IsDigit(Tmp[0]))
					{
						if(FindField(Str, Tmp, 9, NO) == FFFTP_SUCCESS)
						{
							Ret = LIST_IBM;
						}
					}
				}
			}
		}
#if defined(HAVE_TANDEM)
		/* 以下のフォーマットをチェック */
		/* LIST_TANDEM */

		/* OSS の場合は自動判別可能のため Ret == LIST_UNKNOWN のチェックは後 */
		if(AskRealHostType() == HTYPE_TANDEM) {
			if(Ret == LIST_UNKNOWN) {
				SetOSS(NO);
				Ret = LIST_TANDEM;
			} else {
				SetOSS(YES);
			}
		}
#endif

	}

DoPrintf("ListType=%d", Ret);

	return(Ret);
}


/*----- UNIX系リストタイプのチェックを行なう ----------------------------------
*
*	Parameter
*		char *Str : ファイル情報（１行）
*		char *Tmp : 一時ワーク
*		int Add1 : 加算パラメータ1
*		int Add2 : 加算パラメータ2
*		int Day : 日 (0=ここで取得する)
*
*	Return Value
*		int リストタイプ (LIST_xxx)
*----------------------------------------------------------------------------*/

static int CheckUnixType(char *Str, char *Tmp, int Add1, int Add2, int Day)
{
	int Ret;
	int Add3;
	WORD Hour;
	WORD Minute;
	int Flag;

	Flag = 0;
	Ret = LIST_UNKNOWN;

//DayによってAdd3を変える

	Add3 = 0;
	if(Day != 0)
		Add3 = -1;

	// unix系チェック
	if((Day != 0) ||
	   ((FindField(Str, Tmp, 6+Add1+Add2+Add3, NO) == FFFTP_SUCCESS) &&
		((atoi(Tmp) >= 1) && (atoi(Tmp) <= 31))))
	{
		if((FindField(Str, Tmp, 7+Add1+Add2+Add3, NO) == FFFTP_SUCCESS) &&
		   ((atoi(Tmp) >= 1900) || (GetHourAndMinute(Tmp, &Hour, &Minute) == FFFTP_SUCCESS)))
		{
			if(FindField(Str, Tmp, 8+Add1+Add2+Add3, NO) == FFFTP_SUCCESS)
			{
				Flag = 1;
			}
		}
	}

	// 中国語Solaris専用
	if(Flag == 0)
	{
	   if((FindField(Str, Tmp, 7+Add1+Add2+Add3, NO) == FFFTP_SUCCESS) &&
		  ((atoi(Tmp) >= 1) && (atoi(Tmp) <= 31)))
		{
			if((FindField(Str, Tmp, 5+Add1+Add2+Add3, NO) == FFFTP_SUCCESS) &&
			   (atoi(Tmp) >= 1900))
			{
				if((FindField(Str, Tmp, 6+Add1+Add2+Add3, NO) == FFFTP_SUCCESS) &&
				   (((atoi(Tmp) >= 1) && (atoi(Tmp) <= 9) && 
					 ((unsigned char)Tmp[1] == 0xD4) &&
					 ((unsigned char)Tmp[2] == 0xC2)) ||
					((atoi(Tmp) >= 10) && (atoi(Tmp) <= 12) && 
					 ((unsigned char)Tmp[2] == 0xD4) && 
					 ((unsigned char)Tmp[3] == 0xC2))))
				{
					if(FindField(Str, Tmp, 8+Add1+Add2+Add3, NO) == FFFTP_SUCCESS)
					{
						Flag = 2;
					}
				}
			}
		}
	}

	if(Flag != 0)
	{
		if(Add2 == 2)
		{
			Ret = LIST_UNIX_60;
			if(Flag == 2)
				Ret = LIST_UNIX_64;
			if(Day != 0)
				Ret = LIST_UNIX_61;

			if(Add1 == -1)
			{
				Ret = LIST_UNIX_62;
				if(Flag == 2)
					Ret = LIST_UNIX_65;
				if(Day != 0)
					Ret = LIST_UNIX_63;
			}
		}
		else if(Add2 == 1)
		{
			Ret = LIST_UNIX_70;
			if(Flag == 2)
				Ret = LIST_UNIX_74;
			if(Day != 0)
				Ret = LIST_UNIX_71;

			if(Add1 == -1)
			{
				Ret = LIST_UNIX_72;
				if(Flag == 2)
					Ret = LIST_UNIX_75;
				if(Day != 0)
					Ret = LIST_UNIX_73;
			}
		}
		else if(Add2 == 0)
		{
			Ret = LIST_UNIX_10;
			if(Flag == 2)
				Ret = LIST_UNIX_14;
			if(Day != 0)
				Ret = LIST_UNIX_11;

			if(Add1 == -1)
			{
				Ret = LIST_UNIX_12;
				if(Flag == 2)
					Ret = LIST_UNIX_15;
				if(Day != 0)
					Ret = LIST_UNIX_13;
			}
		}
		else if(Add2 == -1)
		{
			Ret = LIST_UNIX_20;
			if(Flag == 2)
				Ret = LIST_UNIX_24;
			if(Day != 0)
				Ret = LIST_UNIX_21;

			if(Add1 == -1)
			{
				Ret = LIST_UNIX_22;
				if(Flag == 2)
					Ret = LIST_UNIX_25;
				if(Day != 0)
					Ret = LIST_UNIX_23;
			}
		}
		else
		{
			Ret = LIST_UNIX_50;
			if(Flag == 2)
				Ret = LIST_UNIX_54;
			if(Day != 0)
				Ret = LIST_UNIX_51;
		}
	}
	return(Ret);
}


/*----- HH:MM 形式の文字列かどうかをチェック ----------------------------------
*
*	Parameter
*		char *Str : 文字列
*
*	Return Value
*		int ステータス (YES/NO)
*
*	Note
*		区切り文字は何でもよい
*		時分でなくてもよい
*		後ろに余分な文字が付いていてもよい
*----------------------------------------------------------------------------*/

static int CheckHHMMformat(char *Str)
{
	int Ret;

	Ret = NO;
	if((strlen(Str) >= 3) &&
	   (IsDigit(Str[0]) != 0))
	{
		if((Str = strchr(Str, ':')) != NULL)
		{
			if(IsDigit(*(Str+1)) != 0)
				Ret = YES;
		}
	}
	return(Ret);
}


/*----- YY/MM/DD 形式の文字列かどうかをチェック -------------------------------
*
*	Parameter
*		char *Str : 文字列
*		char Sym : 数字の代わりに使える記号 (NUL=数字以外使えない)
*		int Dig3 : 3桁の年を許可
*
*	Return Value
*		int ステータス
*			0 = 該当しない
*			1 = ??/??/??, ??/??/???
*			2 = ???/??/??
*
*	Note
*		区切り文字は何でもよい
*		年月日でなくてもよい
*----------------------------------------------------------------------------*/

static int CheckYYMMDDformat(char *Str, char Sym, int Dig3)
{
	int Ret;

	Ret = 0;
	if((strlen(Str) == 8) &&
	   (IsDigitSym(Str[0], Sym) != 0) && (IsDigitSym(Str[1], Sym) != 0) &&
	   (IsDigit(Str[2]) == 0) &&
	   (IsDigitSym(Str[3], Sym) != 0) && (IsDigitSym(Str[4], Sym) != 0) &&
	   (IsDigit(Str[5]) == 0) &&
	   (IsDigitSym(Str[6], Sym) != 0) && (IsDigitSym(Str[7], Sym) != 0))
	{
		Ret = 1; 
	}
	if(Dig3 == YES)
	{
		if((strlen(Str) == 9) &&
		   (IsDigitSym(Str[0], Sym) != 0) && (IsDigitSym(Str[1], Sym) != 0) && (IsDigitSym(Str[2], Sym) != 0) &&
		   (IsDigit(Str[3]) == 0) &&
		   (IsDigitSym(Str[4], Sym) != 0) && (IsDigitSym(Str[5], Sym) != 0) &&
		   (IsDigit(Str[6]) == 0) &&
		   (IsDigitSym(Str[7], Sym) != 0) && (IsDigitSym(Str[8], Sym) != 0))
		{
			Ret = 2; 
		}
		else if((strlen(Str) == 9) &&
				(IsDigitSym(Str[0], Sym) != 0) && (IsDigitSym(Str[1], Sym) != 0) &&
				(IsDigit(Str[2]) == 0) &&
				(IsDigitSym(Str[3], Sym) != 0) && (IsDigitSym(Str[4], Sym) != 0) &&
				(IsDigit(Str[5]) == 0) &&
				(IsDigitSym(Str[6], Sym) != 0) && (IsDigitSym(Str[7], Sym) != 0) && (IsDigitSym(Str[8], Sym) != 0))
		{
			Ret = 1; 
		}
	}
	return(Ret);
}


/*----- YYYY/MM/DD 形式の文字列かどうかをチェック -----------------------------
*
*	Parameter
*		char *Str : 文字列
*		char Sym : 数字の代わりに使える記号 (NUL=数字以外使えない)
*
*	Return Value
*		int ステータス (YES/NO)
*
*	Note
*		区切り文字は何でもよい
*		年月日でなくてもよい
*----------------------------------------------------------------------------*/

static int CheckYYYYMMDDformat(char *Str, char Sym)
{
	int Ret;

	Ret = NO;
	if((strlen(Str) == 10) &&
	   (IsDigitSym(Str[0], Sym) != 0) && (IsDigitSym(Str[1], Sym) != 0) &&
	   (IsDigitSym(Str[2], Sym) != 0) && (IsDigitSym(Str[3], Sym) != 0) &&
	   (IsDigit(Str[4]) == 0) &&
	   (IsDigitSym(Str[5], Sym) != 0) && (IsDigitSym(Str[6], Sym) != 0) &&
	   (IsDigit(Str[7]) == 0) &&
	   (IsDigitSym(Str[8], Sym) != 0) && (IsDigitSym(Str[9], Sym) != 0))
	{
		Ret = YES; 
	}
	return(Ret);
}


// Windows Server 2008 R2
static int CheckMMDDYYYYformat(char *Str, char Sym)
{
	int Ret;

	Ret = NO;
	if((strlen(Str) == 10) &&
	   (IsDigitSym(Str[0], Sym) != 0) && (IsDigitSym(Str[1], Sym) != 0) &&
	   (IsDigit(Str[2]) == 0) &&
	   (IsDigitSym(Str[3], Sym) != 0) && (IsDigitSym(Str[4], Sym) != 0) &&
	   (IsDigit(Str[5]) == 0) &&
	   (IsDigitSym(Str[6], Sym) != 0) && (IsDigitSym(Str[7], Sym) != 0) &&
	   (IsDigitSym(Str[8], Sym) != 0) && (IsDigitSym(Str[9], Sym) != 0))
	{
		Ret = YES; 
	}
	return(Ret);
}


/*----- ファイル情報からファイル名、サイズなどを取り出す ----------------------
*
*	Parameter
*		char *Str : ファイル情報（１行）
*		int ListType : リストのタイプ
*		char *Name : ファイル名のコピー先
*		LONGLONG *Size : サイズのコピー先
*		FILETIME *Time : 日付のコピー先
*		int *Attr : 属性のコピー先
*		char *Owner : オーナ名
*		int *Link : リンクかどうか (YES/NO)
*		int *InfoExist : 時刻の情報があったかどうか (YES/NO)
*
*	Return Value
*		int 種類 (NODE_xxxx)
*----------------------------------------------------------------------------*/

static int ResolveFileInfo(char *Str, int ListType, char *Fname, LONGLONG *Size, FILETIME *Time, int *Attr, char *Owner, int *Link, int *InfoExist)
{
	SYSTEMTIME sTime;
	char Buf[FMAX_PATH+1];
	char *Pos;
	char Flag;
	int Ret;
	int offs;
	int offs2;
	int offs3;
	int OrgListType;
	int err;
	int Flag2;

	static const int DosPos[3][4] = { { 1, 0, 2, 3 }, { 1, 0, 2, 3 }, { 3, 2, 1, 0 } };
	static const int DosDate[3][3][2] = { { {0, 0}, {3, 4}, {6, 7} }, { {6, 7}, {0, 0}, {3, 4} }, { {6, 7}, {0, 0}, {3, 4} } };
	static const int DosLongFname[3] = { YES, YES, NO };

	/* まずクリアしておく */
	Ret = NODE_NONE;
	// バグ対策
	memset(Fname, NUL, FMAX_PATH+1);
	*Size = -1;
	*Attr = 0;
	*Link = NO;
	memset(Owner, NUL, OWNER_NAME_LEN+1);
	Time->dwLowDateTime = 0;
	Time->dwHighDateTime = 0;
	*InfoExist = 0;
	offs = 0;
	offs2 = 0;
	offs3 = 0;

	OrgListType = ListType;
	ListType &= LIST_MASKFLG;
	switch(ListType)
	{
		case LIST_DOS_1 :
		case LIST_DOS_2 :
		case LIST_DOS_3 :
			if(ListType == LIST_DOS_1)
				offs = 0;
			else if(ListType == LIST_DOS_2)
				offs = 1;
			else
				offs = 2;

			*InfoExist |= (FINFO_DATE | FINFO_SIZE);

			/* 時刻 */
			FindField(Str, Buf, DosPos[offs][0], NO);
			if((Pos = strchr(Buf, ':')) != NULL)
			{
				*InfoExist |= FINFO_TIME;
				sTime.wHour = atoi(Buf);
				sTime.wMinute = atoi(Pos+1);
				sTime.wSecond = 0;
				sTime.wMilliseconds = 0;

				if(strlen(Pos) >= 4)
				{
					if(tolower(Pos[3]) == 'a')
					{
						if(sTime.wHour == 12)
							sTime.wHour = 0;
					}
					else if(tolower(Pos[3]) == 'p')
					{
						if(sTime.wHour != 12)
							sTime.wHour += 12;
					}
				}
			}

			/* 日付 */
			FindField(Str, Buf, DosPos[offs][1], NO);
			if((offs2 = CheckYYMMDDformat(Buf, NUL, YES)) == 0)
				break;
			offs2--;
			sTime.wYear = Assume1900or2000(atoi(Buf + DosDate[offs][0][offs2]));
			sTime.wMonth = atoi(Buf + DosDate[offs][1][offs2]);
			sTime.wDay = atoi(Buf + DosDate[offs][2][offs2]);
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* サイズ */
			FindField(Str, Buf, DosPos[offs][2], NO);
			*Size = _atoi64(Buf);

			/* 名前 */
			if(FindField(Str, Fname, DosPos[offs][3], DosLongFname[offs]) == FFFTP_SUCCESS)
			{
				Ret = NODE_FILE;
				if(Buf[0] == '<')
					Ret = NODE_DIR;
			}
			break;

		case LIST_DOS_4 :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE);

			/* 日付 */
			FindField(Str, Buf, 0, NO);
			sTime.wYear = atoi(Buf);
			sTime.wMonth = atoi(Buf+5);
			sTime.wDay = atoi(Buf+8);

			/* 時刻 */
			*InfoExist |= FINFO_TIME;
			FindField(Str, Buf, 1, NO);
			sTime.wHour = atoi(Buf);
			sTime.wMinute = atoi(Buf+3);
			sTime.wSecond = 0;				// atoi(Buf+6);
			sTime.wMilliseconds = 0;
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* サイズ */
			FindField(Str, Buf, 2, NO);
			*Size = _atoi64(Buf);

			/* 名前 */
			if(FindField(Str, Fname, 3, YES) == FFFTP_SUCCESS)
			{
				Ret = NODE_FILE;
				if(Buf[0] == '<')
					Ret = NODE_DIR;
			}
			break;

		// Windows Server 2008 R2
		case LIST_DOS_5 :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE);

			/* 日付 */
			FindField(Str, Buf, 0, NO);
			sTime.wMonth = atoi(Buf);
			sTime.wDay = atoi(Buf+3);
			sTime.wYear = atoi(Buf+6);

			/* 時刻 */
			FindField(Str, Buf, 1, NO);
			sTime.wHour = atoi(Buf);
			sTime.wMinute = atoi(Buf+3);
			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;
			if(_strnicmp(Buf+5, "AM", 2) == 0)
			{
				if(sTime.wHour == 12)
					sTime.wHour = 0;
			}
			else if(_strnicmp(Buf+5, "PM", 2) == 0)
			{
				if(sTime.wHour != 12)
					sTime.wHour += 12;
			}
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* サイズ */
			FindField(Str, Buf, 2, NO);
			*Size = _atoi64(Buf);

			/* 名前 */
			if(FindField(Str, Fname, 3, YES) == FFFTP_SUCCESS)
			{
				Ret = NODE_FILE;
				if(Buf[0] == '<')
					Ret = NODE_DIR;
			}
			break;

		case LIST_OS2 :
			*InfoExist |= (FINFO_DATE | FINFO_SIZE);

			/* 時刻 */
			FindField(Str, Buf, 3, NO);
			if((Pos = strchr(Buf, ':')) != NULL)
			{
				*InfoExist |= FINFO_TIME;
				sTime.wHour = atoi(Buf);
				sTime.wMinute = atoi(Pos+1);
				sTime.wSecond = 0;
				sTime.wMilliseconds = 0;
			}

			/* 日付 */
			FindField(Str, Buf, 2, NO);
			sTime.wYear = Assume1900or2000(atoi(Buf+6));
			sTime.wMonth = atoi(Buf+0);
			sTime.wDay = atoi(Buf+3);
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* サイズ */
			FindField(Str, Buf, 0, NO);
			*Size = _atoi64(Buf);

			/* 名前 */
			if(FindField(Str, Fname, 4, YES) == FFFTP_SUCCESS)
			{
				FindField(Str, Buf, 1, NO);
				Ret = NODE_FILE;
				if(strstr(Buf, "DIR") != NULL)
					Ret = NODE_DIR;
			}
			break;

		case LIST_CHAMELEON :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE | FINFO_ATTR);

			/* 属性 */
			FindField(Str, Buf, 6, NO);
			strcat(Buf, "------");
			*Attr = AttrString2Value(Buf+1);

			/* 日付 */
			FindField(Str, Buf, 2, NO);
			GetMonth(Buf, &sTime.wMonth, &sTime.wDay);	/* wDayは常に0 */
			FindField(Str, Buf, 3, NO);
			sTime.wDay = atoi(Buf);
			FindField(Str, Buf, 4, NO);
			sTime.wYear = atoi(Buf);

			/* 時刻 */
			FindField(Str, Buf, 5, NO);
			sTime.wHour = atoi(Buf);
			sTime.wMinute = atoi(Buf+3);
			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* サイズ */
			FindField(Str, Buf, 1, NO);
			*Size = _atoi64(Buf);

			/* 名前 */
			if(FindField(Str, Fname, 0, NO) == FFFTP_SUCCESS)
			{
				Ret = NODE_FILE;
				if(Buf[0] == '<')
					Ret = NODE_DIR;
			}
			break;

		case LIST_AS400 :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE);

			/* オーナ名 */
			FindField(Str, Buf, 0, NO);
			strncpy(Owner, Buf, OWNER_NAME_LEN);

			/* 時刻 */
			FindField(Str, Buf, 3, NO);
			sTime.wHour = atoi(Buf);
			sTime.wMinute = atoi(Buf+3);
			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;

			/* 日付 */
			FindField(Str, Buf, 2, NO);
			sTime.wYear = Assume1900or2000(atoi(Buf));
			sTime.wMonth = atoi(Buf + 3);
			sTime.wDay = atoi(Buf + 6);
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* サイズ */
			FindField(Str, Buf, 1, NO);
			*Size = _atoi64(Buf);

			/* 名前 */
			if(FindField(Str, Fname, 5, YES) == FFFTP_SUCCESS)
			{
				Ret = NODE_FILE;
				if((Pos = strchr(Fname, '/')) != NULL)
				{
					Ret = NODE_DIR;
					*Pos = NUL;
				}
			}
			break;

		case LIST_M1800 :
			*InfoExist |= FINFO_ATTR;

			/* 属性 */
			FindField(Str, Buf, 0, NO);
			strcat(Buf, "------");
			*Attr = AttrString2Value(Buf+1);

			/* 日付 */
			Time->dwLowDateTime = 0;
			Time->dwHighDateTime = 0;
			FindField(Str, Buf, 5, NO);
			if(Buf[0] != '*')
			{
				*InfoExist |= FINFO_DATE;
				sTime.wHour = 0;
				sTime.wMinute = 0;
				sTime.wSecond = 0;
				sTime.wMilliseconds = 0;

				sTime.wYear = Assume1900or2000(atoi(Buf));
				sTime.wMonth = atoi(Buf + 3);
				sTime.wDay = atoi(Buf + 6);
				SystemTimeToFileTime(&sTime, Time);
				SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());
			}

			/* 名前 */
			if(FindField(Str, Fname, 6, YES) == FFFTP_SUCCESS)
			{
				RemoveTailingSpaces(Fname);
				Ret = NODE_FILE;
				if((Pos = strchr(Fname, '/')) != NULL)
				{
					Ret = NODE_DIR;
					*Pos = NUL;
				}
			}
			break;

		case LIST_GP6000 :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE | FINFO_ATTR);

			/* オーナ名 */
			FindField(Str, Buf, 3, NO);
			strncpy(Owner, Buf, OWNER_NAME_LEN);

			/* 時刻 */
			FindField(Str, Buf, 2, NO);
			sTime.wHour = atoi(Buf);
			sTime.wMinute = atoi(Buf+3);
			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;

			/* 日付 */
			FindField(Str, Buf, 1, NO);
			sTime.wYear = Assume1900or2000(atoi(Buf));
			sTime.wMonth = atoi(Buf + 3);
			sTime.wDay = atoi(Buf + 6);
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* サイズ */
			FindField(Str, Buf, 5, NO);
			*Size = _atoi64(Buf);

			/* 属性 */
			FindField(Str, Buf, 0, NO);
			*Attr = AttrString2Value(Buf+1);

			/* 名前 */
			if(FindField(Str, Fname, 6, YES) == FFFTP_SUCCESS)
			{
				Ret = NODE_FILE;
				if(strchr("dl", Buf[0]) != NULL)
					Ret = NODE_DIR;
			}
			break;

		case LIST_ACOS :
		case LIST_ACOS_4 :
			/* 名前 */
			FindField(Str, Fname, 0, NO);
			Ret = NODE_FILE;
			break;

		case LIST_VMS :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE);

			/* サイズ */
			FindField(Str, Buf, 1, NO);
			*Size = _atoi64(Buf) * BLOCK_SIZE;

			/* 時刻／日付 */
			FindField(Str, Buf, 2, NO);
			GetVMSdate(Buf, &sTime.wYear, &sTime.wMonth, &sTime.wDay);

			FindField(Str, Buf, 3, NO);
			GetHourAndMinute(Buf, &sTime.wHour, &sTime.wMinute);

			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* 名前 */
			FindField(Str, Fname, 0, NO);

			Ret = NODE_FILE;
			if((Pos = strchr(Fname, '.')) != NULL)
			{
				if(_strnicmp(Pos, ".DIR;", 5) == 0)
				{
					/* OpenVMSの場合、ファイル/ディレクトリ削除時には".DIR;?"までないと
					 * 削除できないので、ここではつぶさない */
#if !defined(HAVE_OPENVMS)
					*Pos = NUL;
#endif
					Ret = NODE_DIR;
				}
			}
			break;

		case LIST_OS7_2 :
			*InfoExist |= FINFO_SIZE;
			offs = 2;

			/* サイズ */
			FindField(Str, Buf, 2, NO);
			*Size = _atoi64(Buf);
			/* ここにbreakはない */

		case LIST_OS7_1 :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_ATTR);

			/* 日付 */
			FindField(Str, Buf, 1+offs, NO);
			sTime.wYear = Assume1900or2000(atoi(Buf));
			sTime.wMonth = atoi(Buf + 3);
			sTime.wDay = atoi(Buf + 6);

			/* 時刻 */
			FindField(Str, Buf, 2+offs, NO);
			sTime.wHour = atoi(Buf);
			sTime.wMinute = atoi(Buf+3);
			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* 属性 */
			FindField(Str, Buf, 0, NO);
			*Attr = AttrString2Value(Buf+1);

			/* 名前 */
			if(FindField(Str, Fname, 3+offs, YES) == FFFTP_SUCCESS)
			{
				RemoveTailingSpaces(Fname);
				Ret = NODE_FILE;
				if(strchr("dl", Buf[0]) != NULL)
					Ret = NODE_DIR;
			}
			break;

		case LIST_STRATUS :
			if(FindField(Str, Buf, 0, NO) != FFFTP_SUCCESS)
				break;
			if(_strnicmp(Buf, "Files:", 6) == 0)
				StratusMode = 0;
			else if(_strnicmp(Buf, "Dirs:", 5) == 0)
				StratusMode = 1;
			else if(_strnicmp(Buf, "Links:", 6) == 0)
				StratusMode = 2;
			else
			{
				if(StratusMode == 0)
					offs = 1;
				else if(StratusMode == 1)
					offs = 0;
				else
					break;

				*InfoExist |= (FINFO_TIME | FINFO_DATE);

				/* 日付 */
				if(FindField(Str, Buf, 2+offs, NO) != FFFTP_SUCCESS)
					break;
				sTime.wYear = Assume1900or2000(atoi(Buf));
				sTime.wMonth = atoi(Buf + 3);
				sTime.wDay = atoi(Buf + 6);

				/* 時刻 */
				if(FindField(Str, Buf, 3+offs, NO) != FFFTP_SUCCESS)
					break;
				sTime.wHour = atoi(Buf);
				sTime.wMinute = atoi(Buf+3);
				sTime.wSecond = 0;
				sTime.wMilliseconds = 0;
				SystemTimeToFileTime(&sTime, Time);
				SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

				/* 名前 */
				if(FindField(Str, Fname, 4+offs, YES) != FFFTP_SUCCESS)
					break;

				if(StratusMode == 0)
				{
					*InfoExist |= FINFO_SIZE;

					/* サイズ */
					if(FindField(Str, Buf, 1, NO) != FFFTP_SUCCESS)
						break;
					*Size = _atoi64(Buf) * 4096;

					/* 種類（オーナ名のフィールドにいれる） */
					if(FindField(Str, Buf, 2, NO) != FFFTP_SUCCESS)
						break;
					strncpy(Owner, Buf, OWNER_NAME_LEN);

					Ret = NODE_FILE;
				}
				else
					Ret = NODE_DIR;
			}
			break;

		case LIST_IRMX :
			*InfoExist |= (FINFO_DATE | FINFO_SIZE);

			/* 日付 */
			for(offs = 11; offs > 7; offs--)
			{
				if((err = FindField(Str, Buf, offs, NO)) == FFFTP_SUCCESS)
					break;
			}
			if(err != FFFTP_SUCCESS)
				break;
			if(IsDigit(*Buf) == 0)
				break;
			sTime.wYear = Assume1900or2000(atoi(Buf));
			if(FindField(Str, Buf, --offs, NO) != FFFTP_SUCCESS)
				break;
			GetMonth(Buf, &sTime.wMonth, &sTime.wDay);
			if(FindField(Str, Buf, --offs, NO) != FFFTP_SUCCESS)
				break;
			if(IsDigit(*Buf) == 0)
				break;
			sTime.wDay = atoi(Buf);
			sTime.wHour = 0;
			sTime.wMinute = 0;
			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* オーナ名 */
			if(FindField(Str, Buf, --offs, NO) != FFFTP_SUCCESS)
				break;
			strncpy(Owner, Buf, OWNER_NAME_LEN);

			/* サイズ */
			do
			{
				if((err = FindField(Str, Buf, --offs, NO)) != FFFTP_SUCCESS)
					break;
			}
			while(IsDigit(*Buf) == 0);
			--offs;
			if((err = FindField(Str, Buf, --offs, NO)) != FFFTP_SUCCESS)
				break;
			RemoveComma(Buf);
			*Size = _atoi64(Buf);
			if((err = FindField(Str, Buf, --offs, NO)) != FFFTP_SUCCESS)
				break;
			if(IsDigit(*Buf) == 0)
				break;
			/* 名前 */
			if(FindField(Str, Fname, 0, NO) != FFFTP_SUCCESS)
				break;
			/* 種類 */
			if(offs == 0)
				Ret = NODE_FILE;
			else
			{
				if((FindField(Str, Buf, 1, NO) == FFFTP_SUCCESS) &&
				   (strcmp(Buf, "DR") == 0))
					Ret = NODE_DIR;
				else
					Ret = NODE_FILE;
			}
			break;

		case LIST_ALLIED :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE);

			/* 日付 */
			FindField(Str, Buf, 3, NO);
			GetMonth(Buf, &sTime.wMonth, &sTime.wDay);	/* wDayは常に0 */
			FindField(Str, Buf, 4, NO);
			sTime.wDay = atoi(Buf);
			FindField(Str, Buf, 6, NO);
			sTime.wYear = atoi(Buf);

			/* 時刻 */
			FindField(Str, Buf, 5, NO);
			sTime.wHour = atoi(Buf);
			sTime.wMinute = atoi(Buf+3);
			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* サイズ */
			FindField(Str, Buf, 0, NO);
			*Size = _atoi64(Buf);

			/* 名前 */
			if(FindField(Str, Fname, 1, NO) == FFFTP_SUCCESS)
			{
				Ret = NODE_FILE;
				if(Buf[0] == '<')
					Ret = NODE_DIR;
			}
			break;

		case LIST_OS9 :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE);

			/* 日付 */
			FindField(Str, Buf, 1, NO);
			sTime.wYear = Assume1900or2000(atoi(Buf));
			sTime.wMonth = atoi(Buf + 3);
			sTime.wDay = atoi(Buf + 6);
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* 時刻 */
			FindField(Str, Buf, 2, NO);
			sTime.wHour = atoi_n(Buf, 2);
			sTime.wMinute = atoi(Buf+2);
			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* サイズ */
			FindField(Str, Buf, 5, NO);
			*Size = _atoi64(Buf);

			/* オーナ名 */
			FindField(Str, Buf, 0, NO);
			strncpy(Owner, Buf, OWNER_NAME_LEN);

			/* オーナ名 */
			FindField(Str, Buf, 3, NO);

			/* 名前 */
			if(FindField(Str, Fname, 6, NO) == FFFTP_SUCCESS)
			{
				if((Buf[0] == 'd') || (Buf[0] == 'D'))
					Ret = NODE_DIR;
				else
					Ret = NODE_FILE;
			}
			break;

		case LIST_IBM :
			*InfoExist |= FINFO_DATE;


			/* 日付 */
			FindField(Str, Buf, 2, NO);
			sTime.wYear = atoi(Buf);
			sTime.wMonth = atoi(Buf + 5);
			sTime.wDay = atoi(Buf + 8);
			sTime.wHour = 0;
			sTime.wMinute = 0;
			sTime.wSecond = 0;
			sTime.wMilliseconds = 0;
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* 名前 */
			FindField(Str, Buf, 8, NO);
			if(FindField(Str, Fname, 9, NO) == FFFTP_SUCCESS)
			{
				if(strcmp(Buf, "PO") == 0)
					Ret = NODE_DIR;
				else if(strcmp(Buf, "PS") == 0)
					Ret = NODE_FILE;
			}
			break;

		case LIST_AGILENT :
			*InfoExist |= (FINFO_SIZE | FINFO_ATTR);

			/* オーナ名 */
			FindField(Str, Buf, 2, NO);
			strncpy(Owner, Buf, OWNER_NAME_LEN);

			/* サイズ */
			FindField(Str, Buf, 4, NO);
			*Size = _atoi64(Buf);

			/* 属性 */
			FindField(Str, Buf, 0, NO);
			*Attr = AttrString2Value(Buf+1);

			/* 名前 */
			if(FindField(Str, Fname, 5, YES) == FFFTP_SUCCESS)
			{
				Ret = NODE_FILE;
				if(strchr("dl", Buf[0]) != NULL)
					Ret = NODE_DIR;
			}
			break;

		case LIST_SHIBASOKU :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE);

			/* サイズ */
			FindField(Str, Buf, 0, NO);
			if(IsDigit(Buf[0]))
			{
				*Size = _atoi64(Buf);

				/* 日付 */
				FindField(Str, Buf, 1, NO);
				Buf[3] = '\0';
				GetMonth(Buf, &sTime.wMonth, &sTime.wDay);
				sTime.wDay = atoi(Buf+4);
				sTime.wYear = atoi(Buf+7);

				/* 時刻 */
				FindField(Str, Buf, 2, NO);
				sTime.wHour = atoi(Buf);
				sTime.wMinute = atoi(Buf+3);
				sTime.wSecond = 0;
				sTime.wMilliseconds = 0;
				SystemTimeToFileTime(&sTime, Time);
				SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

				/* 名前 */
				FindField(Str, Fname, 3, NO);

				/* 種類 */
				Ret = NODE_FILE;
				if(FindField(Str, Buf, 4, NO) == FFFTP_SUCCESS)
				{
					if(strcmp(Buf, "<DIR>") == 0)
						Ret = NODE_DIR;
				}
			}
			break;

#if defined(HAVE_TANDEM)
		case LIST_TANDEM :
			*InfoExist |= (FINFO_TIME | FINFO_DATE | FINFO_SIZE | FINFO_ATTR);
			/* Open 中だったらずらす */
			if(FindField(Str, Buf, 1, NO) != FFFTP_SUCCESS)
				break;
			if (!strncmp(Buf, "O", 1)) {
				offs = 1;
			}
			/* 日付 */
			if(FindField(Str, Buf, 3 + offs, NO) != FFFTP_SUCCESS)
				break;
			if (Buf[1] == '-') {  /* 日付が 1桁 */
				sTime.wYear = Assume1900or2000(atoi(Buf + 6));
				Buf[5] = 0;
				GetMonth(Buf+2, &sTime.wMonth, &sTime.wDay);	/* wDayは常に0 */
				sTime.wDay = atoi(Buf);
				sTime.wDayOfWeek = 0;
			} else {
				sTime.wYear = Assume1900or2000(atoi(Buf + 7));
				Buf[6] = 0;
				GetMonth(Buf+3, &sTime.wMonth, &sTime.wDay);	/* wDayは常に0 */
				sTime.wDay = atoi(Buf);
				sTime.wDayOfWeek = 0;
			}
			/* 時刻 */
			FindField(Str, Buf, 4 + offs, NO);
			sTime.wHour = atoi(Buf);
			sTime.wMinute = atoi(Buf+3);
			sTime.wSecond = atoi(Buf+6);
			sTime.wMilliseconds = 0;
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* 属性 セキュリティではなく FileCode を保存する */
			FindField(Str, Buf, 1 + offs, NO);
			*Attr = atoi(Buf);
			/* サイズ */
			FindField(Str, Buf, 2 + offs, NO);
			*Size = _atoi64(Buf);
			/* オーナ名 */
			if(FindField(Str, Buf, 5 + offs, NO) == FFFTP_SUCCESS) {
				if(strncmp(Buf, "Owner", sizeof("Owner"))) {
					memset(Owner, NUL, OWNER_NAME_LEN+1);
					strncpy(Owner, Buf, OWNER_NAME_LEN);
					/* 通常は 255,255 だが、20, 33 などにも対応する */
					/* 最後の文字が , だったら後ろとつなげる */
					if (Buf[strlen(Buf)-1] == ',') {
						FindField(Str, Buf, 6 + offs, NO);
						strncat(Owner, Buf, OWNER_NAME_LEN - strlen(Buf));
					}
					/* ファイル名 */
					if(FindField(Str, Fname, 0, NO) == FFFTP_SUCCESS) {
						Ret = NODE_FILE;
					}
				}
			}
			break;
#endif

			// MLSD対応
			// 以下の形式に対応
			// fact1=value1;fact2=value2;fact3=value3; filename\r\n
			// 不完全な実装のホストが存在するため以下の形式も許容
			// fact1=value1;fact2=value2;fact3=value3 filename\r\n
			// fact1=value1;fact2=value2;fact3=value3;filename\r\n
			// SymlinkはRFC3659の7.7.4. A More Complex Exampleに
			// よるとtype=OS.unix=slink:(target)だが
			// ProFTPDはtype=OS.unix=symlink:(target)となる
		case LIST_MLSD:
			{
				int i = 0;
				char StrBuf[(FMAX_PATH * 2) + 1];
				char Fact[FMAX_PATH + 1];
				char Name[FMAX_PATH + 1];
				char Value[FMAX_PATH + 1];
				char Value2[FMAX_PATH + 1];
				char* pFileName;
				strncpy(StrBuf, Str, FMAX_PATH * 2);
				StrBuf[FMAX_PATH * 2] = '\0';
				if((pFileName = strstr(StrBuf, "; ")) != NULL)
				{
					*pFileName = '\0';
					pFileName += 2;
				}
				else if((pFileName = strchr(StrBuf, ' ')) != NULL)
				{
					*pFileName = '\0';
					pFileName++;
				}
				else if((pFileName = strrchr(StrBuf, ';')) != NULL)
				{
					*pFileName = '\0';
					pFileName++;
				}
				if(pFileName != NULL)
					strcpy(Fname, pFileName);
				while(FindField2(StrBuf, Fact, ';', i, NO) == FFFTP_SUCCESS)
				{
					if(FindField2(Fact, Name, '=', 0, NO) == FFFTP_SUCCESS && FindField2(Fact, Value, '=', 1, NO) == FFFTP_SUCCESS)
					{
						if(_stricmp(Name, "type") == 0)
						{
							if(_stricmp(Value, "dir") == 0)
								Ret = NODE_DIR;
							else if(_stricmp(Value, "file") == 0)
								Ret = NODE_FILE;
							else if(_stricmp(Value, "OS.unix") == 0)
								if(FindField2(Fact, Value2, '=', 2, NO) == FFFTP_SUCCESS)
									if(_stricmp(Value2, "symlink") == 0 || _stricmp(Value2, "slink") == 0) { // ProFTPD is symlink. A example of RFC3659 is slink.
										Ret = NODE_DIR;
										*Link = YES;
									}
						}
						else if(_stricmp(Name, "size") == 0)
						{
							*Size = _atoi64(Value);
							*InfoExist |= FINFO_SIZE;
						}
						else if(_stricmp(Name, "modify") == 0)
						{
							sTime.wYear = atoi_n(Value, 4);
							sTime.wMonth = atoi_n(Value + 4, 2);
							sTime.wDay = atoi_n(Value + 6, 2);
							sTime.wHour = atoi_n(Value + 8, 2);
							sTime.wMinute = atoi_n(Value + 10, 2);
							sTime.wSecond = atoi_n(Value + 12, 2);
							sTime.wMilliseconds = 0;
							SystemTimeToFileTime(&sTime, Time);
							// 時刻はGMT
//							SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());
							*InfoExist |= FINFO_DATE | FINFO_TIME;
						}
						else if(_stricmp(Name, "UNIX.mode") == 0)
						{
							*Attr = strtol(Value, NULL, 16);
							*InfoExist |= FINFO_ATTR;
						}
						else if(_stricmp(Name, "UNIX.owner") == 0)
							strcpy(Owner, Value);
					}
					i++;
				}
			}
			break;

		case LIST_UNIX_10 :
		case LIST_UNIX_11 :
		case LIST_UNIX_12 :
		case LIST_UNIX_13 :
		case LIST_UNIX_14 :
		case LIST_UNIX_15 :
		case LIST_UNIX_20 :
		case LIST_UNIX_21 :
		case LIST_UNIX_22 :
		case LIST_UNIX_23 :
		case LIST_UNIX_24 :
		case LIST_UNIX_25 :
		case LIST_UNIX_50 :
		case LIST_UNIX_51 :
		case LIST_UNIX_54 :
		case LIST_UNIX_60 :
		case LIST_UNIX_61 :
		case LIST_UNIX_62 :
		case LIST_UNIX_63 :
		case LIST_UNIX_64 :
		case LIST_UNIX_65 :
		case LIST_UNIX_70 :
		case LIST_UNIX_71 :
		case LIST_UNIX_72 :
		case LIST_UNIX_73 :
		case LIST_UNIX_74 :
		case LIST_UNIX_75 :
// MELCOMはビットフラグになっている
//		case LIST_MELCOM :
		// linux-ftpd
		case LIST_UNIX_16 :
		// uClinux
		case LIST_UNIX_17 :
		default:
			/* offsはサイズの位置, offs=0はカラム4 */
			offs = 0;
			if((ListType == LIST_UNIX_12) ||
			   (ListType == LIST_UNIX_13) ||
			   (ListType == LIST_UNIX_15) ||
			   (ListType == LIST_UNIX_20) ||
			   (ListType == LIST_UNIX_21) ||
			   (ListType == LIST_UNIX_24))
				offs = -1;

			if((ListType == LIST_UNIX_22) ||
			   (ListType == LIST_UNIX_23) ||
			   (ListType == LIST_UNIX_25) ||
			   (ListType == LIST_UNIX_50) ||
			   (ListType == LIST_UNIX_51) ||
			   (ListType == LIST_UNIX_54))
				offs = -2;

			if((ListType == LIST_UNIX_60) ||
			   (ListType == LIST_UNIX_61) ||
			   (ListType == LIST_UNIX_64))
				offs = 2;

			if((ListType == LIST_UNIX_62) ||
			   (ListType == LIST_UNIX_63) ||
			   (ListType == LIST_UNIX_65) ||
			   (ListType == LIST_UNIX_70) ||
			   (ListType == LIST_UNIX_71) ||
			   (ListType == LIST_UNIX_74))
				offs = 1;

			/* offs2は時間(もしくは年)の位置 */
			offs2 = 0;
			// linux-ftpd
//			if((ListType == LIST_UNIX_11) ||
//			   (ListType == LIST_UNIX_13) ||
//			   (ListType == LIST_UNIX_21) ||
//			   (ListType == LIST_UNIX_23) ||
//			   (ListType == LIST_UNIX_51) ||
//			   (ListType == LIST_UNIX_61) ||
//			   (ListType == LIST_UNIX_63) ||
//			   (ListType == LIST_UNIX_71) ||
//			   (ListType == LIST_UNIX_73))
			if((ListType == LIST_UNIX_11) ||
			   (ListType == LIST_UNIX_13) ||
			   (ListType == LIST_UNIX_21) ||
			   (ListType == LIST_UNIX_23) ||
			   (ListType == LIST_UNIX_51) ||
			   (ListType == LIST_UNIX_61) ||
			   (ListType == LIST_UNIX_63) ||
			   (ListType == LIST_UNIX_71) ||
			   (ListType == LIST_UNIX_73) ||
			   (ListType == LIST_UNIX_16))
				offs2 = -1;
			// uClinux
			if(ListType == LIST_UNIX_17)
				offs2 = -3;

			/* offs3はオーナ名の位置 */
			offs3 = 0;
			if((ListType == LIST_UNIX_12) ||
			   (ListType == LIST_UNIX_13) ||
			   (ListType == LIST_UNIX_15) ||
			   (ListType == LIST_UNIX_22) ||
			   (ListType == LIST_UNIX_23) ||
			   (ListType == LIST_UNIX_25) ||
			   (ListType == LIST_UNIX_50) ||
			   (ListType == LIST_UNIX_51) ||
			   (ListType == LIST_UNIX_62) ||
			   (ListType == LIST_UNIX_63) ||
			   (ListType == LIST_UNIX_65) ||
			   (ListType == LIST_UNIX_72) ||
			   (ListType == LIST_UNIX_73) ||
			   (ListType == LIST_UNIX_75))
				offs3 = -1;

			Flag2 = 0;
			if((ListType == LIST_UNIX_14) ||
			   (ListType == LIST_UNIX_15) ||
			   (ListType == LIST_UNIX_24) ||
			   (ListType == LIST_UNIX_25) ||
			   (ListType == LIST_UNIX_54) ||
			   (ListType == LIST_UNIX_64) ||
			   (ListType == LIST_UNIX_65) ||
			   (ListType == LIST_UNIX_74) ||
			   (ListType == LIST_UNIX_75))
				Flag2 = 1;
			// uClinux
			if(ListType == LIST_UNIX_17)
				Flag2 = -1;

			*InfoExist |= (FINFO_DATE | FINFO_SIZE | FINFO_ATTR);

			/* 属性 */
			FindField(Str, Buf, 0, NO);
			*Attr = AttrString2Value(Buf+1);

			/* オーナ名 */
			FindField(Str, Buf, 2+offs3, NO);
			strncpy(Owner, Buf, OWNER_NAME_LEN);

			/* サイズ */
			FindField(Str, Buf, 4+offs, NO);
			Pos = Buf;
			if((*Pos != NUL) && (IsDigit(*Pos) == 0))
			{
				Pos = strchr(Pos, NUL) - 1;
				for(; Pos > Buf; Pos--)
				{
					if(IsDigit(*Pos) == 0)
					{
						Pos++;
						break;
					}
				}
			}
			*Size = _atoi64(Pos);

			if(Flag2 == 0)
			{
				/* 時刻／日付 */
				GetLocalTime(&sTime);
				sTime.wSecond = 0;
				sTime.wMilliseconds = 0;

				FindField(Str, Buf, 5+offs, NO);
				/* 日付が yy/mm/dd の場合に対応 */
				if(GetYearMonthDay(Buf, &sTime.wYear, &sTime.wMonth, &sTime.wDay) == FFFTP_SUCCESS)
				{
					sTime.wYear = Assume1900or2000(sTime.wYear);

					FindField(Str, Buf, 7+offs+offs2, NO);
					if(GetHourAndMinute(Buf, &sTime.wHour, &sTime.wMinute) == FFFTP_SUCCESS)
						*InfoExist |= FINFO_TIME;
				}
				// linux-ftpd
				else if(CheckYYYYMMDDformat(Buf, NUL) != 0)
				{
					sTime.wYear = atoi(Buf);
					sTime.wMonth = atoi(Buf+5);
					sTime.wDay = atoi(Buf+8);
					FindField(Str, Buf, 7+offs+offs2, NO);
					if(GetHourAndMinute(Buf, &sTime.wHour, &sTime.wMinute) == FFFTP_SUCCESS)
						*InfoExist |= FINFO_TIME;
				}
				else
				{
					GetMonth(Buf, &sTime.wMonth, &sTime.wDay);
					if(offs2 == 0)
					{
						FindField(Str, Buf, 6+offs, NO);
						sTime.wDay = atoi(Buf);
					}

					FindField(Str, Buf, 7+offs+offs2, NO);
					if(GetHourAndMinute(Buf, &sTime.wHour, &sTime.wMinute) == FFFTP_FAIL)
					{
						sTime.wYear = atoi(Buf);
					}
					else
					{
						*InfoExist |= FINFO_TIME;

						/* 年がない */
						/* 現在の日付から推定 */
						/* 今年の今日以降のファイルは、実は去年のファイル */
						// UTCに変換して比較する
						SYSTEMTIME utcNow, utcTime;
						GetSystemTime(&utcNow);
						TIME_ZONE_INFORMATION tz{ AskHostTimeZone() * -60 };
						TzSpecificLocalTimeToSystemTime(&tz, &sTime, &utcTime);
						if (utcNow.wMonth < utcTime.wMonth || utcNow.wMonth == utcTime.wMonth && (utcNow.wDay < utcTime.wDay || utcNow.wDay == utcTime.wDay && utcNow.wHour < utcTime.wHour))
							sTime.wYear--;
					}
				}
			}
			// uClinux
			else if(Flag2 == -1)
			{
				*InfoExist &= ~(FINFO_DATE | FINFO_TIME);
			}
			else
			{
				/* LIST_UNIX_?4, LIST_UNIX_?5 の時 */
				FindField(Str, Buf, 5+offs, NO);
				sTime.wYear = atoi(Buf);
				FindField(Str, Buf, 6+offs, NO);
				sTime.wMonth = atoi(Buf);
				FindField(Str, Buf, 7+offs, NO);
				sTime.wDay = atoi(Buf);
				sTime.wHour = 0;
				sTime.wMinute = 0;
				sTime.wSecond = 0;
				sTime.wMilliseconds = 0;
			}
			SystemTimeToFileTime(&sTime, Time);
			SpecificLocalFileTime2FileTime(Time, AskHostTimeZone());

			/* 名前 */
			if(FindField(Str, Fname, 8+offs+offs2, YES) == FFFTP_SUCCESS)
			{
				Flag = 'B';
				if(OrgListType & LIST_MELCOM)
				{
					Flag = Fname[14];
					Fname[14] = NUL;
					RemoveTailingSpaces(Fname);
				}
				else
				{
					if((Pos = strstr(Fname, " -> ")) != NULL)
						*Pos = NUL;
				}

				if(strchr("dl", *Str) != NULL)
				{
					// 0x5Cが含まれる文字列を扱えないバグ修正
//					if((_mbscmp(_mbsninc(Fname, _mbslen(Fname) - 1), "/") == 0) ||
//					   (_mbscmp(_mbsninc(Fname, _mbslen(Fname) - 1), "\\") == 0))
//					{
//						*(Fname + strlen(Fname) - 1) = NUL;
//					}
					Ret = NODE_DIR;
					if(*Str == 'l')
						*Link = YES;
				}
				else if(strchr("-+f", *Str) != NULL)
					Ret = NODE_FILE;

				if((Ret == NODE_FILE) && (Flag != 'B'))
					Ret = NODE_NONE;
			}
			break;
	}

	return(Ret);
}


/*----- 指定の番号のフィールドを求める ----------------------------------------
*
*	Parameter
*		char *Str : 文字列
*		char *Buf : 文字列のコピー先
*		int Num : フィールド番号
*		int ToLast : 文字列の最後までコピー (YES/NO)
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL
*----------------------------------------------------------------------------*/

static int FindField(char *Str, char *Buf, int Num, int ToLast)
{
	char *Pos;
	int Sts;

	Sts = FFFTP_FAIL;
	*Buf = NUL;
	if(Num >= 0)
	{
		while(*Str == ' ')
			Str++;

		for(; Num > 0; Num--)
		{
			if((Str = strchr(Str, ' ')) != NULL)
			{
				while(*Str == ' ')
				{
					if(*Str == NUL)
					{
						Str = NULL;
						break;
					}
					Str++;
				}
			}
			else
				break;
		}
	}

	if(Str != NULL)
	{
		if((ToLast == YES) || ((Pos = strchr(Str, ' ')) == NULL))
			strcpy(Buf, Str);
		else
		{
			strncpy(Buf, Str, Pos - Str);
			*(Buf + (Pos - Str)) = NUL;
		}
		Sts = FFFTP_SUCCESS;
	}
	return(Sts);
}


// MLSD対応
static int FindField2(char *Str, char *Buf, char Separator, int Num, int ToLast)
{
	char *Pos;
	int Sts;

	Sts = FFFTP_FAIL;
	*Buf = NUL;
	if(Num >= 0)
	{
		while(*Str == Separator)
			Str++;

		for(; Num > 0; Num--)
		{
			if((Str = strchr(Str, Separator)) != NULL)
			{
				while(*Str == Separator)
				{
					if(*Str == NUL)
					{
						Str = NULL;
						break;
					}
					Str++;
				}
			}
			else
				break;
		}
	}

	if(Str != NULL)
	{
		if((ToLast == YES) || ((Pos = strchr(Str, Separator)) == NULL))
			strcpy(Buf, Str);
		else
		{
			strncpy(Buf, Str, Pos - Str);
			*(Buf + (Pos - Str)) = NUL;
		}
		Sts = FFFTP_SUCCESS;
	}
	return(Sts);
}


/*----- 文字列から月を求める --------------------------------------------------
*
*	Parameter
*		char *Str : 文字列
*		WORD *Month : 月 (0=月を表す文字列ではない)
*		WORD *Day : 日 (0=日は含まれていない)
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

static void GetMonth(char *Str, WORD *Month, WORD *Day)
{
	static const char DateStr[] = { "JanFebMarAprMayJunJulAugSepOctNovDec" };
	const char *Pos;

	*Month = 0;
	*Day = 0;

	if(IsDigit(*Str) == 0)
	{
		_strlwr(Str);
		*Str = toupper(*Str);
		if((Pos = strstr(DateStr, Str)) != NULL)
			*Month = ((WORD)(Pos - DateStr) / 3) + 1;
	}
	else
	{
		Pos = Str;
		while(*Pos != NUL)
		{
			if(!IsDigit(*Pos))
			{
				// "月"
				//   UTF-8        E6 9C 88
				//   Shift-JIS    8C 8E
				//   EUC-JP       B7 EE
				//   GB 2312      D4 C2
				//   ISO-2022-JP  37 6E
				//     JIS C 6226-1978  ESC $ @
				//     JIS X 0208-1983  ESC $ B
				//     JIS X 0208:1990  ESC $ B
				//     JIS X 0213:2000  ESC $ ( O
				//     JIS X 0213:2004  ESC $ ( Q
				//     ASCII            ESC ( B
				//     JIS C 6220-1976  ESC ( J
				static std::regex re{ R"(^(?:\xE6\x9C\x88|\x8C\x8E|\xB7\xEE|\xD4\xC2|\x1B\$(?:[@B]|\([OQ])\x37\x6E\x1B\([BJ]))" };
				if (std::cmatch m; std::regex_search(Pos, m, re)) {
					Pos += m.length();
					*Month = atoi(Str);
					if((*Month < 1) || (*Month > 12))
						*Month = 0;
					else
					{
						/* 「10月11日」のように日がくっついている事がある */
						if(*Pos != NUL)
						{
							*Day = atoi(Pos);
							if((*Day < 1) || (*Day > 31))
								*Day = 0;
						}
					}
				}
				else if(_mbsncmp((const unsigned char*)Pos, (const unsigned char*)"/", 1) == 0)
				{
					/* 「10/」のような日付を返すものがある */
					Pos += 1;
					*Month = atoi(Str);
					if((*Month < 1) || (*Month > 12))
						*Month = 0;
					else
					{
						/* 「10/11」のように日がくっついている事がある */
						if(*Pos != NUL)
						{
							*Day = atoi(Pos);
							if((*Day < 1) || (*Day > 31))
								*Day = 0;
						}
					}
				}
				break;
			}
			Pos++;
		}
	}
	return;
}


/*----- 文字列から年月日を求める ----------------------------------------------
*
*	Parameter
*		char *Str : 文字列
*		WORD *Year : 年
*		WORD *Month : 月
*		WORD *Day : 日
*
*	Return Value
*		int ステータス (FFFTP_SUCCESS/FFFTP_FAIL=日付を表す文字ではない)
*
*	Note
*		以下の形式をサポート
*			01/07/25
*		FFFTP_FAILを返す時は *Year = 0; *Month = 0; *Day = 0
*----------------------------------------------------------------------------*/
static int GetYearMonthDay(char *Str, WORD *Year, WORD *Month, WORD *Day)
{
	int Sts;

	Sts = FFFTP_FAIL;
	if(strlen(Str) == 8)
	{
		if(IsDigit(Str[0]) && IsDigit(Str[1]) && !IsDigit(Str[2]) &&
		   IsDigit(Str[3]) && IsDigit(Str[4]) && !IsDigit(Str[5]) &&
		   IsDigit(Str[6]) && IsDigit(Str[7]))
		{
			*Year = atoi(&Str[0]);
			*Month = atoi(&Str[3]);
			*Day = atoi(&Str[6]);
			Sts = FFFTP_SUCCESS;
		}
	}
	return(Sts);
}


/*----- 文字列から時刻を取り出す ----------------------------------------------
*
*	Parameter
*		char *Str : 文字列
*		WORD *Hour : 時
*		WORD *Minute : 分
*
*	Return Value
*		int ステータス (FFFTP_SUCCESS/FFFTP_FAIL=時刻を表す文字ではない)
*
*	Note
*		以下の形式をサポート
*			HH:MM
*			HH時MM分
*		FFFTP_FAILを返す時は *Hour = 0; *Minute = 0
*----------------------------------------------------------------------------*/

static int GetHourAndMinute(char *Str, WORD *Hour, WORD *Minute)
{
	int Ret;
	char *Pos;

	Ret = FFFTP_FAIL;
	if((_mbslen((const unsigned char*)Str) >= 3) && (isdigit(Str[0]) != 0))
	{
		*Hour = atoi(Str);
		if(*Hour <= 24)
		{
			if((Pos = (char*)_mbschr((const unsigned char*)Str, ':')) != NULL)
			{
				Pos++;
				if(IsDigit(*Pos) != 0)
				{
					*Minute = atoi(Pos);
					if(*Minute < 60)
						Ret = FFFTP_SUCCESS;
				}
			}
			else
			{
				Pos = Str;
				while(*Pos != NUL)
				{
					if(IsDigit(*Pos) == 0)
					{
						// "時"
						//   UTF-8        E6 99 82
						//   Shift-JIS    8E 9E
						//   EUC-JP       BB FE
						//   GB 2312      95 72
						//   ISO-2022-JP  3B 7E
						//     JIS C 6226-1978  ESC $ @
						//     JIS X 0208-1983  ESC $ B
						//     JIS X 0208:1990  ESC $ B
						//     JIS X 0213:2000  ESC $ ( O
						//     JIS X 0213:2004  ESC $ ( Q
						//     ASCII            ESC ( B
						//     JIS C 6220-1976  ESC ( J
						static std::regex re{ R"(^(?:\xE6\x99\x82|\x8E\x9E|\xBB\xFE|\x95\x72|\x1B\$(?:[@B]|\([OQ])\x3B\x7E\x1B\([BJ]))" };
						if (std::cmatch m; std::regex_search(Pos, m, re)) {
							Pos += m.length();
							if(*Pos != NUL)
							{
								*Minute = atoi(Pos);
								if(*Minute < 60)
									Ret = FFFTP_SUCCESS;
							}
						}
						break;
					}
					Pos++;
				}
			}
		}
	}
	else if((_stricmp(Str, "a:m") == 0) || (_stricmp(Str, "p:m") == 0))
	{
		*Hour = 0;
		*Minute = 0;
		Ret = FFFTP_SUCCESS;
	}

	if(Ret == FFFTP_FAIL)
	{
		*Hour = 0;
		*Minute = 0;
	}
	return(Ret);
}


/*----- VAX VMSの日付文字列から日付を取り出す ---------------------------------
*
*	Parameter
*		char *Str : 文字列
*		WORD *Year : 年
*		WORD *Month : 月
*		WORD *Day : 日
*
*	Return Value
*		int ステータス (FFFTP_SUCCESS/FFFTP_FAIL=日付を表す文字ではない)
*
*	Note
*		以下の形式をサポート
*			18-SEP-1998
*		FFFTP_FAILを返す時は *Year = 0; *Month = 0; *Day = 0
*----------------------------------------------------------------------------*/

static int GetVMSdate(char *Str, WORD *Year, WORD *Month, WORD *Day)
{
	char *Pos;
	int Ret;
	WORD Tmp;
	char Buf[4];

	Ret = FFFTP_FAIL;
	*Day = atoi(Str);
	if((Pos = strchr(Str, '-')) != NULL)
	{
		Pos++;
		strncpy(Buf, Pos, 3);
		Buf[3] = NUL;
		GetMonth(Buf, Month, &Tmp);
		if((Pos = strchr(Pos, '-')) != NULL)
		{
			Pos++;
			*Year = atoi(Pos);
			Ret = FFFTP_SUCCESS;
		}
	}

	if(Ret == FFFTP_FAIL)
	{
		*Year = 0;
		*Month = 0;
		*Day = 0;
	}
	return(Ret);
}


/*----- 1900年代か2000年代かを決める ------------------------------------------
*
*	Parameter
*		int Year : 年（２桁）
*
*	Return Value
*		int 年
*----------------------------------------------------------------------------*/

int Assume1900or2000(int Year)
{
	if(Year >= 60)
		Year += 1900;
	else
		Year += 2000;
	return(Year);
}


// フィルタに指定されたファイル名かどうかを返す
static int AskFilterStr(const char *Fname, int Type) {
	static std::wregex re{ L";" };
	if (Type != NODE_FILE || strlen(FilterStr) == 0)
		return YES;
	auto const wFname = u8(Fname), wFilterStr = u8(FilterStr);
	for (std::wsregex_token_iterator it{ begin(wFilterStr), end(wFilterStr), re, -1 }, end; it != end; ++it)
		if (it->matched && CheckFname(wFname, *it))
			return YES;
	return NO;
}


// フィルタを設定する
void SetFilter(int *CancelCheckWork) {
	struct Filter {
		using result_t = bool;
		INT_PTR OnInit(HWND hDlg) {
			SendDlgItemMessageW(hDlg, FILTER_STR, EM_LIMITTEXT, FILTER_EXT_LEN + 1, 0);
			SendDlgItemMessageW(hDlg, FILTER_STR, WM_SETTEXT, 0, (LPARAM)u8(FilterStr).c_str());
			return TRUE;
		}
		void OnCommand(HWND hDlg, WORD id) {
			switch (id) {
			case IDOK:
				strcpy(FilterStr, u8(GetText(hDlg, FILTER_STR)).c_str());
				EndDialog(hDlg, true);
				break;
			case IDCANCEL:
				EndDialog(hDlg, false);
				break;
			case FILTER_NOR:
				strcpy(FilterStr, "*");
				EndDialog(hDlg, true);
				break;
			case IDHELP:
				ShowHelp(IDH_HELP_TOPIC_0000021);
				break;
			}
		}
	};
	if (Dialog(GetFtpInst(), filter_dlg, GetMainHwnd(), Filter{})) {
		DispWindowTitle();
		UpdateStatusBar();
		GetLocalDirForWnd();
		GetRemoteDirForWnd(CACHE_LASTREAD, CancelCheckWork);
	}
}


static int atoi_n(const char *Str, int Len)
{
	char *Tmp;
	int Ret;

	Ret = 0;
	if((Tmp = (char*)malloc(Len+1)) != NULL)
	{
		memset(Tmp, 0, Len+1);
		strncpy(Tmp, Str, Len);
		Ret = atoi(Tmp);
		free(Tmp);
	}
	return(Ret);
}
