#include <cassert>
#include <string>

#include "../../../lib/debug_log/runtime_config.h"

FakeSerial Serial;
std::string savedReceiver;
bool storageAvailable = true;
bool failWrite = false;
bool failRemove = false;

void command(RuntimeDebugConfig &config, const std::string &value) {
    Serial.input += value;
    while (Serial.available()) config.poll();
}

int main() {
    RuntimeDebugConfig config;
    assert(config.begin() == "");
    command(config, "debug-address logs.local:9000\r\n");
    assert(savedReceiver == "logs.local:9000");
    assert(Serial.output.find("restart to apply") != std::string::npos);

    RuntimeDebugConfig afterRestart;
    assert(afterRestart.begin() == "http://logs.local:9000/");
    assert(afterRestart.receiver() == "logs.local:9000");
    assert(!afterRestart.saveReceiver("bad:0"));
    assert(savedReceiver == "logs.local:9000");
    assert(afterRestart.saveReceiver("other.local"));
    assert(savedReceiver == "other.local");
    assert(afterRestart.saveReceiver("logs.local:9000"));
    command(afterRestart, "debug-address bad:0\n");
    assert(savedReceiver == "logs.local:9000");
    assert(Serial.output.find("Invalid debug receiver") != std::string::npos);
    command(afterRestart, "debug-address " + std::string(200, 'a') + "\n");
    assert(savedReceiver == "logs.local:9000");
    assert(Serial.output.find("Debug command too long") != std::string::npos);
    command(afterRestart, std::string("debug-address hi") + char(0) + "bad\n");
    assert(savedReceiver == "logs.local:9000");

    failWrite = true;
    assert(!afterRestart.saveReceiver("other.local"));
    command(afterRestart, "debug-address other.local\n");
    assert(savedReceiver == "logs.local:9000");
    assert(Serial.output.find("Failed to save") != std::string::npos);
    failWrite = false;
    failRemove = true;
    assert(!afterRestart.saveReceiver(""));
    command(afterRestart, "debug-address off\n");
    assert(savedReceiver == "logs.local:9000");
    assert(Serial.output.find("Failed to clear") != std::string::npos);
    failRemove = false;
    assert(afterRestart.saveReceiver(""));
    assert(savedReceiver.empty());
    assert(afterRestart.saveReceiver(""));
    assert(afterRestart.saveReceiver("logs.local:9000"));
    command(afterRestart, "debug-address off\n");
    assert(savedReceiver.empty());
    RuntimeDebugConfig disabled;
    assert(disabled.begin() == "");

    savedReceiver = "bad:0";
    RuntimeDebugConfig corrupted;
    assert(corrupted.begin() == "");
    assert(Serial.output.find("Saved debug receiver is invalid") != std::string::npos);
    storageAvailable = false;
    RuntimeDebugConfig unavailable;
    assert(unavailable.begin() == "");
    assert(!unavailable.saveReceiver("good.local"));
    command(unavailable, "debug-address good.local\n");
    assert(savedReceiver == "bad:0");
    assert(Serial.output.find("storage unavailable") != std::string::npos);
}
