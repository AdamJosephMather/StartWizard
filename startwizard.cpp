#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _CRT_SECURE_NO_WARNINGS

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "helper_types.h"
#include <set>
#include <windows.h>
#include <iostream>
#include <unicode/unistr.h>
#include <unicode/ustream.h>
#include <filesystem>
#include <chrono>
#include <appmodel.h>

namespace fs = std::filesystem;

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h> // Required for glfwGetWin32Window
#include "text_renderer.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <vector>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "user32.lib")

const float M_PI = 3.141592653589793238;

std::set<UChar32> whitespace = {0x20, 0x09, 0x0A, 0x0D, 0x00A0, 0x2028, 0x2029};
std::set<UChar32> numeric = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
std::set<UChar32> allowed_in_var_names = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x5F};
std::set<UChar32> punctuationset = {U'!', U'#', U'$', U'%', U'&', U'(', U')', U'*', U'+', U',', U'-', U'.', U'/', U':', U';', U'<', U'=', U'>', U'?', U'@', U'[', U'\\', U']', U'^', U'`', U'{', U'|', U'}', U'~'};

HHOOK hhkLowLevelKybd = NULL;
bool win_used_in_combo = false;
bool win_down = false;
GLFWwindow* window;

int WIN_WIDTH = 100;
int WIN_HEIGHT = 100;
int WIN_X = 0;
int WIN_Y = 0;
int RAD_BIG = 1;
int RAD_SMALL = 5;

float FONT_SIZE = 30.0;
const char* FONT_PATH;

bool recalculating = false;

Theme theme;
int border_width = 1;

struct Cursor {
	int anchor_char = 0;
	int head_char = 0;
};

float target_scroll_offset = 0;
float scroll_offset = 0; // both meassured in characters (width constant - monospace)
Cursor curs;

icu::UnicodeString current_search;

struct SubEntry {
	HWND hwnd;
	icu::UnicodeString name;
};

struct Entry {
	icu::UnicodeString name;
	std::string name_str;
	GLuint tex = 0;
	std::wstring exe;
	std::wstring aumid;
	HWND hwnd = NULL;
	std::vector<SubEntry> children;
	std::string copy = "";
	bool open = false;
};

struct App {
	icu::UnicodeString name;
	std::wstring exe;
	std::string name_str;
	std::wstring aumid;
	GLuint textureID;
};

struct WindowInfo {
	HWND hwnd;
	std::wstring title;
	std::wstring exe;
	std::wstring aumid;
};

std::vector<Entry> entries;
int selected_id = 0;
int scroll_vert = 0;

std::vector<App> apps;
std::wstring windir;

std::wstring getWinDir() {
	// 1. Get the required buffer size
	DWORD size = GetEnvironmentVariableW(L"windir", nullptr, 0);
	if (size == 0) return L""; // Variable not found

	// 2. Resize wstring to hold the path
	std::wstring result;
	result.resize(size);

	// 3. Get the variable, passing direct buffer
	// size-1 to avoid counting null terminator twice
	GetEnvironmentVariableW(L"windir", &result[0], size);
	
	// Resize to remove excess null terminator
	result.resize(size - 1);
	
	return result;
}

GLuint HBitmapToTexture(HBITMAP hBitmap) {
	if (!hBitmap) return 0;

	BITMAP bm;
	GetObject(hBitmap, sizeof(bm), &bm);

	BITMAPINFOHEADER bi = { sizeof(bi), bm.bmWidth, -bm.bmHeight, 1, 32, BI_RGB };
	std::vector<uint32_t> pixels(bm.bmWidth * bm.bmHeight);
	
	HDC hdc = GetDC(NULL);
	GetDIBits(hdc, hBitmap, 0, bm.bmHeight, pixels.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
	ReleaseDC(NULL, hdc);

	// Convert BGR (Windows) to RGB (OpenGL) and handle Alpha
	bool hasAlpha = false;
	for (auto& pixel : pixels) {
		uint32_t a = (pixel >> 24) & 0xFF;
		uint32_t r = (pixel >> 16) & 0xFF;
		uint32_t g = (pixel >> 8) & 0xFF;
		uint32_t b = pixel & 0xFF;
		if (a > 0) hasAlpha = true;
		pixel = (a << 24) | (b << 16) | (g << 8) | r;
	}

	// If no alpha was found in any pixel, force all pixels to be opaque.
	// This handles 24-bit or 32-bit bitmaps where the alpha channel is unused (all zero).
	if (!hasAlpha) {
		for (auto& pixel : pixels) {
			pixel |= 0xFF000000;
		}
	}

	GLuint textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bm.bmWidth, bm.bmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	
	return textureID;
}

bool launch_via_aumid(const std::wstring& aumid) {
	IApplicationActivationManager* paam = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&paam));
	if (FAILED(hr)) return false;

	DWORD pid = 0;
	hr = paam->ActivateApplication(aumid.c_str(), nullptr, AO_NONE, &pid);
	paam->Release();

	if (FAILED(hr)) {
		std::cout << "Failed to launch via AUMID\n";
		return false;
	}

	std::cout << "Successfully launched via AUMID\n";
	return true;
}

bool launch_app(const Entry& entry) {
	if (!entry.copy.empty()) {
		SetClipboardText(entry.copy);
		current_search = icu::UnicodeString::fromUTF8(entry.copy);
		recalculating = true;
		curs.head_char = current_search.length();
		curs.anchor_char = curs.head_char;
		return false;
	} else if (entry.hwnd != NULL) {
		if (IsIconic(entry.hwnd)) {
			ShowWindow(entry.hwnd, SW_RESTORE);
		}
		SetForegroundWindow(entry.hwnd);
		SetFocus(entry.hwnd);
		return true;
	}

	std::cout << "Launching: " << std::string(entry.exe.begin(), entry.exe.end()) << "\n";

	if (!entry.exe.empty()) {
		SHELLEXECUTEINFOW sei{};
		sei.cbSize = sizeof(sei);
		sei.fMask = SEE_MASK_NOCLOSEPROCESS;
		sei.lpVerb = L"open";
		sei.lpFile = entry.exe.c_str();
		sei.nShow = SW_SHOWNORMAL;
		if (ShellExecuteExW(&sei)) {
			if (sei.hProcess) CloseHandle(sei.hProcess);
			std::cout << "Successfully launched\n";
			return true;
		}
		std::cout << "Exe launch failed, trying AUMID\n";
	}

	if (!entry.aumid.empty()) {
		return launch_via_aumid(entry.aumid);
	}

	return false;
}

