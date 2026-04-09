#define _CRT_SECURE_NO_WARNINGS
#include "framework.h"
#include "WndPictre.h" // ※ご自身のファイル名が "WndPicture.h" の場合は修正してください
#include <stdio.h>
#include <vector>
#include <string>

// stb_image.h のスタックサイズ警告(C6262)を無視する設定
#pragma warning(push)
#pragma warning(disable: 6262)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#pragma warning(pop)

// =========================================================
// 1. グローバル変数の宣言
// =========================================================
static int g_imgWidth = 0;
static int g_imgHeight = 0;
static int g_imgChannels = 0;
static unsigned char* g_imgData = nullptr;
static HBITMAP g_hBitmap = NULL;
static float g_scale = 1.0f;

static std::wstring g_currentImagePath = L"";
static int g_currentClassId = 0;

static std::vector<BBox> g_bboxes;
static int g_selectedBBoxIndex = -1; // ★追加: 選択中のBBoxのインデックス (-1は未選択)

// ★追加: マウス操作のモード管理
enum DragMode { DRAG_NONE, DRAG_NEW, DRAG_MOVE, DRAG_RESIZE_LT, DRAG_RESIZE_RT, DRAG_RESIZE_LB, DRAG_RESIZE_RB };
static DragMode g_dragMode = DRAG_NONE;

static POINT g_ptStartImg;
static POINT g_ptCurrentImg;
static int g_dragOffsetX = 0; // 移動時の掴んだ位置のズレ
static int g_dragOffsetY = 0;

void SaveBBoxesToYolo();
void LoadBBoxesFromYolo();
std::wstring GetTxtFilePath(const std::wstring& imgPath);

LRESULT CALLBACK WndProcChild(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

void SetCurrentClassId(int classId) {
    g_currentClassId = classId;
    // 選択中のBBoxがあればクラスを即座に変更する
    if (g_selectedBBoxIndex >= 0 && g_selectedBBoxIndex < g_bboxes.size()) {
        g_bboxes[g_selectedBBoxIndex].classId = classId;
        SaveBBoxesToYolo(); // 保存
    }
}

// =========================================================
// 2. YOLO形式の保存と読み込み
// =========================================================
std::wstring GetTxtFilePath(const std::wstring& imgPath) {
    size_t pos = imgPath.find_last_of(L".");
    if (pos != std::wstring::npos) return imgPath.substr(0, pos) + L".txt";
    return imgPath + L".txt";
}

void SaveBBoxesToYolo() {
    if (g_currentImagePath.empty() || g_imgWidth == 0 || g_imgHeight == 0) return;

    std::wstring txtPath = GetTxtFilePath(g_currentImagePath);
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, txtPath.c_str(), L"w") == 0 && fp) {
        for (const auto& box : g_bboxes) {
            float width = (box.rect.right - box.rect.left) / (float)g_imgWidth;
            float height = (box.rect.bottom - box.rect.top) / (float)g_imgHeight;
            float x_center = (box.rect.left + box.rect.right) / 2.0f / (float)g_imgWidth;
            float y_center = (box.rect.top + box.rect.bottom) / 2.0f / (float)g_imgHeight;
            fprintf(fp, "%d %f %f %f %f\n", box.classId, x_center, y_center, width, height);
        }
        fclose(fp);
    }
}

void LoadBBoxesFromYolo() {
    g_bboxes.clear();
    g_selectedBBoxIndex = -1; // 読み込み時は選択解除
    if (g_currentImagePath.empty() || g_imgWidth == 0 || g_imgHeight == 0) return;

    std::wstring txtPath = GetTxtFilePath(g_currentImagePath);
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, txtPath.c_str(), L"r") == 0 && fp) {
        int classId; float x_center, y_center, width, height;
        while (fscanf_s(fp, "%d %f %f %f %f", &classId, &x_center, &y_center, &width, &height) == 5) {
            BBox box;
            box.classId = classId;
            box.rect.left = (int)((x_center - width / 2.0f) * g_imgWidth);
            box.rect.right = (int)((x_center + width / 2.0f) * g_imgWidth);
            box.rect.top = (int)((y_center - height / 2.0f) * g_imgHeight);
            box.rect.bottom = (int)((y_center + height / 2.0f) * g_imgHeight);
            g_bboxes.push_back(box);
        }
        fclose(fp);
    }
}

