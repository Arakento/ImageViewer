#pragma once
#include <windows.h>
#include <vector>

// BBoxの情報を保持する構造体
struct BBox {
    RECT rect;       // 実際の画像サイズにおける座標
    int classId;     // 学習クラスのID
};

// ここには関数の「宣言」だけを書く（中身は WndPicture.cpp に書く）
void RegisterPictureWnd(HINSTANCE hInst);
void SetPictureFilePath(HWND hWnd, const WCHAR* filePath);
void ZoomIn(HWND hWnd);
void ZoomOut(HWND hWnd);
float GetZoomRatio(HWND hWnd);
void SetCurrentClassId(int classId);