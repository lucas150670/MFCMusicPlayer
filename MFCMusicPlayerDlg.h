
// MFCMusicPlayerDlg.h: 头文件
//

#pragma once

#include "MusicPlayer.h"

// CMFCMusicPlayerDlg 对话框
class CMFCMusicPlayerDlg : public CDialogEx
{
// 构造
public:
	CMFCMusicPlayerDlg(CWnd* pParent = nullptr);	// 标准构造函数

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_MFCMUSICPLAYER_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 支持


// 实现
protected:
	HICON m_hIcon;
	MusicPlayer* music_player;

	// 生成的消息映射函数
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnClickedButtonOpen();
	afx_msg void OnClickedButtonPlay();
	afx_msg void OnClickedButtonPause();
	afx_msg LRESULT OnPlayerFileInit(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPlayerTimeChange(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPlayerPause(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPlayerStop(WPARAM wParam, LPARAM lParam);
	afx_msg void OnClose();
	void DestroyMediaPlayer();
	virtual void OnCancel();
	CStatic m_labelTime;
	afx_msg void OnClickedButtonStop();
};
