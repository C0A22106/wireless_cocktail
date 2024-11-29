// Interaction.cpp : 実装ファイル
// Interaction.cpp : 実装ファイル
// グラフィックス描画部分を抽出した開発用コード

#include "pch.h"
#include "framework.h"
#include "WirelessMotion.h"
#include "WirelessMotionDlg.h"
#include "afxdialogex.h"
#include "cstdlib"

// MFC管理下にないグローバル変数への参照
extern int rf_status; // ワイヤレス通信の実行状況を表す変数　0 ... 実行なし	1 ... 実行あり
extern int rf_firsttime; // パケットエラーの計数開始時のみ1になるフラフ
extern int rf_errcnt; // ワイヤレス通信におけるパケットエラーの累計数
extern int rf_interlock; // 描画時間が長い際にワイヤレス通信スレッドを優先実行するためのフラグ

#define GRAPH_Y_RANGE 4000.0					// グラフY軸の数値幅
#define GRAPH_Y_OFFSET (GRAPH_Y_RANGE*0.5)		// データの値がゼロの時のY軸値（オフセット値）
#define PHI_RANGE 360.0
#define THETA_RANGE 180.0
#define PHI_OFFSET (PHI_RANGE*0.5)
#define THETA_OFFSET (THETA_RANGE*0.5)

// モーションデータの内容
extern double databuf[DATASORT][MAXDATASIZE];		// グローバル変数配列にセンサデータを格納する
// 0 ... seq
// 1, 2, 3 ... ax, ay, az ３軸加速度(G)
// 4, 5, 6 ... wx, wy, wz ３軸角速度(dps)
// 7, 8, 9 ... e4x, e4y, e4z 重力姿勢角ベクトル（direction cosine形式)
// 10 ... alpha (信頼度係数, 0から1までの浮動小数点数値）
// 11, 12 ... 前腕傾斜角θ, 前腕ひねり角φ（deg)
// 13, 14, 15 ... jx, jy, jz ３軸加加速度（jerk) (任意単位）
// 16, 17, 18 ... ajx, ajy, ajz ３軸角加加速度（angular jerk) (任意単位)
extern int datasize;

//オリジナル変数初期化
int dir = 0;
int period = 0;
double val;
int peak_timing = -1;
double peak_val = 0.0;
int stop_count;
double sum_swing_speed = 0;
double sum_theta_dif = 0;
int sample_count = 0;
double AATL = 0;
double BPM = 0;
clock_t start_time, end_time;
BOOLEAN pour, stop = FALSE;

extern double bpm_buf[2][MAXDATASIZE];

//注いでいることを判定する独自関数
void jud_pour(int time) {
	if ((stop_count >= 20) && (databuf[12][time] >= 120.0) && (databuf[12][time] <= 180.0)) {
		pour = TRUE;
	}
}

//止まっているときstop_countを1加算する
void jud_stop(int time) {
	if (databuf[16][time] <= 2000) {
		stop_count++;
	}
	else {
		stop_count = 0;
	}
}

// 手入力で追記したグラフ描画用メッセージハンドラー

