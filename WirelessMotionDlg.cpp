
// WirelessMotionDlg.cpp : 実装ファイル
//

#include "pch.h"
#include "framework.h"
#include "WirelessMotion.h"
#include "WirelessMotionDlg.h"
#include "afxdialogex.h"

#include <mmeapi.h>
//#include "Interaction.cpp" // 独自MIDIの初期化のため

// ワイヤレス通信関連
#define MaxComPort	99							// 識別できるシリアルポート番号の限界
#define DefaultPort 4							// シリアルポート番号のデフォルト値

int RScomport = DefaultPort;					// default COM port #4

// MFC管理下にないグローバル変数の宣言
int rf_status; // ワイヤレス通信の実行状況を表す変数　0 ... 実行なし	1 ... 実行あり
int rf_firsttime; // パケットエラーの計数開始時のみ1になるフラフ
int rf_errcnt; // ワイヤレス通信におけるパケットエラーの累計数
int rf_interlock; // 描画時間が長い際にワイヤレス通信スレッドを優先実行するためのフラグ

//以下メトローノーム
#include <windows.h>
#include <time.h>
#include <mmeapi.h>
// CTime関連関数のために必要（？）
#include <mmsystem.h>
#pragma comment( lib, "winmm.lib")
int interval = 600; //メトロノームの周期(ミリ秒)
int pos = 100;
double bpm_buf[2][MAXDATASIZE];
HMIDIOUT hMIDI;	// グローバル変数としてMIDI音源オブジェクトを定義しておく 独自定義
DWORD dwTimerID; // マルチメディアタイマーの識別ID
BOOLEAN timer_GO;//タイマーが動いているかを表す。動いているとtrue

