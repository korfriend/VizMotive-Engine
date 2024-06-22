#pragma once
#include "../Common/CommonInclude.h"
#include "../Libs/Math.h"

namespace vz
{
	struct Color
	{
		uint32_t rgba = 0;

		constexpr Color(uint32_t rgba) :rgba(rgba) {}
		constexpr Color(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 255) : rgba(uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24)) {}
		constexpr Color(const char* hex)
		{
			rgba = 0;
			uint32_t shift = 0u;
			for (int i = 0; i < 9; ++i)
			{
				const char c = hex[i];
				switch (c)
				{
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					rgba |= (c - '0') << shift;
					shift += 4u;
					break;
				case 'A':
				case 'B':
				case 'C':
				case 'D':
				case 'E':
				case 'F':
					rgba |= (c - 'A' + 10) << shift;
					shift += 4u;
					break;
				case '#':
					break;
				default:
				case 0:
					return;
				}
			}
		}

		constexpr uint8_t GetR() const { return (rgba >> 0) & 0xFF; }
		constexpr uint8_t GetG() const { return (rgba >> 8) & 0xFF; }
		constexpr uint8_t GetB() const { return (rgba >> 16) & 0xFF; }
		constexpr uint8_t GetA() const { return (rgba >> 24) & 0xFF; }

		constexpr void SetR(uint8_t value) { *this = Color(value, GetG(), GetB(), GetA()); }
		constexpr void SetG(uint8_t value) { *this = Color(GetR(), value, GetB(), GetA()); }
		constexpr void SetB(uint8_t value) { *this = Color(GetR(), GetG(), value, GetA()); }
		constexpr void SetA(uint8_t value) { *this = Color(GetR(), GetG(), GetB(), value); }

		constexpr XMFLOAT3 ToFloat3() const
		{
			return XMFLOAT3(
				((rgba >> 0) & 0xFF) / 255.0f,
				((rgba >> 8) & 0xFF) / 255.0f,
				((rgba >> 16) & 0xFF) / 255.0f
			);
		}
		constexpr XMFLOAT4 ToFloat4() const
		{
			return XMFLOAT4(
				((rgba >> 0) & 0xFF) / 255.0f,
				((rgba >> 8) & 0xFF) / 255.0f,
				((rgba >> 16) & 0xFF) / 255.0f,
				((rgba >> 24) & 0xFF) / 255.0f
			);
		}
		constexpr operator XMFLOAT3() const { return ToFloat3(); }
		constexpr operator XMFLOAT4() const { return ToFloat4(); }
		constexpr operator uint32_t() const { return rgba; }

		template<size_t capacity>
		struct char_return
		{
			char text[capacity] = {};
			constexpr operator const char* () const { return text; }
		};
		constexpr const char_return<9> ToHex() const
		{
			char_return<9> ret;
			for (int i = 0; i < 8; ++i)
			{
				const uint8_t v = (rgba >> (i * 4)) & 0xF;
				ret.text[i] = v < 10 ? ('0' + v) : ('A' + v - 10);
			}
			return ret;
		}

		static constexpr Color FromFloat4(const XMFLOAT4& value)
		{
			return Color((uint8_t)(value.x * 255), (uint8_t)(value.y * 255), (uint8_t)(value.z * 255), (uint8_t)(value.w * 255));
		}
		static constexpr Color FromFloat3(const XMFLOAT3& value)
		{
			return Color((uint8_t)(value.x * 255), (uint8_t)(value.y * 255), (uint8_t)(value.z * 255));
		}

		static constexpr Color Lerp(Color a, Color b, float i)
		{
			return FromFloat4(vz::math::Lerp(a.ToFloat4(), b.ToFloat4(), i));
		}

