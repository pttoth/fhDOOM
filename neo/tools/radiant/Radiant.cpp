/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../../idlib/StrRef.h"

#include "qe3.h"
#include "radiant.h"
#include "MainFrm.h"
#include "lightdlg.h"

#include <process.h>    // for _beginthreadex and _endthreadex
#include <ddeml.h>  // for MSGF_DDEMGR

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

idCVar radiant_entityMode( "radiant_entityMode", "0", CVAR_TOOL | CVAR_ARCHIVE, "" );

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp

BEGIN_MESSAGE_MAP(CRadiantApp, CWinApp)
	//{{AFX_MSG_MAP(CRadiantApp)
	ON_COMMAND(ID_HELP, OnHelp)
	//}}AFX_MSG_MAP
	// Standard file based document commands
	ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
	// Standard print setup command
	ON_COMMAND(ID_FILE_PRINT_SETUP, CWinApp::OnFilePrintSetup)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp construction

CRadiantApp::CRadiantApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CRadiantApp object

CRadiantApp theApp;
HINSTANCE g_DoomInstance = NULL;
bool g_editorAlive = false;

void RadiantPrint( fhStrRef text ) {
	if ( g_editorAlive && g_Inspectors ) {
		if (g_Inspectors->consoleWnd.GetSafeHwnd()) {
			g_Inspectors->consoleWnd.AddText( text );
		}
	}
}

void RadiantShutdown( void ) {
	theApp.ExitInstance();
}

/*
=================
RadiantInit

This is also called when you 'quit' in doom
=================
*/
void RadiantInit( void ) {

	// make sure the renderer is initialized
	if ( !renderSystem->IsOpenGLRunning() ) {
		common->Printf( "no OpenGL running\n" );
		return;
	}

	g_editorAlive = true;

	// allocate a renderWorld and a soundWorld
	if ( g_qeglobals.rw == NULL ) {
		g_qeglobals.rw = renderSystem->AllocRenderWorld();
		g_qeglobals.rw->InitFromMap( NULL );
	}
	if ( g_qeglobals.sw == NULL ) {
		g_qeglobals.sw = soundSystem->AllocSoundWorld( g_qeglobals.rw );
	}

	if ( g_DoomInstance ) {
		if ( ::IsWindowVisible( win32.hWnd ) ) {
			::ShowWindow( win32.hWnd, SW_HIDE );
			g_pParentWnd->ShowWindow( SW_SHOW );
			g_pParentWnd->SetFocus();
		}
	} else {
		Sys_GrabMouseCursor( false );

		g_DoomInstance = win32.hInstance;
		CWinApp* pApp = AfxGetApp();
		CWinThread *pThread = AfxGetThread();

		InitAfx();

		// App global initializations (rare)
		pApp->InitApplication();

		// Perform specific initializations
		pThread->InitInstance();

		glFinish();
		//wglMakeCurrent(0, 0);
		wglMakeCurrent(win32.hDC, win32.hGLRC);

		// hide the doom window by default
		::ShowWindow( win32.hWnd, SW_HIDE );
	}
}

void RadiantRun( void ) {
	static bool exceptionErr = false;
	int show = ::IsWindowVisible(win32.hWnd);

	try {
		if (!exceptionErr && !show) {
			//glPushAttrib(GL_ALL_ATTRIB_BITS);
			glDepthMask(true);
			theApp.Run();
			//glPopAttrib();
			//wglMakeCurrent(0, 0);
			wglMakeCurrent(win32.hDC, win32.hGLRC);
		}
	}
	catch( idException &ex ) {
		::MessageBox(NULL, ex.error, "Exception error", MB_OK);
		RadiantShutdown();
	}
}

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp initialization


BOOL CRadiantApp::InitInstance()
{
#ifdef _AFXDLL
	//Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	//Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif

	// Change the registry key under which our settings are stored.
	SetRegistryKey( EDITOR_REGISTRY_KEY );

	LoadStdProfileSettings();  // Load standard INI file options (including MRU)

	// create main MDI Frame window

	g_PrefsDlg.LoadPrefs();

	CMainFrame* pMainFrame = new CMainFrame();
	if (!pMainFrame->LoadFrame(IDR_MENU_QUAKE3)) {
		return FALSE;
	}

	if (pMainFrame->m_hAccelTable) {
		::DestroyAcceleratorTable(pMainFrame->m_hAccelTable);
	}

	pMainFrame->LoadAccelTable(MAKEINTRESOURCE(IDR_MINIACCEL));

	m_pMainWnd = pMainFrame;

	// The main window has been initialized, so show and update it.
	pMainFrame->ShowWindow(m_nCmdShow);
	pMainFrame->UpdateWindow();

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp commands

int CRadiantApp::ExitInstance()
{
	common->Shutdown();
	g_pParentWnd = NULL;
	int ret = CWinApp::ExitInstance();
	ExitProcess(0);
	return ret;
}


BOOL CRadiantApp::OnIdle(LONG lCount) {
	if (g_pParentWnd) {
		g_pParentWnd->RoutineProcessing();
	}
	return FALSE;
	//return CWinApp::OnIdle(lCount);
}

void CRadiantApp::OnHelp()
{
	ShellExecute(m_pMainWnd->GetSafeHwnd(), "open", "http://www.idDevNet.com", NULL, NULL, SW_SHOW);
}

int CRadiantApp::Run( void )
{
	BOOL bIdle = TRUE;
	LONG lIdleCount = 0;


#if _MSC_VER >= 1300
	MSG *msg = AfxGetCurrentMessage();			// TODO Robert fix me!!
#else
	MSG *msg = &m_msgCur;
#endif

	// phase1: check to see if we can do idle work
	while (bIdle &&	!::PeekMessage(msg, NULL, NULL, NULL, PM_NOREMOVE)) {
		// call OnIdle while in bIdle state
		if (!OnIdle(lIdleCount++)) {
			bIdle = FALSE; // assume "no idle" state
		}
	}

	// phase2: pump messages while available
	do {
		// pump message, but quit on WM_QUIT
		if (!PumpMessage()) {
			return ExitInstance();
		}

		// reset "no idle" state after pumping "normal" message
		if (IsIdleMessage(msg)) {
			bIdle = TRUE;
			lIdleCount = 0;
		}

	} while (::PeekMessage(msg, NULL, NULL, NULL, PM_NOREMOVE));

	return 0;
}


/*
=============================================================

REGISTRY INFO

=============================================================
*/

bool SaveRegistryInfo(const char *pszName, void *pvBuf, long lSize)
{
	SetCvarBinary(pszName, pvBuf, lSize);
	common->WriteFlaggedCVarsToFile( "editor.cfg", CVAR_TOOL, "sett" );
	return true;
}

bool LoadRegistryInfo(const char *pszName, void *pvBuf, long *plSize)
{
	return GetCvarBinary(pszName, pvBuf, *plSize);
}


/*
===============================================================

  STATUS WINDOW

===============================================================
*/

void Sys_Status(const char *psz, int part )
{
	if ( part < 0 ) {
		common->Printf("%s", psz);
		part = 0;
	}
	g_pParentWnd->SetStatusText(part, psz);
}
