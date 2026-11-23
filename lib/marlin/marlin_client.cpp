#include "marlin_client.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
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

std::string logText(const std::string &text) {
    std::string result;
    for (size_t i = 0; i < text.size() && i < 80; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        result += c >= 0x20 && c <= 0x7e ? static_cast<char>(c) : '?';
    }
    if (text.size() > 80) result += "...";
    return result;
}

enum class TemperatureResult { missing, invalid, valid };

TemperatureResult temperature(const std::string &line, const char *key, double &value) {
    size_t pos = line.find(key);
    while (pos != std::string::npos && pos != 0 && line[pos - 1] != ' ') {
        pos = line.find(key, pos + 1);
    }
    if (pos == std::string::npos) return TemperatureResult::missing;
    const char *start = line.c_str() + pos + std::char_traits<char>::length(key);
    char *end = nullptr;
    value = std::strtod(start, &end);
    return end != start && std::isfinite(value) && value >= -10000 && value <= 10000 &&
           (*end == '\0' || *end == ' ' || *end == '/')
               ? TemperatureResult::valid : TemperatureResult::invalid;
}
}

MarlinClient::MarlinClient(MarlinTransport &transport) : transport_(transport) {}

void MarlinClient::setDiagnosticLogger(std::function<void(const std::string &)> logger) {
    diagnosticLogger_ = logger;
}

void MarlinClient::diagnostic(const std::string &message) {
    if (diagnosticLogger_) {
        diagnosticLogger_("Printer diag t_ms=" + std::to_string(transport_.now()) + " " + message);
    }
}

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
    pendingCommand_ = command[0] == 'N' ? "numbered" : command.substr(0, command.find(' '));
    if (command.find("M110") != std::string::npos && command[0] == 'N') pendingCommand_ = "M110";
    if (pendingCommand_ == "numbered") {
        const size_t start = command.find(' ');
        if (start != std::string::npos && start + 1 < command.size()) {
            size_t end = start + 2;
            while (end < command.size() && end < start + 9 &&
                   (std::isdigit(static_cast<unsigned char>(command[end])) || command[end] == '.')) ++end;
            const char letter = command[start + 1];
            if ((letter == 'G' || letter == 'M' || letter == 'T' ||
                 letter == 'g' || letter == 'm' || letter == 't') && end > start + 2) {
                pendingCommand_ += ":" + upper(command.substr(start + 1, end - start - 1));
            }
        }
    }
    if (!transport_.connected()) {
        diagnostic("send_failed command=" + pendingCommand_ + " reason=disconnected session=" +
                   std::to_string(session_) + "/" + std::to_string(transport_.session()));
        return fail("Printer is not connected.");
    }
    if (transport_.session() != session_) {
        uncertain_ = true;
        diagnostic("send_failed command=" + pendingCommand_ + " reason=session_changed session=" +
                   std::to_string(session_) + "/" + std::to_string(transport_.session()));
        return fail("Printer USB connection changed during the operation.");
    }
    const std::string wire = command + "\n";
    pendingBytes_ = wire.size();
    if (!transport_.write(wire.data(), wire.size())) {
        diagnostic("send_failed command=" + pendingCommand_ + " line=" +
                   std::to_string(lineNumber_) + " bytes=" + std::to_string(pendingBytes_) +
                   " session=" + std::to_string(session_));
        return fail("Could not send command to printer.");
    }
    return true;
}