// メトロノームのマルチメディアタイマーのコールバック(追加分)
void CALLBACK timerFunc(UINT uID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
// この関数はMFCからはコントロールされていない
{
	midiOutShortMsg(hMIDI, 0x75c0);	// Roland GSコマンドによる音色設定　（メロディックタム音源）
	// 0x00 : アコースティックピアノ
	// 0x75 : メロディックタム

	midiOutShortMsg(hMIDI, 0x7f3c90); // 音程E (ミ）を演奏する
	Sleep(50);	// 0.1sec鳴らす
	midiOutShortMsg(hMIDI, 0x7f3c80); // 演奏を止める

	pos += 1;
	interval = 60000 / pos;

	if (timer_GO) {
	timeKillEvent(dwTimerID);
	dwTimerID = timeSetEvent(interval, 1, timerFunc, 0, TIME_PERIODIC);
	}
}

CString ParamFileName = _T("x26Settings.txt");	// テキストファイルにて下記の情報を入れておく
// シリアル通信ポートの番号
// 腕時計型デバイスのワイヤレス通信PANID番号 (0x2301 - 0x230f)
// ワイヤレス通信を行うチャンネル番号（0x0b - 0x1a)
// データファイル書き出し先のディレクトリ名
// 
// ※テキストエディタにて内容を変更できる

CString DefaultDir = _T("C:\\");				// データファイルを書き出す際のデフォルトディレクトリ
CString CurrentDir = _T("C:\\");				// Parameter Fileの内容で上書きされる

HANDLE	hCOMnd = INVALID_HANDLE_VALUE;			// 非同期シリアル通信用ファイルハンドル
HWND hDlg;										// ダイアログ自体のウィンドウハンドルを格納する変数
HANDLE serialh;									// 通信用スレッドのハンドル
OVERLAPPED rovl, wovl;							// 非同期シリアル通信用オーバーラップド構造体

#define PACKETSIZE 43							// ワイヤレス通信における１パケットあたりのデータバイト数（2024前期バージョン）
// position		content(s) byte(s)
// 0			PREAMBLE 1　プリアンブル(0x65) パケットの開始位置を識別するための固定パターンデータ
// 1			payload size 1	引き続くパケットのサイズ（バイト数）
// 2			SEQ 1　送信元にて設定される 0から255までを順次繰り返すカウント値：パケット欠損の検出に用いる
// 3 - 8		ax <1> ay <2> az <3> 4-byte x 3　３軸加速度データ（16bit整数表現）
// 9 - 14		wx <4> wy <5> wz <6> 4-byte x 3　３軸角速度データ（16bit整数表現）
// 15 - 20		e4x <7> e4y <8> e4z <9> 4-byte x3	姿勢角ベクトルe4（３次元ベクトル、direction cosine->16bit整数表現）
// 21			alpha <10> 1-byte　モーショントラッキング信頼係数（0から1までの数値を8ビット符号なし整数で表記）
// 22 - 33		reserved （ギター分析用データ：非公開）
// 34 - 39		ajx <15> ajy <16> ajz <17> 4-byte x3　角躍度（角加加速度）（float型、任意単位）
// 40 - 41		battery voltage L-H 2-byte　電池電圧（2バイト符号なし整数、任意単位）
// 42			checksum 1-byte　これまでのパケット内の数値を全て足し合わせた数値の下位8ビット（チェックサム）

#define PREAMBLE 0x65						// パケットの先頭を示すデータ（プリアンブル）

unsigned char wbuf[16];						// オーバーラップモードでの送信バッファはheapに置く必要がある
unsigned int rf_panid; // ワイヤレス通信のPAN_ID (Personal Area Network ID) 
unsigned int rf_ch; // ワイヤレス通信のチャンネル番号（CH = 0x0b to 0x1a）
unsigned char _panid[4], _ch[2];			// 設定値の16進数表記（char)

double databuf[DATASORT][MAXDATASIZE];		// 配列にセンサデータを格納する
// 0 ... seq
// 1, 2, 3 ... ax, ay, az ３軸加速度(G)
// 4, 5, 6 ... wx, wy, wz ３軸角速度(dps)
// 7, 8, 9 ... e4x, e4y, e4z 重力姿勢角ベクトル（direction cosine形式)
// 10 ... alpha (信頼度係数, 0から1までの浮動小数点数値）
// 11, 12 ... 前腕傾斜角θ, 前腕ひねり角φ（deg)
// 13, 14, 15 ... jx, jy, jz ３軸加加速度（jerk) (任意単位）
// 16, 17, 18 ... ajx, ajy, ajz ３軸角加加速度（angular jerk) (任意単位)

int datasize;			// サンプルデータの総個数（ワイヤレス受信時は現時点、ファイル読み込み時はファイルの全サイズ）


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


double deg2e(double deg)
// 角度(deg)からdirection cosine (方向余弦)への変換
{
	return sin(deg * (PI / 180.0));
}

double e2deg(double e)
// direction cosine (方向余弦）形式から角度（deg）への変換
{
	e = (e > 1.0) ? 1.0 : e;
	e = (e < -1.0) ? -1.0 : e;
	return (180.0 / PI) * asin(e);
}

static void CloseComPort(void)
// オープンしているシリアルポートをクローズする
{
	if (hCOMnd == INVALID_HANDLE_VALUE)
		return;
	CloseHandle(hCOMnd);
	hCOMnd = INVALID_HANDLE_VALUE;
}

static DWORD OpenComPortSync(int port)
// portにて指定した番号のシリアルポートを同期モードでオープンする
{
	CString ComPortNum;
	COMMPROP	myComProp;
	DCB	myDCB;
	COMSTAT	myComStat;

	_COMMTIMEOUTS myTimeOut;

	if ((port < 0) || (port > MaxComPort))
		return -1;
	if (port < 10) {
		ComPortNum.Format(_T("COM%d"), port);
	}
	else {
		ComPortNum.Format(_T("\\\\.\\COM%d"), port);	// Bill Gates' Magic ...
	}

	hCOMnd = CreateFile((LPCTSTR)ComPortNum, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hCOMnd == INVALID_HANDLE_VALUE) {
		return -1;
	}

	GetCommProperties(hCOMnd, &myComProp);
	GetCommState(hCOMnd, &myDCB);
	//	if( myComProp.dwSettableBaud & BAUD_128000)
	//		myDCB.BaudRate = CBR_128000;
	//	else
	myDCB.BaudRate = CBR_115200;		// 115.2KbpsモードをWindowsAPIは正しく認識しない
	//	myDCB.BaudRate = 460800;
	//	myDCB.BaudRate = CBR_9600;
	myDCB.fDtrControl = DTR_CONTROL_DISABLE;
	myDCB.Parity = NOPARITY;
	myDCB.ByteSize = 8;
	myDCB.StopBits = ONESTOPBIT;
	myDCB.fDsrSensitivity = FALSE;

	SetCommState(hCOMnd, &myDCB);
	DWORD	d;

	d = myComProp.dwMaxBaud;

	DWORD	myErrorMask;
	char	rbuf[32];
	DWORD	length;

	GetCommTimeouts(hCOMnd, &myTimeOut);
	myTimeOut.ReadTotalTimeoutConstant = 5000;	// 5sec
	myTimeOut.ReadIntervalTimeout = 200;	// 200 msec
	SetCommTimeouts(hCOMnd, &myTimeOut);
	//	GetCommTimeouts( hCOMnd, &myTimeOut);
	//	ReadTimeOut = (int)myTimeOut.ReadTotalTimeoutConstant;

	ClearCommError(hCOMnd, &myErrorMask, &myComStat);

	if (myComStat.cbInQue > 0) {
		int	cnt;
		cnt = (int)myComStat.cbInQue;
		for (int i = 0; i < cnt; i++) {
			// Synchronous IO
			ReadFile(hCOMnd, rbuf, 1, &length, NULL);
		}
	}

	return d;
}

static DWORD OpenComPort(int port)
// portにて指定した番号のシリアルポートをオープンする（非同期モード）
{
	CString ComPortNum;
	COMMPROP	myComProp;
	DCB	myDCB;
	COMSTAT	myComStat;

	// 非同期ＩＯモードなのでタイムアウトは無効
	//	_COMMTIMEOUTS myTimeOut;

	if ((port < 0) || (port > MaxComPort))
		return -1;
	if (port < 10) {
		ComPortNum.Format(_T("COM%d"), port);
	}
	else {
		ComPortNum.Format(_T("\\\\.\\COM%d"), port);	// Bill Gates' Magic ...
	}

	ZeroMemory(&rovl, sizeof(rovl));
	ZeroMemory(&wovl, sizeof(wovl));
	rovl.Offset = 0;
	wovl.Offset = 0;
	rovl.OffsetHigh = 0;
	wovl.OffsetHigh = 0;
	rovl.hEvent = NULL;
	wovl.hEvent = NULL;

	hCOMnd = CreateFile((LPCTSTR)ComPortNum, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
	// COMポートをオーバラップドモード（非同期通信モード）にてオープンしている

	if (hCOMnd == INVALID_HANDLE_VALUE) {
		return -1;
	}

	GetCommProperties(hCOMnd, &myComProp);
	GetCommState(hCOMnd, &myDCB);
	//	if( myComProp.dwSettableBaud & BAUD_128000)
	//		myDCB.BaudRate = CBR_128000;
	//	else
//	myDCB.BaudRate = CBR_115200;		// 115.2KbpsモードをWindowsAPIは正しく認識しない
	myDCB.BaudRate = 460800;	// Mar. 14, 2023
	//	myDCB.BaudRate = CBR_9600;
	myDCB.fDtrControl = DTR_CONTROL_DISABLE;
	myDCB.Parity = NOPARITY;
	myDCB.ByteSize = 8;
	myDCB.StopBits = ONESTOPBIT;
	myDCB.fDsrSensitivity = FALSE;
	SetCommState(hCOMnd, &myDCB);
	DWORD	d;

	d = myComProp.dwMaxBaud;

	DWORD	myErrorMask;
	char	rbuf[32];
	DWORD	length;

	// オーバーラップドモードでは、タイムアウト値は意味をなさない

	//	GetCommTimeouts( hCOMnd, &myTimeOut);
	//	myTimeOut.ReadTotalTimeoutConstant = 10;	// 10 msec
	//	myTimeOut.ReadIntervalTimeout = 200;	// 200 msec
	//	SetCommTimeouts( hCOMnd, &myTimeOut);
	//	GetCommTimeouts( hCOMnd, &myTimeOut);
	//	ReadTimeOut = (int)myTimeOut.ReadTotalTimeoutConstant;

	ClearCommError(hCOMnd, &myErrorMask, &myComStat);

	if (myComStat.cbInQue > 0) {
		int	cnt;
		cnt = (int)myComStat.cbInQue;
		for (int i = 0; i < cnt; i++) {
			// Synchronous IO
			//			ReadFile( hCOMnd, rbuf, 1, &length, NULL);		
			//

			// オーバーラップドモードで、同期通信的なことを行うためのパッチコード
			ReadFile(hCOMnd, rbuf, 1, &length, &rovl);
			while (1) {
				if (HasOverlappedIoCompleted(&rovl)) break;
			}
		}
	}

	return d;
}


// コールバック関数によるシリアル通信状況の通知処理

int rcomp;

VOID CALLBACK FileIOCompletionRoutine(DWORD err, DWORD n, LPOVERLAPPED ovl)
{
	rcomp = n;			// 読み込んだバイト数をそのままグローバル変数に返す
}

int wcomp;

VOID CALLBACK WriteIOCompletionRoutine(DWORD err, DWORD n, LPOVERLAPPED ovl)
{
	wcomp = n;
}

// 無線パケットを受信するためのスレッド
// CRL (Common Runtime Library)で書かれているため、直接MFCの管理下に置くことができない

#define RINGBUFSIZE 4096		// シリアル通信バッファと同じサイズにしておく：Sept. 17, 2022

// ワイヤレス通信が途中で止まる不具合を解消したコード　（2023.7.22付)
// 
// 一定時間パケットが受信されないと、受信スレッドを再起動する

unsigned __stdcall serialchk(VOID* dummy)
// スレッドとして起動するシリアル通信受信コード（Common Runtime Library版）
// CE4DMOTIONDlgのクラスメンバー関数ではないため、クラスのメンバー変数に直接アクセスできない
{
	DWORD myErrorMask;						// ClearCommError を使用するための変数
	COMSTAT	 myComStat;						// 受信バッファのデータバイト数を取得するために使用する
	unsigned char buf[RINGBUFSIZE];			// 無線パケットを受信するための一時バッファ

	unsigned char ringbuffer[RINGBUFSIZE];	// パケット解析用リングバッファ
	int rpos, wpos, rlen;					// リングバッファ用ポインタ（read, write)、データバイト数
	int rpos_tmp;							// データ解析用read位置ポインタ
	int rest;								// ClearCommErrorにて得られるデータバイト数を記録する

	int seq, expected_seq;					// モーションセンサから受信するシーケンス番号（８ビット）
	// と、その期待値（エラーがなければ受信する値）

	unsigned char packet[PACKETSIZE];		// パケット解析用バッファ （１パケット分）
	int i, j, chksum;
	short* s_p;								// char*からのポインタ変換用変数
	unsigned short* u_p;					// char*からのポインタ変換用変数
	int _packetsize;						// パケットのペイロードサイズ
	int rcv_message;
	int timeout1;							// 最初の１バイトを読み込む受信スレッドのタイムアウト回数

	double _e4x, _e4y, _e4z;				// e4ベクトル（重力方位ベクトル）の値（direction cosine形式）

	expected_seq = 0;						// 最初に受信されるべきSEQの値はゼロ
	rpos = wpos = 0;						// リングバッファの読み出し及び書き込みポインタを初期化
	rlen = 0;								// リングバッファに残っている有効なデータ数（バイト）
	timeout1 = 0;

	while (rf_status) {
		rcomp = 0;					// FileIOCompletionRoutineが返す受信バイト数をクリアしておく

		// まずは無線パケットの先頭の１バイトを読み出しに行く
		ReadFileEx(hCOMnd, buf, 1, &rovl, FileIOCompletionRoutine);

		while (1) {
			SleepEx(100, TRUE);	// 最大で100ミリ秒 = 0.1秒の間スリープするが、その間に
			if (rcomp == 1) break;	// I/O完了コールバック関数が実行されるとスリープを解除する

			if (!rf_status) {				// 外部プログラムからスレッドの停止を指示された時の処理
				CancelIo(hCOMnd);	// 発行済みのReadFileEx操作を取り消しておく
				break;
			}
			// データが送られてこない時間帯では、受信スレッド内のこの部分
			// が延々と処理されているが、大半がSleepExの実行に費やされる
			// ことで、システムに与える負荷を軽減している。
			timeout1++;
			if (timeout1 >= 10) {
				timeout1 = 0;
				CancelIo(hCOMnd);
				ReadFileEx(hCOMnd, buf, 1, &rovl, FileIOCompletionRoutine); // もう一度ReadFileExを発行する：Jul. 22, 2023
			}
		}

		if (!rf_status) break;				// 外部プログラムからスレッドの停止を指示された

		ringbuffer[wpos] = buf[0];	// 最初の１バイトが受信された
		wpos++;						// リングバッファの書き込み位置ポインタを更新
		wpos = (wpos >= RINGBUFSIZE) ? 0 : wpos;	// １周していたらポインタをゼロに戻す（なのでRING）
		rlen++;						// リングバッファ内の有効なデータ数を＋１する

		ClearCommError(hCOMnd, &myErrorMask, &myComStat);	// 受信バッファの状況を調べるＡＰＩ

		rest = myComStat.cbInQue;	// 受信バッファに入っているデータバイト数が得られる

		if (rest == 0) continue;		// 何も入っていなかったので次の１バイトを待ちにいく

		if (rest > 1024) rest = 1024; // 一度に読み込むデータ量を1024バイトに制限する Sept. 17, 2022

		rcomp = 0;
		ReadFileEx(hCOMnd, buf, rest, &rovl, FileIOCompletionRoutine);
		// 受信バッファに入っているデータを読み出す
		// 原理的にはrestで示される数のデータを受信することができるが、
		// 万一に備えてデータが不足してしまった時の処理を考える。

		//SleepEx(16, TRUE);			// Windowsにおけるシリアルポートの標準レイテンシ（16msec）だけ待つ
		SleepEx(2, TRUE);				// シリアルポートのレイテンシを1msecに変更したため、待ち時間を短縮する：Jun. 1, 2024
		if (rcomp != rest) {
			CancelIo(hCOMnd);		// ClearCommErrorで取得したデータバイト数に満たない
		}							// データしか受信されなかったので、先に発行したReadFileEx
									// をキャンセルしている

		i = 0;
		while (rcomp > 0) {			// rcompには読み出すことのできたデータのバイト数が入っている
			ringbuffer[wpos] = buf[i];	// リングバッファに受信データを転送する
			wpos++;						// リングバッファ書き込み位置の更新
			wpos = (wpos >= RINGBUFSIZE) ? 0 : wpos;
			rlen++;
			i++;
			rcomp--;
		}

		// ここからパケット解析に入る

		while (1) {					// 有効なパケットである限り解析を継続する
			while (rlen > 0) {
				if (ringbuffer[rpos] == PREAMBLE) break;
				rpos++;								// 先頭がPREAMBLEではなかった
				rpos = (rpos >= RINGBUFSIZE) ? 0 : rpos;
				rlen--;								// 有効なデータ数を１つ減らして再度先頭を調べる
			}

			if (rlen == 0) break; // 解析に必要なデータを受信できていない
			rpos_tmp = rpos + 1;
			rpos_tmp = (rpos_tmp >= RINGBUFSIZE) ? 0 : rpos_tmp;

			_packetsize = ringbuffer[rpos_tmp];	// ペイロードサイズを読み出す
			if ((_packetsize != 2) && (_packetsize != 41)) { // 正しいサイズのペイロードではない
				// 正しくないPreambleとペイロード長のデータを破棄する
				rpos++;
				rpos = (rpos >= RINGBUFSIZE) ? 0 : rpos;
				rlen--;
				rpos++;
				rpos = (rpos >= RINGBUFSIZE) ? 0 : rpos;
				rlen--;
				break;
			}

			if (rlen < (_packetsize + 2)) break;
			// 解析に必要なデータバイト数に達していなかったので最初の１バイトを待つ処理に戻る

			rpos_tmp = rpos;	// リングバッファを検証するための仮ポインタ
			// まだリングバッファ上にあるデータが有効であると分かったわけではない

			for (i = 0, chksum = 0; i < (_packetsize + 1); i++) {
				packet[i] = ringbuffer[rpos_tmp];	// とりあえず解析用バッファにデータを整列させる
				chksum += packet[i];
				rpos_tmp++;
				rpos_tmp = (rpos_tmp >= RINGBUFSIZE) ? 0 : rpos_tmp;
			}

			if ((chksum & 0xff) != ringbuffer[rpos_tmp]) {	// チェックサムエラーなのでパケットは無効
				rpos++;										// 先頭の１バイトを放棄する
				rpos = (rpos >= RINGBUFSIZE) ? 0 : rpos;
				rlen--;
				continue;	// 次のPREAMBLEを探しにいく
			}

			// PREAMBLE、チェックサムの値が正しいサイズのデータがpacket[]に入っている

			if (_packetsize == 41) {
				seq = packet[2];
				databuf[0][datasize] = (double)seq;		// seq は８ビットにて0～255の間を1ずつカウントアップしていく数値

				if (rf_firsttime) {					// 最初に受信するパケットではパケットエラーとしてカウントしない
					rf_firsttime = 0;
				}
				else {
					if (seq != expected_seq) {		// 受信されたseqが、１つ前のseqに+1したものであることをチェックする
						rf_errcnt += (seq + 256 - expected_seq) % 256;	// パケットエラー数を更新する
					}
				}
				expected_seq = (seq + 1) % 256;					// 次のseqの期待値をexpected_seqに入れる

				// データ抽出
				for (j = 0; j < 3; j++) { // ax, ay, az
					s_p = (short*)&packet[j * 2 + 3]; // 3, 5, 7
					databuf[j + 1][datasize] = (double)(*s_p) * (16.0 / 30000.0); // 1, 2, 3
				}

				for (j = 0; j < 3; j++) { // gx, gy, gz
					s_p = (short*)&packet[j * 2 + 9]; // 9, 11, 13
					databuf[j + 4][datasize] = (double)(*s_p) * (2000.0 / 30000.0); // 4, 5, 6
				}

				for (j = 0; j < 3; j++) { // e4x, e4y, e4z
					s_p = (short*)&packet[j * 2 + 15]; // 15, 17, 19
					databuf[j + 7][datasize] = (double)(*s_p) * (1.0 / 30000.0); // 7, 8, 9
				}

				_e4x = databuf[7][datasize];
				_e4y = databuf[8][datasize];
				_e4z = databuf[9][datasize];

				databuf[10][datasize] = (double)packet[21] / 255.0; // モーショントラッキング信頼係数 alpha (0 to 1)

				// e4zをx軸、e4yをy軸と見なした際の偏角を　(-PI, PI)の範囲で計算し、単位をラジアンから度に変換する
				// guitar mode (-90.0 ~ +270.0)　手首が回外側へ90度回った状態を原点とする(ギター演奏用の設定)

				databuf[11][datasize] = e2deg(_e4x);	// x軸の対水平面傾斜角（単位：deg）
				databuf[12][datasize] = (180.0 / PI) * atan2(_e4y, _e4z) + 90.0;	// y軸の対水平面傾斜角（単位：deg）

				// dummy data store
				databuf[13][datasize] = 0.0;
				databuf[14][datasize] = 0.0;
				databuf[15][datasize] = 0.0;

				bpm_buf[0][datasize] = pos; // そのときのbpmを格納

				for (j = 0; j < 3; j++) { // ajx, ajy, ajz
					u_p = (unsigned short*)&packet[j * 2 + 34]; // 34, 36, 38
					databuf[j + 16][datasize] = (double)(*u_p) * 256.0; // 16, 17, 18
				}

				// (double)((int)packet[40] | ((int)packet[41] << 8)) * (133.0 * 3.0 / (33.0 * 4095.0)); // battery voltage (int)
				// バッテリーの電圧値を計算

				datasize++;
				datasize = (datasize >= MAXDATASIZE) ? (MAXDATASIZE - 1) : datasize;	// データ数が限界に到達した際の処理
				rcv_message = 1;
			}
			else {
				rcv_message = 2; // 2バイト（ペイロード）長のコマンド用パケット
			}

			if (rcv_message == 1) {
				// 有効なパケットを受信しており、かつデータ受信バッファに一定量以上のデータが入っていない
				// 速度が遅いPCにて処理リソースをデータ処理に独占されてしまわないための処理

				if (rf_interlock == 0) {
					PostMessage(hDlg, WM_RCV, (WPARAM)NULL, (WPARAM)NULL); // 描画スレッドにメッセージを送出
					rf_interlock = 5;	// 描画メッセージを送出した後に、描画が続いていると次のメッセージを送出しないようにする
				}
				else {
					rf_interlock--; // こうしておかないと描画スレッドを起動できない場合にデッドロックしてしまう
					// June 2, 2023
				}
			}

			rpos = (rpos + (_packetsize + 2)) % RINGBUFSIZE;		// 正しく読み出せたデータをリングバッファから除去する
			rlen -= (_packetsize + 2);								// バッファの残り容量を更新する
		}
	}
	_endthreadex(0);	// スレッドを消滅させる（原理上このコードが実行されることはない）
	return 0;
}

static unsigned char num2hexchar(int n)
{
	if ((n < 0) || (n > 16)) {
		return 0; // invalid
	}

	if (n < 10) {
		return '0' + n;
	}

	return 'a' + (n - 10);
}

static void wireless_setting(void)
{
	// panid -> _panid[], ch -> _ch[]

	_panid[0] = num2hexchar(rf_panid >> 12);
	_panid[1] = num2hexchar((rf_panid & 0x0f00) >> 8);
	_panid[2] = num2hexchar((rf_panid & 0x00f0) >> 4);
	_panid[3] = num2hexchar(rf_panid & 0x0f);

	_ch[0] = num2hexchar(rf_ch >> 4);
	_ch[1] = num2hexchar(rf_ch & 0x0f);
}


void CWirelessMotionDlg::Initialize(void)
{
	// 設定ファイルを読み込み、シリアル通信用ポート番号, RFデバイスPANIDとファイル保存先フォルダを設定する
	CStdioFile pFile;
	TCHAR buf[256];
	TCHAR* bufend;
	unsigned int comport;
	CString sbuf;

	comport = 4;
	rf_panid = 0x0000;
	rf_ch = 0x0d;
	DefaultDir = _T("c:\\");

	if (!pFile.Open(ParamFileName, CFile::modeRead)) {
		msgED.SetWindowTextW(_T("設定ファイルx26RTM.txtが見つかりません"));
	}
	else {
		// 設定ファイル内容
		// シリアルポート番号
		// ワイヤレス通信PANID
		// ワイヤレス通信CH
		// データ保存用フォルダパス

		while (1) { // 設定ファイルの解析が途中で失敗する場合に備えて無限ループ+脱出のコードとしている
			if (pFile.ReadString(buf, 256) == NULL) break;
			comport = ::_ttoi(buf);
			if (pFile.ReadString(buf, 256) == NULL) break;
			rf_panid = ::_tcstol(buf, &bufend, 16);
			if (pFile.ReadString(sbuf) == NULL) break;
			swscanf_s(sbuf, _T("%x"), &rf_ch);
			if (pFile.ReadString(sbuf) == NULL) break;
			DefaultDir = sbuf;
			break;
		}
		pFile.Close();
	}

	if ((comport > 0) && (comport <= MaxComPort)) {
		RScomport = comport;
	}
	else {
		RScomport = DefaultPort;
	}

	wireless_setting();

	if (rf_panid == 0x0000) {
		sbuf.Format(_T("Port = %d default Panid Ch = %c%c"), comport, _ch[0], _ch[1]);
	}
	else {
		sbuf.Format(_T("Port = %d Panid = %c%c%c%c Ch = %c%c"), comport, _panid[0], _panid[1], _panid[2], _panid[3], _ch[0], _ch[1]);
	}
	sbuf = sbuf + _T(" dir = ") + DefaultDir;
	msgED.SetWindowTextW(sbuf);
}

// アプリケーションのバージョン情報に使われる CAboutDlg ダイアログ

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// ダイアログ データ
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート

// 実装
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CWirelessMotionDlg ダイアログ


CWirelessMotionDlg::CWirelessMotionDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_WIRELESSMOTION_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CWirelessMotionDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT1, msgED);
	DDX_Control(pDX, IDC_PICT1, mPICT1);
	DDX_Control(pDX, IDC_PICT2, mPICT2);
	DDX_Control(pDX, IDC_EDIT4, msgED2);
	DDX_Control(pDX, IDC_EDIT5, msgED3);
	DDX_Control(pDX, IDC_EDIT3, msgED4);
}

