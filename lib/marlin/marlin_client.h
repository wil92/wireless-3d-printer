#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class MarlinTransport {
public:
    virtual ~MarlinTransport() {}
    virtual bool connected() = 0;
    virtual bool write(const char *data, size_t length) = 0;
    // Returns a byte, -1 on timeout, or -2 on connection or buffer failure.
    virtual int read(unsigned long timeoutMs) = 0;
    virtual unsigned long now() = 0;
    virtual uint32_t session() = 0;
};

struct MarlinFile {
    std::string name;
    uint32_t size;
    std::string path;
};

struct MarlinStatus {
    bool sdPrinting = false;
    bool hasNozzleTemperature = false;
    bool hasBedTemperature = false;
    double nozzleTemperature = 0;
    double bedTemperature = 0;
};

class MarlinClient {
public:
    explicit MarlinClient(MarlinTransport &transport);
    void setDiagnosticLogger(std::function<void(const std::string &)> logger);
    bool listFiles(std::vector<MarlinFile> &files);
    bool deleteFile(const std::string &path);
    bool getStatus(MarlinStatus &status);
    bool beginUpload(const std::string &filename, std::string &target);
    bool uploadLine(const std::string &line);
    bool finishUpload();
    void abortUpload();
    const std::string &error() const { return error_; }
    bool uploading() const { return uploading_; }

    static bool makeFilename(const std::string &input, std::string &output);
    static bool validFilePath(const std::string &path);

private:
    bool readLine(std::string &line, unsigned long timeoutMs);
    bool sendCommand(const std::string &command);
    bool waitOk(bool *matched = nullptr, const char *expected = nullptr);
    bool synchronize();
    bool checkIdle();
    bool readSdStatus(bool &printing);
    bool fail(const char *message);
    bool numbered(const std::string &command);
    void diagnostic(const std::string &message);

    MarlinTransport &transport_;
    std::function<void(const std::string &)> diagnosticLogger_;
    std::string pendingCommand_;
    size_t pendingBytes_ = 0;
    unsigned int loggedDataReplies_ = 0;
    std::string error_;
    uint32_t lineNumber_ = 0;
    uint32_t session_ = 0;
    bool uploading_ = false;
    bool uncertain_ = false;
};
