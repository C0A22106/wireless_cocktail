
// WirelessMotion.h : PROJECT_NAME アプリケーションのメイン ヘッダー ファイルです
//

#pragma once

#ifndef __AFXWIN_H__
	#error "PCH に対してこのファイルをインクルードする前に 'pch.h' をインクルードしてください"
#endif

#include "resource.h"		// メイン シンボル


// CWirelessMotionApp:
// このクラスの実装については、WirelessMotion.cpp を参照してください
//

class CWirelessMotionApp : public CWinApp
{
public:
	CWirelessMotionApp();

// オーバーライド
public:
	virtual BOOL InitInstance();

// 実装

	DECLARE_MESSAGE_MAP()
};

extern CWirelessMotionApp theApp;
