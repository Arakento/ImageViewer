#include "framework.h"
#include "WndThumbnail.h"
#include <vector>
#include <string>
#include <stdio.h>

// stb_image (WndPicture.cppで実装済みを想定し、ここでは宣言のみ)
#include "stb_image.h"
#define STB_IMAGE_IMPLEMENTATION

static std::vector<ThumbnailItem> g_thumbnails;
static int g_selectedIndex = -1;
static const int THUMB_SIZE = 100;
static const int MARGIN = 10;


// サムネイルビットマップを破棄するヘルパー
void ClearThumbnails() {
    for (auto& item : g_thumbnails) {
        if (item.hThumbBmp) DeleteObject(item.hThumbBmp);
    }
    g_thumbnails.clear();
    g_selectedIndex = -1;
}

// 画像ファイルからサムネイル(HBITMAP)を作成する関数
HBITMAP CreateThumbnail(const WCHAR* path, int size) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path, L"rb") != 0 || !fp) return NULL;

    int w, h, c;
    unsigned char* data = stbi_load_from_file(fp, &w, &h, &c, 4);
    fclose(fp);

    if (!data) return NULL;

    // 一旦フルサイズのビットマップを作成
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HDC hdcThumb = CreateCompatibleDC(hdcScreen);

    HBITMAP hThumb = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hOldThumb = (HBITMAP)SelectObject(hdcThumb, hThumb);

    // 背景を塗りつぶし
    RECT rc = { 0, 0, size, size };
    FillRect(hdcThumb, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // DIB作成
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hFull = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hFull && pBits) {
        unsigned char* d = (unsigned char*)pBits;
        for (int i = 0; i < w * h * 4; i += 4) {
            d[i + 0] = data[i + 2]; d[i + 1] = data[i + 1]; d[i + 2] = data[i + 0]; d[i + 3] = data[i + 3];
        }
        HBITMAP hOldFull = (HBITMAP)SelectObject(hdcMem, hFull);

        // アスペクト比を維持して縮小描画
        float scale = (float)size / (w > h ? w : h);
        int tw = (int)(w * scale);
        int th = (int)(h * scale);
        SetStretchBltMode(hdcThumb, HALFTONE);
        StretchBlt(hdcThumb, (size - tw) / 2, (size - th) / 2, tw, th, hdcMem, 0, 0, w, h, SRCCOPY);

        SelectObject(hdcMem, hOldFull);
        DeleteObject(hFull);
    }

    SelectObject(hdcThumb, hOldThumb);
    DeleteDC(hdcMem);
    DeleteDC(hdcThumb);
    ReleaseDC(NULL, hdcScreen);
    stbi_image_free(data);

    return hThumb;
}

// スクロール情報の更新
void UpdateScrollInfo(HWND hWnd) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    int totalWidth = (int)g_thumbnails.size() * (THUMB_SIZE + MARGIN) + MARGIN;

    SCROLLINFO si = { 0 };
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE;
    si.nMin = 0;
    si.nMax = totalWidth;
    si.nPage = rc.right; // 表示領域の幅
    SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
}

LRESULT CALLBACK WndProcThumbnail(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        SCROLLINFO si = { 0 };
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        GetScrollInfo(hWnd, SB_HORZ, &si);
        int scrollX = si.nPos;

        HDC hdcMem = CreateCompatibleDC(hdc);

        for (int i = 0; i < (int)g_thumbnails.size(); ++i) {
            int x = MARGIN + i * (THUMB_SIZE + MARGIN) - scrollX;
            // 画面外なら描画スキップ
            if (x + THUMB_SIZE < 0) continue;
            if (x > ps.rcPaint.right) break;

            RECT rect = { x, MARGIN, x + THUMB_SIZE, MARGIN + THUMB_SIZE };

            // 画像の描画
            if (g_thumbnails[i].hThumbBmp) {
                HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, g_thumbnails[i].hThumbBmp);
                BitBlt(hdc, x, MARGIN, THUMB_SIZE, THUMB_SIZE, hdcMem, 0, 0, SRCCOPY);
                SelectObject(hdcMem, hOld);
            }

            // 選択枠
            HBRUSH hFrame = CreateSolidBrush(i == g_selectedIndex ? RGB(0, 255, 0) : RGB(100, 100, 100));
            FrameRect(hdc, &rect, hFrame);
            DeleteObject(hFrame);
        }
        DeleteDC(hdcMem);
        EndPaint(hWnd, &ps);
    }
    break;

    case WM_HSCROLL:
    {
        SCROLLINFO si = { 0 };
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hWnd, SB_HORZ, &si);

        int curPos = si.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINELEFT: si.nPos -= 20; break;
        case SB_LINERIGHT: si.nPos += 20; break;
        case SB_PAGELEFT: si.nPos -= si.nPage; break;
        case SB_PAGERIGHT: si.nPos += si.nPage; break;
        case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
        }
        si.fMask = SIF_POS;
        SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
        GetScrollInfo(hWnd, SB_HORZ, &si);
        if (si.nPos != curPos) {
            InvalidateRect(hWnd, NULL, TRUE);
        }
    }
    break;

    case WM_LBUTTONDOWN:
    {
        SCROLLINFO si = { 0 };
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        GetScrollInfo(hWnd, SB_HORZ, &si);

        int clickX = LOWORD(lParam) + si.nPos;
        int index = (clickX - MARGIN) / (THUMB_SIZE + MARGIN);

        if (index >= 0 && index < (int)g_thumbnails.size()) {
            g_selectedIndex = index;
            InvalidateRect(hWnd, NULL, TRUE);
            SendMessage(GetParent(hWnd), WM_THUMBNAIL_SELECTED, (WPARAM)index, 0);
        }
    }
    break;

    case WM_SIZE:
        UpdateScrollInfo(hWnd);
        break;

    case WM_DESTROY:
        ClearThumbnails();
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void RegisterThumbnailWnd(HINSTANCE hInst) {
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProcThumbnail;
    wcex.hInstance = hInst;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"ThumbnailWnd";
    wcex.hbrBackground = CreateSolidBrush(RGB(20, 20, 20));
    RegisterClassExW(&wcex);
}

void LoadThumbnailsFromFolder(HWND hWnd, const WCHAR* folderPath) {
    ClearThumbnails();

    std::wstring searchPath = folderPath;
    searchPath += L"*.*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                const WCHAR* ext = wcsrchr(ffd.cFileName, L'.');
                if (ext && (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".png") == 0 || _wcsicmp(ext, L".bmp") == 0 || _wcsicmp(ext, L".jpeg") == 0)) {
                    ThumbnailItem item;
                    item.filePath = folderPath;
                    item.filePath += ffd.cFileName;
                    // サムネイル画像の作成
                    item.hThumbBmp = CreateThumbnail(item.filePath.c_str(), THUMB_SIZE);
                    g_thumbnails.push_back(item);
                }
            }
        } while (FindNextFileW(hFind, &ffd) != 0);
        FindClose(hFind);
    }

    UpdateScrollInfo(hWnd);
    InvalidateRect(hWnd, NULL, TRUE);

    if (!g_thumbnails.empty()) {
        g_selectedIndex = 0;
        SendMessage(GetParent(hWnd), WM_THUMBNAIL_SELECTED, 0, 0);
    }
}

const WCHAR* GetSelectedThumbnailPath(HWND hWnd, int index) {
    if (index >= 0 && index < (int)g_thumbnails.size()) {
        return g_thumbnails[index].filePath.c_str();
    }
    return nullptr;
}