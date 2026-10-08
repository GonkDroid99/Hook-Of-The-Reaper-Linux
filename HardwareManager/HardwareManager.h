#ifndef HARDWAREMANAGER_H
#define HARDWAREMANAGER_H

#include <QObject>
#include <QDateTime>
#include <QList>
#include <QTimer>
#include <QJsonObject>

// The lifecycle of a physical gun as seen by HardwareManager. Detection and
// transport state are kept separate conceptually: a gun can be physically
// present while its serial port is opening, recovering, or disconnected.
enum class HardwareConnectionState
{
    Absent,       // The device is known but was not found in the latest scan.
    Detected,     // The device is present and has enough configuration to use.
    Opening,      // The game layer is opening the gun's output transport.
    Connected,    // The game layer successfully opened the output transport.
    Disconnected, // The device is present in the registry but its transport failed.
    Recovering,   // A transport retry is scheduled or in progress.
    NeedsSetup,   // The device needs a type, profile, or player assignment.
    Disabled      // The device is intentionally disabled.
};

// Explains why a player number was selected. The mode is persisted so the
// next scan knows whether it may automatically choose a different player.
enum class HardwareAssignmentMode
{
    Persistent,   // Match the saved physical identity across reconnects.
    PhysicalPort, // Match the USB port when a serial number is unavailable.
    FirstAvailable, // Use the first unused player slot.
    Manual        // The user explicitly chose the player in the UI.
};

// Stable information about a physical device. This is what is written to
// devices.json and used to recognize the same gun after a replug or reboot.
struct DeviceIdentity
{
    QString type;           // Transport: serial, hid, or tcp.
    QString value;          // Strong identity value, when one is available.
    QString vid;            // USB vendor ID, normalized as hexadecimal text.
    QString pid;            // USB product ID, normalized as hexadecimal text.
    QString serial;         // Hardware serial number.
    QString stableByIdPath; // Linux /dev/serial/by-id path, if available.
    QString stableByPath;   // Linux /dev/serial/by-path path, if available.
    QString usbParentPath;  // Physical USB parent, e.g. 1-3.

    // True when this identity contains enough information to be useful.
    bool isMeaningful() const;
    // Logical key used as the registry key and signal identifier.
    QString key() const;
    // Physical key used to merge serial and HID interfaces of one gun.
    QString physicalKey() const;
    // Convert the identity to the on-disk JSON representation.
    QJsonObject toJson() const;
    // Recreate an identity from devices.json.
    static DeviceIdentity fromJson(const QJsonObject &json);
};

// A snapshot of one currently discovered endpoint. A physical gun may have
// more than one endpoint—for example an RS3 Reaper can expose serial and HID.
struct DeviceFingerprint
{
    QString transport;       // serial, hid, or tcp.
    QString vid;             // USB vendor ID.
    QString pid;             // USB product ID.
    QString serialNumber;    // Serial reported by the endpoint.
    QString manufacturer;    // USB manufacturer string.
    QString product;         // USB product/description string.
    QString path;            // Current device path, such as /dev/ttyUSB0.
    QString stableByIdPath;  // Persistent serial symlink, if available.
    QString stableByPath;    // Persistent physical-port symlink, if available.
    QString usbParentPath;   // Physical USB parent shared by related endpoints.
    qint32 interfaceNumber = -1; // HID interface number, when reported.

    // Build the persistent identity represented by this endpoint.
    DeviceIdentity identity() const;
    // Build a physical identity used to merge related endpoints.
    QString physicalKey() const;
    // Convert the discovery snapshot to JSON.
    QJsonObject toJson() const;
    // Restore a discovery snapshot from JSON.
    static DeviceFingerprint fromJson(const QJsonObject &json);
};

// The complete manager record for one physical device. This is the bridge
// between discovery, player assignment, profile selection, and the legacy
// LightGun/ComDeviceList code.
struct HardwareDevice
{
    DeviceIdentity identity;          // Persistent identity of the physical gun.
    DeviceFingerprint fingerprint;    // Preferred/current endpoint snapshot.
    QList<DeviceFingerprint> endpoints; // All known serial/HID endpoints.
    QString deviceType;               // Normalized type, e.g. rs3reaper.
    QString profile;                  // Legacy .hor profile name.
    quint8 player = 0;                // Player slot 1-8; zero means unassigned.
    HardwareAssignmentMode assignmentMode = HardwareAssignmentMode::Persistent;
    HardwareConnectionState state = HardwareConnectionState::Absent;
    bool manual = false;              // True after user-selected configuration.
    bool present = false;             // Found in the most recent hardware scan.
    QDateTime lastSeen;               // UTC time of the most recent observation.
    qint64 reservationUntilMs = 0;    // Keeps a disconnected player's slot warm.