std::wstring normalizePath(std::wstring path) {
	if (path.empty()) return path;

	// Trim leading/trailing whitespace
	while (!path.empty() && std::iswspace(path.back())) path.pop_back();
	size_t start = 0;
	while (start < path.length() && std::iswspace(path[start])) start++;
	if (start > 0) path = path.substr(start);

	if (path.empty()) return path;

	// 1. Standardize slashes
	std::replace(path.begin(), path.end(), L'/', L'\\');

	// 2. Remove \\?\ prefix if present
	if (path.length() >= 4 && path.substr(0, 4) == L"\\\\?\\") {
		path = path.substr(4);
	}

	// 3. Get Full Path
	WCHAR fullPath[MAX_PATH];
	DWORD ret = GetFullPathNameW(path.c_str(), MAX_PATH, fullPath, nullptr);
	if (ret > 0 && ret < MAX_PATH) {
		path = fullPath;
	}

	// 4. Get Long Path (handles 8.3 names)
	WCHAR longPath[MAX_PATH];
	ret = GetLongPathNameW(path.c_str(), longPath, MAX_PATH);
	if (ret > 0 && ret < MAX_PATH) {
		path = longPath;
	}

	return path;
}

std::wstring GetWindowAUMID(HWND hwnd) {
	IPropertyStore* pps = nullptr;
	if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&pps)))) return L"";
	
	PROPERTYKEY PKEY_AUMI = { {0x9F4C2855,0x9F79,0x4B39,{0xA8,0xD0,0xE1,0xD4,0x2D,0xE1,0xD5,0xF3}}, 5 };
	PROPVARIANT pv;
	PropVariantInit(&pv);
	std::wstring result;
	if (SUCCEEDED(pps->GetValue(PKEY_AUMI, &pv)) && pv.vt == VT_LPWSTR) {
		result = pv.pwszVal;
	}
	PropVariantClear(&pv);
	pps->Release();
	return result;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
	if (!IsWindowVisible(hwnd)) return TRUE;

	WCHAR title[256];
	GetWindowTextW(hwnd, title, 256);
	if (wcslen(title) == 0) return TRUE;
	
	DWORD processId;
	GetWindowThreadProcessId(hwnd, &processId);
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	std::wstring exePath;
	if (hProcess) {
		WCHAR buffer[MAX_PATH];
		DWORD size = MAX_PATH;
		if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size)) {
			exePath = normalizePath(buffer);
		}
		CloseHandle(hProcess);
	}
	
	std::wstring aumid = GetWindowAUMID(hwnd);
	if (!exePath.empty() || !aumid.empty()) {
		std::vector<WindowInfo>* windows = reinterpret_cast<std::vector<WindowInfo>*>(lParam);
		windows->push_back({ hwnd, title, exePath, aumid });
	}

	return TRUE;
}

std::vector<WindowInfo> EnumerateOpenWindows() {
	std::vector<WindowInfo> windows;
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));
	return windows;
}

std::wstring ResolveAUMIDPath(const std::wstring& aumid) {
	// Matches {GUID}\some\path.exe style AUMIDs
	if (aumid.empty() || aumid[0] != L'{') return L"";
	size_t close = aumid.find(L'}');
	if (close == std::wstring::npos) return L"";

	std::wstring guidStr = aumid.substr(1, close - 1);
	std::wstring rest = aumid.substr(close + 2);

	GUID guid;
	if (FAILED(CLSIDFromString((L"{" + guidStr + L"}").c_str(), &guid))) return L"";

	PWSTR folderPath = nullptr;
	if (FAILED(SHGetKnownFolderPath(guid, 0, nullptr, &folderPath))) return L"";
	
	std::wstring result = std::wstring(folderPath) + L"\\" + rest;
	CoTaskMemFree(folderPath);
	return normalizePath(result);
}

