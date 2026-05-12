// -*- mode: c++ -*-
// Included from Tab4TaskConfig.cpp.  Pulled out only because the Edit
// tool refuses long string replacements; the code is conceptually a
// straight extension of Tab4TaskConfig and shares all its members.

// Each task maps to a list of keyframes (time_seconds, [j1..j6 deg]).
// Linear interpolation between consecutive keys; trajectory ends at
// the last keyframe time. Nothing here touches the real arm — these
// are 3D-only sequences for sanity-checking the model + UI plumbing.
static QVector<QPair<float, QVector<float>>>
trajectory_for(const QString &task_key)
{
    using KF = QPair<float, QVector<float>>;
    if (task_key == "arm_demo") {
        // Exercise every joint in turn, then a coordinated sweep.
        return { KF{ 0.0F, QVector<float>{  0,  0,  0,  0,  0,  0}},
                 KF{ 1.2F, QVector<float>{ 90,  0,  0,  0,  0,  0}},  // J1
                 KF{ 2.4F, QVector<float>{ 90, 40,  0,  0,  0,  0}},  // J2
                 KF{ 3.6F, QVector<float>{ 90, 40, 45,  0,  0,  0}},  // J3
                 KF{ 4.8F, QVector<float>{ 90, 40, 45, 60,  0,  0}},  // J4
                 KF{ 6.0F, QVector<float>{ 90, 40, 45, 60, 45,  0}},  // J5
                 KF{ 7.2F, QVector<float>{ 90, 40, 45, 60, 45, 90}},  // J6
                 KF{ 8.8F, QVector<float>{180, 20, 20, -60,-30,-90}}, // coord sweep
                 KF{10.4F, QVector<float>{  0,  0,  0,  0,  0,  0}} };
    }
    if (task_key == "arm_home") {
        return { KF{0.0F,  QVector<float>{90,30,30,30,30,30}},
                 KF{2.0F,  QVector<float>{0,0,0,0,0,0}} };
    }
    if (task_key == "battery_pick"    || task_key == "pick_place"
        || task_key == "battery_pick_3d" || task_key == "battery_pick_6d") {
        // Stylised pick-and-place: rotate base, lower shoulder + elbow,
        // align wrist, grip pause, raise, swing to drop, return home.
        return { KF{ 0.0F, QVector<float>{  0,  0,  0,  0,  0,  0}},
                 KF{ 1.5F, QVector<float>{ 45,  0,  0,  0,  0,  0}},
                 KF{ 3.0F, QVector<float>{ 45, 40, 30,  0, 30,  0}},  // lower + wrist
                 KF{ 3.8F, QVector<float>{ 45, 40, 30,  0, 30, 45}},  // grip pause (J6 close)
                 KF{ 5.0F, QVector<float>{ 45, 15, 15,  0, 30, 45}},  // lift
                 KF{ 6.5F, QVector<float>{300, 15, 15, 30, 30, 45}},  // swing to drop
                 KF{ 7.5F, QVector<float>{300, 40, 30, 30, 30, 45}},
                 KF{ 8.5F, QVector<float>{300, 40, 30, 30, 30,  0}},  // release
                 KF{ 9.5F, QVector<float>{300, 15, 15,  0,  0,  0}},
                 KF{11.0F, QVector<float>{  0,  0,  0,  0,  0,  0}} };
    }
    if (task_key == "platform_lock") {
        return { KF{0.0F, QVector<float>{0,0,0,0,0,0}},
                 KF{0.6F, QVector<float>{10,0,0,0,0,0}},
                 KF{1.2F, QVector<float>{350,0,0,0,0,0}},
                 KF{1.8F, QVector<float>{0,0,0,0,0,0}} };
    }
    if (task_key == "face_track") {
        return { KF{0.0F, QVector<float>{0,0,0,0,0,0}},
                 KF{1.5F, QVector<float>{30,0,0,0,0,0}},
                 KF{3.0F, QVector<float>{330,0,0,0,0,0}},
                 KF{4.5F, QVector<float>{0,0,0,0,0,0}} };
    }
    // Generic fallback: sequentially exercise each joint to 45 then back.
    QVector<KF> out;
    out.append(KF{0.0F, QVector<float>{0,0,0,0,0,0}});
    for (int j = 0; j < 6; ++j) {
        QVector<float> up(6, 0.0F);   up[j] = 45.0F;
        QVector<float> down(6, 0.0F);
        out.append(KF{(j+1)*1.5F - 0.7F, up});
        out.append(KF{(j+1)*1.5F,         down});
    }
    return out;
}