LRESULT CWirelessMotionDlg::OnMessageRCV(WPARAM wParam, LPARAM lParam)
// 外部からのメッセージで起動する画面描画コード
// MFCの管理下にあるため、GDIグラフィックス等が使用できる
// WPARAMとLPARAMは実際には整数型変数であり、メッセージを発信する側で値をセットする
{
	// グラフ描画
	// リソースビューからピクチャーボックスを作り、IDをIDC_PICT1といった名前に変更する
	// ピクチャーボックスに変数を追加し、mPICT1といった名前をつける（m:メンバー変数の意味）
	// 押しボタンに対応するハンドラーとして、以下にグラフ描画コードを書く

	// これ以降はGDIグラフィックスによるグラフ描画コードをほぼそのまま記載している

	// 基本的なGDIグラフィックスの描画をバッファメモリを使って行う
	CClientDC myPictDC(&mPICT1); // Picture Controlに設定した変数（mPICT1）から描画用デバイスコンテキストを作る
	CRect myRect; // 四辺形のサイズを表現するための構造体
	CDC myDC; // 描画バッファメモリ用のデバイスコンテキスト
	int xsize, ysize;
	CString s; // 描画状況をテキスト表示するための文字列変数

	int start;	// 描画を開始するサンプル番号、描画するサンプル数
	int plot_count;	// グラフにプロットする点の数
	double xgain, ygain; // グラフ描画における拡大・縮小係数

	if (rf_status == 1) {
		start = datasize - graphspan;
		if (start < 0) {
			start = 0;
		}
	}
	else {
		start = datapoint; // 描画開始サンプル番号
	}

	plot_count = graphspan; // 描画領域の全幅に相当するサンプル数（実際のデータサンプル数ではない）

	if((start + plot_count) > datasize){
		plot_count = datasize - start;	// データの数が足りないので、plot_countを減らして調整する
	}

	if (plot_count < 2) return TRUE; // グラフの描画には２つ以上のサンプルが必要


	mPICT1.GetClientRect(myRect); //Picture Controlによる描画領域を表す四辺形のサイズ（ピクセル単位）を取得する
	xsize = myRect.Width();
	xgain = (double)xsize / (double)plot_count; // 1サンプルあたりのX軸方向ピクセル数を求める
	ysize = myRect.Height();
	ygain = (double)ysize / GRAPH_Y_RANGE; // Y軸方向ピクセル数とデータの数値範囲から変換係数を求める

	myDC.CreateCompatibleDC(&myPictDC);	//mPICT1と同じ属性を描画バッファメモリ用のデバイスコンテキストに設定する
	HBITMAP memBM = CreateCompatibleBitmap(myPictDC, xsize, ysize); // 実際の描画に関わる画像メモリの性質を設定する
	memBM = CreateCompatibleBitmap(myPictDC, xsize, ysize);
	SelectObject(myDC, memBM); // 画像メモリの属性をバッファメモリのデバイスコンテキストに対応づける

	myDC.FillSolidRect(myRect, RGB(255, 255, 255)); // 矩形領域を白で塗りつぶす
	CPen myPen(PS_SOLID, 1, RGB(0, 0, 0)); // ペンの種類（SOLID：実線）、ペン幅（1ピクセル)、色（R, G, B)
	CPen* oldPen = myDC.SelectObject(&myPen);	// ペンをmyPenに持ち替えると同時に、以前のペンをoldPenに記憶させる

	// ここからグラフを描画する
	// 描画開始サンプル番号　start
	// 描画サンプル数　total
	// X軸描画時の拡大縮小係数　xgain (double)
	// Y軸描画時の拡大縮小係数　ygain (double)
	// Y軸データのゼロ点オフセット　OFFSET (double)

	int i, xx, yy;

	for (i = 0; i < plot_count; i++) {
		xx = (int)(xgain * (double)i);
		yy = (int)(ygain * (-databuf[6][start + i] + GRAPH_Y_OFFSET));
		// 領域外に描画しないようにクリッピング処理を行う
		xx = (xx < 0) ? 0 : xx;
		yy = (yy < 0) ? 0 : yy;
		xx = (xx > (xsize - 1)) ? xsize - 1 : xx;
		yy = (yy > (ysize - 1)) ? ysize - 1 : yy;
		if (i == 0) {
			myDC.MoveTo(xx, yy);	// ペンを座標( xx, yy)に移動させる（移動するだけなので、線は引いていない）
		}
		else {
			myDC.LineTo(xx, yy);	// ペンを座標 ( xx, yy)に移動させながら線を引く
		}
	}

	// グラフの描画はここまで

	myPictDC.BitBlt(0, 0, xsize, ysize, &myDC, 0, 0, SRCCOPY); // バッファメモリから画面（myPictDC)にデータを転送する

	myDC.SelectObject(oldPen);	// 以前のペンに戻しておく

	// 軌道ダイアグラムの描画
	// Jun. 26, 2024

	CClientDC myPictDC2(&mPICT2); // Picture Controlに設定した変数（mPICT2）から描画用デバイスコンテキストを作る
	CRect myRect2; // 四辺形のサイズを表現するための構造体
	CDC myDC2; // 描画バッファメモリ用のデバイスコンテキスト
	int xsize2, ysize2;

	int start2;	// 描画を開始するサンプル番号、描画するサンプル数
	int plot_count2;	// グラフにプロットする点の数
	double xgain2, ygain2; // グラフ描画における拡大・縮小係数

	if (rf_status == 1) {
		start2 = datasize - graphspan;
		if (start2 < 0) {
			start2 = 0;
		}
	}
	else {
		start2 = datapoint; // 描画開始サンプル番号
	}

	plot_count2 = graphspan; // 描画領域の全幅に相当するサンプル数（実際のデータサンプル数ではない）

	if ((start2 + plot_count2) > datasize) {
		plot_count2 = datasize - start2;	// データの数が足りないので、plot_countを減らして調整する
	}

	if (plot_count2 < 2) return TRUE; // グラフの描画には２つ以上のサンプルが必要

	mPICT2.GetClientRect(myRect2); //Picture Controlによる描画領域を表す四辺形のサイズ（ピクセル単位）を取得する
	xsize2 = myRect2.Width();
	xgain2 = (double)xsize2 / PHI_RANGE;	// X軸方向ピクセル数とデータ」の数値範囲から変換係数を求める
	ysize2 = myRect2.Height();
	ygain2 = (double)ysize2 / THETA_RANGE; // Y軸方向ピクセル数とデータの数値範囲から変換係数を求める

	myDC2.CreateCompatibleDC(&myPictDC2);	//mPICT1と同じ属性を描画バッファメモリ用のデバイスコンテキストに設定する
	HBITMAP memBM2 = CreateCompatibleBitmap(myPictDC2, xsize2, ysize2); // 実際の描画に関わる画像メモリの性質を設定する
	memBM2 = CreateCompatibleBitmap(myPictDC2, xsize2, ysize2);
	SelectObject(myDC2, memBM2); // 画像メモリの属性をバッファメモリのデバイスコンテキストに対応づける

	myDC2.FillSolidRect(myRect2, RGB(255, 255, 255)); // 矩形領域を白で塗りつぶす
	CPen myPen2(PS_SOLID, 1, RGB(0, 0, 0)); // ペンの種類（SOLID：実線）、ペン幅（1ピクセル)、色（R, G, B)
	CPen* oldPen2 = myDC2.SelectObject(&myPen2);	// ペンをmyPenに持ち替えると同時に、以前のペンをoldPenに記憶させる

	// ここからグラフを描画する
	// 描画開始サンプル番号　start
	// 描画サンプル数　total
	// X軸描画時の拡大縮小係数　xgain (double)
	// Y軸描画時の拡大縮小係数　ygain (double)
	// X軸データのゼロ点オフセット  PHI_OFFSET (double)
	// Y軸データのゼロ点オフセット　THETA_OFFSET (double)

//	int i, xx, yy;

	for (i = 0; i < plot_count2; i++) {
		xx = (int)(xgain2 * (-databuf[12][start2 + i] + PHI_OFFSET));
		yy = (int)(ygain2 * (databuf[11][start2 + i] + THETA_OFFSET));
		// 領域外に描画しないようにクリッピング処理を行う
		xx = (xx < 0) ? 0 : xx;
		yy = (yy < 0) ? 0 : yy;
		xx = (xx > (xsize - 1)) ? xsize - 1 : xx;
		yy = (yy > (ysize - 1)) ? ysize - 1 : yy;
		if (i == 0) {
			myDC2.MoveTo(xx, yy);	// ペンを座標( xx, yy)に移動させる（移動するだけなので、線は引いていない）
		}
		else {
			myDC2.LineTo(xx, yy);	// ペンを座標 ( xx, yy)に移動させながら線を引く
		}
	}

	// グラフの描画はここまで

	myPictDC2.BitBlt(0, 0, xsize2, ysize2, &myDC2, 0, 0, SRCCOPY); // バッファメモリから画面（myPictDC)にデータを転送する

	myDC2.SelectObject(oldPen2);	// 以前のペンに戻しておく


	// 軌道ダイアグラム　処理コードここまで

	if (rf_status == 1) {
		s.Format(_T("Sample Count = %d Error = %d"), start, rf_errcnt);
	}
	else {
		s.Format(_T("Sample Count = %d"), start);
	}
	msgED.SetWindowTextW(s);

	DeleteDC(myDC); // メモリバッファのデバイスコンテキストを解放する
	DeleteObject(memBM); // 画像メモリの性質を表すビットマップを解放する

	DeleteDC(myDC2);
	DeleteObject(memBM2);

	rf_interlock = 0; // 描画が完了したことをグローバル変数を介して伝える

	//以下オリジナル
	CString mes_swing;
	CString mes_wrist;
	CString mes_result;
	val = databuf[4][start];

	AATL += abs(databuf[16][start]);

	//1時間単位前の手首ひねり角との差の絶対値をsum_data_difに加算する
	double wrist_def;
	wrist_def = databuf[12][start] - databuf[12][start - 1];
	if (wrist_def < 0) {
		sum_theta_dif += -1 * wrist_def;
	}
	else {
		sum_theta_dif += wrist_def;
	}

	sample_count++;

	//停止していることを判定
	jud_stop(start);

	//一定時間以上止まっているときパラメータをリセット
	if (stop_count >= 20) {
		dir = 0;
		period = 0;
		peak_timing = -1;
		peak_val = 0.0;
		sum_swing_speed = 0;
		sum_theta_dif = 0;
		sample_count = 0;
	}

	//注ぐ姿勢で一定時間止まっているとき注ぐフラグを立てる
	jud_pour(start);
	
	//前腕姿勢角ｙの値から振り速度を求める
	//dirが0の時振り下ろし方向、1の時振り上げ方向
	if (dir == 0) {
		if (val < 0.0) {
			dir = 1;
			peak_timing = start;
		}
		else {
			if (val > peak_val) {
				peak_val = val;
				peak_timing = start;
			}
		}
	}
	else {
		if (val >= 0.0) {
			dir = 0;
			peak_val = val;
			sum_swing_speed += start - peak_timing;
			end_time = clock();
			BPM = 60.0 / ((double)(end_time - start_time) / CLOCKS_PER_SEC);
			peak_timing = start;
			start_time = clock();
			AATL = 0.0;
			period++;
		}
		else {
			if (val < peak_val) {
				peak_val = val;
				peak_timing = start;
			}
		}
	}

	bpm_buf[1][start] = BPM;

	double swing_average = (double)(sum_swing_speed / period);
	double theta_average = (double)(sum_theta_dif / sample_count);
	double swing_score = (1 - (swing_average - 1) * 0.5) * 100;
	double theta_score = (theta_average - 10) * 10;
	double whole_score = swing_score + theta_score;

	mes_swing.Format(_T("平均時間: %lf s\r\nスコア: %lf"), swing_average * 32.0, swing_score);
	mes_wrist.Format(_T("角度平均: %lf 度\r\nスコア: %lf"), theta_average, theta_score);
	mes_result.Format(_T("総合スコア: %lf\r\nBPM: %lf\r\npour: %d"), whole_score, bpm_buf[0][start], pour);
	msgED2.SetWindowTextW(mes_wrist);
	msgED3.SetWindowTextW(mes_swing);
	msgED4.SetWindowTextW(mes_result);

	//オリジナルここまで

	return TRUE; // LRESULT型関数は論理値をリターンすることになっている
}