void GetAllApps() {
	for (auto& app : apps) {
		if (app.textureID != 0) {
			glDeleteTextures(1, &app.textureID);
		}
	}
	apps.clear();

	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(hr)) return;

	PROPERTYKEY PKEY_AUMI = { {0x9F4C2855,0x9F79,0x4B39,{0xA8,0xD0,0xE1,0xD4,0x2D,0xE1,0xD5,0xF3}}, 5 };

	std::set<std::string> seenNames;

	// --- Pass 1: .lnk scan (reliable exe paths for Win32 apps) ---
	std::vector<std::wstring> startMenuPaths;
	PWSTR path = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, NULL, &path))) {
		startMenuPaths.push_back(path);
		CoTaskMemFree(path);
	}
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, NULL, &path))) {
		startMenuPaths.push_back(path);
		CoTaskMemFree(path);
	}

	for (const auto& basePath : startMenuPaths) {
		if (!fs::exists(basePath)) continue;

		std::error_code ec;
		for (const auto& entry : fs::recursive_directory_iterator(basePath, ec)) {
			if (ec) continue;
			if (!entry.is_regular_file() || entry.path().extension() != ".lnk") continue;

			std::wstring name_ws = entry.path().stem().wstring();
			std::string name_str;
			icu::UnicodeString(name_ws.c_str()).toUTF8String(name_str);
			if (seenNames.count(name_str)) continue;

			App a;
			a.exe = normalizePath(entry.path().wstring());
			a.name = icu::UnicodeString(name_ws.c_str());
			a.name_str = name_str;

			IShellLinkW* psl = nullptr;
			if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl)))) {
				IPersistFile* ppf = nullptr;
				if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&ppf)))) {
					if (SUCCEEDED(ppf->Load(a.exe.c_str(), STGM_READ))) {
						WCHAR szGotPath[MAX_PATH];
						if (SUCCEEDED(psl->GetPath(szGotPath, MAX_PATH, NULL, SLGP_UNCPRIORITY | SLGP_RAWPATH))) {
							WCHAR szExpandedPath[MAX_PATH];
							if (ExpandEnvironmentStringsW(szGotPath, szExpandedPath, MAX_PATH) > 0) {
								a.exe = normalizePath(szExpandedPath);
							} else {
								a.exe = normalizePath(szGotPath);
							}
						}

						IPropertyStore* pps = nullptr;
						if (SUCCEEDED(SHGetPropertyStoreFromParsingName(
								entry.path().wstring().c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&pps)))) {
							PROPVARIANT pv;
							PropVariantInit(&pv);
							if (SUCCEEDED(pps->GetValue(PKEY_AUMI, &pv)) && pv.vt == VT_LPWSTR) {
								a.aumid = pv.pwszVal;
							}
							PropVariantClear(&pv);
							pps->Release();
						}
					}
					ppf->Release();
				}
				psl->Release();
			}

			IShellItem* pItem = nullptr;
			if (SUCCEEDED(SHCreateItemFromParsingName(a.exe.c_str(), NULL, IID_PPV_ARGS(&pItem)))) {
				IShellItemImageFactory* pImageFactory = nullptr;
				if (SUCCEEDED(pItem->QueryInterface(IID_PPV_ARGS(&pImageFactory)))) {
					int iconSize = TextRenderer::get_text_height();
					if (iconSize <= 0) iconSize = 32;
					SIZE size = { iconSize, iconSize };
					HBITMAP hBitmap;
					if (SUCCEEDED(pImageFactory->GetImage(size, SIIGBF_ICONONLY, &hBitmap))) {
						a.textureID = HBitmapToTexture(hBitmap);
						DeleteObject(hBitmap);
					} else {
						a.textureID = 0;
					}
					pImageFactory->Release();
				}
				pItem->Release();
			}

			seenNames.insert(name_str);
			apps.push_back(a);
		}
	}

	// --- Pass 2: FOLDERID_AppsFolder (catches UWP/MSIX apps missing from .lnk scan) ---
	IShellItem* pAppsFolder = nullptr;
	if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&pAppsFolder)))) {
		IEnumShellItems* pEnum = nullptr;
		if (SUCCEEDED(pAppsFolder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&pEnum)))) {
			IShellItem* pItem = nullptr;
			while (pEnum->Next(1, &pItem, nullptr) == S_OK) {
				App a;

				LPWSTR pName = nullptr;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY, &pName))) {
					a.name = icu::UnicodeString(pName);
					a.name.toUTF8String(a.name_str);
					CoTaskMemFree(pName);
				}

				if (a.name_str.empty() || seenNames.count(a.name_str)) {
					pItem->Release();
					continue;
				}

				IPropertyStore* pps = nullptr;
				if (SUCCEEDED(pItem->BindToHandler(nullptr, BHID_PropertyStore, IID_PPV_ARGS(&pps)))) {
					PROPVARIANT pv;
					PropVariantInit(&pv);
					if (SUCCEEDED(pps->GetValue(PKEY_AUMI, &pv)) && pv.vt == VT_LPWSTR) {
						a.aumid = pv.pwszVal;
					}
					PropVariantClear(&pv);
					pps->Release();
				}

				LPWSTR pPath = nullptr;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath))) {
					a.exe = normalizePath(pPath);
					CoTaskMemFree(pPath);
				}
				if (a.exe.empty()) {
					a.exe = ResolveAUMIDPath(a.aumid);
				}

				IShellItemImageFactory* pImageFactory = nullptr;
				if (SUCCEEDED(pItem->QueryInterface(IID_PPV_ARGS(&pImageFactory)))) {
					int iconSize = TextRenderer::get_text_height();
					if (iconSize <= 0) iconSize = 32;
					SIZE size = { iconSize, iconSize };
					HBITMAP hBitmap;
					if (SUCCEEDED(pImageFactory->GetImage(size, SIIGBF_ICONONLY, &hBitmap))) {
						a.textureID = HBitmapToTexture(hBitmap);
						DeleteObject(hBitmap);
					}
					pImageFactory->Release();
				}

				seenNames.insert(a.name_str);
				apps.push_back(a);
				pItem->Release();
			}
			pEnum->Release();
		}
		pAppsFolder->Release();
	}

	CoUninitialize();
}

bool fuzzySearch(App app, std::string find) {
	std::string srch = "";
	std::string in = toLower(app.name_str);
	
	for (char c : find) {
		if (c == ' ' || c == '.' || c == ',' || c == '_') {
			if (in.find(srch) == std::string::npos) {
				return false;
			}
			srch = "";
		}else{
			srch += c;
		}
	}
	
	if (srch != "") {
		if (in.find(srch) == std::string::npos) {
			return false;
		}
	}
	
	return true;
}


bool equalsIgnoreCase(const std::wstring& wa, const std::wstring& wb) {
	std::string a(wa.begin(), wa.end());
	std::string b(wb.begin(), wb.end());
	
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t charA, wchar_t charB) {
			return towlower(charA) == towlower(charB);
	});
}

std::wstring GetPackageFamilyFromExePath(const std::wstring& exePath) {
	// Extract folder name from WindowsApps path
	// Path looks like: C:\Program Files\WindowsApps\<PackageFullName>\foo.exe
	const std::wstring marker = L"\\WindowsApps\\";
	size_t start = exePath.find(marker);
	if (start == std::wstring::npos) return L"";
	
	start += marker.length();
	size_t end = exePath.find(L'\\', start);
	if (end == std::wstring::npos) return L"";
	
	std::wstring packageFullName = exePath.substr(start, end - start);
	
	WCHAR familyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
	UINT32 len = PACKAGE_FAMILY_NAME_MAX_LENGTH + 1;
	if (PackageFamilyNameFromFullName(packageFullName.c_str(), &len, familyName) == ERROR_SUCCESS) {
		return familyName;
	}
	return L"";
}

std::wstring GetFamilyFromAUMID(const std::wstring& aumid) {
	size_t bang = aumid.find(L'!');
	if (bang == std::wstring::npos) return aumid;
	return aumid.substr(0, bang);
}

