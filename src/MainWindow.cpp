// This file is part of BowPad.
//
// Copyright (C) 2013 - Stefan Kueng
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See <http://www.gnu.org/licenses/> for a copy of the full license text
//
#include "stdafx.h"
#include "MainWindow.h"
#include "BowPad.h"
#include "BowPadUI.h"
#include "StringUtils.h"
#include "UnicodeUtils.h"
#include "CommandHandler.h"
#include "MRU.h"
#include "KeyboardShortcutHandler.h"
#include "AppUtils.h"

#include <memory>
#include <Shobjidl.h>
#include <Shellapi.h>
#include <Shlobj.h>

IUIFramework *g_pFramework = NULL;  // Reference to the Ribbon framework.

#define STATUSBAR_DOC_TYPE      0
#define STATUSBAR_DOC_SIZE      1
#define STATUSBAR_CUR_POS       2
#define STATUSBAR_EOF_FORMAT    3
#define STATUSBAR_UNICODE_TYPE  4
#define STATUSBAR_TYPING_MODE   5
#define STATUSBAR_CAPS          6

#define URL_REG_EXPR "[A-Za-z]+://[A-Za-z0-9_\\-\\+~.:?&@=/%#,;\\{\\}\\(\\)\\[\\]\\|\\*\\!\\\\]+"


CMainWindow::CMainWindow(HINSTANCE hInst, const WNDCLASSEX* wcx /* = NULL*/)
    : CWindow(hInst, wcx)
    , m_StatusBar(hInst)
    , m_TabBar(hInst)
    , m_scintilla(hInst)
    , m_cRef(1)
{
}

CMainWindow::~CMainWindow(void)
{
}

// IUnknown method implementations.
STDMETHODIMP_(ULONG) CMainWindow::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CMainWindow::Release()
{
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
    {
        DebugBreak();
    }

    return cRef;
}

