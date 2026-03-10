#include "WebSocketServerThread.h"

#include "SpectrumVisualProcessor.h"
#include "AudioThread.h"
#include "DemodDefs.h"
#include "WebSocketProtocol.h"

#include <App.h>
#include <libusockets.h>

#include <iostream>
#include <string>
#include <string_view>
#include <set>
#include <algorithm>

// ---------------------------------------------------------------------------
// Pimpl — all uWS-dependent state lives here.
// ---------------------------------------------------------------------------
struct WebSocketServerThread::Impl {
    uWS::App *app = nullptr;
    struct us_listen_socket_t *listenSocket = nullptr;
    struct us_timer_t *pollTimer = nullptr;
    struct us_loop_t *loop = nullptr;
    std::set<uWS::WebSocket<false, true, WebSocketClientData>*> clients;
};

// ---------------------------------------------------------------------------
// Helpers (file-local)
// ---------------------------------------------------------------------------

// Map a human-readable stream name to the protocol enum.
static bool streamNameToType(const std::string& name, WSStreamType& out) {
    if (name == "spectrum")  { out = WS_STREAM_SPECTRUM;  return true; }
    if (name == "waterfall") { out = WS_STREAM_WATERFALL; return true; }
    if (name == "audio")     { out = WS_STREAM_AUDIO;     return true; }
    if (name == "iq")        { out = WS_STREAM_IQ;        return true; }
    return false;
}

static std::string streamTypeToName(WSStreamType t) {
    switch (t) {
        case WS_STREAM_SPECTRUM:  return "spectrum";
        case WS_STREAM_WATERFALL: return "waterfall";
        case WS_STREAM_AUDIO:     return "audio";
        case WS_STREAM_IQ:        return "iq";
    }
    return "unknown";
}

// Minimal JSON value extraction — no library required.
// Looks for "key":"value" and returns value (without quotes).
static std::string jsonStringValue(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};

    // Skip past the key, colon, optional whitespace, and opening quote.
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos; // skip opening quote

    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

WebSocketServerThread::WebSocketServerThread()
    : port_(8080)
    , serverRunning_(false)
    , impl_(std::make_unique<Impl>())
{
}