void recalculate() {
	entries.clear();
	selected_id = 0;
	scroll_vert = 0;
	
	std::string find;
	current_search.toUTF8String(find);
	find = toLower(find);
	
	auto res = calcExpression(current_search);
	if (res.first){
		Entry e;
		e.name = doubleToUnicodeString_pretty(res.second);
		e.name.toUTF8String(e.copy);
		entries.push_back(e);
	}
	
	auto openWindows = EnumerateOpenWindows();
	
	std::cout << "\n\n\n\nWindows:\n";
	
	for (auto w : openWindows) {
		std::cout << std::string(w.title.begin(), w.title.end()) << " - " << std::string(w.exe.begin(), w.exe.end()) << " - " << std::string(w.aumid.begin(), w.aumid.end()) << "\n";
	}
	
	for (const auto& app : apps) {
		if (fuzzySearch(app, find)) {
			Entry e;
			e.name = app.name;
			e.name_str = app.name_str;
			e.exe = app.exe;
			e.tex = app.textureID;
			e.aumid = app.aumid;
			
			for (auto win : openWindows) {
				bool exeMatch   = !win.exe.empty()   && equalsIgnoreCase(win.exe, app.exe);
				bool aumidMatch = !win.aumid.empty() && !app.aumid.empty() && equalsIgnoreCase(win.aumid, app.aumid);
				bool pkgMatch   = !app.aumid.empty() && !win.exe.empty()
								  && equalsIgnoreCase(GetPackageFamilyFromExePath(win.exe),
													 GetFamilyFromAUMID(app.aumid));
				bool exWinaumApp = !win.exe.empty()  && equalsIgnoreCase(win.exe, app.aumid);
				bool exAppaumWin = !win.aumid.empty()&& equalsIgnoreCase(win.aumid, app.exe);
				
				if (exeMatch || aumidMatch || pkgMatch || exWinaumApp || exAppaumWin) {
					e.children.push_back({win.hwnd, icu::UnicodeString::fromUTF8(std::string(win.title.begin(), win.title.end()))});
				}
			}
			
//			for (auto win : openWindows) {
//				if ((!win.exe.empty() && (equalsIgnoreCase(win.exe, app.exe) || equalsIgnoreCase(win.exe, app.aumid))) || (!win.aumid.empty() && (equalsIgnoreCase(win.aumid, app.aumid) || equalsIgnoreCase(win.aumid, app.exe)))) {
//					e.children.push_back({win.hwnd, icu::UnicodeString::fromUTF8(std::string(win.title.begin(), win.title.end()))});
//				}
//			}
			
			entries.push_back(e);
		}
	}
	
	std::cout << "\n\n\n\nEntries:\n";
	
	for (auto e : entries) {
		std::cout << e.name_str << " - " << std::string(e.exe.begin(), e.exe.end()) << " - " << std::string(e.aumid.begin(), e.aumid.end()) << "\n";
	}
	
	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
		if (a.copy.empty() != b.copy.empty()) {
			return b.copy.empty();
		}
		if (a.children.size() != b.children.size()) {
			return a.children.size() > b.children.size();
		}
		return a.name_str < b.name_str;
	});
}

auto drawCornerEdge = [](float cx, float cy, float startAngle, float endAngle, int segments, double radius, int edgewidth) {
	double smallerradius = radius-edgewidth;
	
	glBegin(GL_QUAD_STRIP);
	for (int i = 0; i <= segments; ++i) {
		float t = (float)i / (float)segments;
		float theta = startAngle + t * (endAngle - startAngle);
		glVertex2f(cx + std::cos(theta) * radius, cy + std::sin(theta) * radius);
		glVertex2f(cx + std::cos(theta) * smallerradius, cy + std::sin(theta) * smallerradius);
	}
	glEnd();
};

auto drawCorner = [](float cx, float cy, float startAngle, float endAngle, int segments, double radius) {
	glBegin(GL_TRIANGLE_FAN);
	glVertex2f(cx, cy);
	for (int i = 0; i <= segments; ++i) {
		float t = (float)i / (float)segments;
		float theta = startAngle + t * (endAngle - startAngle);
		glVertex2f(cx + std::cos(theta) * radius, cy + std::sin(theta) * radius);
	}
	glEnd();
};

void DrawRect(int x, int y, int w, int h, Color* color) {
	glColor4f(color->r, color->g, color->b, color->a);
	glBegin(GL_QUADS);
		glVertex2f(x, y); // top-left
		glVertex2f(x+w, y); // top-right
		glVertex2f(x+w, y+h); // bottom-right
		glVertex2f(x, y+h); // bottom-left
	glEnd();
}

void DrawRoundBorder(int x, int y, int w, int h, Color* color, int segments, double radius) {
	glColor4f(color->r, color->g, color->b, color->a);
	
	drawCornerEdge(x + radius, y + radius, M_PI, 1.5f * M_PI, segments, radius, border_width); // BL
	drawCornerEdge(x + w - radius, y + radius, 1.5f * M_PI, 2.0f * M_PI, segments, radius, border_width); // BR
	drawCornerEdge(x + w - radius, y + h - radius, 0.0f, 0.5f * M_PI, segments, radius, border_width); // TR
	drawCornerEdge(x + radius,      y + h - radius, 0.5f * M_PI, M_PI, segments, radius, border_width); // TL
	
	DrawRect(x+radius, y, w-radius*2, border_width, color);
	DrawRect(x+radius, y+h-border_width, w-radius*2, border_width, color);
	DrawRect(x, y+radius, border_width, h-radius*2, color);
	DrawRect(x+w-border_width, y+radius, border_width, h-radius*2, color);
}

void DrawRoundedRect(float x, float y, float w, float h, float radius, Color* color, Color* bcolor, int segments) {
	glColor4f(color->r, color->g, color->b, color->a);
	
	glBegin(GL_QUADS);
		// Center
		glVertex2f(x + radius,      y);
		glVertex2f(x + w - radius,  y);
		glVertex2f(x + w - radius,  y + h);
		glVertex2f(x + radius,      y + h);

		// Left strip
		glVertex2f(x,          y + radius);
		glVertex2f(x + radius, y + radius);
		glVertex2f(x + radius, y + h - radius);
		glVertex2f(x,          y + h - radius);

		// Right strip
		glVertex2f(x + w - radius, y + radius);
		glVertex2f(x + w,          y + radius);
		glVertex2f(x + w,          y + h - radius);
		glVertex2f(x + w - radius, y + h - radius);
	glEnd();

	// Draw the four quartercircles
	drawCorner(x + radius, y + radius, M_PI, 1.5f * M_PI, segments, radius); // BL
	drawCorner(x + w - radius, y + radius, 1.5f * M_PI, 2.0f * M_PI, segments, radius); // BR
	drawCorner(x + w - radius, y + h - radius, 0.0f, 0.5f * M_PI, segments, radius); // TR
	drawCorner(x + radius,      y + h - radius, 0.5f * M_PI, M_PI, segments, radius); // TL
	
	if (bcolor != nullptr) {
		DrawRoundBorder(x, y, w, h, bcolor, segments, radius);
	}
}

