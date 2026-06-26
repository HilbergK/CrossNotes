#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "NetworkModeSelectionActivity.h"
#include "activities/Activity.h"
#include "network/CrossPointWebServer.h"

enum class WebServerActivityState { MODE_SELECTION, WIFI_SELECTION, AP_STARTING, SERVER_RUNNING, SHUTTING_DOWN };

class CrossPointWebServerActivity final : public Activity {
  WebServerActivityState state = WebServerActivityState::MODE_SELECTION;
  std::string returnBookPath;
  std::string landingPath;  // NEW: path appended to QR code URL (e.g. "highlights")
  bool hasInitialNetworkMode = false;
  NetworkMode initialNetworkMode = NetworkMode::JOIN_NETWORK;

  NetworkMode networkMode = NetworkMode::JOIN_NETWORK;
  bool isApMode = false;

  std::unique_ptr<CrossPointWebServer> webServer;

  std::string connectedIP;
  std::string connectedSSID;

  unsigned long lastHandleClientTime = 0;

  int consecutiveDisconnects = 0;
  unsigned long firstDisconnectAt = 0;
  static constexpr unsigned long WIFI_ABANDON_MS = 5UL * 60UL * 1000UL;

  int lastWifiBars = 0;

  void renderServerRunning() const;
  void renderWifiIndicator(int subHeaderTop) const;

  void onNetworkModeSelected(NetworkMode mode);
  void onWifiSelectionComplete(bool connected);
  void startAccessPoint();
  void startWebServer();
  void stopWebServer();
  void exitToOrigin();

 public:
  // Original constructor — mode selection screen, no landing path
  explicit CrossPointWebServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       std::string returnBookPath = {})
      : Activity("CrossPointWebServer", renderer, mappedInput),
        returnBookPath(std::move(returnBookPath)),
        landingPath("") {}

  // Constructor with pre-selected network mode — used by goToHotspotFileTransfer etc.
  CrossPointWebServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, NetworkMode initialNetworkMode,
                              std::string returnBookPath = {}, std::string landingPath = "")
      : Activity("CrossPointWebServer", renderer, mappedInput),
        returnBookPath(std::move(returnBookPath)),
        landingPath(std::move(landingPath)),
        hasInitialNetworkMode(true),
        initialNetworkMode(initialNetworkMode) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};
