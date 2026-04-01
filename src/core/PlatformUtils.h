#ifndef PLATFORMUTILS_H
#define PLATFORMUTILS_H

#include <QString>
#include <QStringList>
#include <QStandardPaths>

namespace PlatformUtils {

struct ProcessLaunchSpec {
    QString program;
    QStringList arguments;
};

inline ProcessLaunchSpec pythonCommand(const QStringList &extraArguments = {})
{
    const QStringList candidates = {
        QStringLiteral("python3"),
        QStringLiteral("python"),
        QStringLiteral("py")
    };

    for (const QString &candidate : candidates) {
        const QString resolved = QStandardPaths::findExecutable(candidate);
        if (resolved.isEmpty()) {
            continue;
        }

        ProcessLaunchSpec spec;
        spec.program = resolved;
        if (candidate == QStringLiteral("py")) {
            spec.arguments << QStringLiteral("-3");
        }
        spec.arguments << extraArguments;
        return spec;
    }

    ProcessLaunchSpec fallback;
    fallback.program = QStringLiteral("python");
    fallback.arguments = extraArguments;
    return fallback;
}

inline QStringList singlePingArguments(const QString &ip)
{
#ifdef Q_OS_WIN
    return {
        QStringLiteral("-n"),
        QStringLiteral("1"),
        QStringLiteral("-w"),
        QStringLiteral("1000"),
        ip
    };
#else
    return {
        QStringLiteral("-c"),
        QStringLiteral("1"),
        QStringLiteral("-W"),
        QStringLiteral("1"),
        ip
    };
#endif
}

} // namespace PlatformUtils

#endif // PLATFORMUTILS_H