void DrawRoundBorder(int x, int y, int w, int h, Color* color, int segments, 
					  float rTL, float rTR, float rBR, float rBL) {
	glColor4f(color->r, color->g, color->b, color->a);
	
	// Using M_PI for consistency. Standard orientation:
	// 0: Right, 0.5: Bottom, 1.0: Left, 1.5: Top (in radians * PI)
	
	drawCornerEdge(x + w - rTR, y + rTR,     1.5f * M_PI, 2.0f * M_PI, segments, rTR, border_width); // TR
	drawCornerEdge(x + w - rBR, y + h - rBR, 0.0f,        0.5f * M_PI, segments, rBR, border_width); // BR
	drawCornerEdge(x + rBL,     y + h - rBL, 0.5f * M_PI, 1.0f * M_PI, segments, rBL, border_width); // BL
	drawCornerEdge(x + rTL,     y + rTL,     1.0f * M_PI, 1.5f * M_PI, segments, rTL, border_width); // TL
	
	// Straight Edges
	DrawRect(x + rTL, y, w - rTL - rTR, border_width, color);               // Top edge
	DrawRect(x + rBL, y + h - border_width, w - rBL - rBR, border_width, color); // Bottom edge
	DrawRect(x, y + rTL, border_width, h - rTL - rBL, color);               // Left edge
	DrawRect(x + w - border_width, y + rTR, border_width, h - rTR - rBR, color); // Right edge
}

void DrawRoundedRect(float x, float y, float w, float h, Color* color, Color* bcolor, 
					 int segments, float rTL, float rTR, float rBR, float rBL) {
	glColor4f(color->r, color->g, color->b, color->a);
	
	glBegin(GL_QUADS);
		// Center Block (Vertical strip spanning the full height minus the largest corner offsets)
		// This ensures the middle of the box is always filled.
		float maxTop = (rTL > rTR) ? rTL : rTR;
		float maxBottom = (rBL > rBR) ? rBL : rBR;

		glVertex2f(x, y + maxTop);
		glVertex2f(x + w, y + maxTop);
		glVertex2f(x + w, y + h - maxBottom);
		glVertex2f(x, y + h - maxBottom);

		// Top Strip (Filling the gap between TL and TR corners)
		glVertex2f(x + rTL, y);
		glVertex2f(x + w - rTR, y);
		glVertex2f(x + w - rTR, y + maxTop);
		glVertex2f(x + rTL, y + maxTop);

		// Bottom Strip (Filling the gap between BL and BR corners)
		glVertex2f(x + rBL, y + h - maxBottom);
		glVertex2f(x + w - rBR, y + h - maxBottom);
		glVertex2f(x + w - rBR, y + h);
		glVertex2f(x + rBL, y + h);
	glEnd();

	// Fill the 4 corners
	drawCorner(x + w - rTR, y + rTR,     1.5f * M_PI, 2.0f * M_PI, segments, rTR); // TR
	drawCorner(x + w - rBR, y + h - rBR, 0.0f,        0.5f * M_PI, segments, rBR); // BR
	drawCorner(x + rBL,     y + h - rBL, 0.5f * M_PI, 1.0f * M_PI, segments, rBL); // BL
	drawCorner(x + rTL,     y + rTL,     1.0f * M_PI, 1.5f * M_PI, segments, rTL); // TL
	
	if (bcolor != nullptr) {
		DrawRoundBorder(x, y, w, h, bcolor, segments, rTL, rTR, rBR, rBL);
	}
}

void DrawTexturedRect(float x, float y, float w, float h, GLuint textureID) {
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, textureID);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(x, y);
	glTexCoord2f(1, 0); glVertex2f(x + w, y);
	glTexCoord2f(1, 1); glVertex2f(x + w, y + h);
	glTexCoord2f(0, 1); glVertex2f(x, y + h);
	glEnd();
	glDisable(GL_TEXTURE_2D);
}

