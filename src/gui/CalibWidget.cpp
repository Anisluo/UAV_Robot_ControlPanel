#include "CalibWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>

CalibWidget::CalibWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox(QStringLiteral("手眼标定 (RK3588 ⇄ Piper)"), parent)
    , rpc_(rpc)
{
    setStyleSheet(
        "QGroupBox {"
        " border: 1px solid #c89aff; border-radius: 6px;"
        " margin-top: 12px; color: #d8e0f0; background: rgba(28,34,50,200);"
        "}"
        "QGroupBox::title {"
        " subcontrol-origin: margin; left: 10px; padding: 0 8px;"
        " color: #c89aff; font-weight: bold;"
        "}");
    buildUi();
}

void CalibWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 16, 8, 8);
    root->setSpacing(6);

    // ── Mode + ArUco ID ──────────────────────────────────────────────
    auto *configRow = new QHBoxLayout;
    configRow->setSpacing(6);

    auto *modeLbl = new QLabel(QStringLiteral("模式:"), this);
    modeLbl->setStyleSheet("color:#aab6cc;");
    mode_combo_ = new QComboBox(this);
    mode_combo_->addItem(QStringLiteral("eye-in-hand (相机装腕)"),  QStringLiteral("eye_in_hand"));
    mode_combo_->addItem(QStringLiteral("eye-to-base (相机装底盘)"), QStringLiteral("eye_to_base"));
    mode_combo_->setMinimumWidth(160);

    auto *idLbl = new QLabel(QStringLiteral("ArUco ID:"), this);
    idLbl->setStyleSheet("color:#aab6cc;");
    aruco_id_spin_ = new QSpinBox(this);
    aruco_id_spin_->setRange(0, 999);
    aruco_id_spin_->setValue(0);
    aruco_id_spin_->setFixedWidth(64);

    configRow->addWidget(modeLbl);
    configRow->addWidget(mode_combo_);
    configRow->addSpacing(8);
    configRow->addWidget(idLbl);
    configRow->addWidget(aruco_id_spin_);
    configRow->addStretch();
    root->addLayout(configRow);

    // ── Action row 1: capture / solve / apply ────────────────────────
    auto *btnRow1 = new QHBoxLayout;
    btnRow1->setSpacing(6);
    btn_capture_ = new QPushButton(QStringLiteral("📸 捕获样本"), this);
    btn_solve_   = new QPushButton(QStringLiteral("🧮 求解"), this);
    btn_apply_   = new QPushButton(QStringLiteral("⬆ 应用到 RK3588"), this);
    btn_capture_->setFixedHeight(28);
    btn_solve_->setFixedHeight(28);
    btn_apply_->setFixedHeight(28);
    btn_capture_->setStyleSheet(
        "QPushButton{ background:#2d5fb2; color:white; border-radius:4px; font-weight:bold; padding:2px 8px;}"
        "QPushButton:hover{ background:#3a73c9;}"
        "QPushButton:disabled{ background:#446; color:#aab;}");
    btn_solve_->setStyleSheet(
        "QPushButton{ background:#3a8; color:white; border-radius:4px; font-weight:bold; padding:2px 8px;}"
        "QPushButton:hover{ background:#4ab;}"
        "QPushButton:disabled{ background:#446; color:#aab;}");
    btn_apply_->setStyleSheet(
        "QPushButton{ background:#a72; color:white; border-radius:4px; font-weight:bold; padding:2px 8px;}"
        "QPushButton:hover{ background:#b83;}"
        "QPushButton:disabled{ background:#446; color:#aab;}");
    btn_solve_->setEnabled(false);
    btn_apply_->setEnabled(false);
    btnRow1->addWidget(btn_capture_);
    btnRow1->addWidget(btn_solve_);
    btnRow1->addWidget(btn_apply_);
    root->addLayout(btnRow1);

    // ── Action row 2: reset / export ─────────────────────────────────
    auto *btnRow2 = new QHBoxLayout;
    btnRow2->setSpacing(6);
    btn_reset_  = new QPushButton(QStringLiteral("🗑 清空样本"), this);
    btn_export_ = new QPushButton(QStringLiteral("💾 导出 JSON"), this);
    btn_reset_->setFixedHeight(24);
    btn_export_->setFixedHeight(24);
    btn_reset_->setStyleSheet("QPushButton{ background:#553; color:#ddd; border-radius:3px; padding:2px 8px;}");
    btn_export_->setStyleSheet("QPushButton{ background:#445; color:#ddd; border-radius:3px; padding:2px 8px;}");
    btnRow2->addWidget(btn_reset_);
    btnRow2->addWidget(btn_export_);
    btnRow2->addStretch();
    root->addLayout(btnRow2);

    // ── Status: sample count + result summary ────────────────────────
    count_label_ = new QLabel(QStringLiteral("样本: 0  (建议 ≥ 4 个不同姿态)"), this);
    count_label_->setStyleSheet("color:#9aa0b8; font-family: Consolas;");
    root->addWidget(count_label_);

    result_label_ = new QLabel(QStringLiteral("未求解"), this);
    result_label_->setStyleSheet("color:#7af0a0; font-family: Consolas; padding:2px 4px;");
    result_label_->setWordWrap(true);
    root->addWidget(result_label_);

    // ── Log strip ────────────────────────────────────────────────────
    log_view_ = new QPlainTextEdit(this);
    log_view_->setReadOnly(true);
    log_view_->setMaximumHeight(70);
    log_view_->setStyleSheet(
        "QPlainTextEdit{ background:#0e131c; color:#cad3e4; border:1px solid #2a3247;"
        " font-family: Consolas; font-size: 10px; padding:2px;}");
    root->addWidget(log_view_);

    connect(btn_capture_, &QPushButton::clicked, this, &CalibWidget::onCapture);
    connect(btn_solve_,   &QPushButton::clicked, this, &CalibWidget::onSolve);
    connect(btn_apply_,   &QPushButton::clicked, this, &CalibWidget::onApply);
    connect(btn_reset_,   &QPushButton::clicked, this, &CalibWidget::onReset);
    connect(btn_export_,  &QPushButton::clicked, this, &CalibWidget::onExportJson);
}