BEGIN_MESSAGE_MAP(CWirelessMotionDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_MESSAGE(WM_RCV, &CWirelessMotionDlg::OnMessageRCV) // 手入力でWM_RCVメッセージで起動する関数へのポインタを登録している
	ON_BN_CLICKED(IDC_BUTTON1, &CWirelessMotionDlg::OnBnClickedButton1)
	ON_BN_CLICKED(IDC_BUTTON2, &CWirelessMotionDlg::OnBnClickedButton2)
	ON_BN_CLICKED(IDC_BUTTON3, &CWirelessMotionDlg::OnBnClickedButton3)
	ON_BN_CLICKED(IDC_BUTTON4, &CWirelessMotionDlg::OnBnClickedButton4)
	ON_BN_CLICKED(IDC_BUTTON5, &CWirelessMotionDlg::OnBnClickedButton5)
	ON_BN_CLICKED(IDC_BUTTON6, &CWirelessMotionDlg::OnBnClickedButton6)
	ON_BN_CLICKED(IDC_BUTTON7, &CWirelessMotionDlg::OnBnClickedButton7)
	ON_WM_TIMER()
END_MESSAGE_MAP()


// CWirelessMotionDlg メッセージ ハンドラー

BOOL CWirelessMotionDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	hDlg = this->m_hWnd;	// このダイアログへのハンドルを取得する
	// このコードは手入力している

	// "バージョン情報..." メニューをシステム メニューに追加します。

	// IDM_ABOUTBOX は、システム コマンドの範囲内になければなりません。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// このダイアログのアイコンを設定します。アプリケーションのメイン ウィンドウがダイアログでない場合、
	//  Framework は、この設定を自動的に行います。
	SetIcon(m_hIcon, TRUE);			// 大きいアイコンの設定
	SetIcon(m_hIcon, FALSE);		// 小さいアイコンの設定

	// TODO: 初期化をここに追加します。

	rf_status = 0; // ワイヤレス通信の実行状況を表す変数　0 ... 実行なし	1 ... 実行あり
	rf_firsttime = 1; // パケットエラーの計数開始時のみ1になるフラフ
	rf_errcnt = 0; // ワイヤレス通信におけるパケットエラーの累計数
	rf_interlock = 0; // 描画時間が長い際にワイヤレス通信スレッドを優先実行するためのフラグ
	datasize = 0; // モーションデータのカウンタを初期化
	datapoint = 0; // グラフ描画開始データ番号を初期化
	graphspan = 64; // 32Hz, 2sec
	play_status = 0; // ファイルからのグラフ描画再生は停止中
	tmID = NULL; // タイマーは無効

	//以下独自
	midiOutOpen(&hMIDI, MIDI_MAPPER, 0, 0, 0); // ここでMIDIデバイスを初期化することで遅延を軽減する

	Initialize();

	return TRUE;  // フォーカスをコントロールに設定した場合を除き、TRUE を返します。
}

void CWirelessMotionDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// ダイアログに最小化ボタンを追加する場合、アイコンを描画するための
//  下のコードが必要です。ドキュメント/ビュー モデルを使う MFC アプリケーションの場合、
//  これは、Framework によって自動的に設定されます。

void CWirelessMotionDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 描画のデバイス コンテキスト

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// クライアントの四角形領域内の中央
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// アイコンの描画
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// ユーザーが最小化したウィンドウをドラッグしているときに表示するカーソルを取得するために、
//  システムがこの関数を呼び出します。
HCURSOR CWirelessMotionDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CWirelessMotionDlg::OnBnClickedButton1()
{
	// ワイヤレス設定
	// RF Setup
	// ワイヤレス通信モジュール（PCにUSB経由で接続されている）の通信設定を行う
	// 通信速度をデフォルトの115200bpsから460800bpsに上げる
	// ワイヤレス通信モジュールの周波数チャンネル(CH)とネットワークID(PAN_ID)を変更する
	// デフォルトでは CH = 0x0d, PAN_ID = 0x6502に設定されている
	// これらの通信設定値はワイヤレス通信モジュールをPCから外すとデフォルト値にリセットされる

	CString s;
	unsigned char tbuf[256], rbuf[256];
	DWORD len;

	OpenComPortSync(RScomport);

	tbuf[0] = '+';
	tbuf[1] = '+';
	tbuf[2] = '+';
	// 無線通信モジュールをATコマンド（設定値変更）モードに切り替える

	WriteFile(hCOMnd, tbuf, 3, &len, NULL);

	ReadFile(hCOMnd, rbuf, 3, &len, NULL);
	if (len != 3) {
		msgED.SetWindowTextW(_T("RF module not responded..."));
		return;
	}

	if ((rbuf[0] != 'O') || (rbuf[1] != 'K') || (rbuf[2] != 0x0d)) {
		msgED.SetWindowTextW(_T("Illegal response from RF module..."));
		return;
	}

	if (rf_panid != 0x0000) {
		tbuf[0] = 'A';
		tbuf[1] = 'T';
		tbuf[2] = 'I';
		tbuf[3] = 'D';
		tbuf[4] = _panid[0];
		tbuf[5] = _panid[1];
		tbuf[6] = _panid[2];
		tbuf[7] = _panid[3];
		tbuf[8] = 0x0d;
		// 無線モジュールのPANIDを設定する

		WriteFile(hCOMnd, tbuf, 9, &len, NULL);

		ReadFile(hCOMnd, rbuf, 3, &len, NULL);
		if (len != 3) {
			msgED.SetWindowTextW(_T("RF module not responded..."));
			return;
		}
	}

	tbuf[0] = 'A';
	tbuf[1] = 'T';
	tbuf[2] = 'C';
	tbuf[3] = 'H';
	tbuf[4] = _ch[0];
	tbuf[5] = _ch[1];
	tbuf[6] = 0x0d;
	// 無線モジュールの周波数チャンネル（0x0b - 0x1a）を設定する

	WriteFile(hCOMnd, tbuf, 7, &len, NULL);

	ReadFile(hCOMnd, rbuf, 3, &len, NULL);
	if (len != 3) {
		msgED.SetWindowTextW(_T("RF module not responded..."));
		return;
	}

	if ((rbuf[0] != 'O') || (rbuf[1] != 'K') || (rbuf[2] != 0x0d)) {
		msgED.SetWindowTextW(_T("Illegal response from RF module..."));
		return;
	}

	tbuf[0] = 'A';
	tbuf[1] = 'T';
	tbuf[2] = 'B';
	tbuf[3] = 'D';
	tbuf[4] = '9';
	tbuf[5] = 0x0d;	// 460.8kbps
	// 無線モジュールのシリアル通信速度を460.8kbpsに設定する：スピードグレード 9番
	// デフォルト値は7番（115.2kbps)に設定されている

	WriteFile(hCOMnd, tbuf, 6, &len, NULL);

	ReadFile(hCOMnd, rbuf, 3, &len, NULL);
	if (len != 3) {
		msgED.SetWindowTextW(_T("RF module not responded..."));
		return;
	}

	if ((rbuf[0] != 'O') || (rbuf[1] != 'K') || (rbuf[2] != 0x0d)) {
		msgED.SetWindowTextW(_T("Illegal response from RF module..."));
		return;
	}

	tbuf[0] = 'A';
	tbuf[1] = 'T';
	tbuf[2] = 'C';
	tbuf[3] = 'N';
	tbuf[4] = 0x0d;
	// このコマンドを実行すると、ここまでの設定値変更が全て反映される

	WriteFile(hCOMnd, tbuf, 5, &len, NULL);

	ReadFile(hCOMnd, rbuf, 3, &len, NULL);
	if (len != 3) {
		msgED.SetWindowTextW(_T("RF module not responded..."));
		return;
	}

	if ((rbuf[0] != 'O') || (rbuf[1] != 'K') || (rbuf[2] != 0x0d)) {
		msgED.SetWindowTextW(_T("Illegal response from RF module..."));
		return;
	}
	// このOKが受信された後は、通信速度等の設定が変更される
	// このため、一旦シリアル通信ポートをクローズし、新しい設定値で再オープンする必要がある

	msgED.SetWindowTextW(_T("RF module configured"));

	CloseComPort();
}