void render() {
	int sep = RAD_SMALL/2;
	
	int top_h = WIN_HEIGHT / 5 - sep*2;
	
	int remaining = WIN_HEIGHT - top_h - sep;
	int lstTotal = (remaining/10);
	
	int FIT = 10;
	int indiv = lstTotal - sep;
	
	int bottomRad = RAD_SMALL+sep;
	int topRad = RAD_BIG+sep;
	if (entries.size() == 0) {
		bottomRad = topRad;
	}
	
	DrawRoundedRect(0, 0, WIN_WIDTH, sep*2 + top_h + lstTotal*fmin(FIT, entries.size()), theme.extras_background_color, theme.border, 15, topRad, topRad, bottomRad, bottomRad);
	DrawRoundedRect(sep, sep, WIN_WIDTH-sep*2, top_h, RAD_BIG, theme.main_background_color, theme.border, 15);
	
	glEnable(GL_SCISSOR_TEST);
	glScissor(sep, 0, WIN_WIDTH-2*sep, WIN_HEIGHT);
	
	int TextH = TextRenderer::get_text_height();
	int texty = (top_h - TextH) / 2 + sep;
	
	int cursorWidth = TextRenderer::get_text_width(1) * 0.2;
	int cursor_offset = TextRenderer::get_text_width(curs.head_char) - scroll_offset;
	
	if (curs.anchor_char != curs.head_char) {
		int anch_off = TextRenderer::get_text_width(curs.anchor_char) - scroll_offset;
		DrawRect(texty+sep+fmin(cursor_offset, anch_off), texty, fabs(anch_off-cursor_offset), TextH, theme.hover_background_color);
	}
	
	if (current_search.length() == 0) {
		auto now = std::chrono::system_clock::now();
		std::time_t now_c = std::chrono::system_clock::to_time_t(now);
		std::tm* local_tm = std::localtime(&now_c);
		int hour = local_tm->tm_hour;
		
		std::string greeting = "";
		if (hour < 12) {
			greeting = "Good Morning, Boss";
		}else if (hour < 19) {
			greeting = "Good Afternoon, Boss";
		}else {
			greeting = "Good Evening, Boss";
		}
		
		TextRenderer::draw_text(texty+sep, texty, icu::UnicodeString::fromUTF8(greeting), theme.lesser_text_color);
	}else{
		TextRenderer::draw_text(texty+sep, texty, current_search, theme.main_text_color);
	}
	
	DrawRect(texty+cursor_offset+sep, texty, cursorWidth, TextH, theme.main_text_color);
	
	if (selected_id - scroll_vert < 3) {
		scroll_vert = selected_id-2;
	}
	if (selected_id - scroll_vert > FIT-3) {
		scroll_vert = selected_id-FIT+3;
	}
	if (scroll_vert+FIT > entries.size()) {
		scroll_vert = entries.size()-FIT;
	}
	if (scroll_vert < 0) {
		scroll_vert = 0;
	}
	
	int offsety = (indiv-TextRenderer::get_text_height())/2;
	int indent = 4*sep;
	
	for (int i = scroll_vert; i < fmin(scroll_vert + FIT, entries.size()); i++) {
		Color* back = theme.main_background_color;
		Color* txt = theme.main_text_color;
		
		if (i == selected_id) {
			back = theme.main_text_color;
			txt = theme.black;
		}
		
		auto e = entries[i];
		int y = top_h+sep*2 + lstTotal*(i-scroll_vert);
		
		if (e.hwnd != NULL) {
			DrawRoundedRect(sep+indent, y, WIN_WIDTH-indent-sep*2, indiv, RAD_SMALL, back, theme.border, 5);
		}else{
			DrawRoundedRect(sep, y, WIN_WIDTH-sep*2, indiv, RAD_SMALL, back, theme.border, 5);
		}
		
		int textX;
		if (e.hwnd != NULL) {
			textX = RAD_SMALL + sep + indent;
		}else{
			textX = RAD_SMALL + sep;
		}
		
		if (e.tex != 0) {
			int iconSize = TextRenderer::get_text_height();
			DrawTexturedRect(textX, y + offsety, iconSize, iconSize, e.tex);
			textX += iconSize + sep;
		}
		
		if (!e.children.empty()) {
			glScissor(sep, 0, WIN_WIDTH-4*sep-TextRenderer::get_text_width(7), WIN_HEIGHT);
		}
		
		TextRenderer::draw_text(textX, y + offsety, e.name, txt);
		
		glScissor(sep, 0, WIN_WIDTH-2*sep, WIN_HEIGHT);
		
		if (!e.children.empty()) {
			TextRenderer::draw_text(WIN_WIDTH-RAD_SMALL-sep-TextRenderer::get_text_width(7), y + offsety, icu::UnicodeString::fromUTF8("(tab)"), theme.lesser_text_color);
			if (e.open) {
				TextRenderer::draw_text(WIN_WIDTH-RAD_SMALL-sep-TextRenderer::get_text_width(1), y + offsety, icu::UnicodeString::fromUTF8("v"), txt);
			}else{
				TextRenderer::draw_text(WIN_WIDTH-RAD_SMALL-sep-TextRenderer::get_text_width(1), y + offsety, icu::UnicodeString::fromUTF8(">"), txt);
			}
			
		}
	}
	
	glDisable(GL_SCISSOR_TEST);
}

void setSizes() {
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	
	if (!monitor) {
		TextRenderer::set_font_size(FONT_SIZE);
		TextRenderer::init_font(FONT_PATH);
		GetAllApps();
		return;
	}
	
	int monitorX, monitorY, monitorWidth, monitorHeight;
	glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorWidth, &monitorHeight);
	
	WIN_WIDTH = monitorWidth / 2;
	WIN_HEIGHT = monitorHeight / 2;
	
	RAD_BIG = WIN_HEIGHT/15;
	RAD_SMALL = WIN_HEIGHT/50;
	
	int newSize = (int) (RAD_BIG / 2);
	if (newSize != FONT_SIZE) {
		FONT_SIZE = newSize;
		std::cout << "Font size: " << FONT_SIZE << "\n";
		TextRenderer::set_font_size(FONT_SIZE);
		TextRenderer::init_font(FONT_PATH);
		GetAllApps();
	}
	
	if (RAD_SMALL < 5) {
		RAD_SMALL = 5;
	}
	
	glfwSetWindowSize(window, WIN_WIDTH, WIN_HEIGHT);
	
	WIN_X = monitorX + (monitorWidth - WIN_WIDTH) / 2;
	WIN_Y = monitorY + (monitorHeight - WIN_HEIGHT) / 2;
	
	glfwSetWindowPos(window, WIN_X, WIN_Y);
	
	std::cout << "Set pos to " << WIN_X << ", " << WIN_Y << " - " << WIN_WIDTH << "x" << WIN_HEIGHT << "\n";
}

void show() {
	if (glfwGetWindowAttrib(window, GLFW_VISIBLE)) return;
	
	glfwShowWindow(window);
	glfwFocusWindow(window);
	
	setSizes();
	
	curs.anchor_char = 0;
	curs.head_char = current_search.length();
	
	recalculate();
}

void hide() {
	if (glfwGetWindowAttrib(window, GLFW_VISIBLE)) {
		glfwHideWindow(window);
	}
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;

		if (pKeyBoard->flags & LLKHF_INJECTED) {
			return CallNextHookEx(hhkLowLevelKybd, nCode, wParam, lParam);
		}

		bool is_win = (pKeyBoard->vkCode == VK_LWIN || pKeyBoard->vkCode == VK_RWIN);

		if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
			if (is_win) {
				win_down = true;
				win_used_in_combo = false;
			} else if (win_down) {
				win_used_in_combo = true;
			}
		}

		if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
			if (is_win) {
				win_down = false;
				if (!win_used_in_combo) {
					std::cout << "Intercepted solo Windows Key! Suppressing Start Menu..." << std::endl;
					
					// we can't intercept the event because then the system think the windows key is still down. But, we can add some modifier that makes the start menu not open
					keybd_event(0x07, 0, 0, 0); // 0x07 is undefined/reserved
					keybd_event(0x07, 0, KEYEVENTF_KEYUP, 0);
					
					if (!glfwGetWindowAttrib(window, GLFW_VISIBLE)) {
						show();
					}else{
						hide();
					}
				}
			}
		}
	}
	return CallNextHookEx(hhkLowLevelKybd, nCode, wParam, lParam);
}