void CalibWidget::appendLog(const QString &line)
{
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    log_view_->appendPlainText(QString("[%1] %2").arg(stamp, line));
}

void CalibWidget::updateSampleSummary()
{
    count_label_->setText(QStringLiteral("样本: %1  (建议 ≥ 4 个不同姿态)")
                              .arg(samples_.size()));
    btn_solve_->setEnabled(samples_.size() >= 3);
}

// ── Capture: read arm pose + marker pose, push as one sample ─────────────
void CalibWidget::onCapture()
{
    if (!rpc_ || !rpc_->isConnected()) {
        appendLog(QStringLiteral("⚠ RPC 未连接"));
        return;
    }

    // Fire arm.get_pose and npu.get_detections in parallel. We'll join via
    // a small shared state once both replies have landed.
    struct Joiner {
        bool armDone = false, npuDone = false;
        QVector<double> armPose;
        QVector<double> markerPose;
        int             arucoId = -1;
    };
    auto state = std::make_shared<Joiner>();
    const int wantId = aruco_id_spin_->value();

    rpc_->call(Protocol::Methods::ARM_GET_POSE, QJsonObject{},
        [this, state](QJsonObject r) {
            const QJsonArray arr = r.value("pose").toArray();
            for (const auto &v : arr) state->armPose.append(v.toDouble());
            state->armDone = true;
            if (state->armDone && state->npuDone) {
                if (state->armPose.size() != 6 || state->markerPose.isEmpty()) {
                    appendLog(QStringLiteral("捕获失败: 数据不完整"));
                    return;
                }
                Sample s;
                s.arm_pose6    = state->armPose;
                s.marker_pose6 = state->markerPose;
                s.aruco_id     = state->arucoId;
                samples_.append(s);
                updateSampleSummary();
                appendLog(QStringLiteral("捕获 #%1 ✓ arm(x=%2,y=%3,z=%4) marker(x=%5,y=%6,z=%7)")
                          .arg(samples_.size())
                          .arg(s.arm_pose6[0], 0, 'f', 1).arg(s.arm_pose6[1], 0, 'f', 1).arg(s.arm_pose6[2], 0, 'f', 1)
                          .arg(s.marker_pose6[0], 0, 'f', 1).arg(s.marker_pose6[1], 0, 'f', 1).arg(s.marker_pose6[2], 0, 'f', 1));
            }
        });

    rpc_->call(Protocol::Methods::NPU_GET_DETECTIONS, QJsonObject{},
        [this, state, wantId](QJsonObject r) {
            const QJsonArray dets = r.value(Protocol::Fields::DETECTIONS).toArray();
            for (const auto &v : dets) {
                const QJsonObject d = v.toObject();
                const int cls = d.value(Protocol::Fields::CLASS_ID).toInt(-1);
                if (wantId != 0 && cls != wantId) continue;
                if (!d.value(Protocol::Fields::HAS_XYZ).toBool(false)) continue;
                state->markerPose.append(d.value(Protocol::Fields::X_MM).toDouble());
                state->markerPose.append(d.value(Protocol::Fields::Y_MM).toDouble());
                state->markerPose.append(d.value(Protocol::Fields::Z_MM).toDouble());
                // Orientation isn't yet emitted by proc_npu for ArUco — pad
                // with zeros for now; the backend solver will treat missing
                // RPY as point-only constraints.
                state->markerPose.append(d.value("rx_deg").toDouble(0.0));
                state->markerPose.append(d.value("ry_deg").toDouble(0.0));
                state->markerPose.append(d.value("rz_deg").toDouble(0.0));
                state->arucoId = cls;
                break;
            }
            state->npuDone = true;
            if (state->armDone && state->npuDone) {
                if (state->armPose.size() != 6 || state->markerPose.isEmpty()) {
                    appendLog(QStringLiteral("捕获失败: 视野中未检测到 ArUco ID=%1").arg(wantId));
                    return;
                }
                Sample s;
                s.arm_pose6    = state->armPose;
                s.marker_pose6 = state->markerPose;
                s.aruco_id     = state->arucoId;
                samples_.append(s);
                updateSampleSummary();
                appendLog(QStringLiteral("捕获 #%1 ✓ arm(x=%2,y=%3,z=%4) marker(x=%5,y=%6,z=%7)")
                          .arg(samples_.size())
                          .arg(s.arm_pose6[0], 0, 'f', 1).arg(s.arm_pose6[1], 0, 'f', 1).arg(s.arm_pose6[2], 0, 'f', 1)
                          .arg(s.marker_pose6[0], 0, 'f', 1).arg(s.marker_pose6[1], 0, 'f', 1).arg(s.marker_pose6[2], 0, 'f', 1));
            }
        });
}