STDMETHODIMP CMainWindow::QueryInterface(REFIID iid, void** ppv)
{
    if (iid == __uuidof(IUnknown))
    {
        *ppv = static_cast<IUnknown*>(static_cast<IUIApplication*>(this));
    }
    else if (iid == __uuidof(IUIApplication))
    {
        *ppv = static_cast<IUIApplication*>(this);
    }
    else if (iid == __uuidof(IUICommandHandler))
    {
        *ppv = static_cast<IUICommandHandler*>(this);
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

//  FUNCTION: OnCreateUICommand(UINT, UI_COMMANDTYPE, IUICommandHandler)
//
//  PURPOSE: Called by the Ribbon framework for each command specified in markup, to allow
//           the host application to bind a command handler to that command.
STDMETHODIMP CMainWindow::OnCreateUICommand(
    UINT nCmdID,
    UI_COMMANDTYPE typeID,
    IUICommandHandler** ppCommandHandler)
{
    UNREFERENCED_PARAMETER(typeID);
    UNREFERENCED_PARAMETER(nCmdID);

    return QueryInterface(IID_PPV_ARGS(ppCommandHandler));
}

//  FUNCTION: OnViewChanged(UINT, UI_VIEWTYPE, IUnknown*, UI_VIEWVERB, INT)
//
//  PURPOSE: Called when the state of a View (Ribbon is a view) changes, for example, created, destroyed, or resized.
STDMETHODIMP CMainWindow::OnViewChanged(
    UINT viewId,
    UI_VIEWTYPE typeId,
    IUnknown* pView,
    UI_VIEWVERB verb,
    INT uReasonCode)
{
    UNREFERENCED_PARAMETER(uReasonCode);
    UNREFERENCED_PARAMETER(viewId);

    HRESULT hr = E_NOTIMPL;

    // Checks to see if the view that was changed was a Ribbon view.
    if (UI_VIEWTYPE_RIBBON == typeId)
    {
        switch (verb)
        {
            // The view was newly created.
        case UI_VIEWVERB_CREATE:
            {
                hr = pView->QueryInterface(IID_PPV_ARGS(&m_pRibbon));
                CComPtr<IStream> pStrm;
                std::wstring ribbonsettingspath = CAppUtils::GetDataPath() + L"\\ribbonsettings";
                hr = SHCreateStreamOnFileEx(ribbonsettingspath.c_str(), STGM_READ, 0, FALSE, NULL, &pStrm);

                if (SUCCEEDED(hr))
                    m_pRibbon->LoadSettingsFromStream(pStrm);
            }
            break;
            // The view has been resized.  For the Ribbon view, the application should
            // call GetHeight to determine the height of the ribbon.
        case UI_VIEWVERB_SIZE:
            {
                if (m_pRibbon)
                {
                    // Call to the framework to determine the desired height of the Ribbon.
                    hr = m_pRibbon->GetHeight(&m_RibbonHeight);
                    ResizeChildWindows();
                }
            }
            break;
            // The view was destroyed.
        case UI_VIEWVERB_DESTROY:
            {
                hr = S_OK;
                CComPtr<IStream> pStrm;
                std::wstring ribbonsettingspath = CAppUtils::GetDataPath() + L"\\ribbonsettings";
                hr = SHCreateStreamOnFileEx(ribbonsettingspath.c_str(), STGM_WRITE|STGM_CREATE, FILE_ATTRIBUTE_NORMAL, TRUE, NULL, &pStrm);
                if (SUCCEEDED(hr))
                {
                    LARGE_INTEGER liPos;
                    ULARGE_INTEGER uliSize;

                    liPos.QuadPart = 0;
                    uliSize.QuadPart = 0;

                    pStrm->Seek(liPos, STREAM_SEEK_SET, NULL);
                    pStrm->SetSize(uliSize);

                    m_pRibbon->SaveSettingsToStream(pStrm);
                }
                m_pRibbon->Release();
                m_pRibbon = NULL;
            }
            break;
        }
    }

    return hr;
}

STDMETHODIMP CMainWindow::OnDestroyUICommand(
    UINT32 nCmdID,
    UI_COMMANDTYPE typeID,
    IUICommandHandler* commandHandler)
{
    UNREFERENCED_PARAMETER(commandHandler);
    UNREFERENCED_PARAMETER(typeID);
    UNREFERENCED_PARAMETER(nCmdID);

    return E_NOTIMPL;
}

STDMETHODIMP CMainWindow::UpdateProperty(
    UINT nCmdID,
    REFPROPERTYKEY key,
    const PROPVARIANT* ppropvarCurrentValue,
    PROPVARIANT* ppropvarNewValue)
{
    UNREFERENCED_PARAMETER(ppropvarCurrentValue);

    HRESULT hr = E_NOTIMPL;
    ICommand * pCmd = CCommandHandler::Instance().GetCommand(nCmdID);
    if (pCmd)
        hr = pCmd->IUICommandHandlerUpdateProperty(key, ppropvarCurrentValue, ppropvarNewValue);

    if (key == UI_PKEY_TooltipTitle)
    {
        std::wstring shortkey = CKeyboardShortcutHandler::Instance().GetShortCutStringForCommand((WORD)nCmdID);
        if (!shortkey.empty())
        {
            hr = UIInitPropertyFromString(UI_PKEY_TooltipTitle, shortkey.c_str(), ppropvarNewValue);
        }
    }
    return hr;
}

STDMETHODIMP CMainWindow::Execute(
    UINT nCmdID,
    UI_EXECUTIONVERB verb,
    const PROPERTYKEY* key,
    const PROPVARIANT* ppropvarValue,
    IUISimplePropertySet* pCommandExecutionProperties)
{
    UNREFERENCED_PARAMETER(pCommandExecutionProperties);
    UNREFERENCED_PARAMETER(ppropvarValue);
    UNREFERENCED_PARAMETER(key);
    UNREFERENCED_PARAMETER(verb);

    HRESULT hr = S_OK;

    ICommand * pCmd = CCommandHandler::Instance().GetCommand(nCmdID);
    if (pCmd)
    {
        hr = pCmd->IUICommandHandlerExecute(verb, key, ppropvarValue, pCommandExecutionProperties);
        if (hr == E_NOTIMPL)
        {
            hr = S_OK;
            DoCommand(nCmdID);
        }
    }
    else
        DoCommand(nCmdID);
    return hr;
}

bool CMainWindow::RegisterAndCreateWindow()
{
    WNDCLASSEX wcx;

    // Fill in the window class structure with default parameters
    wcx.cbSize = sizeof(WNDCLASSEX);
    wcx.style = 0; // Don't use CS_HREDRAW or CS_VREDRAW with a Ribbon
    wcx.lpfnWndProc = CWindow::stWinMsgHandler;
    wcx.cbClsExtra = 0;
    wcx.cbWndExtra = 0;
    wcx.hInstance = hResource;
    wcx.hCursor = NULL;
    ResString clsName(hResource, IDC_BOWPAD);
    wcx.lpszClassName = clsName;
    wcx.hIcon = LoadIcon(hResource, MAKEINTRESOURCE(IDI_BOWPAD));
    wcx.hbrBackground = (HBRUSH)(COLOR_3DFACE+1);
    wcx.lpszMenuName = NULL;
    wcx.hIconSm = LoadIcon(wcx.hInstance, MAKEINTRESOURCE(IDI_BOWPAD));
    if (RegisterWindow(&wcx))
    {
        if (CreateEx(WS_EX_ACCEPTFILES|WS_EX_COMPOSITED, WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN, NULL))
        {
            ShowWindow(*this, SW_SHOW);
            UpdateWindow(*this);
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK CMainWindow::WinMsgHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        {
            m_hwnd = hwnd;
            Initialize();
            CIniSettings::Instance().RestoreWindowPos(L"MainWindow", hwnd, SW_RESTORE);
        }
        break;
    case WM_COMMAND:
        {
            return DoCommand(LOWORD(wParam));
        }
        break;
    case WM_SIZE:
        {
            ResizeChildWindows();
        }
        break;
    case WM_GETMINMAXINFO:
        {
            MINMAXINFO * mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = 400;
            mmi->ptMinTrackSize.y = 100;
            return 0;
        }
        break;
    case WM_DRAWITEM :
        {
            DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;
            if (dis->CtlType == ODT_TAB)
            {
                return ::SendMessage(dis->hwndItem, WM_DRAWITEM, wParam, lParam);
            }
        }
        break;
    case WM_DROPFILES:
        {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            if (hDrop)
            {
                int filesDropped = DragQueryFile(hDrop, 0xffffffff, NULL, 0);
                std::vector<std::wstring> files;
                for (int i = 0 ; i < filesDropped ; ++i)
                {
                    UINT len = DragQueryFile(hDrop, i, NULL, 0);
                    std::unique_ptr<wchar_t[]> pathBuf(new wchar_t[len+1]);
                    DragQueryFile(hDrop, i, pathBuf.get(), len+1);
                    files.push_back(pathBuf.get());
                }
                DragFinish(hDrop);
                for (auto it:files)
                    OpenFile(it);
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR pNMHDR = reinterpret_cast<LPNMHDR>(lParam);
            if ((pNMHDR->idFrom == (UINT_PTR)&m_TabBar) ||
                (pNMHDR->hwndFrom == m_TabBar))
            {
                TBHDR * ptbhdr = reinterpret_cast<TBHDR*>(lParam);
                CCommandHandler::Instance().TabNotify(ptbhdr);

                switch (((LPNMHDR)lParam)->code)
                {
                case TCN_GETCOLOR:
                    if ((ptbhdr->tabOrigin >= 0) && (ptbhdr->tabOrigin < m_DocManager.GetCount()))
                    {
                        return m_DocManager.GetColorForDocument(ptbhdr->tabOrigin);
                    }
                    break;
                case TCN_SELCHANGE:
                    {
                        // document got activated
                        int tab = m_TabBar.GetCurrentTabIndex();
                        if ((tab >= 0) && (tab < m_DocManager.GetCount()))
                        {
                            CDocument doc = m_DocManager.GetDocument(tab);
                            m_scintilla.Call(SCI_SETDOCPOINTER, 0, doc.m_document);
                            m_scintilla.SetupLexerForLang(doc.m_language);
                            m_scintilla.RestoreCurrentPos(doc.m_position);
                            m_scintilla.SetTabSettings();
                            HandleOutsideModifications(tab);
                            SetFocus(m_scintilla);
                            m_scintilla.Call(SCI_GRABFOCUS);
                            UpdateStatusBar(true);
                        }
                    }
                    break;
                case TCN_SELCHANGING:
                    {
                        // document is about to be deactivated
                        int tab = m_TabBar.GetCurrentTabIndex();
                        if ((tab >= 0) && (tab < m_DocManager.GetCount()))
                        {
                            CDocument doc = m_DocManager.GetDocument(tab);
                            m_scintilla.SaveCurrentPos(&doc.m_position);
                            m_DocManager.SetDocument(tab, doc);
                        }
                    }
                    break;
                case TCN_TABDROPPED:
                    {
                        int src = m_TabBar.GetSrcTab();
                        int dst = m_TabBar.GetDstTab();
                        m_DocManager.ExchangeDocs(src, dst);
                    }
                    break;
                case TCN_TABDELETE:
                    {
                        int tab = m_TabBar.GetCurrentTabIndex();
                        if (CloseTab(ptbhdr->tabOrigin))
                        {
                            if (tab == ptbhdr->tabOrigin)
                            {
                                if (tab > 0)
                                {
                                    m_TabBar.ActivateAt(tab-1);
                                }
                            }
                        }
                    }
                    break;
                }
            }
            else if ((pNMHDR->idFrom == (UINT_PTR)&m_scintilla) ||
                (pNMHDR->hwndFrom == m_scintilla))
            {
                if (pNMHDR->code == NM_COOLSB_CUSTOMDRAW)
                {
                    return m_scintilla.HandleScrollbarCustomDraw(wParam, (NMCSBCUSTOMDRAW *)lParam);
                }

                Scintilla::SCNotification * pScn = reinterpret_cast<Scintilla::SCNotification *>(lParam);
                CCommandHandler::Instance().ScintillaNotify(pScn);

                switch (pScn->nmhdr.code)
                {
                case SCN_PAINTED:
                    {
                        m_scintilla.UpdateLineNumberWidth();
                    }
                    break;
                case SCN_SAVEPOINTREACHED:
                case SCN_SAVEPOINTLEFT:
                    {
                        int tab = m_TabBar.GetCurrentTabIndex();
                        if ((tab >= 0) && (tab < m_DocManager.GetCount()))
                        {
                            CDocument doc = m_DocManager.GetDocument(tab);
                            doc.m_bIsDirty = pScn->nmhdr.code == SCN_SAVEPOINTLEFT;
                            m_DocManager.SetDocument(tab, doc);
                            TCITEM tie;
                            tie.lParam = -1;
                            tie.mask = TCIF_IMAGE;
                            tie.iImage = doc.m_bIsDirty||doc.m_bNeedsSaving?UNSAVED_IMG_INDEX:SAVED_IMG_INDEX;
                            if (doc.m_bIsReadonly)
                                tie.iImage = REDONLY_IMG_INDEX;
                            ::SendMessage(m_TabBar, TCM_SETITEM, tab, reinterpret_cast<LPARAM>(&tie));
                        }
                    }
                    break;
                case SCN_MARGINCLICK:
                    {
                        m_scintilla.MarginClick(pScn);
                    }
                    break;
                case SCN_UPDATEUI:
                    {
                        if ((pScn->updated & SC_UPDATE_SELECTION) ||
                            (pScn->updated & SC_UPDATE_H_SCROLL)  ||
                            (pScn->updated & SC_UPDATE_V_SCROLL))
                            m_scintilla.MarkSelectedWord();

                        m_scintilla.MatchBraces();
                        m_scintilla.MatchTags();
                        AddHotSpots();
                        UpdateStatusBar(false);
                    }
                    break;
                case SCN_CHARADDED:
                    {
                        AutoIndent(pScn);
                    }
                    break;
                case SCN_HOTSPOTCLICK:
                    {
                        if (pScn->modifiers & SCMOD_CTRL)
                        {
                            m_scintilla.Call(SCI_SETWORDCHARS, 0, (LPARAM)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-+.,:?&@=/%#()");

                            long pos = pScn->position;
                            long startPos = static_cast<long>(m_scintilla.Call(SCI_WORDSTARTPOSITION, pos, false));
                            long endPos = static_cast<long>(m_scintilla.Call(SCI_WORDENDPOSITION, pos, false));

                            m_scintilla.Call(SCI_SETTARGETSTART, startPos);
                            m_scintilla.Call(SCI_SETTARGETEND, endPos);

                            long posFound = (long)m_scintilla.Call(SCI_SEARCHINTARGET, strlen(URL_REG_EXPR), (LPARAM)URL_REG_EXPR);
                            if (posFound != -1)
                            {
                                startPos = int(m_scintilla.Call(SCI_GETTARGETSTART));
                                endPos = int(m_scintilla.Call(SCI_GETTARGETEND));
                            }

                            std::unique_ptr<char[]> urltext(new char[endPos - startPos + 2]);
                            Scintilla::TextRange tr;
                            tr.chrg.cpMin = startPos;
                            tr.chrg.cpMax = endPos;
                            tr.lpstrText = urltext.get();
                            m_scintilla.Call(SCI_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr));


                            // This treatment would fail on some valid URLs where there's actually supposed to be a comma or parenthesis at the end.
                            size_t lastCharIndex = strlen(urltext.get())-1;
                            while (lastCharIndex >= 0 && (urltext[lastCharIndex] == ',' || urltext[lastCharIndex] == ')' || urltext[lastCharIndex] == '('))
                            {
                                urltext[lastCharIndex] = '\0';
                                --lastCharIndex;
                            }

                            std::wstring url = CUnicodeUtils::StdGetUnicode(urltext.get());
                            ::ShellExecute(*this, L"open", url.c_str(), NULL, NULL, SW_SHOW);
                            m_scintilla.Call(SCI_SETCHARSDEFAULT);
                        }
                    }
                    break;
                case SCN_DWELLSTART:
                    {
                        int style = (int)m_scintilla.Call(SCI_GETSTYLEAT, pScn->position);
                        if (style & INDIC1_MASK)
                        {
                            // an url hotspot
                            ResString str(hInst, IDS_CTRLCLICKTOOPEN);
                            std::string strA = CUnicodeUtils::StdGetUTF8((LPCWSTR)str);
                            m_scintilla.Call(SCI_CALLTIPSHOW, pScn->position, (sptr_t)strA.c_str());
                        }
                        else
                        {
                            m_scintilla.Call(SCI_SETWORDCHARS, 0, (LPARAM)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.,#");
                            Scintilla::Sci_TextRange tr = {0};
                            tr.chrg.cpMin = static_cast<long>(m_scintilla.Call(SCI_WORDSTARTPOSITION, pScn->position, false));
                            tr.chrg.cpMax = static_cast<long>(m_scintilla.Call(SCI_WORDENDPOSITION, pScn->position, false));
                            std::unique_ptr<char[]> word(new char[tr.chrg.cpMax - tr.chrg.cpMin + 2]);
                            tr.lpstrText = word.get();

                            m_scintilla.Call(SCI_GETTEXTRANGE, 0, (sptr_t)&tr);

                            std::string sWord = tr.lpstrText;
                            if (!sWord.empty())
                            {
                                if ((sWord[0] == '#') &&
                                    ((sWord.size() == 4) || (sWord.size() == 7)))
                                {
                                    // html color
                                    COLORREF color = 0;
                                    DWORD hexval = 0;
                                    if (sWord.size() == 4)
                                    {
                                        // shorthand form
                                        char * endptr = nullptr;
                                        char dig[2] = {0};
                                        dig[0] = sWord[1];
                                        int red = strtol(dig, &endptr, 16);
                                        red = red * 16 + red;
                                        dig[0] = sWord[2];
                                        int green = strtol(dig, &endptr, 16);
                                        green = green * 16 + green;
                                        dig[0] = sWord[3];
                                        int blue = strtol(dig, &endptr, 16);
                                        blue = blue * 16 + blue;
                                        color = RGB(red, green, blue);
                                        hexval = (RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF)) | (color & 0xFF000000);
                                    }
                                    else if (sWord.size() == 7)
                                    {
                                        // normal/long form
                                        char * endptr = nullptr;
                                        hexval = strtol(&sWord[1], &endptr, 16);
                                        color = (RGB((hexval >> 16) & 0xFF, (hexval >> 8) & 0xFF, hexval & 0xFF)) | (hexval & 0xFF000000);
                                    }
                                    std::string sCallTip = CStringUtils::Format("RGB(%d,%d,%d)\nHex: #%06lX\n####################\n####################\n####################", GetRValue(color), GetGValue(color), GetBValue(color), hexval);
                                    m_scintilla.Call(SCI_CALLTIPSETFOREHLT, color);
                                    m_scintilla.Call(SCI_CALLTIPSHOW, pScn->position, (sptr_t)sCallTip.c_str());
                                    size_t pos = sCallTip.find_first_of('\n');
                                    pos = sCallTip.find_first_of('\n', pos+1);
                                    m_scintilla.Call(SCI_CALLTIPSETHLT, pos, pos+63);
                                }
                                else
                                {
                                    char * endptr = nullptr;
                                    long number = strtol(sWord.c_str(), &endptr, 0);
                                    if (number && (endptr != &sWord[sWord.size()-1]))
                                    {
                                        // show number calltip
                                        std::string sCallTip = CStringUtils::Format("Dec: %ld - Hex: %#lX - Oct:%#lo", number, number, number);
                                        m_scintilla.Call(SCI_CALLTIPSHOW, pScn->position, (sptr_t)sCallTip.c_str());
                                    }
                                }
                            }
                            m_scintilla.Call(SCI_SETCHARSDEFAULT);
                        }
                    }
                    break;
                case SCN_DWELLEND:
                    {
                        m_scintilla.Call(SCI_CALLTIPCANCEL);
                    }
                    break;
                }
            }
        }
        break;
    case WM_SETFOCUS:
    case WM_ACTIVATE:
    case WM_ACTIVATEAPP:
        HandleOutsideModifications();
        SetFocus(m_scintilla);
        m_scintilla.Call(SCI_SETFOCUS, true);
        break;
    case WM_DESTROY:
        g_pFramework->Destroy();
        PostQuitMessage(0);
        break;
    case WM_CLOSE:
        CIniSettings::Instance().SaveWindowPos(L"MainWindow", *this);
        if (CloseAllTabs())
            ::DestroyWindow(m_hwnd);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
};

LRESULT CMainWindow::DoCommand(int id)
{
    switch (id)
    {
    case cmdExit:
        {
            if (CloseAllTabs())
                ::PostQuitMessage(0);
        }
        break;
    case cmdNew:
        {
            static int newCount = 0;
            newCount++;
            CDocument doc;
            doc.m_document = m_scintilla.Call(SCI_CREATEDOCUMENT);
            m_DocManager.AddDocumentAtEnd(doc);
            ResString newRes(hInst, IDS_NEW_TABTITLE);
            std::wstring s = CStringUtils::Format(newRes, newCount);
            int index = m_TabBar.InsertAtEnd(s.c_str());
            m_TabBar.ActivateAt(index);
            m_scintilla.SetupLexerForLang(L"Text");
        }
        break;
    case cmdClose:
        CloseTab(m_TabBar.GetCurrentTabIndex());
        break;
    case cmdCloseAll:
        CloseAllTabs();
        break;
    default:
        {
            ICommand * pCmd = CCommandHandler::Instance().GetCommand(id);
            if (pCmd)
                pCmd->Execute();
        }
        break;
    }
    return 1;
}

bool CMainWindow::Initialize()
{
    m_scintilla.Init(hResource, *this);
    int barParts[7] = {100, 300, 550, 650, 750, 780, 820};
    m_StatusBar.Init(hResource, *this, _countof(barParts), barParts);
    m_TabBar.Init(hResource, *this);
    HIMAGELIST hImgList = ImageList_Create(13, 13, ILC_COLOR32 | ILC_MASK, 0, 3);
    HICON hIcon = ::LoadIcon(hResource, MAKEINTRESOURCE(IDI_SAVED_ICON));
    ImageList_AddIcon(hImgList, hIcon);
    ::DestroyIcon(hIcon);
    hIcon = ::LoadIcon(hResource, MAKEINTRESOURCE(IDI_UNSAVED_ICON));
    ImageList_AddIcon(hImgList, hIcon);
    ::DestroyIcon(hIcon);
    hIcon = ::LoadIcon(hResource, MAKEINTRESOURCE(IDI_READONLY_ICON));
    ImageList_AddIcon(hImgList, hIcon);
    ::DestroyIcon(hIcon);
    m_TabBar.SetImageList(hImgList);
    // Here we instantiate the Ribbon framework object.
    HRESULT hr = CoCreateInstance(CLSID_UIRibbonFramework, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pFramework));
    if (FAILED(hr))
    {
        return false;
    }

    hr = g_pFramework->Initialize(*this, this);
    if (FAILED(hr))
    {
        return false;
    }

    // Finally, we load the binary markup.  This will initiate callbacks to the IUIApplication object
    // that was provided to the framework earlier, allowing command handlers to be bound to individual
    // commands.
    hr = g_pFramework->LoadUI(GetModuleHandle(NULL), L"BOWPAD_RIBBON");
    if (FAILED(hr))
    {
        return false;
    }


    // Tell UAC that lower integrity processes are allowed to send WM_COPYDATA messages to this process (or window)
    HMODULE hDll = GetModuleHandle(TEXT("user32.dll"));
    if (hDll)
    {
        // first try ChangeWindowMessageFilterEx, if it's not available (i.e., running on Vista), then
        // try ChangeWindowMessageFilter.
        typedef BOOL (WINAPI *MESSAGEFILTERFUNCEX)(HWND hWnd,UINT message,DWORD action,VOID* pChangeFilterStruct);
        MESSAGEFILTERFUNCEX func = (MESSAGEFILTERFUNCEX)::GetProcAddress( hDll, "ChangeWindowMessageFilterEx" );

        if (func)
        {
            func(*this,       WM_COPYDATA, MSGFLT_ALLOW, NULL );
            func(m_scintilla, WM_COPYDATA, MSGFLT_ALLOW, NULL );
            func(m_TabBar,    WM_COPYDATA, MSGFLT_ALLOW, NULL );
            func(m_StatusBar, WM_COPYDATA, MSGFLT_ALLOW, NULL );
        }
        else
        {
            typedef BOOL (WINAPI *MESSAGEFILTERFUNC)(UINT message,DWORD dwFlag);
            MESSAGEFILTERFUNC func = (MESSAGEFILTERFUNC)::GetProcAddress( hDll, "ChangeWindowMessageFilter" );

            if (func)
                func(WM_COPYDATA, MSGFLT_ADD);
        }
    }

    CCommandHandler::Instance().Init(this);

    return true;
}

void CMainWindow::ResizeChildWindows()
{
    RECT rect;
    GetClientRect(*this, &rect);
    HDWP hDwp = BeginDeferWindowPos(3);
    DeferWindowPos(hDwp, m_StatusBar, NULL, rect.left, rect.bottom-m_StatusBar.GetHeight(), rect.right-rect.left, m_StatusBar.GetHeight(), SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER|SWP_SHOWWINDOW|SWP_NOCOPYBITS);
    DeferWindowPos(hDwp, m_TabBar, NULL, rect.left, rect.top+m_RibbonHeight, rect.right-rect.left, rect.bottom-rect.top, SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER|SWP_SHOWWINDOW|SWP_NOCOPYBITS);
    RECT tabrc;
    TabCtrl_GetItemRect(m_TabBar, 0, &tabrc);
    MapWindowPoints(m_TabBar, *this, (LPPOINT)&tabrc, 2);
    DeferWindowPos(hDwp, m_scintilla, NULL, rect.left, rect.top+m_RibbonHeight+tabrc.bottom-tabrc.top, rect.right-rect.left, rect.bottom-(rect.top-rect.top+m_RibbonHeight+tabrc.bottom-tabrc.top)-m_StatusBar.GetHeight(), SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER|SWP_SHOWWINDOW|SWP_NOCOPYBITS);
    EndDeferWindowPos(hDwp);
}

bool CMainWindow::SaveCurrentTab(bool bSaveAs /* = false */)
{
    bool bRet = false;
    int tab = m_TabBar.GetCurrentTabIndex();
    if ((tab >= 0) && (tab < m_DocManager.GetCount()))
    {
        CDocument doc = m_DocManager.GetDocument(tab);
        if (doc.m_path.empty() || bSaveAs)
        {
            CComPtr<IFileSaveDialog> pfd = NULL;

            HRESULT hr = pfd.CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER);
            if (SUCCEEDED(hr))
            {
                // Set the dialog options
                DWORD dwOptions;
                if (SUCCEEDED(hr = pfd->GetOptions(&dwOptions)))
                {
                    hr = pfd->SetOptions(dwOptions | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
                }

                // Set a title
                if (SUCCEEDED(hr))
                {
                    ResString sTitle(hInst, IDS_APP_TITLE);
                    std::wstring s = (const wchar_t*)sTitle;
                    s += L" - ";
                    wchar_t buf[100] = {0};
                    m_TabBar.GetCurrentTitle(buf, _countof(buf));
                    s += buf;
                    pfd->SetTitle(s.c_str());
                }

                // Show the save/open file dialog
                if (SUCCEEDED(hr) && SUCCEEDED(hr = pfd->Show(*this)))
                {
                    CComPtr<IShellItem> psiResult = NULL;
                    hr = pfd->GetResult(&psiResult);
                    if (SUCCEEDED(hr))
                    {
                        PWSTR pszPath = NULL;
                        hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                        if (SUCCEEDED(hr))
                        {
                            doc.m_path = pszPath;
                            CMRU::Instance().AddPath(doc.m_path);
                        }
                    }
                }
            }
        }
        if (!doc.m_path.empty())
        {
            bRet = m_DocManager.SaveFile(*this, doc);
            if (bRet)
            {
                doc.m_bIsDirty = false;
                doc.m_bNeedsSaving = false;
                m_DocManager.UpdateFileTime(doc);
                m_DocManager.SetDocument(tab, doc);
                UpdateStatusBar(true);
                m_scintilla.Call(SCI_SETSAVEPOINT);
            }
        }
    }
    return bRet;
}

void CMainWindow::EnsureAtLeastOneTab()
{
    if (m_TabBar.GetItemCount() == 0)
        DoCommand(cmdNew);
}

void CMainWindow::GoToLine( size_t line )
{
    m_scintilla.Call(SCI_GOTOLINE, line);
}

void CMainWindow::UpdateStatusBar( bool bEverything )
{
    TCHAR strLnCol[128] = {0};
    TCHAR strSel[64] = {0};
    size_t selByte = 0;
    size_t selLine = 0;

    if (m_scintilla.GetSelectedCount(selByte, selLine))
        wsprintf(strSel, L"Sel : %d | %d", selByte, selLine);
    else
        wsprintf(strSel, L"Sel : %s", L"N/A");

    wsprintf(strLnCol, L"Ln : %d    Col : %d    %s",
                       (m_scintilla.Call(SCI_LINEFROMPOSITION, m_scintilla.Call(SCI_GETCURRENTPOS)) + 1),
                       (m_scintilla.Call(SCI_GETCOLUMN, m_scintilla.Call(SCI_GETCURRENTPOS)) + 1),
                       strSel);

    m_StatusBar.SetText(strLnCol, STATUSBAR_CUR_POS);

    TCHAR strDocLen[256];
    wsprintf(strDocLen, L"length : %d    lines : %d", m_scintilla.Call(SCI_GETLENGTH), m_scintilla.Call(SCI_GETLINECOUNT));
    m_StatusBar.SetText(strDocLen, STATUSBAR_DOC_SIZE);
    m_StatusBar.SetText(m_scintilla.Call(SCI_GETOVERTYPE) ? L"OVR" : L"INS", STATUSBAR_TYPING_MODE);
    bool bCapsLockOn = (GetKeyState(VK_CAPITAL)&0x01)!=0;
    m_StatusBar.SetText(bCapsLockOn ? L"CAPS" : L"", STATUSBAR_CAPS);
    if (bEverything)
    {
        CDocument doc = m_DocManager.GetDocument(m_TabBar.GetCurrentTabIndex());
        m_StatusBar.SetText(doc.m_language.c_str(), STATUSBAR_DOC_TYPE);
        m_StatusBar.SetText(FormatTypeToString(doc.m_format).c_str(), STATUSBAR_EOF_FORMAT);
        m_StatusBar.SetText(doc.GetEncodingString().c_str(), STATUSBAR_UNICODE_TYPE);
    }
}

bool CMainWindow::CloseTab( int tab )
{
    if ((tab < 0) || (tab >= m_DocManager.GetCount()))
        return false;
    CDocument doc = m_DocManager.GetDocument(tab);
    if (doc.m_bIsDirty||doc.m_bNeedsSaving)
    {
        m_TabBar.ActivateAt(tab);
        ResString rTitle(hInst, IDS_HASMODIFICATIONS);
        ResString rQuestion(hInst, IDS_DOYOUWANTOSAVE);
        ResString rSave(hInst, IDS_SAVE);
        ResString rDontSave(hInst, IDS_DONTSAVE);
        wchar_t buf[100] = {0};
        m_TabBar.GetCurrentTitle(buf, _countof(buf));
        std::wstring sQuestion = CStringUtils::Format(rQuestion, buf);

        TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
        TASKDIALOG_BUTTON aCustomButtons[2];
        aCustomButtons[0].nButtonID = 100;
        aCustomButtons[0].pszButtonText = rSave;
        aCustomButtons[1].nButtonID = 101;
        aCustomButtons[1].pszButtonText = rDontSave;

        tdc.hwndParent = *this;
        tdc.hInstance = hInst;
        tdc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        tdc.pButtons = aCustomButtons;
        tdc.cButtons = _countof(aCustomButtons);
        tdc.pszWindowTitle = MAKEINTRESOURCE(IDS_APP_TITLE);
        tdc.pszMainIcon = TD_INFORMATION_ICON;
        tdc.pszMainInstruction = rTitle;
        tdc.pszContent = sQuestion.c_str();
        tdc.nDefaultButton = 100;
        int nClickedBtn = 0;
        HRESULT hr = TaskDialogIndirect ( &tdc, &nClickedBtn, NULL, NULL );

        if (SUCCEEDED(hr))
        {
            if (nClickedBtn == 100)
                SaveCurrentTab();
            else if (nClickedBtn != 101)
            {
                m_TabBar.ActivateAt(tab);
                return false;  // don't close!
            }
        }

        m_TabBar.ActivateAt(tab);
    }

    if ((m_TabBar.GetItemCount() == 1)&&(m_scintilla.Call(SCI_GETTEXTLENGTH)==0)&&(m_scintilla.Call(SCI_GETMODIFY)==0)&&doc.m_path.empty())
        return true;  // leave the empty, new document as is

    m_DocManager.RemoveDocument(tab);
    m_TabBar.DeletItemAt(tab);
    EnsureAtLeastOneTab();
    m_TabBar.ActivateAt(tab < m_TabBar.GetItemCount() ? tab : m_TabBar.GetItemCount()-1);
    return true;
}

bool CMainWindow::CloseAllTabs()
{
    do
    {
        if (CloseTab(m_TabBar.GetItemCount()-1) == false)
            return false;
        if ((m_TabBar.GetItemCount() == 1)&&(m_scintilla.Call(SCI_GETTEXTLENGTH)==0)&&(m_scintilla.Call(SCI_GETMODIFY)==0)&&m_DocManager.GetDocument(0).m_path.empty())
            return true;
    } while (m_TabBar.GetItemCount() > 0);
    return true;
}

bool CMainWindow::OpenFile( const std::wstring& file )
{
    bool bRet = true;
    int encoding = -1;
    int index = m_DocManager.GetIndexForPath(file.c_str());
    if (index != -1)
    {
        // document already open
        m_TabBar.ActivateAt(index);
    }
    else
    {
        CDocument doc = m_DocManager.LoadFile(*this, file.c_str(), encoding);
        if (doc.m_document)
        {
            CMRU::Instance().AddPath(file);
            m_scintilla.Call(SCI_SETDOCPOINTER, 0, doc.m_document);
            doc.m_language = CLexStyles::Instance().GetLanguageForExt(file.substr(file.find_last_of('.')+1));
            m_scintilla.SetupLexerForExt(file.substr(file.find_last_of('.')+1).c_str());
            m_DocManager.AddDocumentAtEnd(doc);
            std::wstring sFileName = file.substr(file.find_last_of('\\')+1);
            int index = m_TabBar.InsertAtEnd(sFileName.c_str());
            m_TabBar.ActivateAt(index);
            SHAddToRecentDocs(SHARD_PATHW, file.c_str());
        }
        else
        {
            CMRU::Instance().RemovePath(file, false);
            bRet = false;
        }
    }
    return bRet;
}

bool CMainWindow::ReloadTab( int tab, int encoding )
{
    if ((tab < m_TabBar.GetItemCount())&&(tab < m_DocManager.GetCount()))
    {
        // first check if the document is modified and needs saving
        CDocument doc = m_DocManager.GetDocument(tab);
        if (doc.m_bIsDirty||doc.m_bNeedsSaving)
        {
            // doc has modifications, ask the user what to do:
            // * reload without saving
            // * save first, then reload
            // * cancel
            m_TabBar.ActivateAt(tab);
            ResString rTitle(hInst, IDS_HASMODIFICATIONS);
            ResString rQuestion(hInst, IDS_DOYOUWANTOSAVE);
            ResString rSave(hInst, IDS_SAVE);
            ResString rDontSave(hInst, IDS_DONTSAVE);
            wchar_t buf[100] = {0};
            m_TabBar.GetCurrentTitle(buf, _countof(buf));
            std::wstring sQuestion = CStringUtils::Format(rQuestion, buf);

            TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
            TASKDIALOG_BUTTON aCustomButtons[2];
            aCustomButtons[0].nButtonID = 100;
            aCustomButtons[0].pszButtonText = rSave;
            aCustomButtons[1].nButtonID = 101;
            aCustomButtons[1].pszButtonText = rDontSave;

            tdc.hwndParent = *this;
            tdc.hInstance = hInst;
            tdc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
            tdc.pButtons = aCustomButtons;
            tdc.cButtons = _countof(aCustomButtons);
            tdc.pszWindowTitle = MAKEINTRESOURCE(IDS_APP_TITLE);
            tdc.pszMainIcon = TD_INFORMATION_ICON;
            tdc.pszMainInstruction = rTitle;
            tdc.pszContent = sQuestion.c_str();
            tdc.nDefaultButton = 100;
            int nClickedBtn = 0;
            HRESULT hr = TaskDialogIndirect ( &tdc, &nClickedBtn, NULL, NULL );

            if (SUCCEEDED(hr))
            {
                if (nClickedBtn == 100)
                    SaveCurrentTab();
                else if (nClickedBtn != 101)
                {
                    m_TabBar.ActivateAt(tab);
                    return false;  // don't close!
                }
            }

            m_TabBar.ActivateAt(tab);
        }

        CDocument docreload = m_DocManager.LoadFile(*this, doc.m_path.c_str(), encoding);
        if (docreload.m_document)
        {
            if (tab == m_TabBar.GetCurrentTabIndex())
                m_scintilla.SaveCurrentPos(&doc.m_position);

            m_scintilla.Call(SCI_SETDOCPOINTER, 0, docreload.m_document);
            docreload.m_language = doc.m_language;
            docreload.m_position = doc.m_position;
            m_DocManager.SetDocument(tab, docreload);
            if (tab == m_TabBar.GetCurrentTabIndex())
            {
                m_scintilla.SetupLexerForLang(docreload.m_language);
                m_scintilla.RestoreCurrentPos(docreload.m_position);
            }
            return true;
        }
    }
    return false;
}

void CMainWindow::AddHotSpots()
{
    long startPos = 0;
    long endPos = -1;
    long endStyle = (long)m_scintilla.Call(SCI_GETENDSTYLED);


    long firstVisibleLine = (long)m_scintilla.Call(SCI_GETFIRSTVISIBLELINE);
    startPos = (long)m_scintilla.Call(SCI_POSITIONFROMLINE, m_scintilla.Call(SCI_DOCLINEFROMVISIBLE, firstVisibleLine));
    long linesOnScreen = (long)m_scintilla.Call(SCI_LINESONSCREEN);
    long lineCount = (long)m_scintilla.Call(SCI_GETLINECOUNT);
    endPos = (long)m_scintilla.Call(SCI_POSITIONFROMLINE, m_scintilla.Call(SCI_DOCLINEFROMVISIBLE, firstVisibleLine + min(linesOnScreen, lineCount)));


    m_scintilla.Call(SCI_SETSEARCHFLAGS, SCFIND_REGEXP|SCFIND_POSIX);

    m_scintilla.Call(SCI_SETTARGETSTART, startPos);
    m_scintilla.Call(SCI_SETTARGETEND, endPos);

    std::vector<unsigned char> hotspotPairs;

    unsigned char style_hotspot = 0;
    unsigned char mask = INDIC1_MASK;

    LRESULT posFound = m_scintilla.Call(SCI_SEARCHINTARGET, strlen(URL_REG_EXPR), (LPARAM)URL_REG_EXPR);

    while (posFound != -1)
    {
        long start = long(m_scintilla.Call(SCI_GETTARGETSTART));
        long end = long(m_scintilla.Call(SCI_GETTARGETEND));
        long foundTextLen = end - start;
        unsigned char idStyle = static_cast<unsigned char>(m_scintilla.Call(SCI_GETSTYLEAT, posFound));

        // Search the style
        long fs = -1;
        for (size_t i = 0 ; i < hotspotPairs.size() ; i++)
        {
            // make sure to ignore "hotspot bit" when comparing document style with archived hotspot style
            if ((hotspotPairs[i] & ~mask) == (idStyle & ~mask))
            {
                fs = hotspotPairs[i];
                m_scintilla.Call(SCI_STYLEGETFORE, fs);
                break;
            }
        }

        // if we found it then use it to colorize
        if (fs != -1)
        {
            m_scintilla.Call(SCI_STARTSTYLING, start, 0xFF);
            m_scintilla.Call(SCI_SETSTYLING, foundTextLen, fs);
        }
        else // generate a new style and add it into a array
        {
            style_hotspot = idStyle | mask; // set "hotspot bit"
            hotspotPairs.push_back(style_hotspot);
            int activeFG = 0xFF0000;
            unsigned char idStyleMSBunset = idStyle & ~mask;
            char fontNameA[128];

            // copy the style data
            m_scintilla.Call(SCI_STYLEGETFONT, idStyleMSBunset, (LPARAM)fontNameA);
            m_scintilla.Call(SCI_STYLESETFONT, style_hotspot, (LPARAM)fontNameA);

            m_scintilla.Call(SCI_STYLESETSIZE, style_hotspot, m_scintilla.Call(SCI_STYLEGETSIZE, idStyleMSBunset));
            m_scintilla.Call(SCI_STYLESETBOLD, style_hotspot, m_scintilla.Call(SCI_STYLEGETBOLD, idStyleMSBunset));
            m_scintilla.Call(SCI_STYLESETWEIGHT, style_hotspot, m_scintilla.Call(SCI_STYLEGETWEIGHT, idStyleMSBunset));
            m_scintilla.Call(SCI_STYLESETITALIC, style_hotspot, m_scintilla.Call(SCI_STYLEGETITALIC, idStyleMSBunset));
            m_scintilla.Call(SCI_STYLESETUNDERLINE, style_hotspot, m_scintilla.Call(SCI_STYLEGETUNDERLINE, idStyleMSBunset));
            m_scintilla.Call(SCI_STYLESETFORE, style_hotspot, m_scintilla.Call(SCI_STYLEGETFORE, idStyleMSBunset));
            m_scintilla.Call(SCI_STYLESETBACK, style_hotspot, m_scintilla.Call(SCI_STYLEGETBACK, idStyleMSBunset));
            m_scintilla.Call(SCI_STYLESETEOLFILLED, style_hotspot, m_scintilla.Call(SCI_STYLEGETEOLFILLED, idStyleMSBunset));
            m_scintilla.Call(SCI_STYLESETCASE, style_hotspot, m_scintilla.Call(SCI_STYLEGETCASE, idStyleMSBunset));


            m_scintilla.Call(SCI_STYLESETHOTSPOT, style_hotspot, TRUE);
            m_scintilla.Call(SCI_SETHOTSPOTACTIVEUNDERLINE, style_hotspot, TRUE);
            m_scintilla.Call(SCI_SETHOTSPOTACTIVEFORE, TRUE, activeFG);
            m_scintilla.Call(SCI_SETHOTSPOTSINGLELINE, style_hotspot, 0);

            // colorize it!
            m_scintilla.Call(SCI_STARTSTYLING, start, 0xFF);
            m_scintilla.Call(SCI_SETSTYLING, foundTextLen, style_hotspot);
        }

        m_scintilla.Call(SCI_SETTARGETSTART, posFound + foundTextLen);
        m_scintilla.Call(SCI_SETTARGETEND, endPos);

        posFound = (int)m_scintilla.Call(SCI_SEARCHINTARGET, strlen(URL_REG_EXPR), (LPARAM)URL_REG_EXPR);
    }

    m_scintilla.Call(SCI_STARTSTYLING, endStyle, 0xFF);
    m_scintilla.Call(SCI_SETSTYLING, 0, 0);
}

void CMainWindow::AutoIndent( Scintilla::SCNotification * pScn )
{
    int eolMode = int(m_scintilla.Call(SCI_GETEOLMODE));
    size_t curLine = m_scintilla.Call(SCI_LINEFROMPOSITION, m_scintilla.Call(SCI_GETCURRENTPOS));
    size_t lastLine = curLine - 1;
    int indentAmount = 0;

    if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && pScn->ch == '\n') ||
        (eolMode == SC_EOL_CR && pScn->ch == '\r'))
    {
        // use the same indentation as the last line
        while (lastLine >= 0 && (m_scintilla.Call(SCI_GETLINEENDPOSITION, lastLine) - m_scintilla.Call(SCI_POSITIONFROMLINE, lastLine)) == 0)
            lastLine--;

        if (lastLine >= 0)
            indentAmount = (int)m_scintilla.Call(SCI_GETLINEINDENTATION, lastLine);

        if (indentAmount > 0)
        {
            Scintilla::CharacterRange crange;
            crange.cpMin = long(m_scintilla.Call(SCI_GETSELECTIONSTART));
            crange.cpMax = long(m_scintilla.Call(SCI_GETSELECTIONEND));
            int posBefore = (int)m_scintilla.Call(SCI_GETLINEINDENTPOSITION, curLine);
            m_scintilla.Call(SCI_SETLINEINDENTATION, curLine, indentAmount);
            int posAfter = (int)m_scintilla.Call(SCI_GETLINEINDENTPOSITION, curLine);
            int posDifference = posAfter - posBefore;
            if (posAfter > posBefore)
            {
                // Move selection on
                if (crange.cpMin >= posBefore)
                    crange.cpMin += posDifference;
                if (crange.cpMax >= posBefore)
                    crange.cpMax += posDifference;
            }
            else if (posAfter < posBefore)
            {
                // Move selection back
                if (crange.cpMin >= posAfter)
                {
                    if (crange.cpMin >= posBefore)
                        crange.cpMin += posDifference;
                    else
                        crange.cpMin = posAfter;
                }
                if (crange.cpMax >= posAfter)
                {
                    if (crange.cpMax >= posBefore)
                        crange.cpMax += posDifference;
                    else
                        crange.cpMax = posAfter;
                }
            }
            m_scintilla.Call(SCI_SETSEL, crange.cpMin, crange.cpMax);
        }
    }
}

bool CMainWindow::HandleOutsideModifications( int index /*= -1*/ )
{
    static bool bInHandleOutsideModifications = false;
    // recurse protection
    if (bInHandleOutsideModifications)
        return false;

    bInHandleOutsideModifications = true;
    bool bRet = true;
    for (int i = 0; i < m_DocManager.GetCount(); ++i)
    {
        if (index != -1)
            i = index;

        if (m_DocManager.HasFileChanged(i))
        {
            CDocument doc = m_DocManager.GetDocument(i);
            m_TabBar.ActivateAt(i);
            ResString rTitle(hInst, IDS_OUTSIDEMODIFICATIONS);
            ResString rQuestion(hInst, doc.m_bNeedsSaving || doc.m_bIsDirty ? IDS_DOYOUWANTRELOADBUTDIRTY : IDS_DOYOUWANTTORELOAD);
            ResString rSave(hInst, IDS_SAVELOSTOUTSIDEMODS);
            ResString rReload(hInst, doc.m_bNeedsSaving || doc.m_bIsDirty ? IDS_RELOADLOSTMODS : IDS_RELOAD);
            ResString rCancel(hInst, IDS_NORELOAD);
            wchar_t buf[100] = {0};
            m_TabBar.GetCurrentTitle(buf, _countof(buf));
            std::wstring sQuestion = CStringUtils::Format(rQuestion, buf);

            TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
            TASKDIALOG_BUTTON aCustomButtons[3];
            int bi = 0;
            aCustomButtons[bi].nButtonID = bi+100;
            aCustomButtons[bi++].pszButtonText = rReload;
            if (doc.m_bNeedsSaving || doc.m_bIsDirty)
            {
                aCustomButtons[bi].nButtonID = bi+100;
                aCustomButtons[bi++].pszButtonText = rSave;
            }
            aCustomButtons[bi].nButtonID = bi+100;
            aCustomButtons[bi].pszButtonText = rCancel;

            tdc.hwndParent = *this;
            tdc.hInstance = hInst;
            tdc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT | TDF_ALLOW_DIALOG_CANCELLATION;
            tdc.pButtons = aCustomButtons;
            tdc.cButtons = doc.m_bNeedsSaving || doc.m_bIsDirty ? 3 : 2;
            tdc.pszWindowTitle = MAKEINTRESOURCE(IDS_APP_TITLE);
            tdc.pszMainIcon = doc.m_bNeedsSaving || doc.m_bIsDirty ? TD_WARNING_ICON : TD_INFORMATION_ICON;
            tdc.pszMainInstruction = rTitle;
            tdc.pszContent = sQuestion.c_str();
            tdc.nDefaultButton = 100;
            int nClickedBtn = 0;
            HRESULT hr = TaskDialogIndirect ( &tdc, &nClickedBtn, NULL, NULL );

            if (SUCCEEDED(hr))
            {
                if (nClickedBtn == 100)
                {
                    // reload the document
                    CDocument docreload = m_DocManager.LoadFile(*this, doc.m_path.c_str(), doc.m_encoding);
                    if (docreload.m_document)
                    {
                        m_scintilla.SaveCurrentPos(&doc.m_position);

                        m_scintilla.Call(SCI_SETDOCPOINTER, 0, docreload.m_document);
                        docreload.m_language = doc.m_language;
                        docreload.m_position = doc.m_position;
                        m_DocManager.SetDocument(i, docreload);
                        m_scintilla.SetupLexerForLang(docreload.m_language);
                        m_scintilla.RestoreCurrentPos(docreload.m_position);
                    }
                }
                else if ((nClickedBtn == 101) && (doc.m_bNeedsSaving || doc.m_bIsDirty))
                {
                    // save the document
                    SaveCurrentTab();
                    bRet = false;
                }
                else
                {
                    // update the filetime of the document to avoid this warning
                    m_DocManager.UpdateFileTime(doc);
                    m_DocManager.SetDocument(i, doc);
                    bRet = false;
                }
            }
            else
            {
                // update the filetime of the document to avoid this warning
                m_DocManager.UpdateFileTime(doc);
                m_DocManager.SetDocument(i, doc);
            }
        }

        if (index != -1)
            break;
    }
    bInHandleOutsideModifications = false;
    return bRet;
}
