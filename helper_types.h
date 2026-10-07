#pragma once

#include <corecrt_io.h>
#include <functional>
#include <iostream>
#include <process.h>
#include <vector>
#include "unicode/unistr.h"
#include <unicode/unum.h>
#include <windows.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <array>
#include <codecvt>
#include <locale>
#include <filesystem>
#include <string>
#include <cstdio>
#include <system_error>

struct Color {
	float r;
	float g;
	float b;
	float a;
};

static constexpr std::array<unsigned char,256> ToLower = []{
	std::array<unsigned char,256> m{};
	for(int i=0;i<256;i++){
		m[i] = (i >= 'A' && i <= 'Z') ? (i + 32) : i;
	}
	return m;
}();

inline Color* MakeColor(float r, float g, float b, float a = 1.0f){
	auto c = new Color();
	c->r = r;
	c->g = g;
	c->b = b;
	c->a = a;
	return c;
}

struct Theme {
	Color* main_text_color;
	Color* lesser_text_color;
	Color* main_background_color;
	Color* extras_background_color;
	Color* hover_background_color;
	Color* darker_background_color;
	Color* overlay_background_color;
	Color* border;
	
	Color* add_diff;
	Color* del_diff;
	Color* equal_diff;
	
	Color* tint_color;
	
	Color* white;
	Color* black;
};

static void setTintedColor(Color* tint_c, Color* c, float b) {
	if (tint_c->r == 1 && tint_c->g == 1 && tint_c->b == 1) {
		c->r = b;
		c->g = b;
		c->b = b;
		return;
	}
	
	float tcb = tint_c->r*0.299+tint_c->g*0.587+tint_c->b*0.114;
	
	if (tcb == 0) {
		tint_c->r = 0.1;
		tint_c->g = 0.1;
		tint_c->b = 0.1;
		
		tcb = 0.1;
	}else if (tcb < 0.1) {
		tint_c->r *= 0.1/tcb;
		tint_c->g *= 0.1/tcb;
		tint_c->b *= 0.1/tcb;
		
		tcb = 0.1;
	}
	
	float scale = (b/tcb+b*2)/3;
	
	float new_r = fmin(255.0, tint_c->r*scale);
	float new_g = fmin(255.0, tint_c->g*scale);
	float new_b = fmin(255.0, tint_c->b*scale);
	
	c->r = new_r;
	c->g = new_g;
	c->b = new_b;
}

static void updateFromTintColor(Theme* t, bool darkmode) {
	if (darkmode) {
		setTintedColor(t->tint_color, t->main_background_color,    0.098039);
		setTintedColor(t->tint_color, t->extras_background_color,  0.164706);
		setTintedColor(t->tint_color, t->hover_background_color,   0.26);
		setTintedColor(t->tint_color, t->main_text_color,          1.0);
		setTintedColor(t->tint_color, t->border,                   0.35);
		setTintedColor(t->tint_color, t->darker_background_color,  0.05);
		setTintedColor(t->tint_color, t->overlay_background_color, 0.12);
		setTintedColor(t->tint_color, t->lesser_text_color,        0.392157);
	}else{
		setTintedColor(t->tint_color, t->main_background_color,    0.9);
		setTintedColor(t->tint_color, t->extras_background_color,  0.8);
		setTintedColor(t->tint_color, t->hover_background_color,   0.7);
		setTintedColor(t->tint_color, t->main_text_color,          0.0);
		setTintedColor(t->tint_color, t->border,                   0.6);
		setTintedColor(t->tint_color, t->darker_background_color,  0.95);
		setTintedColor(t->tint_color, t->overlay_background_color, 0.8);
		setTintedColor(t->tint_color, t->lesser_text_color,        0.4);
	}
}

static std::vector<icu::UnicodeString> splitByChar(const icu::UnicodeString& input, UChar delimiter) {
	std::vector<icu::UnicodeString> result;
	int32_t start = 0;
	int32_t pos;
	
	while ((pos = input.indexOf(delimiter, start)) != -1) {
		icu::UnicodeString substr;
		input.extractBetween(start, pos, substr);
		result.push_back(substr);
		start = pos + 1;
	}
	
	// Add the last part
	icu::UnicodeString substr;
	input.extractBetween(start, input.length(), substr);
	result.push_back(substr);
	return result;
}

static icu::UnicodeString joinByString(const std::vector<icu::UnicodeString> items, const icu::UnicodeString joiner) {
	icu::UnicodeString out = items[0];
	
	for (int l = 1; l < items.size(); l ++) {
		out += joiner;
		out += items[l];
	}
	
	return out;
}