// ── Solve: send sample list to backend (Tsai-Lenz on the gateway) ────────
void CalibWidget::onSolve()
{
    if (samples_.size() < 3) {
        appendLog(QStringLiteral("样本不足 — 至少 3 个,推荐 ≥ 4"));
        return;
    }
    if (!rpc_ || !rpc_->isConnected()) {
        appendLog(QStringLiteral("⚠ RPC 未连接 — 求解需要 backend 协助"));
        return;
    }

    QJsonArray jsamples;
    for (const Sample &s : samples_) {
        QJsonObject o;
        QJsonArray ap; for (double v : s.arm_pose6)    ap.append(v);
        QJsonArray mp; for (double v : s.marker_pose6) mp.append(v);
        o["arm_pose"]    = ap;
        o["marker_pose"] = mp;
        o["aruco_id"]    = s.aruco_id;
        jsamples.append(o);
    }
    QJsonObject params;
    params["mode"]    = mode_combo_->currentData().toString();
    params["samples"] = jsamples;

    appendLog(QStringLiteral("求解中... 发送 %1 个样本到 backend").arg(samples_.size()));
    rpc_->call("calib.solve_hand_eye", params,
        [this](QJsonObject reply) {
            if (!reply.value("ok").toBool(false)) {
                const QString err = reply.value("error").toString("未知错误");
                appendLog(QStringLiteral("求解失败: %1").arg(err));
                return;
            }
            const QJsonArray T = reply.value("T").toArray();
            if (T.size() != 16) {
                appendLog(QStringLiteral("求解失败: 后端返回的矩阵格式非法"));
                return;
            }
            for (int i = 0; i < 16; ++i) last_T_[i] = T.at(i).toDouble();
            has_result_ = true;

            // Decompose for display: translation (mm) + RPY (deg).
            const double tx = last_T_[3], ty = last_T_[7], tz = last_T_[11];
            const double r11 = last_T_[0], r21 = last_T_[4], r31 = last_T_[8];
            const double r32 = last_T_[9], r33 = last_T_[10];
            const double pitch = std::asin(-r31) * 180.0 / M_PI;
            const double roll  = std::atan2(r32, r33) * 180.0 / M_PI;
            const double yaw   = std::atan2(r21, r11) * 180.0 / M_PI;
            result_label_->setText(
                QStringLiteral("✓ T = [t=(%1, %2, %3) mm,  rpy=(%4, %5, %6)°]")
                  .arg(tx,    0, 'f', 2).arg(ty, 0, 'f', 2).arg(tz, 0, 'f', 2)
                  .arg(roll,  0, 'f', 2).arg(pitch, 0, 'f', 2).arg(yaw, 0, 'f', 2));
            btn_apply_->setEnabled(true);
            appendLog(QStringLiteral("求解完成 — 点击「应用到 RK3588」推送到 proc_grasp"));

            // Persist locally as a backup.
            QSettings s;
            s.beginGroup("CalibWidget");
            QStringList parts;
            for (double v : last_T_) parts << QString::number(v, 'g', 8);
            s.setValue("last_T", parts.join(','));
            s.setValue("mode",   mode_combo_->currentData().toString());
            s.setValue("ts",     QDateTime::currentDateTime().toString(Qt::ISODate));
            s.endGroup();
        });
}