bool MarlinClient::waitOk(bool *matched, const char *expected) {
    if (matched) *matched = false;
    const unsigned long start = transport_.now();
    std::string lastResponse;
    unsigned int otherResponses = 0;
    while (true) {
        const unsigned long elapsed = transport_.now() - start;
        if (elapsed >= response_timeout_ms) break;
        std::string line;
        if (!readLine(line, response_timeout_ms - elapsed)) {
            if (!line.empty()) lastResponse = line;
            diagnostic("wait_failed command=" + pendingCommand_ + " line=" +
                       std::to_string(lineNumber_) + " bytes=" + std::to_string(pendingBytes_) +
                       " elapsed_ms=" + std::to_string(transport_.now() - start) +
                       " session=" + std::to_string(session_) + "/" + std::to_string(transport_.session()) +
                       " reason=" + error_ + " last=" + logText(lastResponse));
            return false;
        }
        if (!isOk(line)) {
            lastResponse = line;
            ++otherResponses;
            const bool dataReply = startsWith(pendingCommand_, "numbered");
            if (otherResponses <= 3 && pendingCommand_ != "M115" &&
                (!dataReply || loggedDataReplies_++ < 8)) {
                diagnostic("reply command=" + pendingCommand_ + " line=" +
                           std::to_string(lineNumber_) + " text=" + logText(line));
            }
        }
        if (expected && matched) {
            const bool exact = startsWith(expected, "Writing to file:") ||
                               startsWith(expected, "File deleted:");
            if ((exact && line == expected) || (!exact && startsWith(line, expected))) {
                *matched = true;
            }
        }
        if (startsWith(line, "Error:") || startsWith(line, "error:") ||
            startsWith(line, "Resend:") || startsWith(line, "rs ")) {
            uncertain_ = true;
            error_ = "Printer reported: " + line.substr(0, 160);
            diagnostic("rejected command=" + pendingCommand_ + " line=" +
                       std::to_string(lineNumber_) + " text=" + logText(line));
            return false;
        }
        if (isOk(line)) {
            if (matched && !*matched) {
                diagnostic("confirmation_missing command=" + pendingCommand_ + " last=" +
                           logText(lastResponse));
                return fail("Printer did not confirm the requested operation.");
            }
            if (pendingCommand_ == "M28" || pendingCommand_ == "M29") {
                diagnostic("ack command=" + pendingCommand_ + " elapsed_ms=" +
                           std::to_string(transport_.now() - start) + " other_replies=" +
                           std::to_string(otherResponses));
            }
            return true;
        }
    }
    uncertain_ = true;
    diagnostic("wait_failed command=" + pendingCommand_ + " line=" +
               std::to_string(lineNumber_) + " bytes=" + std::to_string(pendingBytes_) +
               " elapsed_ms=" + std::to_string(transport_.now() - start) +
               " session=" + std::to_string(session_) + "/" + std::to_string(transport_.session()) +
               " reason=timeout last=" + logText(lastResponse));
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
    bool printing = false;
    return readSdStatus(printing) &&
           (!printing || fail("Printer did not confirm an idle SD state."));
}

bool MarlinClient::readSdStatus(bool &printing) {
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
            if (idle == busy) return fail("Printer did not confirm an SD state.");
            printing = busy;
            return true;
        }
    }
    uncertain_ = true;
    return fail("Printer status timed out.");
}

bool MarlinClient::getStatus(MarlinStatus &status) {
    status = MarlinStatus();
    error_.clear();
    if (uploading_ || uncertain_) return fail("Printer transfer state is uncertain.");
    if (!synchronize() || !readSdStatus(status.sdPrinting) || !sendCommand("M105")) return false;
    const unsigned long start = transport_.now();
    while (transport_.now() - start < response_timeout_ms) {
        std::string line;
        if (!readLine(line, response_timeout_ms - (transport_.now() - start))) return false;
        if (startsWith(line, "Error:") || startsWith(line, "error:") ||
            startsWith(line, "Resend:") || startsWith(line, "rs ")) {
            uncertain_ = true;
            return fail("Printer rejected the temperature request.");
        }
        double value;
        const TemperatureResult nozzle = temperature(line, "T:", value);
        if (nozzle == TemperatureResult::invalid) return fail("Invalid nozzle temperature.");
        if (nozzle == TemperatureResult::valid) {
            status.nozzleTemperature = value;
            status.hasNozzleTemperature = true;
        }
        const TemperatureResult bed = temperature(line, "B:", value);
        if (bed == TemperatureResult::invalid) return fail("Invalid bed temperature.");
        if (bed == TemperatureResult::valid) {
            status.bedTemperature = value;
            status.hasBedTemperature = true;
        }
        if (isOk(line)) {
            return status.hasNozzleTemperature || fail("Printer did not report nozzle temperature.");
        }
    }
    uncertain_ = true;
    return fail("Printer temperature request timed out.");
}