void removeSelected() {
	current_search.remove(fmin(curs.head_char, curs.anchor_char), fabs(curs.head_char - curs.anchor_char));
	curs.head_char = fmin(curs.head_char, curs.anchor_char);
	curs.anchor_char = curs.head_char;
	
	recalculating = true;
}

void insertText(icu::UnicodeString toInsert) {
	if (curs.head_char != curs.anchor_char) {
		removeSelected();
	}
	
	current_search.insert(curs.head_char, toInsert);
	curs.head_char = curs.head_char + toInsert.length();
	curs.anchor_char = curs.head_char;
	
	recalculating = true;
}

void character_callback(GLFWwindow* window, unsigned int codepoint) {
	UChar32 ch = static_cast<UChar32>(codepoint);
	
	char utf8[5] = {};
	int len = std::snprintf(utf8, sizeof(utf8), "%c", codepoint);
	if (len > 0) { // there is something printable
		// this doesn't detect newlines, tabs,
		// we're going to handle all whitespace in the key down
		if (ch == '\n' || ch == '\t') {
			return;
		}
		
		icu::UnicodeString to_insert;
		to_insert.append(ch);
		
		insertText(to_insert);
	}
}

int charType(UChar32 c) {
	if (whitespace.count(c)) {return 0;}
	if (allowed_in_var_names.count(c)) {return 1;}
	return 2;
}

int calcWordJump(int dir, int location) {
	if (dir == -1) {
		location --;
		int toc = charType(current_search.char32At(location));
		bool notseenwhite = (toc != 0);

		while (true) {
			location --;

			if (location < 0) {
				location = 0;
				break;
			}

			auto ntoc = charType(current_search.char32At(location));

			if (!notseenwhite && ntoc != 0) {
				notseenwhite = true;
				toc = ntoc;
			}

			if (ntoc != toc) {
				location ++;
				break;
			}
		}
	}else{
		int toc = charType(current_search.char32At(location));
		bool seenwhite = (toc == 0);

		while (true) {
			location ++;

			if (location >= current_search.length()) {
				break;
			}

			auto ntoc = charType(current_search.char32At(location));

			if (!seenwhite && ntoc == 0) {
				seenwhite = true;
				toc = ntoc;
			}

			if (ntoc != toc) {
				break;
			}
		}
	}
	
	return fabs(location - curs.head_char);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_ESCAPE) {
		hide();
		return;
	}
	
	bool is_shift_held = ((mods & GLFW_MOD_SHIFT) != 0);
	bool is_control_held = ((mods & GLFW_MOD_CONTROL) != 0);
	
	if (action != GLFW_PRESS && action != GLFW_REPEAT) {
		return;
	}
	
	if (key == GLFW_KEY_BACKSPACE) {
		if (curs.anchor_char != curs.head_char) {
			removeSelected();
		}else if (curs.head_char != 0) {
			int dist = 1;
			if (is_control_held) {
				dist = calcWordJump(-1, curs.head_char);
			}
			
			current_search.remove(curs.head_char-dist, dist);
			curs.head_char -= dist;
			curs.anchor_char -= dist;
			recalculating = true;
		}
		return;
	}else if (key == GLFW_KEY_LEFT) {
		if (is_shift_held) {
			if (curs.head_char != 0) {
				int dist;
				if (is_control_held) {
					dist = calcWordJump(-1, curs.head_char);
				}else{
					dist = 1;
				}
				curs.head_char -= dist;
			}
		}else{
			if (curs.anchor_char != curs.head_char) {
				curs.head_char = fmin(curs.head_char, curs.anchor_char);
				curs.anchor_char = curs.head_char;
			}else if (curs.head_char != 0){
				int dist;
				if (is_control_held) {
					dist = calcWordJump(-1, curs.head_char);
				}else{
					dist = 1;
				}
				curs.head_char -= dist;
				curs.anchor_char -= dist;
			}
		}
		return;
	}else if (key == GLFW_KEY_RIGHT) {
		if (is_shift_held) {
			if (curs.head_char != current_search.length()) {
				int dist;
				if (is_control_held) {
					dist = calcWordJump(1, curs.head_char);
				}else{
					dist = 1;
				}
				curs.head_char += dist;
			}
		}else{
			if (curs.anchor_char != curs.head_char) {
				curs.head_char = fmax(curs.head_char, curs.anchor_char);
				curs.anchor_char = curs.head_char;
			}else if (curs.head_char != current_search.length()){
				int dist;
				if (is_control_held) {
					dist = calcWordJump(1, curs.head_char);
				}else{
					dist = 1;
				}
				curs.head_char += dist;
				curs.anchor_char += dist;
			}
		}
		return;
	}else if (key == GLFW_KEY_END) {
		curs.head_char = current_search.length();
		
		if (!is_shift_held) {
			curs.anchor_char = curs.head_char;
		}
	}else if (key == GLFW_KEY_HOME) {
		curs.head_char = 0;
		
		if (!is_shift_held) {
			curs.anchor_char = 0;
		}
	}else if (key == GLFW_KEY_C && is_control_held) {
		if (curs.head_char != curs.anchor_char) {
			icu::UnicodeString tmp = current_search.tempSubStringBetween(fmin(curs.anchor_char, curs.head_char), fmax(curs.anchor_char, curs.head_char));
			std::string txt;
			tmp.toUTF8String(txt);
			SetClipboardText(txt);
		}
	}else if (key == GLFW_KEY_X && is_control_held) {
		if (curs.head_char != curs.anchor_char) {
			icu::UnicodeString tmp = current_search.tempSubStringBetween(fmin(curs.anchor_char, curs.head_char), fmax(curs.anchor_char, curs.head_char));
			std::string txt;
			tmp.toUTF8String(txt);
			SetClipboardText(txt);
			removeSelected();
		}
	}else if (key == GLFW_KEY_V && is_control_held) {
		insertText(icu::UnicodeString::fromUTF8(GetClipboardText()));
	}else if (key == GLFW_KEY_A && is_control_held) {
		curs.anchor_char = 0;
		curs.head_char = current_search.length();
	}else if (key == GLFW_KEY_DOWN) {
		if (entries.size() == 0) {
			selected_id = 0;
			scroll_vert = 0;
		}
		
		selected_id += 1;
		if (selected_id >= entries.size()) {
			selected_id = entries.size()-1;
		}
	}else if (key == GLFW_KEY_UP) {
		if (entries.size() == 0) {
			selected_id = 0;
			scroll_vert = 0;
		}
		
		selected_id -= 1;
		if (selected_id < 0) {
			selected_id = 0;
			scroll_vert = 0;
		}
	}else if (key == GLFW_KEY_TAB) {
		if (selected_id < entries.size()) {
			Entry e = entries[selected_id];
			
			if (!e.children.empty()) {
				if (entries.size() > selected_id+1) {
					if (entries[selected_id+1].hwnd != NULL) {
						entries.erase(entries.begin()+selected_id+1, entries.begin()+selected_id+1+e.children.size());
						entries[selected_id].open = false;
						return;
					}
				}
				
				int index = selected_id+1;
				for (auto sE : entries[selected_id].children) {
					Entry new_e;
					new_e.hwnd = sE.hwnd;
					new_e.tex = e.tex;
					new_e.name = sE.name;
					sE.name.toUTF8String(new_e.name_str);
					entries.insert(entries.begin()+index, new_e);
					index += 1;
				}
				entries[selected_id].open = true;
			}else if (launch_app(e)) {
				hide();
			}
		}
	}else if (key == GLFW_KEY_ENTER) {
		if (selected_id < entries.size()) {
			std::cout << "Runnnnig: " << entries[selected_id].name_str << "\n";
			
			if (launch_app(entries[selected_id])) {
				hide();
			}
		}
	}
}

