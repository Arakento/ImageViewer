#include "framework.h"
#include "ImageViewer.h"
#include "WndPictre.h"
#include "WndThumbnail.h"
#include <commdlg.h>
#include <stdio.h>
#include <shobjidl.h>

#define MAX_LOADSTRING 100

#define ID_BTN_BIG      (100)
#define ID_BTN_SMALL    (101)
#define ID_BTN_SELECT   (102)
#define IDC_LIST_CLASS  (107) // クラスリスト用のID

// グローバル変数:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
HWND hBtnBig, hBtnSmall, hBtnSelect, hEdit, hWndPicture, hStaticZoom, hWndThumbnail;
HWND hStaticClass, hListClass; // 右側のクラス選択パネル用
HBRUSH hbrDarkBackground;

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        MessageBox(NULL, L"システム(COM)の初期化に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
        return FALSE;
    }

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_IMAGEVIEWER, szWindowClass, MAX_LOADSTRING);

    hbrDarkBackground = CreateSolidBrush(RGB(45, 45, 48));

    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow)) {
        DeleteObject(hbrDarkBackground);
        CoUninitialize();
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_IMAGEVIEWER));
    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DeleteObject(hbrDarkBackground);
    CoUninitialize();

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_IMAGEVIEWER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = hbrDarkBackground;
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_IMAGEVIEWER);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 800, 600, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

void UpdateZoomText() {
    float zoom = GetZoomRatio(hWndPicture);
    WCHAR buf[32];
    swprintf_s(buf, 32, L"%d%%", (int)(zoom * 100));
    SetWindowText(hStaticZoom, buf);
}