bool MarlinClient::listFiles(std::vector<MarlinFile> &files) {
    files.clear();
    if (uploading_ || uncertain_) return fail("Printer transfer state is uncertain.");
    error_.clear();
    if (!synchronize() || !sendCommand("M20 L")) return false;
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
            const size_t space = line.find(' ');
            const size_t endSize = line.find(' ', space == std::string::npos ? 0 : space + 1);
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
            const std::string count = line.substr(space + 1, endSize == std::string::npos
                                                                  ? endSize : endSize - space - 1);
            if (count.empty() || count.find_first_not_of("0123456789") != std::string::npos ||
                count.size() > 10) {
                uncertain_ = true;
                return fail("Invalid printer file size.");
            }
            const unsigned long long size = std::strtoull(count.c_str(), nullptr, 10);
            if (size > std::numeric_limits<uint32_t>::max()) {
                uncertain_ = true;
                return fail("Invalid printer file size.");
            }
            const std::string name = endSize == std::string::npos
                                         ? line.substr(0, space)
                                         : line.substr(endSize + 1);
            if (name.empty() || name.size() > 512 ||
                name.find_first_of("\r\n") != std::string::npos) {
                uncertain_ = true;
                return fail("Invalid printer long filename.");
            }
            files.push_back({name, static_cast<uint32_t>(size), line.substr(0, space)});
        }
    }
    uncertain_ = true;
    return fail("Printer file list timed out.");
}

bool MarlinClient::validFilePath(const std::string &path) {
    return !path.empty() && path.size() <= 128 && path[0] != '/' &&
           path.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.~/") == std::string::npos &&
           path.find("..") == std::string::npos && path.find("//") == std::string::npos &&
           path.back() != '/';
}

bool MarlinClient::deleteFile(const std::string &path) {
    error_.clear();
    if (uploading_ || uncertain_) return fail("Printer transfer state is uncertain.");
    if (!validFilePath(path)) return fail("Invalid printer file path.");
    if (!synchronize() || !checkIdle()) return false;
    std::vector<MarlinFile> files;
    if (!listFiles(files)) return false;
    bool exists = false;
    for (const MarlinFile &file : files) {
        if (upper(file.path) == upper(path)) { exists = true; break; }
    }
    if (!exists) return fail("Printer file was not found.");
    if (!checkIdle()) return false;
    if (!sendCommand("M30 /" + path)) return false;
    const size_t slash = path.rfind('/');
    const std::string expected = "File deleted: " + path.substr(slash == std::string::npos ? 0 : slash + 1);
    bool deleted = false;
    return waitOk(&deleted, expected.c_str());
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
    loggedDataReplies_ = 0;
    diagnostic("upload_preflight target=" + target + " session=" + std::to_string(transport_.session()));
    if (!synchronize() || !checkIdle()) return false;
    std::vector<MarlinFile> files;
    if (!listFiles(files)) return false;
    for (const MarlinFile &file : files) {
        if (upper(file.path) == target) return fail("A printer file with this name already exists.");
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
    diagnostic("upload_open target=" + target + " session=" + std::to_string(session_));
    return true;
}

bool MarlinClient::uploadLine(const std::string &input) {
    if (!uploading_) return fail("No upload is active.");
    if (uncertain_) return fail("Printer transfer state is uncertain.");
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
    bool closesFile = false;
    for (size_t pos = normalized.find("M29"); pos != std::string::npos;
         pos = normalized.find("M29", pos + 3)) {
        if (pos + 3 == normalized.size() || normalized[pos + 3] < '0' ||
            normalized[pos + 3] > '9') {
            closesFile = true;
            break;
        }
    }
    if (normalized[0] == 'N' || normalized.find("M110") != std::string::npos || closesFile) {
        return fail("G-code line contains a reserved transfer command.");
    }
    if (!numbered(line)) return false;
    if (lineNumber_ == std::numeric_limits<uint32_t>::max()) return fail("G-code contains too many lines.");
    ++lineNumber_;
    return true;
}

bool MarlinClient::finishUpload() {
    if (!uploading_) return fail("No upload is active.");
    if (uncertain_) return fail("Printer transfer state is uncertain.");
    uploading_ = false;
    uncertain_ = true;
    bool saved = false;
    diagnostic("upload_close line=" + std::to_string(lineNumber_));
    if (!sendCommand("M29") || !waitOk(&saved, "Done saving file")) return false;
    uncertain_ = false;
    diagnostic("upload_closed line=" + std::to_string(lineNumber_));
    return true;
}

void MarlinClient::abortUpload() {
    if (!uploading_) return;
    const std::string cause = error_;
    uploading_ = false;
    uncertain_ = true;
    bool saved = false;
    diagnostic("upload_abort line=" + std::to_string(lineNumber_) + " cause=" + logText(cause));
    if (sendCommand("M29") && waitOk(&saved, "Done saving file")) uncertain_ = false;
    diagnostic("upload_abort_result closed=" + std::string(uncertain_ ? "no" : "yes") +
               " detail=" + (uncertain_ ? logText(error_) : "ok"));
    error_ = cause.empty() ? "Upload aborted; a partial file may remain." : cause;
}
