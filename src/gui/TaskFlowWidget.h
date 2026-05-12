#ifndef TASKFLOWWIDGET_H
#define TASKFLOWWIDGET_H

#include <QWidget>
#include <QString>
#include <QVector>
#include <QHash>
#include <QTimer>

// TaskFlowWidget — metro-style flow chart for the battery-swap state machine.
//
// Renders 24 task states as colored "stations" on two horizontal "lines"
// (rows). Phase 1 = remove old battery + insert to slot (~14 states);
// phase 2 = fetch new battery + load into UAV (~10 states). Each station
// is grey by default, blue + pulsing when current, green when done, red
// when faulted. The widget never talks to the backend itself — the
// owner (Tab4TaskConfig) calls setCurrentState/setFinishedStates/setError
// based on either a simulation timer or live swap.* RPC.
//
// Mouse hover → tooltip with the long description.
// Mouse click on station → emit stationClicked(state_id).

struct TaskState {
    QString id;       // canonical name, e.g. "MONITOR"
    QString label;    // 4-6 char Chinese for the station label
    QString desc;     // longer description for tooltip
    int     phase;    // 1 or 2 (which subway line)
    QVector<float> demo_joints_deg;   // hint pose for simulation (J1..J6)
};

class TaskFlowWidget : public QWidget {
    Q_OBJECT
public:
    explicit TaskFlowWidget(QWidget *parent = nullptr);

    // Static metadata for all 24 task states (in execution order).
    static const QVector<TaskState>& states();

    // Update visual state — caller owns the truth.
    void setCurrentState(const QString &state_id);       // blue + pulsing
    void markFinished(const QString &state_id);          // green
    void markError(const QString &state_id, const QString &error_msg);
    void resetAll();                                     // all back to grey

    // Read current state name (empty if none).
    QString currentState() const { return current_; }

    QSize sizeHint() const override { return {980, 320}; }

signals:
    void stationClicked(QString state_id);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void onPulseTick();

private:
    // Cached geometry: each station's screen-space center, in widget coords.
    struct StationGeom { QPointF center; double radius; int idx; };
    void recomputeGeometry();

    enum class StateStatus : int { Pending, Current, Done, Error };

    QHash<QString, StateStatus> status_;     // by state id
    QString  current_;
    QHash<QString, QString> errors_;

    QVector<StationGeom> geom_;              // index aligned with states()
    int     hover_idx_     = -1;
    QTimer *pulse_timer_   = nullptr;
    int     pulse_phase_   = 0;   // 0..360, animates the current-station glow
};

#endif // TASKFLOWWIDGET_H