static icu::UnicodeString stripOfChar(const icu::UnicodeString string, const UChar32 to_strip) {
	icu::UnicodeString result = string;
	int32_t pos = result.indexOf(to_strip);
	while (pos != -1) {
		result.remove(pos, 1);
		pos = result.indexOf(to_strip);
	}
	return result;
}

static icu::UnicodeString replaceWith(const icu::UnicodeString base, const icu::UnicodeString before, const icu::UnicodeString replacewith) {
	icu::UnicodeString result = base;
	int32_t pos = result.indexOf(before);
	while (pos != -1) {
		// replace 'before' at pos with 'replacewith'
		result.replace(pos, before.length(), replacewith);
		// advance past the newly-inserted text to avoid re-matching
		pos = result.indexOf(before, pos + replacewith.length());
	}
	return result;
}

static std::string GetClipboardText() {
	if (!OpenClipboard(nullptr)) return "";

	HANDLE hData = GetClipboardData(CF_UNICODETEXT);
	if (!hData) {
		CloseClipboard();
		return "";
	}

	wchar_t* wszText = static_cast<wchar_t*>(GlobalLock(hData));
	if (!wszText) {
		CloseClipboard();
		return "";
	}

	std::wstring wstr(wszText);
	GlobalUnlock(hData);
	CloseClipboard();

	// Convert UTF-16 (wstring) to UTF-8 (string)
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string utf8(len - 1, '\0'); // exclude null terminator
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, utf8.data(), len, nullptr, nullptr);

	return utf8;
}

static void SetClipboardText(const std::string& text) {
	// Convert UTF-8 to UTF-16
	int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (wlen == 0) return;

	HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
	if (!hGlob) return;

	wchar_t* wtext = static_cast<wchar_t*>(GlobalLock(hGlob));
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext, wlen);
	GlobalUnlock(hGlob);

	if (OpenClipboard(nullptr)) {
		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, hGlob);
		CloseClipboard();
		// Do not free hGlob; system owns it now
	} else {
		GlobalFree(hGlob); // clean up if clipboard open fails
	}
}

static void trim_decimal(std::string& s) {
	auto dot = s.find('.');
	if (dot == std::string::npos) return;
	while (!s.empty() && s.back() == '0') s.pop_back();
	if (!s.empty() && s.back() == '.') s.pop_back();
}

static icu::UnicodeString doubleToUnicodeString_pretty(double value) {
	if (std::isnan(value))  return icu::UnicodeString::fromUTF8("nan");
	if (std::isinf(value))  return icu::UnicodeString::fromUTF8(value < 0 ? "-inf" : "inf");
	if (value == 0.0)       return icu::UnicodeString::fromUTF8("0"); // avoid "-0"

	const double av = std::fabs(value);

	// Tune these thresholds to taste.
	// If the number is tiny or huge, use mantissa*10^exp form.
	const bool use_10 =
		(av < 1e-6) || (av >= 1e9);

	// Significant digits for the mantissa (pretty, not necessarily round-trip exact)
	constexpr int SIG = 9;

	if (!use_10) {
		// Defaultfloat gives a nice compact decimal in most cases.
		std::ostringstream oss;
		oss.setf(std::ios::fmtflags(0), std::ios::floatfield); // defaultfloat
		oss << std::setprecision(SIG) << value;

		std::string s = oss.str();
		// If defaultfloat chose scientific (rare with these thresholds), normalize it to decimal-ish:
		// but your parser doesn't accept 'e', so just force fixed if it happens.
		if (s.find_first_of("eE") != std::string::npos) {
			std::ostringstream oss2;
			oss2.setf(std::ios::fixed);
			oss2 << std::setprecision(SIG) << value;
			s = oss2.str();
		}
		trim_decimal(s);
		if (s == "-0") s = "0";
		return icu::UnicodeString::fromUTF8(s);
	}

	// Compute base-10 exponent and mantissa.
	int exp10 = static_cast<int>(std::floor(std::log10(av)));
	double mant = value / std::pow(10.0, exp10);

	// Rounding can push mantissa to 10.0; normalize if that happens.
	if (std::fabs(mant) >= 10.0) {
		mant /= 10.0;
		exp10 += 1;
	}

	// Format mantissa as plain decimal (no 'e'), then trim zeros.
	std::ostringstream moss;
	moss.setf(std::ios::fixed);
	// For mantissa in [1,10), fixed with (SIG-1) decimals gives ~SIG significant digits.
	moss << std::setprecision(std::max(0, SIG - 1)) << mant;

	std::string m = moss.str();
	trim_decimal(m);
	if (m == "-0") m = "0";

	// If exponent is 0, just return mantissa.
	if (exp10 == 0) {
		return icu::UnicodeString::fromUTF8(m);
	}

	// Emit parseable form for your evaluator: "<mantissa>*10^<exp>"
	std::string out = m + "*10^" + std::to_string(exp10);
	return icu::UnicodeString::fromUTF8(out);
}

