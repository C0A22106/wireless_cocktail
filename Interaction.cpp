// Interaction.cpp : �����t�@�C��
// Interaction.cpp : �����t�@�C��
// �O���t�B�b�N�X�`�敔���𒊏o�����J���p�R�[�h

#include "pch.h"
#include "framework.h"
#include "WirelessMotion.h"
#include "WirelessMotionDlg.h"
#include "afxdialogex.h"
#include "cstdlib"

// MFC�Ǘ����ɂȂ��O���[�o���ϐ��ւ̎Q��
extern int rf_status; // ���C�����X�ʐM�̎��s�󋵂�\���ϐ��@0 ... ���s�Ȃ�	1 ... ���s����
extern int rf_firsttime; // �p�P�b�g�G���[�̌v���J�n���̂�1�ɂȂ�t���t
extern int rf_errcnt; // ���C�����X�ʐM�ɂ�����p�P�b�g�G���[�̗݌v��
extern int rf_interlock; // �`�掞�Ԃ������ۂɃ��C�����X�ʐM�X���b�h��D����s���邽�߂̃t���O

#define GRAPH_Y_RANGE 4000.0					// �O���tY���̐��l��
#define GRAPH_Y_OFFSET (GRAPH_Y_RANGE*0.5)		// �f�[�^�̒l���[���̎���Y���l�i�I�t�Z�b�g�l�j
#define PHI_RANGE 360.0
#define THETA_RANGE 180.0
#define PHI_OFFSET (PHI_RANGE*0.5)
#define THETA_OFFSET (THETA_RANGE*0.5)

// ���[�V�����f�[�^�̓��e
extern double databuf[DATASORT][MAXDATASIZE];		// �O���[�o���ϐ��z��ɃZ���T�f�[�^���i�[����
// 0 ... seq
// 1, 2, 3 ... ax, ay, az �R�������x(G)
// 4, 5, 6 ... wx, wy, wz �R���p���x(dps)
// 7, 8, 9 ... e4x, e4y, e4z �d�͎p���p�x�N�g���idirection cosine�`��)
// 10 ... alpha (�M���x�W��, 0����1�܂ł̕��������_���l�j
// 11, 12 ... �O�r�X�Ίp��, �O�r�Ђ˂�p�Ӂideg)
// 13, 14, 15 ... jx, jy, jz �R���������x�ijerk) (�C�ӒP�ʁj
// 16, 17, 18 ... ajx, ajy, ajz �R���p�������x�iangular jerk) (�C�ӒP��)
extern int datasize;

//�I���W�i���ϐ�������
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

//�����ł��邱�Ƃ𔻒肷��Ǝ��֐�
void jud_pour(int time) {
	if ((stop_count >= 20) && (databuf[12][time] >= 120.0) && (databuf[12][time] <= 180.0)) {
		pour = TRUE;
	}
}

//�~�܂��Ă���Ƃ�stop_count��1���Z����
void jud_stop(int time) {
	if (databuf[16][time] <= 2000) {
		stop_count++;
	}
	else {
		stop_count = 0;
	}
}

// ����͂ŒǋL�����O���t�`��p���b�Z�[�W�n���h���[

