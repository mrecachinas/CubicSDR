#pragma once

#include "IOThread.h"
#include "WebSocketProtocol.h"

#include <set>
#include <map>
#include <mutex>
#include <atomic>
#include <memory>
#include <string>

// Forward declarations — keep uWS out of the header.
struct us_listen_socket_t;

// Forward declarations for CubicSDR data queue types.
class SpectrumVisualData;
typedef std::shared_ptr<SpectrumVisualData> SpectrumVisualDataPtr;
typedef ThreadBlockingQueue<SpectrumVisualDataPtr> SpectrumVisualDataQueue;

class AudioThreadInput;
typedef std::shared_ptr<AudioThreadInput> AudioThreadInputPtr;
typedef ThreadBlockingQueue<AudioThreadInputPtr> DemodulatorThreadOutputQueue;

class DemodulatorThreadIQData;
typedef std::shared_ptr<DemodulatorThreadIQData> DemodulatorThreadIQDataPtr;
typedef ThreadBlockingQueue<DemodulatorThreadIQDataPtr> DemodulatorThreadInputQueue;

struct WebSocketClientData {
    std::set<WSStreamType> subscriptions;
};

class WebSocketServerThread : public IOThread {
public:
    WebSocketServerThread();
    ~WebSocketServerThread() override;

    void run() override;
    void terminate() override;

    void setPort(int port);
    int getPort() const;

    void setSpectrumQueue(std::shared_ptr<SpectrumVisualDataQueue> queue);
    void setWaterfallQueue(std::shared_ptr<SpectrumVisualDataQueue> queue);
    void setAudioQueue(std::shared_ptr<DemodulatorThreadOutputQueue> queue);
    void setIQQueue(std::shared_ptr<DemodulatorThreadInputQueue> queue);

    std::shared_ptr<SpectrumVisualDataQueue> getWaterfallQueue() { return waterfallQueue_; }
    std::shared_ptr<DemodulatorThreadOutputQueue> getAudioQueue() { return audioQueue_; }

private:
    void pollQueues();
    void handleClientMessage(const std::string& message, void* ws);

    int port_;
    std::atomic<bool> serverRunning_;

    std::shared_ptr<SpectrumVisualDataQueue> spectrumQueue_;
    std::shared_ptr<SpectrumVisualDataQueue> waterfallQueue_;
    std::shared_ptr<DemodulatorThreadOutputQueue> audioQueue_;
    std::shared_ptr<DemodulatorThreadInputQueue> iqQueue_;

    // Scratch buffers for extracting y-values from interleaved x,y spectrum data
    std::vector<float> spectrumYBuf_;
    std::vector<float> waterfallYBuf_;

    // Pimpl — hides uWS types from every translation unit that includes this header.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
