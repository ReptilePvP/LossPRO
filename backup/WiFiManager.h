#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <vector>
#include <Preferences.h>
#include <functional>

enum class WiFiState {
    WIFI_DISABLED,
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_SCANNING
};

struct NetworkInfo {
    String ssid;
    String password;
    int8_t rssi;
    uint8_t encryptionType;
    bool saved;
    bool connected;
    int priority; // Higher value = higher priority
};

class WiFiManager {
public:
    using StatusCallback = std::function<void(WiFiState, const String&)>;
    using ScanCallback = std::function<void(const std::vector<NetworkInfo>&)>;

    WiFiManager();
    ~WiFiManager();

    void begin();
    void update();

    // Configuration
    void setStatusCallback(StatusCallback cb);
    void setScanCallback(ScanCallback cb);
    void setEnabled(bool enabled);
    bool isEnabled() const;
    bool isInitialized() const;

    // Connection
    bool connect(const String& ssid, const String& password, bool save = true, int priority = 0);
    bool connectToBestNetwork();
    void disconnect(bool manual = true);
    bool isConnected() const;
    String getCurrentSSID() const;
    int getRSSI() const;
    String getIPAddress() const;

    // Scanning
    bool startScan();
    bool isScanning() const;
    std::vector<NetworkInfo> getScanResults() const;

    // Network Management
    bool addNetwork(const String& ssid, const String& password, int priority = 0);
    bool removeNetwork(const String& ssid);
    bool setNetworkPriority(const String& ssid, int priority);
    std::vector<NetworkInfo> getSavedNetworks() const;
    void saveNetworks();
    void loadSavedNetworks();

    // State
    WiFiState getState() const;
    String getStateString() const;

private:
    WiFiState _state;
    bool _enabled;
    bool _manualDisconnect;
    bool _initialized;
    bool _scanInProgress;
    unsigned long _lastConnectionAttempt;
    unsigned long _scanStartTime;
    int _connectionAttempts;
    String _connectingSSID;
    String _connectingPassword;
    std::vector<NetworkInfo> _savedNetworks;
    std::vector<NetworkInfo> _scanResults;
    Preferences _preferences;
    StatusCallback _statusCallback;
    ScanCallback _scanCallback;

    static const unsigned long CONNECTION_TIMEOUT = 10000; // 10s
    static const unsigned long SCAN_TIMEOUT = 8000;       // 8s
    static const unsigned long RECONNECT_INTERVAL = 30000; // 30s between retries
    static const int MAX_CONNECTION_ATTEMPTS = 3;
    static const int MAX_SAVED_NETWORKS = 5;

    void updateState();
    void notifyStatus(const String& message);
    void sortNetworksByPriority(std::vector<NetworkInfo>& networks);
    int findNetwork(const String& ssid, const std::vector<NetworkInfo>& networks) const;
};
#endif