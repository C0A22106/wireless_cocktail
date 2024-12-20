// Interaction.cpp : 実装ファイル
// Interaction.cpp : 実装ファイル
// グラフィックス描画部分を抽出した開発用コード

#include "pch.h"
#include "framework.h"
#include "WirelessMotion.h"
#include "WirelessMotionDlg.h"
#include "afxdialogex.h"
#include "cstdlib"
#include "random"
#include "string"
#include "vector"
#include "pch.h"
#include "resource.h"
#include <random>
#include <mmsystem.h>

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
int stop_count = 0;
double sum_swing_speed = 0;
double sum_theta_dif = 0;
int sample_count = 0;
double AATL = 0;
double BPM = 0;
clock_t start_time, end_time;
BOOLEAN shaking, pour, stop, sound = FALSE;

// ランダムピック
// カクテルのリストを定義
struct Cocktail {
	CString name;
	CString imagePath;
};

Cocktail kakuteru[] = {
	{ _T("カシスオレンジ"), _T("image/CassisOrange.bmp") },
	{ _T("ファジーネーブル"), _T("image/FuzzyNavel.bmp") },
	{ _T("ディタオレンジ"), _T("image/DitaOrange.bmp") },
	{ _T("ピーチツリーフィズ"), _T("image/PeachTreeFizz.bmp") },
	{ _T("ハリケーン"), _T("image/Hurricane.bmp") },
	{ _T("コスモポリタン"), _T("image/Cosmopolitan.bmp") },
	{ _T("ミモザ"), _T("image/Mimosa.bmp") },
	{ _T("カルーアミルク"), _T("image/KahluaMilk.bmp") },
	{ _T("アレキサンダー"), _T("image/Alexander.bmp") },
	{ _T("オーロラ"), _T("image/Aurora.bmp") },
	{ _T("ニューヨーク"), _T("image/NewYork.bmp") },
	{ _T("チャイナブルー"), _T("image/ChinaBlue.bmp") },
	{ _T("ピーチウーロン"), _T("image/PeachOolong.bmp") },
	{ _T("ブルームーン"), _T("image/BlueMoon.bmp") },
	{ _T("マリブコーク"), _T("image/MalibuCoke.bmp") },
	{ _T("キティ"), _T("image/Kitty.bmp") },
	{ _T("グラスホッパー"), _T("image/Grasshopper.bmp") },
	{ _T("ブルーハワイ"), _T("image/BlueHawaii.bmp") },
	{ _T("ピニャコラーダ"), _T("image/PinaColada.bmp") },
	{ _T("ピーチフィズ"), _T("image/PeachFizz.bmp") },
	{ _T("村雨"), _T("image/Murasame.bmp") },
	{ _T("ジンソーダ"), _T("image/GinSoda.bmp") },
	{ _T("ジンライム"), _T("image/GinLime.bmp") },
	{ _T("ジンリッキー"), _T("image/GinRickey.bmp") },
	{ _T("テキーラサンライズ"), _T("image/TequilaSunrise.bmp") },
	{ _T("ディタウーロン"), _T("image/DitaOolong.bmp") },
	{ _T("サムライ"), _T("image/Samurai.bmp") },
	{ _T("プレリュードフィズ"), _T("image/PreludeFizz.bmp") },
	{ _T("ジントニック"), _T("image/GinAndTonic.bmp") },
	{ _T("ブラッディーメアリー"), _T("image/BloodyMary.bmp") },
	{ _T("ソルティドッグ"), _T("image/SaltyDog.bmp") },
	{ _T("モヒート"), _T("image/Mojito.bmp") },
	{ _T("モスコミュール"), _T("image/MoscowMule.bmp") },
	{ _T("カンパリオレンジ"), _T("image/CampariOrange.bmp") },
	{ _T("サラマンダー"), _T("image/Salamander.bmp") },
	{ _T("コブラ"), _T("image/Cobra.bmp") },
	{ _T("トマホーク"), _T("image/Tomahawk.bmp") },
	{ _T("ホーネット"), _T("image/Hornet.bmp") },
	{ _T("ホワイトレディ"), _T("image/WhiteLady.bmp") },
	{ _T("スクリュードライバー"), _T("image/Screwdriver.bmp") },
	{ _T("キューバリブレ"), _T("image/CubaLibre.bmp") },
	{ _T("オペレーター"), _T("image/Operator.bmp") },
	{ _T("ダイキリ"), _T("image/Daiquiri.bmp") },
	{ _T("シンガポールスリング"), _T("image/SingaporeSling.bmp") },
	{ _T("ジャックローズ"), _T("image/JackRose.bmp") },
	{ _T("アプリコットフィズ"), _T("image/ApricotFizz.bmp") },
	{ _T("サイドカー"), _T("image/Sidecar.bmp") },
	{ _T("マンハッタン"), _T("image/Manhattan.bmp") },
	{ _T("ギムレット"), _T("image/Gimlet.bmp") },
	{ _T("マティーニ"), _T("image/Martini.bmp") },
	{ _T("マルガリータ"), _T("image/Margarita.bmp") },
	{ _T("ロングアイランドアイスティー"), _T("image/LongIslandIcedTea.bmp") },
	{ _T("シャンディガフ"), _T("image/ShandyGaff.bmp") },
	{ _T("レッドアイ"), _T("image/RedEye.bmp") },
	{ _T("ラムトニック"), _T("image/RumTonic.bmp") },
	{ _T("カンパリソーダ"), _T("image/CampariSoda.bmp") },
	// 他のカクテルを追加...
};