void OnCreate(HWND hWnd) {
    // 1. 上部のUI作成
    hBtnSelect = CreateWindow(L"button", L"…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 10, 30, 30, hWnd, (HMENU)ID_BTN_SELECT, hInst, NULL);
    hEdit = CreateWindow(L"edit", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL | ES_READONLY, 50, 10, 400, 30, hWnd, (HMENU)103, hInst, NULL);
    hBtnBig = CreateWindow(L"button", L"＋", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 460, 10, 30, 30, hWnd, (HMENU)ID_BTN_BIG, hInst, NULL);
    hBtnSmall = CreateWindow(L"button", L"－", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 500, 10, 30, 30, hWnd, (HMENU)ID_BTN_SMALL, hInst, NULL);
    hStaticZoom = CreateWindow(L"static", L"100%", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_CENTER, 540, 10, 60, 30, hWnd, (HMENU)105, hInst, NULL);

    // 2. ウィンドウクラスの登録
    RegisterPictureWnd(hInst);
    RegisterThumbnailWnd(hInst);

    // 3. メイン画像・サムネイル領域の生成
    hWndPicture = CreateWindow(L"PictureWnd", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 50, 600, 380, hWnd, (HMENU)104, hInst, NULL);
    hWndThumbnail = CreateWindow(L"ThumbnailWnd", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 440, 760, 120, hWnd, (HMENU)106, hInst, NULL);

    // 4. 右パネル(クラス選択リスト)の作成
    hStaticClass = CreateWindow(L"static", L"クラス選択:", WS_CHILD | WS_VISIBLE, 0, 0, 100, 20, hWnd, NULL, hInst, NULL);
    hListClass = CreateWindowEx(WS_EX_CLIENTEDGE, L"listbox", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 0, 0, 100, 200, hWnd, (HMENU)IDC_LIST_CLASS, hInst, NULL);

    // リストに項目を追加
    for (int i = 0; i < 10; ++i) {
        WCHAR buf[32];
        swprintf_s(buf, 32, L"Class %d", i);
        SendMessage(hListClass, LB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessage(hListClass, LB_SETCURSEL, 0, 0); // 初期選択
}

void OnDestroy(HWND hWnd) {
    if (hBtnBig) DestroyWindow(hBtnBig);
    if (hBtnSmall) DestroyWindow(hBtnSmall);
    if (hBtnSelect) DestroyWindow(hBtnSelect);
    if (hEdit) DestroyWindow(hEdit);
    if (hStaticZoom) DestroyWindow(hStaticZoom);
    if (hWndPicture) DestroyWindow(hWndPicture);
    if (hWndThumbnail) DestroyWindow(hWndThumbnail);
    if (hStaticClass) DestroyWindow(hStaticClass);
    if (hListClass) DestroyWindow(hListClass);
}

void OnBtnSelect(HWND hWnd, WPARAM wParam, LPARAM lParam) {
    int code = HIWORD(wParam);
    if (code == BN_CLICKED) {
        IFileOpenDialog* pFileOpen = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

        if (SUCCEEDED(hr)) {
            DWORD dwOptions;
            if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
                pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            }

            hr = pFileOpen->Show(hWnd);

            if (SUCCEEDED(hr)) {
                IShellItem* pItem = nullptr;
                hr = pFileOpen->GetResult(&pItem);
                if (SUCCEEDED(hr)) {
                    PWSTR pszFolderPath = nullptr;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath);

                    if (SUCCEEDED(hr)) {
                        SetWindowText(hEdit, pszFolderPath);

                        WCHAR folderPath[MAX_PATH] = { 0 };
                        wcscpy_s(folderPath, pszFolderPath);
                        size_t len = wcslen(folderPath);
                        if (len > 0 && folderPath[len - 1] != L'\\') {
                            wcscat_s(folderPath, L"\\");
                        }

                        LoadThumbnailsFromFolder(hWndThumbnail, folderPath);
                        CoTaskMemFree(pszFolderPath);
                    }
                    pItem->Release();
                }
            }
            pFileOpen->Release();
        }
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        OnCreate(hWnd);
        break;

    case WM_SIZE:
    {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        int thumbHeight = 120;
        int panelWidth = 120;

        // メイン画像領域 (右パネルの分だけ幅を小さくする)
        if (hWndPicture) {
            MoveWindow(hWndPicture, 10, 50, width - 30 - panelWidth, height - 60 - thumbHeight, TRUE);
        }

        // 右パネル配置
        if (hStaticClass) {
            MoveWindow(hStaticClass, width - 10 - panelWidth, 50, panelWidth, 20, TRUE);
        }
        if (hListClass) {
            MoveWindow(hListClass, width - 10 - panelWidth, 70, panelWidth, height - 80 - thumbHeight, TRUE);
        }

        // サムネイル領域
        if (hWndThumbnail) {
            MoveWindow(hWndThumbnail, 10, height - 10 - thumbHeight, width - 20, thumbHeight, TRUE);
        }
    }
    break;

    case WM_THUMBNAIL_SELECTED:
    {
        int index = (int)wParam;
        const WCHAR* path = GetSelectedThumbnailPath(hWndThumbnail, index);
        if (path) {
            SetWindowText(hEdit, path);
            SetPictureFilePath(hWndPicture, path);
            UpdateZoomText();
        }
    }
    break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(220, 220, 220));
        SetBkColor(hdcStatic, RGB(45, 45, 48));
        return (LRESULT)hbrDarkBackground;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDC_LIST_CLASS:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                int selIndex = (int)SendMessage(hListClass, LB_GETCURSEL, 0, 0);
                if (selIndex != LB_ERR) {
                    SetCurrentClassId(selIndex); // WndPicture側に選択されたIDを渡す
                }
            }
            break;
        case ID_BTN_SELECT:
            OnBtnSelect(hWnd, wParam, lParam);
            break;
        case ID_BTN_BIG:
            ZoomIn(hWndPicture);
            UpdateZoomText();
            break;
        case ID_BTN_SMALL:
            ZoomOut(hWndPicture);
            UpdateZoomText();
            break;
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_DESTROY:
        OnDestroy(hWnd);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG: return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}