// ── Apply: push the 4×4 to the backend for proc_grasp consumption ────────
void CalibWidget::onApply()
{
    if (!has_result_ || !rpc_ || !rpc_->isConnected()) return;
    QJsonArray T;
    for (double v : last_T_) T.append(v);
    QJsonObject params;
    params["mode"] = mode_combo_->currentData().toString();
    params["T"]    = T;
    appendLog(QStringLiteral("推送至 calib.set_hand_eye ..."));
    rpc_->call("calib.set_hand_eye", params,
        [this](QJsonObject reply) {
            if (reply.value("ok").toBool(false)) {
                appendLog(QStringLiteral("✓ 已写入 RK3588 配置,proc_grasp 下一次启动生效"));
            } else {
                appendLog(QStringLiteral("写入失败: %1")
                              .arg(reply.value("error").toString("(无信息)")));
            }
        });
}

void CalibWidget::onReset()
{
    samples_.clear();
    has_result_ = false;
    btn_apply_->setEnabled(false);
    result_label_->setText(QStringLiteral("未求解"));
    updateSampleSummary();
    appendLog(QStringLiteral("已清空所有样本"));
}

void CalibWidget::onExportJson()
{
    if (samples_.isEmpty()) {
        appendLog(QStringLiteral("无样本可导出"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出标定样本"),
        QString("hand_eye_samples_%1.json")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;

    QJsonArray jsamples;
    for (const Sample &s : samples_) {
        QJsonObject o;
        QJsonArray ap; for (double v : s.arm_pose6)    ap.append(v);
        QJsonArray mp; for (double v : s.marker_pose6) mp.append(v);
        o["arm_pose"]    = ap;
        o["marker_pose"] = mp;
        o["aruco_id"]    = s.aruco_id;
        jsamples.append(o);
    }
    QJsonObject root;
    root["mode"]      = mode_combo_->currentData().toString();
    root["captured"]  = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["samples"]   = jsamples;
    if (has_result_) {
        QJsonArray T; for (double v : last_T_) T.append(v);
        root["T"] = T;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendLog(QStringLiteral("写入失败: %1").arg(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    appendLog(QStringLiteral("已导出到: %1").arg(path));
}