QWidget* Tab4TaskConfig::buildSimPanel()
{
    auto *grp = new QGroupBox(QStringLiteral("3D \u4eff\u771f / \u6d4b\u8bd5"), this);
    auto *layout = new QVBoxLayout(grp);
    layout->setContentsMargins(8, 18, 8, 8);
    layout->setSpacing(6);

    static const char *kJointLabels[6] = {"J1","J2","J3","J4","J5","J6"};
    for (int i = 0; i < 6; ++i) {
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        auto *lbl = new QLabel(kJointLabels[i], grp);
        lbl->setMinimumWidth(22);
        lbl->setStyleSheet("font-family: Consolas; color: #c8d0e8;");
        row->addWidget(lbl);

        joint_sliders_[i] = new QSlider(Qt::Horizontal, grp);
        joint_sliders_[i]->setRange(0, 3600);
        joint_sliders_[i]->setValue(0);
        joint_sliders_[i]->setMinimumWidth(140);
        row->addWidget(joint_sliders_[i], 1);

        joint_value_labels_[i] = new QLabel(QStringLiteral("0.0\u00b0"), grp);
        joint_value_labels_[i]->setMinimumWidth(54);
        joint_value_labels_[i]->setStyleSheet(
            "font-family: Consolas; color: #c8d0e8;");
        row->addWidget(joint_value_labels_[i]);

        layout->addLayout(row);

        connect(joint_sliders_[i], &QSlider::valueChanged, this,
                [this, i](int v) {
            const float deg = v / 10.0F;
            joint_value_labels_[i]->setText(
                QString::number(deg, 'f', 1) + QStringLiteral("\u00b0"));
            if (!viewer_3d_) return;
            // User touched a slider: stop running trajectory + pause RPC
            // pushes (otherwise the next live reply snaps it back).
            // Re-enabled by the "Live" button.
            if (sim_timer_ && sim_timer_->isActive()) sim_timer_->stop();
            if (!sim_mode_active_ && sim_worker_) {
                QMetaObject::invokeMethod(sim_worker_, "stopAnglePolling",
                                          Qt::QueuedConnection);
            }
            sim_mode_active_ = true;
            if (sim_status_label_) sim_status_label_->setText(
                QStringLiteral("\u624b\u52a8\u63a7\u5236"));
            QVector<float> a(6, 0.0F);
            for (int k = 0; k < 6; ++k) a[k] = joint_sliders_[k]->value() / 10.0F;
            viewer_3d_->setJointAngles(a);
        });
    }

    auto *btnRow = new QHBoxLayout;
    btn_sim_zero_ = new QPushButton(QStringLiteral("\u5168\u90e8\u5f52\u96f6"), grp);
    btn_sim_run_  = new QPushButton(QStringLiteral("\u4eff\u771f\u9009\u4e2d\u4efb\u52a1"), grp);
    btn_sim_live_ = new QPushButton(QStringLiteral("\u8fd4\u56de\u5b9e\u65f6"), grp);
    btn_sim_zero_->setFixedHeight(26);
    btn_sim_run_->setFixedHeight(26);
    btn_sim_live_->setFixedHeight(26);
    btnRow->addWidget(btn_sim_zero_);
    btnRow->addWidget(btn_sim_run_);
    btnRow->addWidget(btn_sim_live_);
    layout->addLayout(btnRow);

    sim_status_label_ = new QLabel(QStringLiteral("\u7a7a\u95f2"), grp);
    sim_status_label_->setStyleSheet("font-family: Consolas; color: #8a90a8;");
    layout->addWidget(sim_status_label_);
    layout->addStretch(1);

    sim_timer_ = new QTimer(this);
    sim_timer_->setInterval(33);   // ~30 fps
    connect(sim_timer_, &QTimer::timeout, this, &Tab4TaskConfig::onSimTick);

    connect(btn_sim_zero_, &QPushButton::clicked, this, [this]() {
        if (sim_timer_ && sim_timer_->isActive()) sim_timer_->stop();
        for (int i = 0; i < 6; ++i) joint_sliders_[i]->setValue(0);
        if (sim_status_label_) sim_status_label_->setText(
            QStringLiteral("\u5df2\u5f52\u96f6"));
    });
    connect(btn_sim_run_,  &QPushButton::clicked, this,
            &Tab4TaskConfig::onSimulateTask);
    connect(btn_sim_live_, &QPushButton::clicked, this,
            &Tab4TaskConfig::onSimReturnToLive);

    return grp;
}