// =========================================================
// 3. 座標計算と当たり判定 (Hit Test)
// =========================================================
void GetImageRectInfo(HWND hWnd, int& destX, int& destY, int& destW, int& destH) {
    RECT rc; GetClientRect(hWnd, &rc);
    destW = (int)(g_imgWidth * g_scale); destH = (int)(g_imgHeight * g_scale);
    destX = (rc.right - rc.left - destW) / 2; destY = (rc.bottom - rc.top - destH) / 2;
}

void ScreenToImage(HWND hWnd, int sx, int sy, int& ix, int& iy) {
    int dx, dy, dw, dh; GetImageRectInfo(hWnd, dx, dy, dw, dh);
    ix = (int)((sx - dx) / g_scale); iy = (int)((sy - dy) / g_scale);
    if (ix < 0) ix = 0; if (ix > g_imgWidth) ix = g_imgWidth;
    if (iy < 0) iy = 0; if (iy > g_imgHeight) iy = g_imgHeight;
}

void ImageToScreen(HWND hWnd, int ix, int iy, int& sx, int& sy) {
    int dx, dy, dw, dh; GetImageRectInfo(hWnd, dx, dy, dw, dh);
    sx = (int)(ix * g_scale) + dx; sy = (int)(iy * g_scale) + dy;
}

// ★追加: クリック位置がBBoxのどこに当たったか判定
enum HitState { HIT_NONE, HIT_INSIDE, HIT_LT, HIT_RT, HIT_LB, HIT_RB };
HitState CheckHit(const BBox& box, int ix, int iy, float scale) {
    int tol = (int)(6.0f / scale); // 画面上で約6ピクセルの許容範囲(つかみやすさ)
    if (tol < 1) tol = 1;

    bool nearL = abs(ix - box.rect.left) <= tol;
    bool nearR = abs(ix - box.rect.right) <= tol;
    bool nearT = abs(iy - box.rect.top) <= tol;
    bool nearB = abs(iy - box.rect.bottom) <= tol;
    bool inX = (ix >= box.rect.left && ix <= box.rect.right);
    bool inY = (iy >= box.rect.top && iy <= box.rect.bottom);

    if (nearL && nearT) return HIT_LT;
    if (nearR && nearT) return HIT_RT;
    if (nearL && nearB) return HIT_LB;
    if (nearR && nearB) return HIT_RB;
    if (inX && inY) return HIT_INSIDE;
    return HIT_NONE;
}

// =========================================================
// 4. 外部関数
// =========================================================
void ZoomIn(HWND hWnd) { g_scale *= 1.25f; InvalidateRect(hWnd, NULL, TRUE); }
void ZoomOut(HWND hWnd) { g_scale /= 1.25f; InvalidateRect(hWnd, NULL, TRUE); }
float GetZoomRatio(HWND hWnd) { return g_scale; }

void RegisterPictureWnd(HINSTANCE hInst) {
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProcChild, 0, 0, hInst, NULL, LoadCursor(nullptr, IDC_CROSS), CreateSolidBrush(RGB(30, 30, 30)), NULL, L"PictureWnd", NULL };
    RegisterClassExW(&wcex);
}

