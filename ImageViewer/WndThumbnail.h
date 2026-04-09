#pragma once
#include <windows.h>
#include <vector>
#include <string>

#define WM_THUMBNAIL_SELECTED (WM_APP + 1)

// サムネイル一個分の情報を保持する構造体
struct ThumbnailItem {
    std::wstring filePath;
    HBITMAP hThumbBmp = NULL;
};

void RegisterThumbnailWnd(HINSTANCE hInst);
void LoadThumbnailsFromFolder(HWND hWnd, const WCHAR* folderPath);
const WCHAR* GetSelectedThumbnailPath(HWND hWnd, int index);