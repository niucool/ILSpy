// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/Metadata/ILTextEmitter.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ILSpy::Decompiler::Metadata {

namespace {
void AppendOffset(std::string& out, std::uint32_t off) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "IL_%04X", off);
    out += buf;
}

void AppendInt32(std::string& out, std::int32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", v);
    out += buf;
}

bool ReadU8(const std::uint8_t* b, std::size_t size, std::size_t pos, std::uint8_t& out) {
    if (pos >= size) return false;
    out = b[pos];
    return true;
}
bool ReadI8(const std::uint8_t* b, std::size_t size, std::size_t pos, std::int8_t& out) {
    if (pos + 1 > size) return false;
    out = static_cast<std::int8_t>(b[pos]);
    return true;
}
bool ReadU16(const std::uint8_t* b, std::size_t size, std::size_t pos, std::uint16_t& out) {
    if (pos + 2 > size) return false;
    out = static_cast<std::uint16_t>(b[pos]) | (static_cast<std::uint16_t>(b[pos + 1]) << 8);
    return true;
}
bool ReadI32(const std::uint8_t* b, std::size_t size, std::size_t pos, std::int32_t& out) {
    if (pos + 4 > size) return false;
    out = static_cast<std::int32_t>(static_cast<std::uint32_t>(b[pos]) |
        (static_cast<std::uint32_t>(b[pos + 1]) << 8) |
        (static_cast<std::uint32_t>(b[pos + 2]) << 16) |
        (static_cast<std::uint32_t>(b[pos + 3]) << 24));
    return true;
}
bool ReadU32(const std::uint8_t* b, std::size_t size, std::size_t pos, std::uint32_t& out) {
    if (pos + 4 > size) return false;
    out = static_cast<std::uint32_t>(b[pos]) | (static_cast<std::uint32_t>(b[pos + 1]) << 8) |
         (static_cast<std::uint32_t>(b[pos + 2]) << 16) | (static_cast<std::uint32_t>(b[pos + 3]) << 24);
    return true;
}
bool ReadI64(const std::uint8_t* b, std::size_t size, std::size_t pos, std::int64_t& out) {
    if (pos + 8 > size) return false;
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(b[pos + i]) << (8 * i);
    out = static_cast<std::int64_t>(v);
    return true;
}
bool ReadF4(const std::uint8_t* b, std::size_t size, std::size_t pos, float& out) {
    std::uint32_t u = 0;
    if (!ReadU32(b, size, pos, u)) return false;
    std::memcpy(&out, &u, sizeof(out));
    return true;
}
bool ReadF8(const std::uint8_t* b, std::size_t size, std::size_t pos, double& out) {
    std::uint64_t u = 0;
    if (pos + 8 > size) return false;
    for (int i = 0; i < 8; ++i) u |= static_cast<std::uint64_t>(b[pos + i]) << (8 * i);
    std::memcpy(&out, &u, sizeof(out));
    return true;
}
} // namespace

std::string DisassembleILText(Util::Span<const std::uint8_t> il,
                             const std::function<std::string(std::uint32_t)>& tokenResolver) {
    std::string out;
    const auto* b = il.data();
    std::size_t size = il.size();
    std::size_t pos = 0;
    while (pos < size) {
        std::size_t start = pos;
        ILOpCode op = DecodeOpCode(b, size, pos);
        AppendOffset(out, static_cast<std::uint32_t>(start));
        out += ": ";
        out += std::string(GetDisplayName(op));
        std::size_t opLen = pos - start;  // opcode bytes already consumed

        switch (GetOperandType(op)) {
            case OperandType::None:
                break;
            case OperandType::ShortBrTarget: {
                std::int8_t rel = 0;
                ReadI8(b, size, pos, rel); pos += 1;
                out += ' ';
                AppendOffset(out, static_cast<std::uint32_t>(pos) + static_cast<std::int32_t>(rel));
                break;
            }
            case OperandType::BrTarget: {
                std::int32_t rel = 0;
                ReadI32(b, size, pos, rel); pos += 4;
                out += ' ';
                AppendOffset(out, static_cast<std::uint32_t>(pos) + rel);
                break;
            }
            case OperandType::Switch: {
                std::uint32_t n = 0;
                ReadU32(b, size, pos, n); pos += 4;
                std::size_t next = pos + static_cast<std::size_t>(n) * 4;
                out += " (";
                for (std::uint32_t i = 0; i < n; ++i) {
                    std::int32_t rel = 0;
                    ReadI32(b, size, pos, rel); pos += 4;
                    if (i) out += ", ";
                    AppendOffset(out, static_cast<std::uint32_t>(next) + rel);
                }
                out += ')';
                break;
            }
            case OperandType::ShortI: {
                std::int8_t v = 0; ReadI8(b, size, pos, v); pos += 1;
                out += ' '; AppendInt32(out, v); break;
            }
            case OperandType::I: {
                std::int32_t v = 0; ReadI32(b, size, pos, v); pos += 4;
                out += ' '; AppendInt32(out, v); break;
            }
            case OperandType::I8: {
                std::int64_t v = 0; ReadI64(b, size, pos, v); pos += 8;
                char buf[32]; std::snprintf(buf, sizeof(buf), "%lld",
                    static_cast<long long>(v));
                out += ' '; out += buf; break;
            }
            case OperandType::ShortR: {
                float v = 0; ReadF4(b, size, pos, v); pos += 4;
                char buf[32]; std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
                out += ' '; out += buf; break;
            }
            case OperandType::R: {
                double v = 0; ReadF8(b, size, pos, v); pos += 8;
                char buf[32]; std::snprintf(buf, sizeof(buf), "%g", v);
                out += ' '; out += buf; break;
            }
            case OperandType::Variable: {
                std::uint16_t v = 0; ReadU16(b, size, pos, v); pos += 2;
                char buf[16]; std::snprintf(buf, sizeof(buf), " %u", v); out += buf; break;
            }
            case OperandType::ShortVariable: {
                std::uint8_t v = 0; ReadU8(b, size, pos, v); pos += 1;
                char buf[16]; std::snprintf(buf, sizeof(buf), " %u", v); out += buf; break;
            }
            // Token-bearing operands resolve through the caller's resolver.
            case OperandType::Method:
            case OperandType::Field:
            case OperandType::Type:
            case OperandType::String:
            case OperandType::Tok:
            case OperandType::Sig: {
                std::uint32_t tok = 0; ReadU32(b, size, pos, tok); pos += 4;
                out += ' ';
                out += tokenResolver ? tokenResolver(tok) : std::string("0x????????");
                break;
            }
            default:
                // Unknown opcode: skip the fixed operand size so the walk can
                // continue best-effort.
                pos += OperandFixedSize(GetOperandType(op));
                break;
        }
        out += '\n';
    }
    return out;
}

} // namespace ILSpy::Decompiler::Metadata
