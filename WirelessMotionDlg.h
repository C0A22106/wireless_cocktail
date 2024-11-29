
// WirelessMotionDlg.h : ヘッダー ファイル
//

#pragma once

#define PI 3.14159265358979
#define DATASORT 19							// seq, acc[3], gyro[3], e4[3], alpha, theta, phi, jx, jy, jz, ajx, ajy, ajz
#define MAXDATASIZE 655360					// 655360 x 1/32 = 20480 sec -> 5.7 hours...
#define WM_RCV (WM_APP + 1)
#define TIMER_ID 1
// タイマースレッドのID番号を設定する（適当な符号なし整数で良い）

// CWirelessMotionDlg ダイアログ
class CWirelessMotionDlg : public CDialogEx
{
// コンストラクション
public:
	CWirelessMotionDlg(CWnd* pParent = nullptr);	// 標準コンストラクター

// ダイアログ データ
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WIRELESSMOTION_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV サポート


// 実装
protected:
	HICON m_hIcon;

	// 生成された、メッセージ割り当て関数
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	CEdit msgED;
	CStatic mPICT1;
	int play_status;		// ファイルデータからの画面描画再生状態 0 ... 停止　1 ... 再生中
	int datapoint;			// 描画を行う際に参照するデータ番号（ワイヤレス受信時はdatapoint = datasize）
	int graphspan;			// 描画における時間幅をサンプルデータ数で表した数値
	LRESULT OnMessageRCV(WPARAM wParam, LPARAM lParam);
	UINT tmID;				// タイマースレッドのID　手入力で追記
	void Initialize(void);
	afx_msg void OnBnClickedButton1();
	afx_msg void OnBnClickedButton2();
	afx_msg void OnBnClickedButton3();
	afx_msg void OnBnClickedButton4();
	afx_msg void OnBnClickedButton5();
	afx_msg void OnBnClickedButton6();
	afx_msg void OnBnClickedButton7();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	CStatic mPICT2;
	CEdit msgED2;
	CEdit msgED3;
	CEdit msgED4;
	afx_msg void OnEnChangeEdit1();
	afx_msg void OnEnChangeEdit5();
};