void SetPictureFilePath(HWND hWnd, const WCHAR* filePath) {
    if (g_hBitmap) { DeleteObject(g_hBitmap); g_hBitmap = NULL; }
    if (g_imgData) { stbi_image_free(g_imgData); g_imgData = nullptr; }

    g_currentImagePath = filePath;
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, filePath, L"rb") != 0 || !fp) return;

    g_imgData = stbi_load_from_file(fp, &g_imgWidth, &g_imgHeight, &g_imgChannels, 4);
    fclose(fp);
    if (!g_imgData) return;

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_imgWidth; bmi.bmiHeader.biHeight = -g_imgHeight;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr; HDC hdcScreen = GetDC(NULL);
    g_hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    ReleaseDC(NULL, hdcScreen);

    if (g_hBitmap && pBits) {
        unsigned char* dest = (unsigned char*)pBits;
        for (int i = 0; i < g_imgWidth * g_imgHeight * 4; i += 4) {
            dest[i + 0] = g_imgData[i + 2]; dest[i + 1] = g_imgData[i + 1];
            dest[i + 2] = g_imgData[i + 0]; dest[i + 3] = g_imgData[i + 3];
        }
    }
    g_scale = 1.0f;
    LoadBBoxesFromYolo();
    InvalidateRect(hWnd, NULL, TRUE);
}

// =========================================================
// 5. 描画とウィンドウプロシージャ
// =========================================================
void OnPaint(HWND hWnd, HDC hdc) {
    RECT rc; GetClientRect(hWnd, &rc);
    HBRUSH hBrushBg = CreateSolidBrush(RGB(30, 30, 30)); FillRect(hdc, &rc, hBrushBg); DeleteObject(hBrushBg);

    if (g_hBitmap) {
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, g_hBitmap);
        int destX, destY, destW, destH; GetImageRectInfo(hWnd, destX, destY, destW, destH);
        SetStretchBltMode(hdc, HALFTONE); SetBrushOrgEx(hdc, 0, 0, NULL);
        StretchBlt(hdc, destX, destY, destW, destH, hdcMem, 0, 0, g_imgWidth, g_imgHeight, SRCCOPY);
        SelectObject(hdcMem, hOldBmp); DeleteDC(hdcMem);

        // BBoxの描画
        for (int i = 0; i < (int)g_bboxes.size(); ++i) {
            const auto& box = g_bboxes[i];
            int sx1, sy1, sx2, sy2;
            ImageToScreen(hWnd, box.rect.left, box.rect.top, sx1, sy1);
            ImageToScreen(hWnd, box.rect.right, box.rect.bottom, sx2, sy2);
            RECT r = { sx1, sy1, sx2, sy2 };

            // ★変更: 選択中なら水色、それ以外は赤色
            HBRUSH hBrush = CreateSolidBrush((i == g_selectedBBoxIndex) ? RGB(0, 255, 255) : RGB(255, 50, 50));
            FrameRect(hdc, &r, hBrush);
            DeleteObject(hBrush);

            // ★追加: 選択中のBBoxの四隅にハンドル(小さな四角)を描画
            if (i == g_selectedBBoxIndex) {
                HBRUSH hHandle = CreateSolidBrush(RGB(0, 255, 255));
                int hs = 4; // ハンドルのサイズ
                RECT lt = { sx1 - hs, sy1 - hs, sx1 + hs, sy1 + hs }; FillRect(hdc, &lt, hHandle);
                RECT rt = { sx2 - hs, sy1 - hs, sx2 + hs, sy1 + hs }; FillRect(hdc, &rt, hHandle);
                RECT lb = { sx1 - hs, sy2 - hs, sx1 + hs, sy2 + hs }; FillRect(hdc, &lb, hHandle);
                RECT rb = { sx2 - hs, sy2 - hs, sx2 + hs, sy2 + hs }; FillRect(hdc, &rb, hHandle);
                DeleteObject(hHandle);
            }
        }

        // 新規作成中のドラッグ枠(緑色)
        if (g_dragMode == DRAG_NEW) {
            int sx1, sy1, sx2, sy2;
            ImageToScreen(hWnd, g_ptStartImg.x, g_ptStartImg.y, sx1, sy1);
            ImageToScreen(hWnd, g_ptCurrentImg.x, g_ptCurrentImg.y, sx2, sy2);
            RECT r;
            r.left = (sx1 < sx2) ? sx1 : sx2; r.top = (sy1 < sy2) ? sy1 : sy2;
            r.right = (sx1 > sx2) ? sx1 : sx2; r.bottom = (sy1 > sy2) ? sy1 : sy2;
            HBRUSH hDragBrush = CreateSolidBrush(RGB(50, 255, 50));
            FrameRect(hdc, &r, hDragBrush);
            DeleteObject(hDragBrush);
        }
    }
}