LRESULT CWirelessMotionDlg::OnMessageRCV(WPARAM wParam, LPARAM lParam)
// �O������̃��b�Z�[�W�ŋN�������ʕ`��R�[�h
// MFC�̊Ǘ����ɂ��邽�߁AGDI�O���t�B�b�N�X�����g�p�ł���
// WPARAM��LPARAM�͎��ۂɂ͐����^�ϐ��ł���A���b�Z�[�W�𔭐M���鑤�Œl���Z�b�g����
{
	// �O���t�`��
	// ���\�[�X�r���[����s�N�`���[�{�b�N�X�����AID��IDC_PICT1�Ƃ��������O�ɕύX����
	// �s�N�`���[�{�b�N�X�ɕϐ���ǉ����AmPICT1�Ƃ��������O������im:�����o�[�ϐ��̈Ӗ��j
	// �����{�^���ɑΉ�����n���h���[�Ƃ��āA�ȉ��ɃO���t�`��R�[�h������

	// ����ȍ~��GDI�O���t�B�b�N�X�ɂ��O���t�`��R�[�h���قڂ��̂܂܋L�ڂ��Ă���

	// ��{�I��GDI�O���t�B�b�N�X�̕`����o�b�t�@���������g���čs��
	CClientDC myPictDC(&mPICT1); // Picture Control�ɐݒ肵���ϐ��imPICT1�j����`��p�f�o�C�X�R���e�L�X�g�����
	CRect myRect; // �l�ӌ`�̃T�C�Y��\�����邽�߂̍\����
	CDC myDC; // �`��o�b�t�@�������p�̃f�o�C�X�R���e�L�X�g
	int xsize, ysize;
	CString s; // �`��󋵂��e�L�X�g�\�����邽�߂̕�����ϐ�

	int start;	// �`����J�n����T���v���ԍ��A�`�悷��T���v����
	int plot_count;	// �O���t�Ƀv���b�g����_�̐�
	double xgain, ygain; // �O���t�`��ɂ�����g��E�k���W��

	if (rf_status == 1) {
		start = datasize - graphspan;
		if (start < 0) {
			start = 0;
		}
	}
	else {
		start = datapoint; // �`��J�n�T���v���ԍ�
	}

	plot_count = graphspan; // �`��̈�̑S���ɑ�������T���v�����i���ۂ̃f�[�^�T���v�����ł͂Ȃ��j

	if((start + plot_count) > datasize){
		plot_count = datasize - start;	// �f�[�^�̐�������Ȃ��̂ŁAplot_count�����炵�Ē�������
	}

	if (plot_count < 2) return TRUE; // �O���t�̕`��ɂ͂Q�ȏ�̃T���v�����K�v


	mPICT1.GetClientRect(myRect); //Picture Control�ɂ��`��̈��\���l�ӌ`�̃T�C�Y�i�s�N�Z���P�ʁj���擾����
	xsize = myRect.Width();
	xgain = (double)xsize / (double)plot_count; // 1�T���v���������X�������s�N�Z���������߂�
	ysize = myRect.Height();
	ygain = (double)ysize / GRAPH_Y_RANGE; // Y�������s�N�Z�����ƃf�[�^�̐��l�͈͂���ϊ��W�������߂�

	myDC.CreateCompatibleDC(&myPictDC);	//mPICT1�Ɠ���������`��o�b�t�@�������p�̃f�o�C�X�R���e�L�X�g�ɐݒ肷��
	HBITMAP memBM = CreateCompatibleBitmap(myPictDC, xsize, ysize); // ���ۂ̕`��Ɋւ��摜�������̐�����ݒ肷��
	memBM = CreateCompatibleBitmap(myPictDC, xsize, ysize);
	SelectObject(myDC, memBM); // �摜�������̑������o�b�t�@�������̃f�o�C�X�R���e�L�X�g�ɑΉ��Â���

	myDC.FillSolidRect(myRect, RGB(255, 255, 255)); // ��`�̈�𔒂œh��Ԃ�
	CPen myPen(PS_SOLID, 1, RGB(0, 0, 0)); // �y���̎�ށiSOLID�F�����j�A�y�����i1�s�N�Z��)�A�F�iR, G, B)
	CPen* oldPen = myDC.SelectObject(&myPen);	// �y����myPen�Ɏ����ւ���Ɠ����ɁA�ȑO�̃y����oldPen�ɋL��������

	// ��������O���t��`�悷��
	// �`��J�n�T���v���ԍ��@start
	// �`��T���v�����@total
	// X���`�掞�̊g��k���W���@xgain (double)
	// Y���`�掞�̊g��k���W���@ygain (double)
	// Y���f�[�^�̃[���_�I�t�Z�b�g�@OFFSET (double)

	int i, xx, yy;

	for (i = 0; i < plot_count; i++) {
		xx = (int)(xgain * (double)i);
		yy = (int)(ygain * (-databuf[6][start + i] + GRAPH_Y_OFFSET));
		// �̈�O�ɕ`�悵�Ȃ��悤�ɃN���b�s���O�������s��
		xx = (xx < 0) ? 0 : xx;
		yy = (yy < 0) ? 0 : yy;
		xx = (xx > (xsize - 1)) ? xsize - 1 : xx;
		yy = (yy > (ysize - 1)) ? ysize - 1 : yy;
		if (i == 0) {
			myDC.MoveTo(xx, yy);	// �y�������W( xx, yy)�Ɉړ�������i�ړ����邾���Ȃ̂ŁA���͈����Ă��Ȃ��j
		}
		else {
			myDC.LineTo(xx, yy);	// �y�������W ( xx, yy)�Ɉړ������Ȃ����������
		}
	}

	// �O���t�̕`��͂����܂�

	myPictDC.BitBlt(0, 0, xsize, ysize, &myDC, 0, 0, SRCCOPY); // �o�b�t�@�����������ʁimyPictDC)�Ƀf�[�^��]������

	myDC.SelectObject(oldPen);	// �ȑO�̃y���ɖ߂��Ă���

	// �O���_�C�A�O�����̕`��
	// Jun. 26, 2024

	CClientDC myPictDC2(&mPICT2); // Picture Control�ɐݒ肵���ϐ��imPICT2�j����`��p�f�o�C�X�R���e�L�X�g�����
	CRect myRect2; // �l�ӌ`�̃T�C�Y��\�����邽�߂̍\����
	CDC myDC2; // �`��o�b�t�@�������p�̃f�o�C�X�R���e�L�X�g
	int xsize2, ysize2;

	int start2;	// �`����J�n����T���v���ԍ��A�`�悷��T���v����
	int plot_count2;	// �O���t�Ƀv���b�g����_�̐�
	double xgain2, ygain2; // �O���t�`��ɂ�����g��E�k���W��

	if (rf_status == 1) {
		start2 = datasize - graphspan;
		if (start2 < 0) {
			start2 = 0;
		}
	}
	else {
		start2 = datapoint; // �`��J�n�T���v���ԍ�
	}

	plot_count2 = graphspan; // �`��̈�̑S���ɑ�������T���v�����i���ۂ̃f�[�^�T���v�����ł͂Ȃ��j

	if ((start2 + plot_count2) > datasize) {
		plot_count2 = datasize - start2;	// �f�[�^�̐�������Ȃ��̂ŁAplot_count�����炵�Ē�������
	}

	if (plot_count2 < 2) return TRUE; // �O���t�̕`��ɂ͂Q�ȏ�̃T���v�����K�v

	mPICT2.GetClientRect(myRect2); //Picture Control�ɂ��`��̈��\���l�ӌ`�̃T�C�Y�i�s�N�Z���P�ʁj���擾����
	xsize2 = myRect2.Width();
	xgain2 = (double)xsize2 / PHI_RANGE;	// X�������s�N�Z�����ƃf�[�^�v�̐��l�͈͂���ϊ��W�������߂�
	ysize2 = myRect2.Height();
	ygain2 = (double)ysize2 / THETA_RANGE; // Y�������s�N�Z�����ƃf�[�^�̐��l�͈͂���ϊ��W�������߂�

	myDC2.CreateCompatibleDC(&myPictDC2);	//mPICT1�Ɠ���������`��o�b�t�@�������p�̃f�o�C�X�R���e�L�X�g�ɐݒ肷��
	HBITMAP memBM2 = CreateCompatibleBitmap(myPictDC2, xsize2, ysize2); // ���ۂ̕`��Ɋւ��摜�������̐�����ݒ肷��
	memBM2 = CreateCompatibleBitmap(myPictDC2, xsize2, ysize2);
	SelectObject(myDC2, memBM2); // �摜�������̑������o�b�t�@�������̃f�o�C�X�R���e�L�X�g�ɑΉ��Â���

	myDC2.FillSolidRect(myRect2, RGB(255, 255, 255)); // ��`�̈�𔒂œh��Ԃ�
	CPen myPen2(PS_SOLID, 1, RGB(0, 0, 0)); // �y���̎�ށiSOLID�F�����j�A�y�����i1�s�N�Z��)�A�F�iR, G, B)
	CPen* oldPen2 = myDC2.SelectObject(&myPen2);	// �y����myPen�Ɏ����ւ���Ɠ����ɁA�ȑO�̃y����oldPen�ɋL��������

	// ��������O���t��`�悷��
	// �`��J�n�T���v���ԍ��@start
	// �`��T���v�����@total
	// X���`�掞�̊g��k���W���@xgain (double)
	// Y���`�掞�̊g��k���W���@ygain (double)
	// X���f�[�^�̃[���_�I�t�Z�b�g  PHI_OFFSET (double)
	// Y���f�[�^�̃[���_�I�t�Z�b�g�@THETA_OFFSET (double)

//	int i, xx, yy;

	for (i = 0; i < plot_count2; i++) {
		xx = (int)(xgain2 * (-databuf[12][start2 + i] + PHI_OFFSET));
		yy = (int)(ygain2 * (databuf[11][start2 + i] + THETA_OFFSET));
		// �̈�O�ɕ`�悵�Ȃ��悤�ɃN���b�s���O�������s��
		xx = (xx < 0) ? 0 : xx;
		yy = (yy < 0) ? 0 : yy;
		xx = (xx > (xsize - 1)) ? xsize - 1 : xx;
		yy = (yy > (ysize - 1)) ? ysize - 1 : yy;
		if (i == 0) {
			myDC2.MoveTo(xx, yy);	// �y�������W( xx, yy)�Ɉړ�������i�ړ����邾���Ȃ̂ŁA���͈����Ă��Ȃ��j
		}
		else {
			myDC2.LineTo(xx, yy);	// �y�������W ( xx, yy)�Ɉړ������Ȃ����������
		}
	}

	// �O���t�̕`��͂����܂�

	myPictDC2.BitBlt(0, 0, xsize2, ysize2, &myDC2, 0, 0, SRCCOPY); // �o�b�t�@�����������ʁimyPictDC)�Ƀf�[�^��]������

	myDC2.SelectObject(oldPen2);	// �ȑO�̃y���ɖ߂��Ă���


	// �O���_�C�A�O�����@�����R�[�h�����܂�

	if (rf_status == 1) {
		s.Format(_T("Sample Count = %d Error = %d"), start, rf_errcnt);
	}
	else {
		s.Format(_T("Sample Count = %d"), start);
	}
	msgED.SetWindowTextW(s);

	DeleteDC(myDC); // �������o�b�t�@�̃f�o�C�X�R���e�L�X�g���������
	DeleteObject(memBM); // �摜�������̐�����\���r�b�g�}�b�v���������

	DeleteDC(myDC2);
	DeleteObject(memBM2);

	rf_interlock = 0; // �`�悪�����������Ƃ��O���[�o���ϐ�����ē`����

	//�ȉ��I���W�i��
	CString mes_swing;
	CString mes_wrist;
	CString mes_result;
	val = databuf[4][start];

	AATL += abs(databuf[16][start]);

	//1���ԒP�ʑO�̎��Ђ˂�p�Ƃ̍��̐�Βl��sum_data_dif�ɉ��Z����
	double wrist_def;
	wrist_def = databuf[12][start] - databuf[12][start - 1];
	if (wrist_def < 0) {
		sum_theta_dif += -1 * wrist_def;
	}
	else {
		sum_theta_dif += wrist_def;
	}

	sample_count++;

	//��~���Ă��邱�Ƃ𔻒�
	jud_stop(start);

	//��莞�Ԉȏ�~�܂��Ă���Ƃ��p�����[�^�����Z�b�g
	if (stop_count >= 20) {
		dir = 0;
		period = 0;
		peak_timing = -1;
		peak_val = 0.0;
		sum_swing_speed = 0;
		sum_theta_dif = 0;
		sample_count = 0;
	}

	//�����p���ň�莞�Ԏ~�܂��Ă���Ƃ������t���O�𗧂Ă�
	jud_pour(start);
	
	//�O�r�p���p���̒l����U�葬�x�����߂�
	//dir��0�̎��U�艺�낵�����A1�̎��U��グ����
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

	mes_swing.Format(_T("���ώ���: %lf s\r\n�X�R�A: %lf"), swing_average * 32.0, swing_score);
	mes_wrist.Format(_T("�p�x����: %lf �x\r\n�X�R�A: %lf"), theta_average, theta_score);
	mes_result.Format(_T("�����X�R�A: %lf\r\nBPM: %lf\r\npour: %d"), whole_score, bpm_buf[0][start], pour);
	msgED2.SetWindowTextW(mes_wrist);
	msgED3.SetWindowTextW(mes_swing);
	msgED4.SetWindowTextW(mes_result);

	//�I���W�i�������܂�

	return TRUE; // LRESULT�^�֐��͘_���l�����^�[�����邱�ƂɂȂ��Ă���
}