void CWirelessMotionDlg::OnBnClickedButton2()
{
	// ワイヤレス接続
	// CONNECT
	// x26SRFデバイスとの通信を開始する

	if (play_status != 0) return;

	DWORD d;
	unsigned char c;

	wbuf[0] = 0x65; // Preamble
	wbuf[1] = 0x02;	// payload length

	c = 'A'; // 32Hz, 43byte data packet mode
	wbuf[2] = c;
	wbuf[3] = 0x65 + 0x02 + c;

	if (rf_status) {
		// 既にワイヤレス通信が有効化されていたので、x25RFデバイスへのコマンド伝送のみを行う
		WriteFileEx(hCOMnd, wbuf, 4, &wovl, WriteIOCompletionRoutine);
		// オーバーラップ（非同期）モードでコマンドを送出する
		return;
	}

	d = OpenComPort(RScomport);	// シリアル通信ポートをオープンする

	if (d < 0) {
		msgED.SetWindowTextW(_T("can't initialize COM port"));
		Invalidate();
		UpdateWindow();
		return;
	}

	rf_status = 1;
	rf_errcnt = 0;

	unsigned int tid;
	serialh = (HANDLE)_beginthreadex(NULL, 0, serialchk, NULL, 0, &tid);
	// ワイヤレス通信受信スレッドを起動する

	if (serialh != NULL) {	// 受信スレッドの起動に成功したので、x25RFデバイスに計測開始コマンドを送る
		msgED.SetWindowTextW(_T("RF Start"));

		WriteFileEx(hCOMnd, wbuf, 4, &wovl, WriteIOCompletionRoutine);
		// オーバーラップモードでコマンドを送信する
		rf_interlock = 0;
	}
	else {
		// 受信スレッドの起動を行えていなかったため、通信ポートをクローズする
		rf_status = 0;
		CloseComPort();
		msgED.SetWindowTextW(_T("Wireless Communication Thread is not running..."));
	}

	interval = 600;
	pos = 100;
	timer_GO = 1;
	dwTimerID = timeSetEvent(interval, 1, timerFunc, 0, TIME_PERIODIC); //メトロノームのタイマーを開始
}

