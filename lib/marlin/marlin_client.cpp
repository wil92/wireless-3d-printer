#include "marlin_client.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
const unsigned long response_timeout_ms = 10000;
const size_t max_response_line = 1024;
const size_t max_files = 256;
// Includes N, checksum and terminator within Marlin's typical 96-byte buffer.
const size_t max_wire_line = 94;

bool startsWith(const std::string &line, const char *prefix) {
    return line.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

bool isOk(const std::string &line) {
    return line == "ok" || startsWith(line, "ok ");
}

std::string upper(const std::string &s) {
    std::string result = s;
    for (char &ch : result) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return result;
}
}

MarlinClient::MarlinClient(MarlinTransport &transport) : transport_(transport) {}

bool MarlinClient::fail(const char *message) {
    error_ = message;
    return false;
}

bool MarlinClient::readLine(std::string &line, unsigned long timeoutMs) {
    line.clear();
    const unsigned long start = transport_.now();
    while (transport_.now() - start < timeoutMs) {
        const unsigned long elapsed = transport_.now() - start;
        if (elapsed >= timeoutMs) break;
        if (transport_.session() != session_) {
            uncertain_ = true;
            return fail("Printer USB connection changed during the operation.");
        }
        const unsigned long remaining = timeoutMs - elapsed;
        const int c = transport_.read(remaining);
        if (transport_.session() != session_) {
            uncertain_ = true;
            return fail("Printer USB connection changed during the operation.");
        }
        if (c == -2) { uncertain_ = true; return fail("Printer disconnected or USB receive buffer overflowed."); }
        if (c < 0) { uncertain_ = true; return fail("Printer response timed out."); }
        if (c == '\r') continue;
        if (c == '\n') return true;
        if (line.size() >= max_response_line) { uncertain_ = true; return fail("Printer response line too long."); }
        line += static_cast<char>(c);
    }
    uncertain_ = true;
    return fail("Printer response timed out.");
}

bool MarlinClient::sendCommand(const std::string &command) {
    if (!transport_.connected()) return fail("Printer is not connected.");
    if (transport_.session() != session_) {
        uncertain_ = true;
        return fail("Printer USB connection changed during the operation.");
    }
    const std::string wire = command + "\n";
    if (!transport_.write(wire.data(), wire.size())) {
        return fail("Could not send command to printer.");
    }
    return true;
}

bool MarlinClient::waitOk(bool *matched, const char *expected) {
    if (matched) *matched = false;
    const unsigned long start = transport_.now();
    while (true) {
        const unsigned long elapsed = transport_.now() - start;
        if (elapsed >= response_timeout_ms) break;
        std::string line;
        if (!readLine(line, response_timeout_ms - elapsed)) return false;
        if (expected && matched) {
            const bool exact = startsWith(expected, "Writing to file:");
            if ((exact && line == expected) || (!exact && startsWith(line, expected))) {
                *matched = true;
            }
        }
        if (startsWith(line, "Error:") || startsWith(line, "error:") ||
            startsWith(line, "Resend:") || startsWith(line, "rs ")) {
            uncertain_ = true;
            return fail("Printer reported an error or requested a resend.");
        }
        if (isOk(line)) {
            if (matched && !*matched) return fail("Printer did not confirm the requested operation.");
            return true;
        }
    }
    uncertain_ = true;
    return fail("Printer response timed out.");
}

bool MarlinClient::synchronize() {
    if (!transport_.connected()) return fail("Printer is not connected.");
    session_ = transport_.session();
    const unsigned long start = transport_.now();
    while (transport_.now() - start < 2000) {
        const int c = transport_.read(150);
        if (transport_.session() != session_) {
            uncertain_ = true;
            return fail("Printer USB connection changed during synchronization.");
        }
        if (c == -2) return fail("Printer disconnected or USB receive buffer overflowed.");
        if (c == -1) {
            bool identified = false;
            return sendCommand("M115") && waitOk(&identified, "FIRMWARE_NAME:");
        }
    }
    return fail("Printer serial input did not become idle.");
}

bool MarlinClient::checkIdle() {
    if (!sendCommand("M27")) return false;
    bool idle = false, busy = false;
    const unsigned long start = transport_.now();
    while (true) {
        const unsigned long elapsed = transport_.now() - start;
        if (elapsed >= response_timeout_ms) break;
        std::string line;
        if (!readLine(line, response_timeout_ms - elapsed)) return false;
        if (line == "Not SD printing") idle = true;
        if (startsWith(line, "SD printing byte")) busy = true;
        if (startsWith(line, "Error:") || startsWith(line, "error:") ||
            startsWith(line, "Resend:") || startsWith(line, "rs ")) {
            uncertain_ = true;
            return fail("Printer rejected the status request.");
        }
        if (isOk(line)) {
            return idle && !busy ? true : fail("Printer did not confirm an idle SD state.");
        }
    }
    uncertain_ = true;
    return fail("Printer status timed out.");
}