    // Convert the complete record to devices.json.
    QJsonObject toJson() const;
    // Restore a record and migrate older registry formats where possible.
    static HardwareDevice fromJson(const QJsonObject &json);
};

// Discovers physical light guns, remembers them in devices.json, assigns
// players, and reports connection changes to HookerEngine. It deliberately
// owns hardware identity while ComDeviceList continues to own legacy profiles.
class HardwareManager : public QObject
{
    Q_OBJECT

public:
    explicit HardwareManager(QObject *parent = nullptr);

    // Start scanning immediately, then continue on the configured timer.
    void start();
    // Stop scanning and save the current registry.
    void stop();
    // Perform one immediate serial/HID discovery pass.
    void rescan();

    // Read-only snapshots used by the UI and HookerEngine.
    QList<HardwareDevice> devices() const { return m_devices; }
    QString registryPath() const { return m_registryPath; }
    bool gameActive() const { return m_gameActive; }
    // Prevent profile creation while an emulator is running.
    void setGameActive(bool active);
    // Assign a physical device to a player slot.
    bool assignPlayer(const QString &identity, quint8 player, HardwareAssignmentMode mode = HardwareAssignmentMode::Persistent);
    // Remove a device from its player slot without deleting its identity.
    bool unassignPlayer(const QString &identity, HardwareAssignmentMode mode = HardwareAssignmentMode::Manual);
    // Set or override the detected type/profile for a device.
    bool configureDevice(const QString &identity, const QString &deviceType, const QString &profile,
                         bool manual = true, HardwareAssignmentMode mode = HardwareAssignmentMode::Manual);
    // Mirror legacy serial transport events into the manager state machine.
    void markPlayerTransportOpening(quint8 player);
    void markPlayerTransportRecovering(quint8 player);
    void markPlayerTransportConnected(quint8 player);
    void markPlayerTransportDisconnected(quint8 player);

    // Score how confidently a live endpoint matches a saved identity.
    static int matchConfidence(const DeviceIdentity &saved, const DeviceFingerprint &current);
    // Human-readable state name for logs and status displays.
    static QString connectionStateName(HardwareConnectionState state);

signals:
    void deviceConnected(const HardwareDevice &device);
    void deviceDisconnected(const HardwareDevice &device);
    void deviceRecovered(const HardwareDevice &device);
    void deviceReplaced(const QString &oldIdentity, const HardwareDevice &device);
    void playerAssignmentChanged(quint8 player, const QString &identity);
    void devicesChanged();

private slots:
    // Timer callback used for periodic safety scans.
    void safetyScan();

private:
    // Registry persistence.
    void loadRegistry();
    bool saveRegistry() const;
    // Hardware enumeration.
    QList<DeviceFingerprint> discoverSerialDevices() const;
    QList<DeviceFingerprint> discoverHidDevices() const;
    QList<DeviceFingerprint> discoverDevices() const;
    // Merge the latest enumeration into the persistent device list.
    void reconcile(const QList<DeviceFingerprint> &found);
    // Convert raw USB strings into HOTR's supported device/profile names.
    QString detectDeviceType(const DeviceFingerprint &fingerprint) const;
    QString profileForType(const QString &deviceType) const;
    // Registry lookup and matching.
    HardwareDevice *findByIdentity(const QString &identity);
    int findMatch(const DeviceFingerprint &fingerprint) const;
    // Player allocation and state transitions.
    quint8 firstFreePlayer() const;
    void transition(HardwareDevice &device, HardwareConnectionState next, const QString &reason);

    QList<HardwareDevice> m_devices; // Persistent records known to HOTR.
    QString m_registryPath;          // Full path to devices.json.
    QTimer m_scanTimer;              // Periodic discovery timer.
    bool m_started = false;           // Prevent duplicate timer/start work.
    bool m_gameActive = false;        // Blocks new legacy profile creation.
};

#endif