void Tab4TaskConfig::onSimulateTask()
{
    if (!task_list_ || !task_list_->currentItem()) {
        appendLog("WARN", QStringLiteral("[3D] \u8bf7\u5148\u5728\u4efb\u52a1\u5217\u8868\u4e2d\u9009\u62e9\u4e00\u4e2a\u4efb\u52a1"));
        return;
    }
    const QString key = task_list_->currentItem()->data(Qt::UserRole).toString();
    sim_traj_ = trajectory_for(key);
    if (sim_traj_.isEmpty()) {
        appendLog("WARN", QStringLiteral("[3D] \u8be5\u4efb\u52a1\u672a\u5b9a\u4e49\u8f68\u8ff9: ") + key);
        return;
    }
    if (sim_worker_) {
        QMetaObject::invokeMethod(sim_worker_, "stopAnglePolling",
                                  Qt::QueuedConnection);
    }
    sim_mode_active_ = true;
    sim_t0_ms_ = double(QDateTime::currentMSecsSinceEpoch());
    sim_timer_->start();
    if (sim_status_label_) {
        sim_status_label_->setText(
            QStringLiteral("\u4eff\u771f\u4e2d: %1").arg(key));
    }
    appendLog("INFO",
              QStringLiteral("[3D] \u5f00\u59cb\u4eff\u771f\u4efb\u52a1: %1 (%2 keyframes)")
              .arg(key).arg(sim_traj_.size()));
}

void Tab4TaskConfig::onSimReturnToLive()
{
    if (sim_timer_ && sim_timer_->isActive()) sim_timer_->stop();
    sim_mode_active_ = false;
    if (sim_status_label_) sim_status_label_->setText(
        QStringLiteral("\u5b9e\u65f6\u6a21\u5f0f"));
    if (sim_worker_) {
        QMetaObject::invokeMethod(sim_worker_, "startAnglePolling",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, 200));
    }
    appendLog("INFO",
              QStringLiteral("[3D] \u5df2\u5207\u56de\u5b9e\u65f6\u6a21\u5f0f (RPC \u9a71\u52a8)"));
}

void Tab4TaskConfig::onSimTick()
{
    if (sim_traj_.isEmpty() || !viewer_3d_) {
        sim_timer_->stop();
        return;
    }
    const double now_ms = double(QDateTime::currentMSecsSinceEpoch());
    const float  t      = float((now_ms - sim_t0_ms_) / 1000.0);
    const float  t_end  = sim_traj_.last().first;

    QVector<float> angles(6, 0.0F);
    if (t >= t_end) {
        angles = sim_traj_.last().second;
        sim_timer_->stop();
        if (sim_status_label_) sim_status_label_->setText(
            QStringLiteral("\u4eff\u771f\u5b8c\u6210"));
    } else {
        int k = 0;
        while (k + 1 < sim_traj_.size() && sim_traj_[k+1].first < t) ++k;
        const float t0 = sim_traj_[k].first;
        const float t1 = sim_traj_[k+1].first;
        const float u  = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0F;
        const QVector<float> &a0 = sim_traj_[k].second;
        const QVector<float> &a1 = sim_traj_[k+1].second;
        for (int i = 0; i < 6; ++i) angles[i] = a0[i] * (1.0F - u) + a1[i] * u;
    }

    viewer_3d_->setJointAngles(angles);
    for (int i = 0; i < 6; ++i) {
        QSignalBlocker block(joint_sliders_[i]);
        float deg = angles[i];
        while (deg <    0.0F) deg += 360.0F;
        while (deg >= 360.0F) deg -= 360.0F;
        joint_sliders_[i]->setValue(int(deg * 10.0F));
        joint_value_labels_[i]->setText(
            QString::number(angles[i], 'f', 1) + QStringLiteral("\u00b0"));
    }
}
