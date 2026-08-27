#pragma once

#include <string>
#include <vector>

/** Records connect/publish/subscribe attempts; injects failures. */
class FakeMqttTransport {
  public:
    bool failNextConnect = false;
    bool failNextPublish = false;
    bool connected       = false;

    struct Pub {
        int         messageId;
        std::string topic;
        std::string payload;
    };

    std::vector<std::string> connectLog;
    std::vector<Pub>         publishLog;
    std::vector<std::string> subscribeLog;

    void reset() {
        failNextConnect = false;
        failNextPublish = false;
        connected       = false;
        connectLog.clear();
        publishLog.clear();
        subscribeLog.clear();
        nextMessageId_ = 1;
    }

    bool connect(const char* server, uint16_t port) {
        connectLog.push_back(std::string(server ? server : "") + ":" + std::to_string(port));
        if (failNextConnect) {
            failNextConnect = false;
            connected       = false;
            return false;
        }
        connected = true;
        return true;
    }

    void disconnect() {
        connected = false;
    }

    bool subscribe(const char* topic) {
        if (!connected || topic == nullptr || topic[0] == '\0') {
            return false;
        }
        subscribeLog.emplace_back(topic);
        return true;
    }

    int publish(const char* topic, const char* payload) {
        if (!connected) {
            return -1;
        }
        if (failNextPublish) {
            failNextPublish = false;
            return -1;
        }
        const int messageId = nextMessageId_++;
        publishLog.push_back(Pub{messageId, topic ? topic : "", payload ? payload : ""});
        return messageId;
    }

  private:
    int nextMessageId_ = 1;
};
