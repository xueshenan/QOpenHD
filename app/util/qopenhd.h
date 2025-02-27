#ifndef QOPENHD_H
#define QOPENHD_H

#include <QFont>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QTranslator>

#ifdef ENABLE_SPEECH
#include <QTextToSpeech>
#endif

/**
 * Dirty, but for some reason stephen made translation(s) and a bit more work this way.
 * This singleton is for handling Appplication (QOpenHD, NOT OpenHD) - specific things that
 * need the QT Engine or some other quirks.
 */
class QOpenHD : public QObject
{
    Q_OBJECT
public:
    explicit QOpenHD(QObject *parent = nullptr);
    static QOpenHD& instance();
    Q_INVOKABLE void switchToLanguage(const QString &language);
    void setEngine(QQmlApplicationEngine *engine);
    Q_PROPERTY(QString fontFamily MEMBER m_fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    void setFontFamily(QString fontFamily);
    void textToSpeech_sayMessage(QString message);

    // This only terminates the App, on most OpenHD images the system service will then restart
    // QOpenHD. Can be usefully for debugging, if something's wrong with the app and you need to restart it
    Q_INVOKABLE void quit_qopenhd();
    // This not only quits qopenhd, but also disables the autostart service
    // (until next reboot)
    Q_INVOKABLE void disable_service_and_quit();
    // Develoment only
    Q_INVOKABLE void restart_local_oenhd_service();
    Q_INVOKABLE void run_dhclient_eth0();
    // Save/Backup QOpenHD local settings to a file, such that they can be reused after a reflash
    // The backup file is stored in /boot/openhd/QOpenHD.conf
    // returns true on success, false otherwise
    Q_INVOKABLE bool backup_settings_locally();
    // Overwrite the local QOpenHD settings with a previous packed up settings file
    // returns true on success, false otherwise
    // To apply, QOpenHD needs to be restarted
    Q_INVOKABLE bool overwrite_settings_from_backup_file();
    // Deletes the local QOpenHD.conf file, such that QOpenHD re-creates all settings after a restart
    Q_INVOKABLE bool reset_settings();
    // only works on linux, dirty helper to get local IP address
    Q_INVOKABLE QString show_local_ip();
    // returns true if the platform qopenhd is running on is linux (embedded or x86)
    // some settings an stuff depend on that, called from .qml
    // NOTE: android is not linux in this definition !
    Q_INVOKABLE bool is_linux();
    Q_INVOKABLE bool is_android();
    Q_INVOKABLE void android_open_tethering_settings();
    // runs systemctl start/stop/enable/disable openhd
    // opens error message if the openhd service file does not exist (e.g. false on all non linux platforms)
    Q_INVOKABLE void sysctl_openhd(int task);
signals:
    void fontFamilyChanged(QString fontFamily);
private:
     QQmlApplicationEngine *m_engine = nullptr;
     QTranslator m_translator;
     QString m_fontFamily;
     QFont m_font;
#if defined(ENABLE_SPEECH)
    QTextToSpeech *m_speech;
#endif
public:
    // We always want the screen to be kept "On" while QOpenHD is running -
    // but how to do that depends highly on the platform
    Q_INVOKABLE void keep_screen_on(bool on);
};

#endif // QOPENHD_H