const int numCocktails = sizeof(kakuteru) / sizeof(kakuteru[0]);

// シェイク数を引数として受け取り、ランダムにカクテルを選択する関数
Cocktail GetRandomCocktail(int shakeCount) {
	// 乱数のシードを初期化
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, shakeCount);

	// 0 から shakeCount までの乱数を生成
	int randomIndex = dis(gen) % numCocktails;

	// ランダムに選択されたカクテルの名前と画像パスを取得
	Cocktail selectedCocktail = kakuteru[randomIndex];

	return selectedCocktail;
}

// 使用例
int shakeCount = 5; // シェイク数を指定
Cocktail randomCocktail = GetRandomCocktail(shakeCount);
CString kakutel_name = randomCocktail.name;
CString kakutel_path = randomCocktail.imagePath;

extern double bpm_buf[2][MAXDATASIZE];

// 注いでいることを判定する独自関数
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

//ファイル名を引数としてwavサウンドファイルを再生する関数
CString wav_play(int track) {
	CString path; //ファイルパスを格納する

	CString files[4] = { _T("MusMus-BGM-146.wav"), _T("街の道路.wav"), _T("シェイク音.wav"), _T("注ぐ音.wav") };
	path = files[track];
	path = _T("sounds/") + path;
	if (sound == FALSE) {
		PlaySound(path, NULL, SND_ASYNC); //非同期で音声を再生する
		sound = TRUE; //音声が再生中であることを示すフラグを立てる
	}

	return path;
}

void wav_stop() {
	if (sound == TRUE) {
		PlaySound(NULL, NULL, SND_PURGE);
		sound = FALSE;
	}
}

// Δ追記
// 画面モードを管理する構造体
enum Mode
{
	idol, // 待機画面（振る前）
	shake, // ゲーム中（振っている）
	finish, // 終了（カクテルを注ぐ画面）
	result // 結果発表
};

Mode mode = idol;

// 振り始めた事を判定する関数
void start_shake(int time)
{
	if (databuf[16][time] >= 2000)
	{
		shaking = TRUE;
	}
}

