﻿/*=============================================================================
*
*								タスクウインドウ
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

#define TASK_BUFSIZE	(16*1024)
extern int ClientWidth;
extern int SepaWidth;
extern int ListHeight;
extern int TaskHeight;
extern HFONT ListFont;
extern int DebugConsole;
extern int RemoveOldLog;
static HWND hWndTask = NULL;
static std::mutex mutex;
static std::wstring queued;

static VOID CALLBACK Writer(HWND hwnd, UINT, UINT_PTR, DWORD) {
	std::unique_lock lock{ mutex };
	if (empty(queued))
		return;
	if (auto length = GetWindowTextLengthW(hwnd); RemoveOldLog == YES) {
		if (TASK_BUFSIZE <= size_as<int>(queued))
			SendMessageW(hwnd, EM_SETSEL, 0, -1);
		else {
			for (; TASK_BUFSIZE <= length + size_as<int>(queued); length = GetWindowTextLengthW(hwnd)) {
				SendMessageW(hwnd, EM_SETSEL, 0, SendMessageW(hwnd, EM_LINEINDEX, 1, 0));
				SendMessageW(hwnd, EM_REPLACESEL, false, (LPARAM)L"");
			}
			SendMessageW(hwnd, EM_SETSEL, length, length);
		}
	} else
		SendMessageW(hwnd, EM_SETSEL, length, length);
	SendMessageW(hwnd, EM_REPLACESEL, false, (LPARAM)queued.c_str());
	queued.clear();
}

// タスクウインドウを作成する
int MakeTaskWindow(HWND hWnd, HINSTANCE hInst) {
	constexpr DWORD style = WS_CHILD | WS_BORDER | ES_AUTOVSCROLL | WS_VSCROLL | ES_MULTILINE | ES_READONLY | WS_CLIPSIBLINGS;
	hWndTask = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, nullptr, style, 0, AskToolWinHeight() * 2 + ListHeight + SepaWidth, ClientWidth, TaskHeight, hWnd, 0, hInst, nullptr);
	if (hWndTask == NULL)
		return FFFTP_FAIL;

	SendMessageW(hWndTask, EM_LIMITTEXT, 0x7fffffff, 0);
	if (ListFont != NULL)
		SendMessageW(hWndTask, WM_SETFONT, (WPARAM)ListFont, MAKELPARAM(TRUE, 0));
	SetTimer(hWndTask, 1, USER_TIMER_MINIMUM, Writer);
	ShowWindow(hWndTask, SW_SHOW);
	return FFFTP_SUCCESS;
}


// タスクウインドウを削除
void DeleteTaskWindow() {
	DestroyWindow(hWndTask);
}


// タスクウインドウのウインドウハンドルを返す
HWND GetTaskWnd() {
	return hWndTask;
}


// タスクメッセージを表示する
// デバッグビルドではフォーマット済み文字列が渡される
void _SetTaskMsg(const char* format, ...) {
	char buffer[10240 + 3];
#ifdef _DEBUG
	strcpy(buffer, format);
	size_t result = strlen(buffer);
#else
	va_list args;
	va_start(args, format);
	int result = vsprintf(buffer, format, args);
	va_end(args);
#endif
	if (0 < result) {
		strcat(buffer, "\r\n");
		auto wbuffer = u8(buffer);
		std::unique_lock{ mutex };
		queued += wbuffer;
	}
}


// タスク内容をビューワで表示
void DispTaskMsg() {
	auto temp = tempDirectory() / L"_ffftp.tsk";
	if (auto text = u8(GetText(hWndTask)); std::ofstream{ temp, std::ofstream::binary }.write(data(text), size(text)).bad()) {
		fs::remove(temp);
		return;
	}
	auto path = temp.u8string();
	AddTempFileList(data(path));
	ExecViewer(data(path), 0);
}


// デバッグコンソールにメッセージを表示する
// デバッグビルドではフォーマット済み文字列が渡される
void _DoPrintf(const char* format, ...) {
	if (DebugConsole != YES)
		return;
#ifdef _DEBUG
	const char* buffer = format;
	size_t result = strlen(buffer);
#else
	char buffer[10240];
	va_list args;
	va_start(args, format);
	int result = vsprintf(buffer, format, args);
	va_end(args);
#endif
	if (0 < result)
		SetTaskMsg("## %s", buffer);
}