int main() {
	if (!glfwInit()) return -1;
	
	windir = getWinDir();
	
	theme.main_text_color = MakeColor(0, 0, 0);
	theme.lesser_text_color = MakeColor(0, 0, 0);
	theme.main_background_color = MakeColor(0, 0, 0);
	theme.extras_background_color = MakeColor(0, 0, 0);
	theme.hover_background_color = MakeColor(0, 0, 0);
	theme.darker_background_color = MakeColor(0, 0, 0);
	theme.overlay_background_color = MakeColor(0, 0, 0);
	theme.border = MakeColor(0, 0, 0);
	theme.add_diff = MakeColor(0, 0, 0);
	theme.del_diff = MakeColor(0, 0, 0);
	theme.equal_diff = MakeColor(0, 0, 0);
	theme.tint_color = MakeColor(0, 0, 0);
	theme.white = MakeColor(0, 0, 0);
	theme.black = MakeColor(0, 0, 0);
	
	current_search = icu::UnicodeString();
	
	theme.tint_color = MakeColor(0.705882353,0.784313725,1);
	updateFromTintColor(&theme, true);
	
	hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);

	if (hhkLowLevelKybd == NULL) {
		std::cerr << "Failed to install hook!" << std::endl;
		return 1;
	}
	
	std::cout << "Logic Active. Solo Win key is suppressed. Combos (Win+R, etc) still work." << std::endl;
	std::cout << "Press Ctrl+C to exit." << std::endl;
	
	
	
	
	
	// 1. Create window but keep it hidden initially
	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); // No title bar/borders
	glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);  // Always on top
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	
	window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "Overlay", NULL, NULL);
	glfwMakeContextCurrent(window);
	
	if (glewInit() != GLEW_OK) {
		std::cerr << "Failed to initialize GLEW!" << std::endl;
		return -1;
	}
	
	glEnable(GL_MULTISAMPLE);
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, WIN_WIDTH, WIN_HEIGHT, 0, -1, 1); // top-left origin, y-down
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
	glfwSwapInterval(1); // Enable vsync
	
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);
	
	// 2. Remove from Taskbar using Win32
	HWND hwnd = glfwGetWin32Window(window);
	LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
	style |= WS_EX_TOOLWINDOW; // Prevents taskbar icon
	style &= ~WS_EX_APPWINDOW; // Ensures it's not a "main" app window
	SetWindowLongPtr(hwnd, GWL_EXSTYLE, style);
	
	// 3. Handle Auto-Hide when focus is lost
	glfwSetWindowFocusCallback(window, [](GLFWwindow* win, int focused) {
		if (!focused) {
			hide();
		}
	});
	
	glfwSetCharCallback(window, character_callback);
	glfwSetKeyCallback(window, key_callback);
	
	
	
	FONT_SIZE = 30;
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::cout << "Executable path: " << path << std::endl;
	fs::path p = path;
	p.remove_filename();
	std::string fontpath = p.string()+"CascadiaCode-Regular.ttf";
	FONT_PATH = fontpath.c_str();
	
	setSizes();
	
	
	while (!glfwWindowShouldClose(window)) {
		if (!glfwGetWindowAttrib(window, GLFW_VISIBLE)) {
			MSG msg;
			while (GetMessage(&msg, NULL, 0, 0)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				if (glfwGetWindowAttrib(window, GLFW_VISIBLE)) {
					break;
				}
			}
		}
		
		recalculating = false;
		
		glfwPollEvents();
		
		if (recalculating) {
			recalculate();
		}
		
		if (glfwGetWindowAttrib(window, GLFW_VISIBLE)) {
			int fbw, fbh;
			glfwGetFramebufferSize(window, &fbw, &fbh);
		
			glViewport(0, 0, fbw, fbh);
		
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0, fbw, fbh, 0, -1, 1); // left, right, bottom, top — y=0 at top
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
		
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		
			render();
			glfwSwapBuffers(window);
		}
	}

	UnhookWindowsHookEx(hhkLowLevelKybd);
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
	return main();
}