// Δここまで

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


	//// ここからグラフを描画する
	//// 描画開始サンプル番号　start
	//// 描画サンプル数　total
	//// X軸描画時の拡大縮小係数　xgain (double)
	//// Y軸描画時の拡大縮小係数　ygain (double)
	//// Y軸データのゼロ点オフセット　OFFSET (double)

	int i, xx, yy;

	//for (i = 0; i < plot_count; i++) {
	//	xx = (int)(xgain * (double)i);
	//	yy = (int)(ygain * (-databuf[6][start + i] + GRAPH_Y_OFFSET));

	//	// 領域外に描画しないようにクリッピング処理を行う
	//	xx = (xx < 0) ? 0 : xx;
	//	yy = (yy < 0) ? 0 : yy;
	//	xx = (xx > (xsize - 1)) ? xsize - 1 : xx;
	//	yy = (yy > (ysize - 1)) ? ysize - 1 : yy;
	//	if (i == 0) {

	//		myDC.MoveTo(xx, yy);	// ペンを座標( xx, yy)に移動させる（移動するだけなので、線は引いていない）
	//	}
	//	else {
	//		myDC.LineTo(xx, yy);	// ペンを座標 ( xx, yy)に移動させながら線を引く
	//	}
	//}

	// Δ追記
	// 画面描画
	// 上のグラフ描画は削除またはコメントアウト
	 /* いらない子
	switch (mode)
	{
		// 振る前の画面
		case idol:
			s.Format(_T("Screem Mode idol"));
			break;

		// 振っている時の画面
		case shake:
			s.Format(_T("Screem Mode shake"));
			break;

		// 注ぐ画面
		case finish:
			s.Format(_T("Screem Mode finish"));
			break;

		// 結果の画面
		case result:
			// 出来上がったカクテルの画像を真ん中にドン！
			s.Format(_T("Screem Mode result"));
			break;
	}
	*/
	// Δここまで


	// グラフの描画はここまで

	//myPictDC.BitBlt(0, 0, xsize, ysize, &myDC, 0, 0, SRCCOPY); // バッファメモリから画面（myPictDC)にデータを転送する

	//myDC.SelectObject(oldPen);	// 以前のペンに戻しておく

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

	myPictDC2.BitBlt(0, 0, xsize2, ysize2, &myDC2, 0, 0, SRCCOPY); // �o�b�t�@�����������ʁimyPictDC)�Ƀf�[�^��]������

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
	CString mes_random;
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

	// Δ追記
	// 振り始めたら振っているフラグを立てる
	start_shake(start);

	if (shaking == TRUE && mode == idol)
	{
		mode = shake; // ゲーム開始から振り始めた画面へ

		// 画面をリセット
		mPICT1.GetClientRect(myRect);	// PICT1のサイズ情報がmyRectに入る
		myPictDC.FillSolidRect(myRect, RGB(255, 255, 255));	// myRectで示される四辺形を白で塗りつぶす
	}

	if (pour == TRUE && mode == shake)
	{
		// mode = finish; // 振っている状態からリザルト画面へ
		mode = result; // 振っている状態からリザルト画面へ

		// 画面をリセット
		mPICT1.GetClientRect(myRect);	// PICT1のサイズ情報がmyRectに入る
		myPictDC.FillSolidRect(myRect, RGB(255, 255, 255));	// myRectで示される四辺形を白で塗りつぶす
	}
	// Δここまで


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

	// 12/6Δ追記
	// 画像の設定あれこれ

	// 画面をリセット
	//mPICT1.GetClientRect(myRect);	// PICT1のサイズ情報がmyRectに入る
	//myPictDC.FillSolidRect(myRect, RGB(255, 255, 255));	// myRectで示される四辺形を白で塗りつぶす

	//CClientDC myPictDC(&mPICT1); // Picture Controlに設定した変数（mPICT2）から描画用デバイスコンテキストを作る
	//CRect myRect;
	BITMAP bmp; // ビットマップの情報を格納
	//CRect myRect; // PictureBoxの領域を格納する変数

	mPICT1.GetClientRect(myRect); // PICT1の画面サイズ取得
	int xpictsize, ypictsize; // 画面上への描画サイズ
	xpictsize = myRect.Width();	// PICT1の幅
	ypictsize = myRect.Height(); // PICT1の高さ

	HBITMAP hbmp = 0;
	HDC hMdc = CreateCompatibleDC(myPictDC); // メモリデバイスコンテキストを作成

	// 画像読み込み処理
	//switch (mode)
	//{
	//case idol:
	/*	hbmp = (HBITMAP)LoadImage(NULL, _T("BloodyMary.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		break;
	case shake:
		hbmp = (HBITMAP)LoadImage(NULL, _T("image/img.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		break;
	case result:
		hbmp = (HBITMAP)LoadImage(NULL, _T("image/aaa.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		break;
	}*/

	
	CString imagePath;
	switch (mode)
	{
	case idol:
		imagePath = _T("image/start.bmp");
		break;
	case shake:
		imagePath = _T("image/shake.bmp");
		break;
	case result:
		imagePath = kakutel_path;
		break;
	}
	if (hbmp == NULL) {
		msgED.SetDlgItemTextW(0, _T("画像ファイルが見つかりません"));
	}
	// 画像読み込み処理
	hbmp = (HBITMAP)LoadImage(NULL, imagePath, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);

	//StretchBlt(myPictDC, 0, 0, xpictsize, ypictsize, hMdc, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY); // サイズ合わせの上で表示する
	// ビットマップ情報を取得
	GetObject(hbmp, sizeof(BITMAP), &bmp);
	int imgWidth = bmp.bmWidth;   // 画像の幅
	int imgHeight = bmp.bmHeight; // 画像の高さ

	//HBITMAP oldBmp = (HBITMAP)SelectObject(hMdc, hbmp); // メモリデバイスコンテキストに画像を選択

	// StretchBltの描画
	//switch (mode)
	//{
	//case idol:
	//	StretchBlt(myPictDC, 0, 0, xpictsize, ypictsize, hMdc, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY); // サイズ合わせの上で表示する
	//	break;
	//case shake:
	//	StretchBlt(myPictDC, 0, 0, xpictsize, ypictsize, hMdc, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY); // サイズ合わせの上で表示する
	//	break;
	//case result:
	//	StretchBlt(myPictDC, 0, 0, xpictsize, ypictsize, hMdc, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY); // サイズ合わせの上で表示する
	//	break;
	//}
	//BOOL aresult = StretchBlt(myPictDC, 0, 0, xpictsize, ypictsize, hMdc, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);
	HBITMAP oldBmp = (HBITMAP)SelectObject(hMdc, hbmp); // メモリデバイスコンテキストに画像を選択
	StretchBlt(myPictDC, 0, 0, xpictsize, ypictsize, hMdc, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);
	//BitBlt(myPictDC, 0, 0, xpictsize, ypictsize, hMdc, 0, 0, SRCCOPY);

	s.Format(_T("Size of img.bmp : x = %d y = %d Size of Picture box : width = %d, height = %d"),
		imgWidth, imgHeight, xpictsize, ypictsize);
	msgED.SetWindowTextW(s); // 情報を表示

	// リソース解放
	SelectObject(hMdc, oldBmp); // 古いビットマップを復元
	//SelectObject(hMdc, hbmp);	// 画像ファイルのフォーマットを踏襲させる
	SelectObject(myPictDC, hbmp); // 画像描画DCの属性をビットマップに対応づける
	DeleteDC(hMdc);
	DeleteObject(hbmp);

	// Δここまで

	mes_swing.Format(_T("平均時間: %lf s\r\nスコア: %lf"), swing_average * 32.0, swing_score);
	mes_wrist.Format(_T("角度平均: %lf ?\r\nスコア: %lf"), theta_average, theta_score);
	mes_result.Format(_T("総合スコア: %lf\r\nBPM: %lf\r\npour: %d\r\nカクテル: %s"), whole_score, bpm_buf[0][start], pour, kakutel_name);
	msgED2.SetWindowTextW(mes_wrist);

	msgED3.SetWindowTextW(mes_swing);
	msgED4.SetWindowTextW(mes_result);


	wav_play(1);
	//オリジナルここまで

	return TRUE; // LRESULT型関数は論理値をリターンすることになっている
}