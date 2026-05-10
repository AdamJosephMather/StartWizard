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
	GLuint tex;
	std::wstring exe;
	std::vector<SubEntry> children;
};

struct App {
	icu::UnicodeString name;
	std::wstring exe;
	std::string name_str;
	GLuint textureID;
};

struct WindowInfo {
	HWND hwnd;
	std::wstring title;
	std::wstring exe;
};

std::vector<Entry> entries;
int selected_id = 0;

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
	for (auto& pixel : pixels) {
		uint32_t a = (pixel >> 24) & 0xFF;
		uint32_t r = (pixel >> 16) & 0xFF;
		uint32_t g = (pixel >> 8) & 0xFF;
		uint32_t b = pixel & 0xFF;
		pixel = (a << 24) | (b << 16) | (g << 8) | r;
	}

	GLuint textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bm.bmWidth, bm.bmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	
	return textureID;
}

bool launch_app(const Entry& entry) {
	std::cout << "Launching: " << std::string(entry.exe.begin(), entry.exe.end()) << "\n";
	
	if (!entry.exe.empty()) {
		SHELLEXECUTEINFOW sei{};
		sei.cbSize = sizeof(sei);
		sei.fMask = SEE_MASK_NOCLOSEPROCESS;
		sei.lpVerb = L"open";
		sei.lpFile = entry.exe.c_str();
		sei.nShow = SW_SHOWNORMAL;

		if (!ShellExecuteExW(&sei)) {
			std::cout << "Failed to launch exe\n";
			return false;
		}

		if (sei.hProcess) {
			CloseHandle(sei.hProcess);
		}
		
		std::cout << "Successfully launched\n";

		return true;
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

	if (!exePath.empty()) {
		std::vector<WindowInfo>* windows = reinterpret_cast<std::vector<WindowInfo>*>(lParam);
		windows->push_back({ hwnd, title, exePath });
	}

	return TRUE;
}

std::vector<WindowInfo> EnumerateOpenWindows() {
	std::vector<WindowInfo> windows;
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));
	return windows;
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

	std::set<std::wstring> seenNames;

	for (const auto& basePath : startMenuPaths) {
		if (!fs::exists(basePath)) continue;
		
		std::error_code ec;
		for (const auto& entry : fs::recursive_directory_iterator(basePath, ec)) {
			if (ec) continue;
			if (entry.is_regular_file() && entry.path().extension() == ".lnk") {
				std::wstring name_ws = entry.path().stem().wstring();
				if (seenNames.count(name_ws)) continue;
				seenNames.insert(name_ws);

				App a;
				a.exe = normalizePath(entry.path().wstring());
				a.name = icu::UnicodeString(name_ws.c_str());
				a.name.toUTF8String(a.name_str);

				IShellLinkW* psl = nullptr;
				if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl)))) {
					IPersistFile* ppf = nullptr;
					if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&ppf)))) {
						if (SUCCEEDED(ppf->Load(a.exe.c_str(), STGM_READ))) {
							WCHAR szGotPath[MAX_PATH];
							// Get the raw path (which may contain %variables%)
							if (SUCCEEDED(psl->GetPath(szGotPath, MAX_PATH, NULL, SLGP_UNCPRIORITY | SLGP_RAWPATH))) {
								
								// --- EXPAND ENVIRONMENT STRINGS ---
								WCHAR szExpandedPath[MAX_PATH];
								if (ExpandEnvironmentStringsW(szGotPath, szExpandedPath, MAX_PATH) > 0) {
									a.exe = normalizePath(szExpandedPath);
								} else {
									a.exe = normalizePath(szGotPath);
								}
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
				apps.push_back(a);
			}
		}
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

void recalculate() {
	entries.clear();
	selected_id = 0;
	
	icu::UnicodeString toSearch = current_search.toLower();
	std::string find;
	toSearch.toUTF8String(find);
	
	auto openWindows = EnumerateOpenWindows();
	
	for (const auto& app : apps) {
		if (fuzzySearch(app, find)) {
			Entry e;
			e.name = app.name;
			e.name_str = app.name_str;
			e.exe = app.exe;
			e.tex = app.textureID;
			
			for (auto win : openWindows) {
				if (equalsIgnoreCase(win.exe, app.exe)) {
					e.children.push_back({win.hwnd, icu::UnicodeString::fromUTF8(std::string(win.title.begin(), win.title.end()))});
				}
			}
			
			entries.push_back(e);
		}
	}
	
	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
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

void render() {
	int top_h = WIN_HEIGHT / 5;
	
	int sep = RAD_SMALL/2;
	int remaining = WIN_HEIGHT - top_h;
	int lstTotal = (remaining/10);
	
	int FIT = 10;
	int indiv = lstTotal - sep;
	
	DrawRoundedRect(0, 0, WIN_WIDTH, top_h, RAD_BIG, theme.main_background_color, theme.border, 15);
	
	int TextH = TextRenderer::get_text_height();
	int texty = (top_h - TextH) / 2;
	
	int cursorWidth = TextRenderer::get_text_width(1) * 0.2;
	int cursor_offset = TextRenderer::get_text_width(curs.head_char) - scroll_offset;
	
	if (curs.anchor_char != curs.head_char) {
		int anch_off = TextRenderer::get_text_width(curs.anchor_char) - scroll_offset;
		DrawRect(texty+fmin(cursor_offset, anch_off), texty, fabs(anch_off-cursor_offset), TextH, theme.hover_background_color);
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
		
		TextRenderer::draw_text(texty, texty, icu::UnicodeString::fromUTF8(greeting), theme.lesser_text_color);
	}else{
		TextRenderer::draw_text(texty, texty, current_search, theme.main_text_color);
	}
	
	DrawRect(texty+cursor_offset, texty, cursorWidth, TextH, theme.main_text_color);
	
	int start = selected_id - (FIT/2);
	if (start < 0) {
		start = 0;
	}else if (start+FIT > entries.size()) {
		start = entries.size()-FIT;
	}
	
	int offsety = (indiv-TextRenderer::get_text_height())/2;
	
	for (int i = start; i < fmin(start + FIT, entries.size()); i++) {
		Color* back = theme.main_background_color;
		Color* txt = theme.main_text_color;
		
		if (i == selected_id) {
			back = theme.main_text_color;
			txt = theme.black;
		}
		
		auto e = entries[i];
		int y = top_h+sep + lstTotal*(i-start);
		DrawRoundedRect(0, y, WIN_WIDTH, indiv, RAD_SMALL, back, theme.border, 5);
		TextRenderer::draw_text(RAD_SMALL, y + offsety, e.name, txt);
	}
}

void show() {
	if (glfwGetWindowAttrib(window, GLFW_VISIBLE)) return;
	
	glfwShowWindow(window);
	glfwFocusWindow(window);
	
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	
	if (!monitor) return;
	
	int monitorX, monitorY, monitorWidth, monitorHeight;
	glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorWidth, &monitorHeight);
	
	WIN_WIDTH = monitorWidth / 2;
	WIN_HEIGHT = monitorHeight / 2;
	
	RAD_BIG = WIN_HEIGHT/15;
	RAD_SMALL = WIN_HEIGHT/50;
	
	int newSize = (int) (RAD_BIG / 2);
	if (newSize != FONT_SIZE) {
		FONT_SIZE = newSize;
		GetAllApps();
		std::cout << "Font size: " << FONT_SIZE << "\n";
		TextRenderer::set_font_size(FONT_SIZE);
		TextRenderer::init_font(FONT_PATH);
	}
	
	if (RAD_SMALL < 5) {
		RAD_SMALL = 5;
	}
	
	glfwSetWindowSize(window, WIN_WIDTH, WIN_HEIGHT);
	
	WIN_X = monitorX + (monitorWidth - WIN_WIDTH) / 2;
	WIN_Y = monitorY + (monitorHeight - WIN_HEIGHT) / 2;
	
	glfwSetWindowPos(window, WIN_X, WIN_Y);
	
	std::cout << "Set pos to " << WIN_X << ", " << WIN_Y << " - " << WIN_WIDTH << "x" << WIN_HEIGHT << "\n";
	
	
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
		}
		
		selected_id += 1;
		if (selected_id >= entries.size()) {
			selected_id = entries.size()-1;
		}
	}else if (key == GLFW_KEY_UP) {
		if (entries.size() == 0) {
			selected_id = 0;
		}
		
		selected_id -= 1;
		if (selected_id < 0) {
			selected_id = 0;
		}
	}else if (key == GLFW_KEY_ENTER) {
		std::cout << "Detect enter\n";
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
	
	std::cout << "Collecting apps\n";
	GetAllApps();
	
	std::cout << "Logic Active. Solo Win key is suppressed. Combos (Win+R, etc) still work." << std::endl;
	std::cout << "Press Ctrl+C to exit." << std::endl;
	
	
	
	
	
	// 1. Create window but keep it hidden initially
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); // No title bar/borders
	glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);  // Always on top
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	
	window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "Overlay", NULL, NULL);
	glfwMakeContextCurrent(window);
	
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
	TextRenderer::set_font_size(FONT_SIZE);
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::cout << "Executable path: " << path << std::endl;
	fs::path p = path;
	p.remove_filename();
	std::string fontpath = p.string()+"CascadiaCode-Regular.ttf";
	FONT_PATH = fontpath.c_str();
	TextRenderer::init_font(FONT_PATH);
	
	
	
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