WebSocketServerThread::~WebSocketServerThread() {
    terminate();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void WebSocketServerThread::setPort(int port) { port_ = port; }
int  WebSocketServerThread::getPort() const   { return port_; }

void WebSocketServerThread::setSpectrumQueue(std::shared_ptr<SpectrumVisualDataQueue> queue) {
    spectrumQueue_ = std::move(queue);
}

void WebSocketServerThread::setWaterfallQueue(std::shared_ptr<SpectrumVisualDataQueue> queue) {
    waterfallQueue_ = std::move(queue);
}

void WebSocketServerThread::setAudioQueue(std::shared_ptr<DemodulatorThreadOutputQueue> queue) {
    audioQueue_ = std::move(queue);
}

void WebSocketServerThread::setIQQueue(std::shared_ptr<DemodulatorThreadInputQueue> queue) {
    iqQueue_ = std::move(queue);
}

// ---------------------------------------------------------------------------
// Queue polling — called on the uWS event-loop thread via timer.
// ---------------------------------------------------------------------------

void WebSocketServerThread::pollQueues() {
    if (!impl_ || !impl_->app) {
        return;
    }

    // --- Spectrum ---
    // spectrum_points is interleaved [x0,y0,x1,y1,...] for OpenGL.
    // Extract only the y-values (magnitudes) for the WebSocket stream.
    if (spectrumQueue_) {
        SpectrumVisualDataPtr specData;
        while (spectrumQueue_->try_pop(specData)) {
            if (!specData || specData->spectrum_points.empty()) continue;
            uint32_t numBins = static_cast<uint32_t>(specData->spectrum_points.size() / 2);
            spectrumYBuf_.resize(numBins);
            for (uint32_t i = 0; i < numBins; i++) {
                spectrumYBuf_[i] = specData->spectrum_points[i * 2 + 1];
            }
            std::string msg = serializeBinaryMessage(
                WS_STREAM_SPECTRUM,
                WS_FORMAT_FLOAT32,
                static_cast<int64_t>(specData->centerFreq),
                static_cast<uint32_t>(specData->bandwidth),
                spectrumYBuf_.data(),
                numBins);
            impl_->app->publish("spectrum", std::string_view(msg), uWS::OpCode::BINARY);
        }
    }

    // --- Waterfall ---
    // Same interleaved format — extract y-values only.
    if (waterfallQueue_) {
        SpectrumVisualDataPtr wfData;
        while (waterfallQueue_->try_pop(wfData)) {
            if (!wfData || wfData->spectrum_points.empty()) continue;
            uint32_t numBins = static_cast<uint32_t>(wfData->spectrum_points.size() / 2);
            waterfallYBuf_.resize(numBins);
            for (uint32_t i = 0; i < numBins; i++) {
                waterfallYBuf_[i] = wfData->spectrum_points[i * 2 + 1];
            }
            std::string msg = serializeBinaryMessage(
                WS_STREAM_WATERFALL,
                WS_FORMAT_FLOAT32,
                static_cast<int64_t>(wfData->centerFreq),
                static_cast<uint32_t>(wfData->bandwidth),
                waterfallYBuf_.data(),
                numBins);
            impl_->app->publish("waterfall", std::string_view(msg), uWS::OpCode::BINARY);
        }
    }

    // --- Audio ---
    if (audioQueue_) {
        AudioThreadInputPtr audioData;
        while (audioQueue_->try_pop(audioData)) {
            if (!audioData) continue;
            std::string msg = serializeBinaryMessage(
                WS_STREAM_AUDIO,
                WS_FORMAT_FLOAT32,
                static_cast<int64_t>(audioData->frequency),
                static_cast<uint32_t>(audioData->sampleRate),
                audioData->data.data(),
                static_cast<uint32_t>(audioData->data.size()));
            impl_->app->publish("audio", std::string_view(msg), uWS::OpCode::BINARY);
        }
    }

    // --- IQ ---
    if (iqQueue_) {
        DemodulatorThreadIQDataPtr iqData;
        while (iqQueue_->try_pop(iqData)) {
            if (!iqData) continue;
            // liquid_float_complex is { float real, imag } — two floats per sample.
            uint32_t numSamples = static_cast<uint32_t>(iqData->data.size());
            size_t   dataBytes  = numSamples * sizeof(liquid_float_complex);
            std::string msg = serializeBinaryMessage(
                WS_STREAM_IQ,
                WS_FORMAT_COMPLEX_FLOAT32,
                static_cast<int64_t>(iqData->frequency),
                static_cast<uint32_t>(iqData->sampleRate),
                iqData->data.data(),
                dataBytes,
                numSamples);
            impl_->app->publish("iq", std::string_view(msg), uWS::OpCode::BINARY);
        }
    }
}

// ---------------------------------------------------------------------------
// Client message handling — runs on the uWS event-loop thread.
// ---------------------------------------------------------------------------

void WebSocketServerThread::handleClientMessage(const std::string& message, void* wsRaw) {
    auto* ws = static_cast<uWS::WebSocket<false, true, WebSocketClientData>*>(wsRaw);
    auto* clientData = ws->getUserData();
    if (!clientData) return;

    std::string type   = jsonStringValue(message, "type");
    std::string stream = jsonStringValue(message, "stream");

    WSStreamType streamType;
    if (!streamNameToType(stream, streamType)) {
        std::string err = buildJsonMessage("error", "\"message\":\"unknown stream: " + stream + "\"");
        ws->send(std::string_view(err), uWS::OpCode::TEXT);
        return;
    }

    if (type == "subscribe") {
        clientData->subscriptions.insert(streamType);
        ws->subscribe(stream);

        // Send a metadata acknowledgement.
        std::string resp = buildSubscribeResponse(
            stream,
            /*centerFreq=*/0,
            /*sampleRate=*/0,
            /*fftSize=*/0,
            streamType == WS_STREAM_IQ ? "complex_float32" : "float32");
        ws->send(std::string_view(resp), uWS::OpCode::TEXT);

    } else if (type == "unsubscribe") {
        clientData->subscriptions.erase(streamType);
        ws->unsubscribe(stream);

        std::string resp = buildJsonMessage("unsubscribe_ok", "\"stream\":\"" + stream + "\"");
        ws->send(std::string_view(resp), uWS::OpCode::TEXT);

    } else {
        std::string err = buildJsonMessage("error", "\"message\":\"unknown type: " + type + "\"");
        ws->send(std::string_view(err), uWS::OpCode::TEXT);
    }
}

// ---------------------------------------------------------------------------
// Main event loop — called from IOThread's threadMain().
// ---------------------------------------------------------------------------

void WebSocketServerThread::run() {
    impl_ = std::make_unique<Impl>();

    uWS::App app;
    impl_->app = &app;

    // ---- WebSocket route ----
    app.ws<WebSocketClientData>("/*", {
        .compression = uWS::DISABLED,
        .maxPayloadLength = 16 * 1024,
        .idleTimeout = 120,
        .maxBackpressure = 64 * 1024,

        .open = [this](auto* ws) {
            impl_->clients.insert(ws);
            std::cout << "WebSocketServer: client connected ("
                      << impl_->clients.size() << " total)" << std::endl;
        },

        .message = [this](auto* ws, std::string_view msg, uWS::OpCode opCode) {
            if (opCode == uWS::OpCode::TEXT) {
                handleClientMessage(std::string(msg), ws);
            }
        },

        .close = [this](auto* ws, int /*code*/, std::string_view /*msg*/) {
            impl_->clients.erase(ws);
            std::cout << "WebSocketServer: client disconnected ("
                      << impl_->clients.size() << " remaining)" << std::endl;
        }
    });

    // ---- Listen ----
    app.listen(port_, [this](us_listen_socket_t* token) {
        if (token) {
            impl_->listenSocket = token;
            serverRunning_.store(true);
            std::cout << "WebSocketServer: listening on port " << port_ << std::endl;
        } else {
            std::cerr << "WebSocketServer: failed to listen on port " << port_ << std::endl;
        }
    });

    if (!serverRunning_.load()) {
        impl_->app = nullptr;
        return;
    }

    // ---- Poll timer (≈30 Hz) ----
    struct us_loop_t* loop = (struct us_loop_t*)uWS::Loop::get();
    impl_->loop = loop;
    impl_->pollTimer = us_create_timer(loop, 0, sizeof(WebSocketServerThread*));
    if (impl_->pollTimer) {
        // Store `this` in the timer's user-data extension.
        WebSocketServerThread* self = this;
        std::memcpy(us_timer_ext(impl_->pollTimer), &self, sizeof(self));

        us_timer_set(impl_->pollTimer, [](struct us_timer_t* t) {
            WebSocketServerThread* self;
            std::memcpy(&self, us_timer_ext(t), sizeof(self));
            if (!self->stopping) {
                self->pollQueues();
            }
        }, 33, 33);
    }

    // ---- Block here until the loop is shut down by terminate(). ----
    app.run();

    // ---- Cleanup ----
    serverRunning_.store(false);
    impl_->app = nullptr;
    impl_->listenSocket = nullptr;
    impl_->pollTimer = nullptr;
    impl_->loop = nullptr;
    impl_->clients.clear();
}

// ---------------------------------------------------------------------------
// Termination — safe to call from any thread.
// ---------------------------------------------------------------------------

void WebSocketServerThread::terminate() {
    IOThread::terminate(); // sets stopping = true

    if (!serverRunning_.load() || !impl_ || !impl_->loop) {
        return;
    }

    // All uWS mutations must happen on its own event-loop thread.
    // Use the stored loop pointer (not uWS::Loop::get() which is thread-local).
    uWS::Loop *loop = (uWS::Loop*)impl_->loop;
    loop->defer([this]() {
        // Stop the poll timer.
        if (impl_->pollTimer) {
            us_timer_set(impl_->pollTimer, nullptr, 0, 0);
            us_timer_close(impl_->pollTimer);
            impl_->pollTimer = nullptr;
        }

        // Close every client connection.
        for (auto* ws : impl_->clients) {
            ws->close();
        }
        impl_->clients.clear();

        // Close the listen socket — this causes app.run() to return.
        if (impl_->listenSocket) {
            us_listen_socket_close(0, impl_->listenSocket);
            impl_->listenSocket = nullptr;
        }
    });
}
