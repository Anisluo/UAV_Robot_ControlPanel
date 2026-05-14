#include "TaskStep.h"

#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>

// ════════════════════════════════════════════════════════════════════════
// TaskStep
// ════════════════════════════════════════════════════════════════════════
QString TaskStep::typeLabel(StepType t)
{
    switch (t) {
        case StepType::MOVE_JOINTS:     return QStringLiteral("机械臂关节");
        case StepType::MOVE_CARTESIAN:  return QStringLiteral("机械臂笛卡尔");
        case StepType::GRIPPER:         return QStringLiteral("夹爪");
        case StepType::AIRPORT_RAIL:    return QStringLiteral("机场导轨");
        case StepType::AIRPORT_GRIPPER: return QStringLiteral("机场夹爪");
        case StepType::WAIT_DETECT_UAV: return QStringLiteral("等待识别UAV");
        case StepType::WAIT_DETECT_BAT: return QStringLiteral("等待识别电池");
        case StepType::DWELL:           return QStringLiteral("延时");
        case StepType::FIX_POINT:       return QStringLiteral("定点跟踪");
    }
    return QStringLiteral("?");
}

QString TaskStep::summary() const
{
    switch (type) {
        case StepType::MOVE_JOINTS: {
            const QVariantList j = params.value("joints").toList();
            if (j.size() == 6) {
                return QStringLiteral("J=[%1 %2 %3 %4 %5 %6]° spd=%7%")
                    .arg(j[0].toDouble(), 0, 'f', 1)
                    .arg(j[1].toDouble(), 0, 'f', 1)
                    .arg(j[2].toDouble(), 0, 'f', 1)
                    .arg(j[3].toDouble(), 0, 'f', 1)
                    .arg(j[4].toDouble(), 0, 'f', 1)
                    .arg(j[5].toDouble(), 0, 'f', 1)
                    .arg(int(params.value("speed_ratio", 1.0).toDouble() * 100));
            }
            return QStringLiteral("(joints invalid)");
        }
        case StepType::MOVE_CARTESIAN:
            return QStringLiteral("(%1, %2, %3)mm  RPY=(%4, %5, %6)°  %7")
                .arg(params.value("x_mm").toDouble(), 0, 'f', 1)
                .arg(params.value("y_mm").toDouble(), 0, 'f', 1)
                .arg(params.value("z_mm").toDouble(), 0, 'f', 1)
                .arg(params.value("rx_deg").toDouble(), 0, 'f', 1)
                .arg(params.value("ry_deg").toDouble(), 0, 'f', 1)
                .arg(params.value("rz_deg").toDouble(), 0, 'f', 1)
                .arg(params.value("mode", "P").toString());
        case StepType::GRIPPER:
            return QStringLiteral("angle=%1mm  force=%2%")
                .arg(params.value("angle_mm").toDouble(), 0, 'f', 1)
                .arg(params.value("force_pct").toInt());
        case StepType::AIRPORT_RAIL: {
            const QString action = params.value("action", "lock").toString();
            const int rpm = params.value("speed_rpm", 1500).toInt();
            const QString stop_mode = params.value("stop_mode", "stall").toString();
            QString name;
            if      (action == "release")     name = QStringLiteral("平台释放(1+3)");
            else if (action == "rail2_fwd")   name = QStringLiteral("机场夹爪导轨 前进");
            else if (action == "rail2_back")  name = QStringLiteral("机场夹爪导轨 后退");
            else                              name = QStringLiteral("平台锁定(1+3)");
            if (stop_mode == "distance") {
                const double dist = params.value("distance_mm", 50.0).toDouble();
                return QStringLiteral("%1 @ %2rpm  → %3mm").arg(name).arg(rpm).arg(dist, 0, 'f', 1);
            }
            return QStringLiteral("%1 @ %2rpm  → 堵转停").arg(name).arg(rpm);
        }
        case StepType::AIRPORT_GRIPPER:
            return params.value("open").toBool()
                       ? QStringLiteral("机场夹爪 张开")
                       : QStringLiteral("机场夹爪 闭合");
        case StepType::WAIT_DETECT_UAV:
            return QStringLiteral("UAV %1  超时 %2s")
                .arg(params.value("present", true).toBool() ? "出现" : "消失")
                .arg(params.value("timeout_ms").toInt() / 1000.0, 0, 'f', 1);
        case StepType::WAIT_DETECT_BAT:
            return QStringLiteral("电池 %1  超时 %2s")
                .arg(params.value("present", true).toBool() ? "出现" : "消失")
                .arg(params.value("timeout_ms").toInt() / 1000.0, 0, 'f', 1);
        case StepType::DWELL:
            return QStringLiteral("%1 ms").arg(params.value("ms").toInt());
        case StepType::FIX_POINT:
            return QStringLiteral("(%1, %2, %3)mm  保持 %4ms")
                .arg(params.value("x_mm").toDouble(), 0, 'f', 1)
                .arg(params.value("y_mm").toDouble(), 0, 'f', 1)
                .arg(params.value("z_mm").toDouble(), 0, 'f', 1)
                .arg(params.value("duration_ms").toInt());
    }
    return {};
}