		static constexpr Color Red() { return Color(255, 0, 0, 255); }
		static constexpr Color Green() { return Color(0, 255, 0, 255); }
		static constexpr Color Blue() { return Color(0, 0, 255, 255); }
		static constexpr Color Black() { return Color(0, 0, 0, 255); }
		static constexpr Color White() { return Color(255, 255, 255, 255); }
		static constexpr Color Yellow() { return Color(255, 255, 0, 255); }
		static constexpr Color Purple() { return Color(255, 0, 255, 255); }
		static constexpr Color Cyan() { return Color(0, 255, 255, 255); }
		static constexpr Color Transparent() { return Color(0, 0, 0, 0); }
		static constexpr Color Gray() { return Color(127, 127, 127, 255); }
		static constexpr Color Ghost() { return Color(127, 127, 127, 127); }
		static constexpr Color Booger() { return Color(127, 127, 127, 200); }
		static constexpr Color Shadow() { return Color(0, 0, 0, 100); }

		static constexpr Color Warning() { return 0xFF66FFFF; } // light yellow
		static constexpr Color Error() { return 0xFF6666FF; } // light red
	};

	struct Color16
	{
		uint64_t rgba = 0;

		constexpr Color16(uint64_t rgba) :rgba(rgba) {}
		constexpr Color16(uint16_t r = 0, uint16_t g = 0, uint16_t b = 0, uint16_t a = 65535) : rgba(uint64_t(r) | (uint64_t(g) << 16) | (uint64_t(b) << 32) | (uint64_t(a) << 48)) {}
		constexpr Color16(Color color) { *this = FromFloat4(color.ToFloat4()); }

		constexpr uint16_t GetR() const { return (rgba >> 0) & 0xFFFF; }
		constexpr uint16_t GetG() const { return (rgba >> 16) & 0xFFFF; }
		constexpr uint16_t GetB() const { return (rgba >> 32) & 0xFFFF; }
		constexpr uint16_t GetA() const { return (rgba >> 48) & 0xFFFF; }

		constexpr void SetR(uint16_t value) { *this = Color16(value, GetG(), GetB(), GetA()); }
		constexpr void SetG(uint16_t value) { *this = Color16(GetR(), value, GetB(), GetA()); }
		constexpr void SetB(uint16_t value) { *this = Color16(GetR(), GetG(), value, GetA()); }
		constexpr void SetA(uint16_t value) { *this = Color16(GetR(), GetG(), GetB(), value); }

		constexpr XMFLOAT3 ToFloat3() const
		{
			return XMFLOAT3(
				((rgba >> 0) & 0xFFFF) / 65535.0f,
				((rgba >> 16) & 0xFFFF) / 65535.0f,
				((rgba >> 32) & 0xFFFF) / 65535.0f
			);
		}
		constexpr XMFLOAT4 ToFloat4() const
		{
			return XMFLOAT4(
				((rgba >> 0) & 0xFFFF) / 65535.0f,
				((rgba >> 16) & 0xFFFF) / 65535.0f,
				((rgba >> 32) & 0xFFFF) / 65535.0f,
				((rgba >> 48) & 0xFFFF) / 65535.0f
			);
		}
		constexpr operator XMFLOAT3() const { return ToFloat3(); }
		constexpr operator XMFLOAT4() const { return ToFloat4(); }
		constexpr operator uint64_t() const { return rgba; }

		static constexpr Color16 FromFloat4(const XMFLOAT4& value)
		{
			return Color16(uint16_t(value.x * 65535), uint16_t(value.y * 65535), uint16_t(value.z * 65535), uint16_t(value.w * 65535));
		}
		static constexpr Color16 fromFloat3(const XMFLOAT3& value)
		{
			return Color16(uint16_t(value.x * 65535), uint16_t(value.y * 65535), uint16_t(value.z * 65535));
		}

		static constexpr Color16 lerp(Color16 a, Color16 b, float i)
		{
			return FromFloat4(vz::math::Lerp(a.ToFloat4(), b.ToFloat4(), i));
		}
	};
}