void CWirelessMotionDlg::OnBnClickedButton3()
{
	// デバイスOFF
	// Power OFF
	// x26SRFデバイスとの通信を終了してデバイスの電源を切る

	if (rf_status) {
		wbuf[0] = 0x65; // Preamble
		wbuf[1] = 0x02; // device #
		wbuf[2] = 0xfe;	// stop & power-off command
		wbuf[3] = 0x65; // check sum

		WriteFileEx(hCOMnd, wbuf, 4, &wovl, WriteIOCompletionRoutine);
		// x25RFデバイスにワイヤレス計測停止コマンドを送る
	}
	else {
		// ワイヤレスデバイスがSTOPコマンド受信に失敗している可能性があるため
		// 停止コマンドを再送出する

		msgED.SetWindowTextW(_T("Recording is not running..."));

		OpenComPort(RScomport);

		wbuf[0] = 0x65; // Preamble
		wbuf[1] = 0x02; // payload length
		wbuf[2] = 0xfe;	// stop command
		wbuf[3] = 0x65; // check sum

		WriteFileEx(hCOMnd, wbuf, 4, &wovl, WriteIOCompletionRoutine);
		CloseComPort();
	}
}

void CWirelessMotionDlg::OnBnClickedButton4()
{
	// ワイヤレス解除
	// DISCONNECT
	// x26SRFデバイスとの通信を終了する

	if (rf_status) {
		rf_status = 0;
		rf_firsttime = 1;

		wbuf[0] = 0x65; // Preamble
		wbuf[1] = 0x02; // device #
		wbuf[2] = 'p';	// stop command
		wbuf[3] = 0xd7; // check sum

		WriteFileEx(hCOMnd, wbuf, 4, &wovl, WriteIOCompletionRoutine);
		// x25RFデバイスにワイヤレス計測停止コマンドを送る

		DWORD dwExitCode;
		while (1) {
			GetExitCodeThread(serialh, &dwExitCode);	// 受信スレッドを停止させる
			if (dwExitCode != STILL_ACTIVE) break;		// STILL_ACTIVEではなくなるまで待つ
		}
		CloseHandle(serialh);
		serialh = NULL;
		CloseComPort();
		msgED.SetWindowTextW(_T("Stop Recording"));
	}
	else {
		// ワイヤレスデバイスがSTOPコマンド受信に失敗している可能性があるため
		// 停止コマンドを再送出する

		msgED.SetWindowTextW(_T("Recording is not running..."));

		OpenComPort(RScomport);

		wbuf[0] = 0x65; // Preamble
		wbuf[1] = 0x02; // payload length
		wbuf[2] = 'p';	// stop command
		wbuf[3] = 0xd7; // check sum

		WriteFileEx(hCOMnd, wbuf, 4, &wovl, WriteIOCompletionRoutine);
		CloseComPort();
	}
}