QJsonObject TaskStep::toJson() const
{
    QJsonObject o;
    o["type"]   = int(type);
    o["label"]  = label;
    // QVariantMap → QJsonObject (works for primitives + arrays of primitives).
    o["params"] = QJsonObject::fromVariantMap(params);
    return o;
}

TaskStep TaskStep::fromJson(const QJsonObject &o)
{
    TaskStep s;
    s.type   = StepType(o.value("type").toInt(int(StepType::DWELL)));
    s.label  = o.value("label").toString();
    s.params = o.value("params").toObject().toVariantMap();
    return s;
}


// ════════════════════════════════════════════════════════════════════════
// StageScript
// ════════════════════════════════════════════════════════════════════════
QJsonObject StageScript::toJson() const
{
    QJsonArray steps_arr;
    for (const auto &s : steps) steps_arr.append(s.toJson());
    QJsonObject o;
    o["stage_id"] = stage_id;
    o["steps"]    = steps_arr;
    return o;
}

StageScript StageScript::fromJson(const QJsonObject &o)
{
    StageScript s;
    s.stage_id = o.value("stage_id").toString();
    const auto arr = o.value("steps").toArray();
    for (const auto &v : arr) s.steps.append(TaskStep::fromJson(v.toObject()));
    return s;
}


// ════════════════════════════════════════════════════════════════════════
// TaskConfig — all-9 bundle, persisted as a single JSON in user home
// ════════════════════════════════════════════════════════════════════════
QJsonObject TaskConfig::toJson() const
{
    QJsonObject scripts_obj;
    for (auto it = scripts.constBegin(); it != scripts.constEnd(); ++it) {
        QJsonArray steps_arr;
        for (const auto &s : it.value()) steps_arr.append(s.toJson());
        scripts_obj[it.key()] = steps_arr;
    }
    QJsonObject root;
    root["version"] = version;
    root["scripts"] = scripts_obj;
    return root;
}

TaskConfig TaskConfig::fromJson(const QJsonObject &o)
{
    TaskConfig c;
    c.version = o.value("version").toInt(1);
    const QJsonObject scripts_obj = o.value("scripts").toObject();
    for (auto it = scripts_obj.constBegin(); it != scripts_obj.constEnd(); ++it) {
        QVector<TaskStep> steps;
        const auto arr = it.value().toArray();
        for (const auto &v : arr) steps.append(TaskStep::fromJson(v.toObject()));
        c.scripts.insert(it.key(), steps);
    }
    return c;
}

QString TaskConfig::homeFilePath()
{
    // QStandardPaths::AppConfigLocation on Windows → %APPDATA%/<org>/<app>/
    //                                      Linux  → ~/.config/<org>/<app>/
    // Falls back to home + .uav_robot_controlpanel/ if no app config available.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.UAV_Robot_ControlPanel";
    QDir().mkpath(dir);
    return dir + "/task_stages.json";
}

bool TaskConfig::saveToHomeFile() const
{
    const QString path = homeFilePath();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray bytes = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size()) return false;
    return f.commit();
}

TaskConfig TaskConfig::loadFromHomeFile()
{
    TaskConfig c;
    const QString path = homeFilePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return c;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return c;
    return fromJson(doc.object());
}