LRESULT CALLBACK WndProcChild(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_LBUTTONDOWN:
        if (g_hBitmap) {
            SetFocus(hWnd); // ★キーボード入力(削除)を受け付けるためにフォーカスを当てる
            SetCapture(hWnd);
            int ix, iy; ScreenToImage(hWnd, LOWORD(lParam), HIWORD(lParam), ix, iy);
            g_ptStartImg.x = ix; g_ptStartImg.y = iy;
            g_ptCurrentImg = g_ptStartImg;

            g_dragMode = DRAG_NEW;
            int clickedIndex = -1;
            HitState hit = HIT_NONE;

            // 1. まず選択中のBBoxをクリックしたか優先して判定
            if (g_selectedBBoxIndex >= 0 && g_selectedBBoxIndex < g_bboxes.size()) {
                hit = CheckHit(g_bboxes[g_selectedBBoxIndex], ix, iy, g_scale);
                if (hit != HIT_NONE) clickedIndex = g_selectedBBoxIndex;
            }

            // 2. それ以外をクリックした場合は、前面(後から描いた順)から探す
            if (clickedIndex == -1) {
                for (int i = (int)g_bboxes.size() - 1; i >= 0; --i) {
                    hit = CheckHit(g_bboxes[i], ix, iy, g_scale);
                    if (hit != HIT_NONE) { clickedIndex = i; break; }
                }
            }

            // アクション（モード）の決定
            if (clickedIndex != -1) {
                g_selectedBBoxIndex = clickedIndex;
                if (hit == HIT_INSIDE) {
                    g_dragMode = DRAG_MOVE;
                    g_dragOffsetX = ix - g_bboxes[clickedIndex].rect.left;
                    g_dragOffsetY = iy - g_bboxes[clickedIndex].rect.top;
                }
                else if (hit == HIT_LT) g_dragMode = DRAG_RESIZE_LT;
                else if (hit == HIT_RT) g_dragMode = DRAG_RESIZE_RT;
                else if (hit == HIT_LB) g_dragMode = DRAG_RESIZE_LB;
                else if (hit == HIT_RB) g_dragMode = DRAG_RESIZE_RB;
            }
            else {
                g_selectedBBoxIndex = -1; // 何も無い場所をクリックしたら選択解除
                g_dragMode = DRAG_NEW;
            }
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;

    case WM_MOUSEMOVE:
        if (g_dragMode != DRAG_NONE) {
            int ix, iy; ScreenToImage(hWnd, LOWORD(lParam), HIWORD(lParam), ix, iy);
            g_ptCurrentImg.x = ix; g_ptCurrentImg.y = iy;

            if (g_dragMode == DRAG_MOVE && g_selectedBBoxIndex != -1) {
                int bw = g_bboxes[g_selectedBBoxIndex].rect.right - g_bboxes[g_selectedBBoxIndex].rect.left;
                int bh = g_bboxes[g_selectedBBoxIndex].rect.bottom - g_bboxes[g_selectedBBoxIndex].rect.top;
                g_bboxes[g_selectedBBoxIndex].rect.left = ix - g_dragOffsetX;
                g_bboxes[g_selectedBBoxIndex].rect.top = iy - g_dragOffsetY;
                g_bboxes[g_selectedBBoxIndex].rect.right = g_bboxes[g_selectedBBoxIndex].rect.left + bw;
                g_bboxes[g_selectedBBoxIndex].rect.bottom = g_bboxes[g_selectedBBoxIndex].rect.top + bh;
            }
            else if (g_dragMode >= DRAG_RESIZE_LT && g_dragMode <= DRAG_RESIZE_RB && g_selectedBBoxIndex != -1) {
                if (g_dragMode == DRAG_RESIZE_LT) { g_bboxes[g_selectedBBoxIndex].rect.left = ix; g_bboxes[g_selectedBBoxIndex].rect.top = iy; }
                else if (g_dragMode == DRAG_RESIZE_RT) { g_bboxes[g_selectedBBoxIndex].rect.right = ix; g_bboxes[g_selectedBBoxIndex].rect.top = iy; }
                else if (g_dragMode == DRAG_RESIZE_LB) { g_bboxes[g_selectedBBoxIndex].rect.left = ix; g_bboxes[g_selectedBBoxIndex].rect.bottom = iy; }
                else if (g_dragMode == DRAG_RESIZE_RB) { g_bboxes[g_selectedBBoxIndex].rect.right = ix; g_bboxes[g_selectedBBoxIndex].rect.bottom = iy; }
            }
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;

    case WM_LBUTTONUP:
        if (g_dragMode != DRAG_NONE) {
            if (g_dragMode == DRAG_NEW) {
                if (g_ptStartImg.x != g_ptCurrentImg.x && g_ptStartImg.y != g_ptCurrentImg.y) {
                    BBox box;
                    box.rect.left = (g_ptStartImg.x < g_ptCurrentImg.x) ? g_ptStartImg.x : g_ptCurrentImg.x;
                    box.rect.top = (g_ptStartImg.y < g_ptCurrentImg.y) ? g_ptStartImg.y : g_ptCurrentImg.y;
                    box.rect.right = (g_ptStartImg.x > g_ptCurrentImg.x) ? g_ptStartImg.x : g_ptCurrentImg.x;
                    box.rect.bottom = (g_ptStartImg.y > g_ptCurrentImg.y) ? g_ptStartImg.y : g_ptCurrentImg.y;
                    box.classId = g_currentClassId;
                    g_bboxes.push_back(box);
                    g_selectedBBoxIndex = (int)g_bboxes.size() - 1; // 新規作成したものを選択状態に
                    SaveBBoxesToYolo();
                }
            }
            else if (g_selectedBBoxIndex != -1) {
                // リサイズ後に座標が反転(左辺が右辺より右に来る等)した場合は補正する
                BBox& box = g_bboxes[g_selectedBBoxIndex];
                int l = (box.rect.left < box.rect.right) ? box.rect.left : box.rect.right;
                int r = (box.rect.left > box.rect.right) ? box.rect.left : box.rect.right;
                int t = (box.rect.top < box.rect.bottom) ? box.rect.top : box.rect.bottom;
                int b = (box.rect.top > box.rect.bottom) ? box.rect.top : box.rect.bottom;
                box.rect.left = l; box.rect.right = r; box.rect.top = t; box.rect.bottom = b;
                SaveBBoxesToYolo();
            }

            g_dragMode = DRAG_NONE;
            ReleaseCapture();
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;

        // ★追加: DeleteキーまたはBackspaceキーで削除
    case WM_KEYDOWN:
        if ((wParam == VK_DELETE || wParam == VK_BACK) && g_selectedBBoxIndex != -1) {
            g_bboxes.erase(g_bboxes.begin() + g_selectedBBoxIndex); // リストから削除
            g_selectedBBoxIndex = -1; // 選択解除
            SaveBBoxesToYolo();       // ファイルを上書き保存
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        OnPaint(hWnd, hdc); EndPaint(hWnd, &ps);
    }
    break;

    case WM_ERASEBKGND: return 1;

    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}