void CWirelessMotionDlg::OnBnClickedButton5()
{
	// CSVファイル書出
	if (rf_status) {
		msgED.SetWindowTextW(_T("Wireless Communication running..."));
		return;
	}

	if (play_status) {
		msgED.SetWindowTextW(_T("Graph Movie is running..."));
		return;
	}

	if (datasize == 0) {
		msgED.SetWindowTextW(_T("There is no data..."));
		return;
	}
	// ワイヤレスにて受信したデータをCSV形式にてファイルに書き出す

	DWORD		dwFlags;
	LPCTSTR		lpszFilter = _T("CSV File(*.csv)|*.csv|");
	CString		fn, pathn;
	int			i, j, k;
	CString		rbuf;
	CString		writebuffer;

	dwFlags = OFN_OVERWRITEPROMPT | OFN_CREATEPROMPT;

	CFileDialog myDLG(FALSE, _T("csv"), NULL, dwFlags, lpszFilter, NULL);
	myDLG.m_ofn.lpstrInitialDir = DefaultDir;


	if (myDLG.DoModal() != IDOK) return;


	CStdioFile fout(myDLG.GetPathName(), CFile::modeWrite | CFile::typeText | CFile::modeCreate);

	pathn = myDLG.GetPathName();
	fn = myDLG.GetFileName();
	j = pathn.GetLength();
	k = fn.GetLength();
	DefaultDir = pathn.Left(j - k);

	msgED.SetWindowTextW(_T("Writing the Data File..."));

	Invalidate();
	UpdateWindow();

	// RAW DATA FORMAT
	for (i = 0; i < datasize; i++) {
		writebuffer.Format(_T("%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n"),
			databuf[0][i],								 // sequence counter
			databuf[1][i], databuf[2][i], databuf[3][i], // ax, ay, az
			databuf[4][i], databuf[5][i], databuf[6][i], // gx, gy, gz
			databuf[7][i], databuf[8][i], databuf[9][i], // e4x, e4y, e4z
			databuf[10][i],								 // alpha
			databuf[11][i], databuf[12][i],              // theta, phi
			databuf[13][i], databuf[14][i], databuf[15][i], // jx, jy, jz (dummy : 0.0 all)
			databuf[16][i], databuf[17][i], databuf[18][i], // ajx, ajy, ajz
			bpm_buf[0][i], bpm_buf[1][i]									//bpm
		);
		fout.WriteString((LPCTSTR)writebuffer);
	}

	fout.Close();

	msgED.SetWindowTextW(_T("Motion Data File Writing Succeeded"));
}