bool MarlinClient::listFiles(std::vector<MarlinFile> &files) {
    files.clear();
    if (uploading_ || uncertain_) return fail("Printer transfer state is uncertain.");
    if (!synchronize() || !sendCommand("M20")) return false;
    bool started = false, ended = false;
    const unsigned long start = transport_.now();
    while (true) {
        const unsigned long elapsed = transport_.now() - start;
        if (elapsed >= response_timeout_ms) break;
        std::string line;
        if (!readLine(line, response_timeout_ms - elapsed)) return false;
        if (line == "Begin file list" && !started) { started = true; continue; }
        if (line == "End file list" && started && !ended) { ended = true; continue; }
        if (isOk(line)) {
            if (!started || !ended) return fail("Printer did not return a complete file list.");
            return true;
        }
        if (startsWith(line, "Error:") || startsWith(line, "error:") ||
            startsWith(line, "Resend:") || startsWith(line, "rs ")) {
            uncertain_ = true;
            return fail("Printer rejected the file list request.");
        }
        if (started && !ended) {
            const size_t space = line.rfind(' ');
            if (space == std::string::npos || space == 0 || space > 128 ||
                space + 1 == line.size() ||
                files.size() >= max_files) {
                uncertain_ = true;
                return fail("Invalid or oversized printer file list.");
            }
            for (size_t i = 0; i < space; ++i) {
                const unsigned char c = static_cast<unsigned char>(line[i]);
                if (c < 0x20 || c > 0x7e) {
                    uncertain_ = true;
                    return fail("Unsupported printer filename in file list.");
                }
            }
            const std::string count = line.substr(space + 1);
            if (count.find_first_not_of("0123456789") != std::string::npos ||
                count.size() > 10) {
                uncertain_ = true;
                return fail("Invalid printer file size.");
            }
            const unsigned long long size = std::strtoull(count.c_str(), nullptr, 10);
            if (size > std::numeric_limits<uint32_t>::max()) {
                uncertain_ = true;
                return fail("Invalid printer file size.");
            }
            files.push_back({line.substr(0, space), static_cast<uint32_t>(size)});
        }
    }
    uncertain_ = true;
    return fail("Printer file list timed out.");
}

bool MarlinClient::makeFilename(const std::string &input, std::string &output) {
    if (input.empty() || input.find_first_of("/\\") != std::string::npos) return false;
    const size_t dot = input.rfind('.');
    if (dot == std::string::npos || upper(input.substr(dot)) != ".GCODE") return false;
    output.clear();
    for (size_t i = 0; i < dot && output.size() < 8; ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            output += static_cast<char>(std::toupper(c));
        } else {
            output += '_';
        }
    }
    if (output.empty()) return false;
    output += ".GCO";
    return true;
}

bool MarlinClient::numbered(const std::string &command) {
    char number[16];
    std::snprintf(number, sizeof(number), "N%lu ", static_cast<unsigned long>(lineNumber_));
    std::string wire = std::string(number) + command;
    unsigned char checksum = 0;
    for (char ch : wire) checksum ^= static_cast<unsigned char>(ch);
    wire += "*" + std::to_string(checksum);
    if (wire.size() > max_wire_line) return fail("G-code line exceeds printer command limit.");
    return sendCommand(wire) && waitOk();
}

bool MarlinClient::beginUpload(const std::string &filename, std::string &target) {
    error_.clear();
    if (uploading_ || uncertain_) return fail("Printer transfer state is uncertain.");
    if (!makeFilename(filename, target)) return fail("Invalid G-code filename.");
    if (!synchronize() || !checkIdle()) return false;
    std::vector<MarlinFile> files;
    if (!listFiles(files)) return false;
    for (const MarlinFile &file : files) {
        if (upper(file.name) == target) return fail("A printer file with this name already exists.");
    }
    if (!checkIdle()) return false;
    lineNumber_ = 0;
    if (!numbered("M110 N0")) return false;
    if (!sendCommand("M28 /" + target)) return false;
    // From here, an error may mean M28 succeeded even if its response was lost.
    uncertain_ = true;
    bool opened = false;
    const std::string expected = "Writing to file: " + target;
    if (!waitOk(&opened, expected.c_str())) return false;
    uploading_ = true;
    uncertain_ = false;
    lineNumber_ = 1;
    return true;
}

bool MarlinClient::uploadLine(const std::string &input) {
    if (!uploading_) return fail("No upload is active.");
    std::string line = input.substr(0, input.find(';'));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) return true;
    line.erase(0, first);
    for (char ch : line) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c > 0x7e || ch == '*' || ch == '\\' || ch == '(' ||
            ch == ')' || ch == '"') return fail("Unsupported characters in G-code line.");
    }
    const std::string normalized = upper(line);
    if (normalized[0] == 'N' || normalized.find("M110") != std::string::npos ||
        normalized.find("M29") != std::string::npos) {
        return fail("G-code line contains a reserved transfer command.");
    }
    if (!numbered(line)) return false;
    if (lineNumber_ == std::numeric_limits<uint32_t>::max()) return fail("G-code contains too many lines.");
    ++lineNumber_;
    return true;
}

bool MarlinClient::finishUpload() {
    if (!uploading_) return fail("No upload is active.");
    uploading_ = false;
    uncertain_ = true;
    bool saved = false;
    if (!sendCommand("M29") || !waitOk(&saved, "Done saving file")) return false;
    uncertain_ = false;
    return true;
}

void MarlinClient::abortUpload() {
    if (!uploading_) return;
    const std::string cause = error_;
    uploading_ = false;
    uncertain_ = true;
    bool saved = false;
    if (sendCommand("M29") && waitOk(&saved, "Done saving file")) uncertain_ = false;
    error_ = cause.empty() ? "Upload aborted; a partial file may remain." : cause;
}