static icu::UnicodeString doubleToUnicodeString(double value) {
	// Handle special cases explicitly (optional, but avoids weird outputs).
	if (std::isnan(value))  return icu::UnicodeString::fromUTF8("nan");
	if (std::isinf(value))  return icu::UnicodeString::fromUTF8(value < 0 ? "-inf" : "inf");
	if (value == 0.0)       return icu::UnicodeString::fromUTF8("0"); // avoids "-0"

	const double av = std::fabs(value);

	// We want enough significant digits that parsing back reproduces the same double.
	constexpr int P = std::numeric_limits<double>::max_digits10; // 17

	// e10 = floor(log10(|value|)) gives exponent in base-10.
	// For fixed formatting: digits_after_decimal needed to keep ~P significant digits.
	int e10 = static_cast<int>(std::floor(std::log10(av)));

	int digits_after_decimal;
	if (e10 >= 0) {
		// value has (e10+1) digits before decimal.
		digits_after_decimal = std::max(0, P - (e10 + 1));
	} else {
		// value is < 1. Need to skip leading zeros after decimal: -e10 places,
		// then add (P-1) more digits for significance.
		digits_after_decimal = (-e10) + (P - 1);
	}

	// Safety cap to avoid absurdly long strings if someone enters crazy-small numbers.
	// (You can raise this if you want.)
	digits_after_decimal = std::min(digits_after_decimal, 400);

	std::ostringstream oss;
	oss.setf(std::ios::fixed);
	oss << std::setprecision(digits_after_decimal) << value;

	std::string s = oss.str();

	// Trim trailing zeros and then a trailing decimal point.
	if (s.find('.') != std::string::npos) {
		while (!s.empty() && s.back() == '0') s.pop_back();
		if (!s.empty() && s.back() == '.') s.pop_back();
	}

	// If we trimmed down to "-0", normalize to "0".
	if (s == "-0") s = "0";

	return icu::UnicodeString::fromUTF8(s);
}

static double unicodeStringToDouble_quick(const icu::UnicodeString& str, bool& worked) {
	std::string ascii;
	str.toUTF8String(ascii);        // UTF-8 → ASCII (for digits & . it's a no-op)
	try {
		worked = true;
		return std::stod(ascii);
	} catch (const std::exception&) {
		worked = false;
		return 0.0;
	}
}

static std::string toLower(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (unsigned char c : s) out.push_back(std::tolower(c));
	return out;
}

static std::string trim(const std::string& s) {
	size_t b = 0, e = s.size();
	while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
	while (e > b && std::isspace(static_cast<unsigned char>(s[e-1]))) --e;
	return s.substr(b, e - b);
}

static std::string colorToString(Color* c) {
	int r = (int)(c->r*255.0);
	int g = (int)(c->g*255.0);
	int b = (int)(c->b*255.0);
	return std::to_string(r)+","+std::to_string(g)+","+std::to_string(b);
}

static Color stringToColor(std::string s, bool& worked) {
	worked = true;
	Color c;
	
	std::vector<int> vals = { 0 };
	
	static std::string numbers = "0123456789";
	
	for (auto chr : s) {
		if (chr == ' ') { continue; }
		
		if (chr == ',') {
			vals.push_back(0);
			continue;
		}
		
		auto nm = numbers.find(chr);
		if (nm == std::string::npos){
			worked = false;
			return c;
		}
		
		vals[vals.size()-1] *= 10;
		vals[vals.size()-1] += nm;
	}
	
	if (vals.size() != 3) {
		worked = false;
		return c;
	}
	
	for (auto v : vals) {
		if (v < 0 || v > 255) {
			worked = false;
			return c;
		}
	}
	
	c.r = (float)(vals[0])/255.0f;
	c.g = (float)(vals[1])/255.0f;
	c.b = (float)(vals[2])/255.0f;
	
	return c;
}