void CWirelessMotionDlg::OnBnClickedButton6()
{
	// CSVファイル読込
	// CSV LOAD

	if (rf_status != 0) return;
	if (play_status != 0) return;

	int i;
	LPCTSTR		lpszFilter = _T("csv File(*.csv)|*.csv|");  // 拡張子がcsvのファイルに反応する
	CString s, fn;		// 文字列型オブジェクト（可変長）
	CStdioFile fin;		// C言語の標準入出力的に扱うことができるファイルオブジェクト
	CString linebuf;	// １行ずつデータを読み込みながら処理するためのバッファメモリ

	CFileDialog myDLG(TRUE, _T("csv"), fn, (DWORD)NULL, lpszFilter, NULL);  // 開くファイルを選ぶウィンドウを呼ぶ
	if (myDLG.DoModal() != IDOK) return;	// OKボタンが押されなかったので何もしない

	s = myDLG.GetPathName();
	fin.Open(s, CFile::modeRead);	// 指定されたファイルを読み出しモードで開く

	i = 0;
	while (1) {
		if (fin.ReadString(linebuf) == NULL) break;	// NULLが帰ってくると読み込み終了
		swscanf_s(linebuf, _T("%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf"),
			&databuf[0][i],										// sequence counter
			&databuf[1][i], &databuf[2][i], &databuf[3][i],		// ax, ay, az
			&databuf[4][i], &databuf[5][i], &databuf[6][i],		// gx, gy, gz
			&databuf[7][i], &databuf[8][i], &databuf[9][i],		// e4x, e4y, e4z
			&databuf[10][i],									// alpha
			&databuf[11][i], &databuf[12][i],					// theta, phi
			&databuf[13][i], &databuf[14][i], &databuf[15][i],	// jx, jy, jz (dummy : 0.0 all)
			&databuf[16][i], &databuf[17][i], &databuf[18][i]	// ajx, ajy, ajz
		); 
		// かなり面倒なのだが、ファイルからデータを読み出すにはこうするしかない
		i++;
		if (i >= MAXDATASIZE) break;	// サンプル数が限界を超えたら途中でファイル読み込みを止める
	}
	datasize = i; // 読み込んだサンプル数を記憶しておく
	fin.Close();

	s.Format(_T("%d 個のデータを読み込みました"), datasize);	// 書式つきプリントを行う
	msgED.SetWindowTextW(s);	// msgEDに対応しているエディットコントロールに表示される
}

void CWirelessMotionDlg::OnBnClickedButton7()
{
	// PLAY/STOP
	// ファイルからのグラフ描画（動画）を行う
	if (rf_status == 1) {
		// ワイヤレスデータ受信中はファイルデータ再生を行えない
		return;
	}

	if (datasize < 2) return; // 描画に必要なデータが不足している

	switch (play_status) {
	case 0:
		datapoint = 0; // グラフ表示の開始時刻を0にリセットする
		rf_interlock = 0;
		tmID = (UINT)SetTimer(TIMER_ID, 32, NULL); // 32ミリ秒（約1/32秒）ごとにTIMERイベントが発生する
		play_status = 1;
		interval = 600;
		pos = 100;
		timer_GO = 1;
		dwTimerID = timeSetEvent(interval, 1, timerFunc, 0, TIME_PERIODIC); //メトロノームのタイマーを開始
		break;
	case 1:
		KillTimer(tmID); // タイマーのスレッドを消滅させる
		timeKillEvent(dwTimerID);
		tmID = NULL;
		play_status = 0;
		dwTimerID = 0;
		timer_GO = 0;
		break;
	default:
		break;
	}
}

void CWirelessMotionDlg::OnTimer(UINT_PTR nIDEvent)
{
	// クラスビュー → CWirelessMotionDlg → プロパティ → メッセージ → WM_TIMERにて追加
	// この関数は Visual Studio が生成しており、メッセージマップやクラスにおける
	// メソッドの宣言等は自動的に行われている

	if (nIDEvent == TIMER_ID) {
		if (rf_interlock == 0) {
			PostMessage(WM_RCV, (WPARAM)NULL, (LPARAM)NULL);
			rf_interlock = 5; // 画面描画が遅い場合には１秒あたりの描画回数を減らして対応する
		}
		else {
			rf_interlock--; // 何らかの原因で画面描画スレッドが正しく動いていないケースに対応する
		}

		datapoint++; // ファイルからのデータ再生位置を1サンプル分進める
		if (datasize < (datapoint + graphspan)) { // 画面の右端にファイルの最終データが到達している？
			datapoint = 0; // 表示開始位置を巻き戻す
		}
	}

	CDialogEx::OnTimer(nIDEvent); // Visual Studioで自動的に生成されているコード
}
