#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUdpSocket>

/* 思翼 A8 mini 云台相机以太网 SDK 控制类（UDP）。
 *
 * 相机地址是 192.168.144.25:37260，只有机载电脑可达；地面站实际连接的是
 * 机载电脑上的 UDP 中转（systemd: siyi-gimbal-relay），默认 192.168.2.113:37260。
 *
 * 协议（SDK V0.1.1）：
 *   帧 = 55 66 | control | payload_len(2,LE) | seq(2,LE) | cmd(1) | payload | crc16(2,LE)
 *   CRC16 = CCITT poly 0x1021，初值 0（等价 Python binascii.crc_hqx(data, 0)）
 *
 * 安全约束（实测教训）：
 *   * 心跳包 CMD 0x00 仅 TCP 支持，对 UDP 发送会让相机命令服务挂死，
 *     因此本类【不实现】心跳，永远不发 0x00。
 *   * A8 mini 的 UDP SDK 是单客户端模型，源端口必须保持稳定 ——
 *     QUdpSocket 在对象生命周期内只 bind 一次且不重连。
 */
class GimbalLink final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY changed)
    Q_PROPERTY(double yaw READ yaw NOTIFY attitudeChanged)
    Q_PROPERTY(double pitch READ pitch NOTIFY attitudeChanged)
    Q_PROPERTY(double roll READ roll NOTIFY attitudeChanged)
    Q_PROPERTY(bool attitudeFresh READ attitudeFresh NOTIFY attitudeChanged)
    Q_PROPERTY(int mode READ mode NOTIFY changed)
    Q_PROPERTY(QString modeName READ modeName NOTIFY changed)
    Q_PROPERTY(int recordStatus READ recordStatus NOTIFY changed)
    Q_PROPERTY(QString recordStatusName READ recordStatusName NOTIFY changed)
    Q_PROPERTY(double zoom READ zoom NOTIFY changed)
    Q_PROPERTY(double zoomMax READ zoomMax NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double yawLimit READ yawLimit CONSTANT)
    Q_PROPERTY(double pitchMin READ pitchMin CONSTANT)
    Q_PROPERTY(double pitchMax READ pitchMax CONSTANT)

public:
    explicit GimbalLink(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QString status() const { return m_status; }
    QString endpoint() const { return m_endpoint; }
    double yaw() const { return m_yaw; }
    double pitch() const { return m_pitch; }
    double roll() const { return m_roll; }
    bool attitudeFresh() const { return m_attitudeFresh; }
    int mode() const { return m_mode; }
    QString modeName() const;
    int recordStatus() const { return m_recordStatus; }
    QString recordStatusName() const;
    double zoom() const { return m_zoom; }
    double zoomMax() const { return m_zoomMax; }
    bool busy() const { return !m_pending.isEmpty(); }

    // A8 mini 的角度与变倍限位（来自 SDK 文档）
    double yawLimit() const { return kYawLimit; }
    double pitchMin() const { return kPitchMin; }
    double pitchMax() const { return kPitchMax; }

    // 连接管理
    Q_INVOKABLE void connectToGimbal(const QString &host, int port);
    Q_INVOKABLE void disconnectFromGimbal();

    // 姿态控制
    Q_INVOKABLE void setAttitude(double yaw, double pitch);   // 绝对角度（滑条/拖动）
    Q_INVOKABLE void jog(double deltaYaw, double deltaPitch); // 相对步进（方向键）
    Q_INVOKABLE void centerGimbal();                          // 一键回中 0x08
    Q_INVOKABLE void lookDown();                              // 一键朝下 0x0C/09

    // 相机功能
    Q_INVOKABLE void setMode(int mode);       // 0 锁定 / 1 跟随 / 2 FPV
    Q_INVOKABLE void takePhoto();             // 0x0C/00
    Q_INVOKABLE void toggleRecording();       // 0x0C/02
    Q_INVOKABLE void zoomStep(int direction); // 0x05，+1 拉近 / -1 拉远
    Q_INVOKABLE void setZoomAbsolute(double multiple); // 0x0F

    // 状态查询与恢复
    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void refreshAttitude();
    Q_INVOKABLE void softReboot();            // 0x80，命令服务挂死时的恢复手段

signals:
    void changed();
    void attitudeChanged();
    void notified(const QString &text);

private slots:
    void onReadyRead();
    void onPoll();
    void onMaintenance();

private:
    struct Pending {
        qint64 sentAtMs{0};
        int attempts{0};
    };

    static constexpr double kYawLimit = 135.0;
    static constexpr double kPitchMin = -90.0;
    static constexpr double kPitchMax = 25.0;
    static constexpr int kAckTimeoutMs = 900;
    static constexpr int kMaxAttempts = 3;
    static constexpr qint64 kAttitudeStaleMs = 3000;

    static quint16 crc16Ccitt(const QByteArray &data);
    static QByteArray buildFrame(quint8 command, const QByteArray &payload, quint16 sequence);
    static bool parseFrame(const QByteArray &datagram, quint8 *command, QByteArray *payload);

    void sendCommand(quint8 command, const QByteArray &payload, bool expectAck = true);
    void retryPending();
    void flushAttitudeTarget();
    void setStatus(const QString &text);
    void noteAttitude(double yaw, double pitch, double roll);
    void decodePayload(quint8 command, const QByteArray &payload);
    void resetState();

    QUdpSocket m_socket;
    QTimer m_pollTimer;
    QTimer m_maintenanceTimer;
    QTimer m_attitudeFlushTimer;

    QHash<int, Pending> m_pending;
    quint16 m_sequence{0};
    bool m_bound{false};
    bool m_targetDirty{false};
    double m_targetYaw{0.0};
    double m_targetPitch{0.0};

    bool m_connected{false};
    bool m_attitudeFresh{false};
    QString m_status{QStringLiteral("未连接云台")};
    QString m_endpoint;

    double m_yaw{0.0};
    double m_pitch{0.0};
    double m_roll{0.0};
    qint64 m_attitudeAtMs{0};

    int m_mode{0};
    int m_recordStatus{0};
    double m_zoom{1.0};
    double m_zoomMax{3.5};
};
