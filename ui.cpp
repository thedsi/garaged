#ifdef _WIN32
#include <Windows.h>
#undef INPUT
#undef max
#endif
#include "emu.h"
#include "garaged.h"
#include <random>
#include <iostream>
#include <functional>
#include <vector>
#include <cassert>
#include <cmath>
#include <atomic>
#include <limits>
#include <algorithm>
#include <QApplication>
#include <QWidget>
#include <QTextEdit>
#include <QThread>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QHeaderView>
#include <QABstractTableModel>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>


static std::atomic_bool Button = false;
static std::atomic_bool Gate = false;
static void(*ISR_Gate)() = nullptr;
static void(*ISR_Button)() = nullptr;
static auto MyEpoch = Clock::now();

static QString FormatTime(Time time)
{
    if (time.time_since_epoch().count() == 0)
        return "ASAP";
    return QString::number((double)std::chrono::duration_cast<std::chrono::nanoseconds>(time - MyEpoch).count() / 1000000000.0);
}

struct EventNotification
{
    EventAction action;
    EventId num;
    Event event;
    Time planTime;
    Time currentTime;
};
Q_DECLARE_METATYPE(EventNotification);

class UiConnection : public QObject
{
    Q_OBJECT
signals :
    void SendLog(const QString& str);

    void SendPinControl(int pin, bool value);

    void SendNotification(const EventNotification& notification);
};

static UiConnection* gUiConnection = nullptr;

void Z_EventNotify(EventAction ea, const EventQueue::Entry* entry)
{
    EventNotification notification;
    notification.action = ea;
    notification.currentTime = Clock::now();
    if (entry)
    {
        notification.event = entry->event;
        notification.num = entry->num;
        notification.planTime = entry->time;
    }
    gUiConnection->SendNotification(notification);
}

int Z_system(const char *) { return 0; }
void wiringPiSetup() { }

void digitalWrite(int pin, int value)
{
    assert(
        pin == PN_Relay ||
        pin == PN_InternalLed ||
        pin == PN_ExternalLed
    );
    gUiConnection->SendPinControl(pin, value == LOW ? false : true);
    
}

int digitalRead(int pin)
{
    assert(
        pin == PN_Button ||
        pin == PN_Gate
    );
    if (pin == PN_Button)
    {
        return Button ? LOW : HIGH;
    }
    else if (pin == PN_Gate)
    {
        return Gate ? HIGH : LOW;
    }
    return LOW;
}

void pinMode(int pin, int mode)
{
    assert(
        pin == PN_Relay       && mode == OUTPUT ||
        pin == PN_Button      && mode == INPUT  ||
        pin == PN_Gate        && mode == INPUT  ||
        pin == PN_InternalLed && mode == OUTPUT ||
        pin == PN_ExternalLed && mode == OUTPUT
    );
}

void pullUpDnControl(int pin, int pudMode)
{
    assert(
        pin == PN_Button && pudMode == PUD_OFF ||
        pin == PN_Gate   && pudMode == PUD_OFF
    );
}

void wiringPiISR(int pin, int edgeMode, void (*handler)())
{
    assert(
        pin == PN_Button && edgeMode == INT_EDGE_BOTH ||
        pin == PN_Gate && edgeMode == INT_EDGE_BOTH
    );
    if (pin == PN_Button)
        ISR_Button = handler;
    else if (pin == PN_Gate)
        ISR_Gate = handler;
}

int Z_sysinfo(struct Z_sysinfo* si)
{
    struct Z_sysinfo result = {};
#ifdef _WIN32
    result.uptime = GetTickCount() / 1000;
#endif
    *si = result;
    return 0;
}

enum SavedEventMode
{
    SEM_None,
    SEM_Dispatched,
    SEM_Added,
    SEM_Deleted,
};

struct SavedEvent
{
    EventId id;
    Event evt;
    Time plannedTime;
    SavedEventMode mode;

    bool operator<(const SavedEvent& rhs)
    {
        return plannedTime < rhs.plannedTime || plannedTime == rhs.plannedTime && id < rhs.id;
    }
};

struct Snapshot
{
    std::vector<SavedEvent> savedEvts;
    Time snapshotTime = Time();
    bool button = false;
    bool gate = false;
};

class SnapshotModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    void BeginSetSnapshot()
    {
        beginResetModel();
    }

    void EndSetSnapshot(Snapshot* snap)
    {
        _snap = snap;
        endResetModel();
    }

    Snapshot* GetSnapshot() const { return _snap; }

    SnapshotModel(QObject* parent) : QAbstractTableModel(parent)
    {
    }

    virtual int rowCount(const QModelIndex& parent) const override
    {
        return (parent.isValid() || !_snap) ? 0 : (int)_snap->savedEvts.size();
    }

    virtual int columnCount(const QModelIndex& parent) const override
    {
        return (parent.isValid() || !_snap) ? 0 : 3;
    }

    virtual QVariant data(const QModelIndex& index, int role) const override
    {       
        if (_snap && !index.parent().isValid())
        {
            if (role == Qt::DisplayRole)
            {
                switch (index.column())
                {
                case 0: return QString::number(_snap->savedEvts[index.row()].id);
                case 1: return QString::fromLatin1(GetEventName(_snap->savedEvts[index.row()].evt.Type())) + "(" + QString::number(_snap->savedEvts[index.row()].evt.Data()) + ")";
                case 2: return FormatTime(_snap->savedEvts[index.row()].plannedTime);
                }
            }
            else if (role == Qt::BackgroundRole)
            {
                switch (_snap->savedEvts[index.row()].mode)
                {
                case SEM_Added:      return QBrush(QColor(237, 249, 200));
                case SEM_Deleted:    return QBrush(QColor(255, 197, 193));
                case SEM_Dispatched: return QBrush(QColor(121, 188, 255));   
                }
            }
        }
        return QVariant();
    }

    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role == Qt::DisplayRole)
        {
            if (orientation == Qt::Horizontal)
            {
                switch (section)
                {
                case 0: return "ID";
                case 1: return "Type";
                case 2: return "Time";
                }
            }
            else
            {
                return QString::number(section + 1);
            }
        }
        return QVariant();
    }

private:
    Snapshot* _snap = nullptr;
};

class MainWnd : public QWidget
{
    Q_OBJECT
public:


    MainWnd() : QWidget()
    {
        setWindowTitle("garaged Emu");

        _rnd = std::mt19937(std::random_device()());
        _exp = std::exponential_distribution<>(1.0/7.0);
        _binaryDist = std::uniform_int_distribution<>(0, 1);

        resize(800, 372);
        _logEdit = new QTextEdit(this);
        _logEdit->setGeometry(10, 10, 410, 200);
        
        QLabel* label1 = new QLabel("Internal LED:", this);
        QLabel* label2 = new QLabel("External LED:", this);
        label1->setGeometry(10, 230, 64, 16);
        label2->setGeometry(330, 230, 64, 16);
        _internalLed = new QFrame(this);
        _internalLed->setGeometry(80, 230, 16, 16);
        _internalLed->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        _internalLed->setAutoFillBackground(true);
        _externalLed = new QFrame(this);
        _externalLed->setGeometry(404, 230, 16, 16);
        _externalLed->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        _externalLed->setAutoFillBackground(true);
        _light = new QLabel(this);
        _light->setGeometry(10, 260, 410, 40);
        _light->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        _light->setAutoFillBackground(true);
        _light->setAlignment(Qt::AlignCenter);
        _button = new QPushButton("Button", this);
        _button->setGeometry(10, 330, 200, 32);
        _gateButton = new QPushButton("Gate", this);
        _gateButton->setGeometry(220, 330, 200, 32);
        _gateButton->setCheckable(true);
        
        _snapCount = new QLabel(this);
        _snapCount->setGeometry(800 - 10 - 64, 20, 64, 20);
        //_slider = new QSlider(Qt::Horizontal, this);
        //_slider->setGeometry(430, 10, 360, 20);

        _snapStatusLabel = new QLabel(this);
        _snapStatusLabel->setGeometry(430, 40, 800 - 10 - 64 - 430, 20);

        _spinBox = new QSpinBox(this);
        _spinBox->setGeometry(800 - 10 - 64, 40, 64, 20);
        _spinBox->setMinimum(1);
        _spinBox->setMaximum((std::numeric_limits<int>::max)());
        _spinBox->setValue(0);       

        _evtList = new QTableView(this);
        _evtList->setGeometry(430, 70, 360, 292);
        
        _snapModel = new SnapshotModel(this);
        _evtList->setModel(_snapModel);

        _deleteAllSnaps = new QPushButton("Clear", this);
        _deleteAllSnaps->setGeometry(430, 10, 100, 32);

        connect(_gateButton, &QPushButton::toggled, this, &MainWnd::GateChanged);
        connect(_button, &QPushButton::pressed, this, &MainWnd::ButtonPressed);
        connect(_button, &QPushButton::released, this, &MainWnd::ButtonReleased);
        connect(_spinBox, (void(QSpinBox::*)(int))&QSpinBox::valueChanged, this, &MainWnd::SpinBoxValueChanged);
        connect(_deleteAllSnaps, &QPushButton::clicked, this, &MainWnd::DeleteAllSnaps);

        UpdateGateButtonLabel();
        UpdateLabelsFromSnapshot();
        UpdateCount();

        ControlLed(false, false);
        ControlLed(true, false);
        ControlLight(true);
    }


    void ControlLed(bool internal, bool on)
    {
        QFrame* led = internal ? _internalLed : _externalLed;
        SetBg(led, on ? Qt::red : Qt::black);
    }

    void ControlLight(bool on)
    {
        SetBg(_light, on ? QColor(240,247,240) : Qt::black);
        _light->setText(on ? "Light on" : "Light off");
    }

    

public slots:

    void DeleteAllSnaps()
    {
        _snapModel->BeginSetSnapshot();
        _snaps.clear();
        _snapModel->EndSetSnapshot(nullptr);
        UpdateLabelsFromSnapshot();
        UpdateCount();
    }
    void UpdateCount()
    {
        _snapCount->setText("Total: " + QString::number((int)_snaps.size()));
    }

    void PrintLog(const QString& str)
    {
        _logEdit->moveCursor(QTextCursor::End);
        _logEdit->insertPlainText(str);
        _logEdit->moveCursor(QTextCursor::End);
    }
    
    void PinControl(int pin, bool value)
    {
        if (pin == PN_Relay)
        {
            ControlLight(value);
        }
        else if (pin == PN_InternalLed)
        {
            ControlLed(true, value);
        }
        else if (pin == PN_ExternalLed)
        {
            ControlLed(false, value);
        }
    }

    void GateChanged(bool gateChecked)
    {
        if (ISR_Gate)
        {
            int num = int(_exp(_rnd));
            std::cout << "Gate: Sending total " << num + 1 << " changes" << std::endl;
            for (int i = 0; i < num; ++i)
            {
                Gate = _binaryDist(_rnd) ? false : true;
                ISR_Gate();
            }
            Gate = gateChecked;
            CurrentSnap(Clock::now())->gate = gateChecked;
            ISR_Gate();
        }
        UpdateGateButtonLabel();
    }

    void ButtonPressed()
    {
        ButtonPressedOrReleased(true);
    }
    void ButtonReleased()
    {
        ButtonPressedOrReleased(false);
    }

    void EvtNotification(const EventNotification& notify)
    {
        bool scroll = (_snaps.empty() || _spinBox->value() == (int)_snaps.size());
        if (scroll)
            _snapModel->BeginSetSnapshot();
        if (notify.action == EA_Wait)
        {
            Snapshot* snap = CloneSnap(notify.currentTime);
            auto it = std::find_if(snap->savedEvts.begin(), snap->savedEvts.end(), [](auto& evt) { return evt.mode == SEM_Dispatched; });
            if (it != snap->savedEvts.end())
                snap->savedEvts.erase(it);
        }
        else if (notify.action == EA_New)
        {
            /*Snapshot* snap = */
            CloneSnap(notify.currentTime);
        }
        else if (notify.action == EA_Plan)
        {
            Snapshot* snap = CurrentSnap(notify.currentTime);
            SavedEvent evt;
            evt.evt = notify.event;
            evt.id = notify.num;
            evt.mode = SEM_Added;
            evt.plannedTime = notify.planTime;
            snap->savedEvts.push_back(evt);
            std::sort(snap->savedEvts.begin(), snap->savedEvts.end());
        }
        else if (notify.action == EA_Delete)
        {
            Snapshot* snap = CurrentSnap(notify.currentTime);
            auto it = std::find_if(snap->savedEvts.begin(), snap->savedEvts.end(), [&notify](auto& evt) { return evt.id == notify.num; });
            if(it != snap->savedEvts.end())
                it->mode = SEM_Deleted;
        }
        else if (notify.action == EA_Dispatch)
        {
            Snapshot* snap = CloneSnap(notify.currentTime);
            auto it = std::find_if(snap->savedEvts.begin(), snap->savedEvts.end(), [&notify](auto& evt) { return evt.id == notify.num; });
            if (it != snap->savedEvts.end())
                it->mode = SEM_Dispatched;
        }
        
        if (scroll)
        {
            auto snap = (_snaps.empty() ? nullptr : &_snaps.back());
            _snapModel->EndSetSnapshot(snap);
            if (snap)
            {
                QSignalBlocker blocker(_spinBox);
                _spinBox->setValue((int)_snaps.size());
                UpdateLabelsFromSnapshot();
            }
        }
        UpdateCount();
        
    }

    void SpinBoxValueChanged(int value)
    {
        if (!_snaps.empty())
        {
            if (value > (int)_snaps.size())
            {
                _spinBox->setValue((int)_snaps.size());
            }
            else
            {
                _snapModel->BeginSetSnapshot();
                _snapModel->EndSetSnapshot(&_snaps[value - 1]);
                UpdateLabelsFromSnapshot();
            }
        }
    }

    void UpdateLabelsFromSnapshot()
    {
        if (!_snaps.empty())
        {
            auto& snap = _snaps[_spinBox->value() - 1];
            _snapStatusLabel->setText("Time: " + FormatTime(snap.snapshotTime) + ", Gate: " + (snap.gate ? "Open" : "Closed") + ", Button: " + (snap.button ? "Pressed" : "Released"));
        }
        else
            _snapStatusLabel->setText("No snapshot");
    }

private:

    Snapshot* CurrentSnap(Time time)
    {
        if (_snaps.empty())
        {
            _snaps.emplace_back();
            _snaps.back().snapshotTime = time;
        }
        return &_snaps.back();
    }

    Snapshot* CloneSnap(Time time)
    {
        if (!_snaps.empty())
        {
            auto copy = _snaps.back();
            for (auto it = copy.savedEvts.begin(); it != copy.savedEvts.end();)
            {
                if (it->mode == SEM_Added)
                {
                    it->mode = SEM_None;
                }
                else if (it->mode == SEM_Deleted)
                {
                    it = copy.savedEvts.erase(it);
                    continue;
                }
                ++it;
            }
            copy.snapshotTime = time;
            _snaps.push_back(copy);

        }
        return CurrentSnap(time);
    }

    void ButtonPressedOrReleased(bool pressed)
    {
        if (ISR_Button)
        {
            int num = int(_exp(_rnd));
            std::cout << "Button: Sending total " << num + 1 << " changes" << std::endl;
            for (int i = 0; i < num; ++i)
            {
                Button = _binaryDist(_rnd) ? false : true;
                ISR_Button();   
            }            
            Button = pressed;
            CurrentSnap(Clock::now())->button = pressed;
            ISR_Button();            
        }
    }

    void UpdateGateButtonLabel()
    {
        _gateButton->setText(_gateButton->isChecked() ? "Gate Open" : "Gate Closed");
    }
    void SetBg(QWidget* widget, const QColor& color)
    {
        QPalette palette = widget->palette();
        palette.setColor(QPalette::Background, color);
        palette.setColor(QPalette::Foreground, QColor::fromRgb(~color.rgb()));
        widget->setPalette(palette);
    }


    QFrame* _internalLed;
    QFrame* _externalLed;
    QLabel* _light;
    QTextEdit* _logEdit;
    QPushButton* _gateButton;
    QPushButton* _button;
    QTableView* _evtList;
    QSlider* _slider;
    QSpinBox* _spinBox;
    std::exponential_distribution<> _exp;
    std::uniform_int_distribution<> _binaryDist;
    std::mt19937 _rnd;
    std::vector<Snapshot> _snaps;
    SnapshotModel* _snapModel;
    QLabel* _snapStatusLabel;
    QLabel* _snapCount;
    QPushButton* _deleteAllSnaps;
};

class OutputStream : public std::streambuf
{
public:
    template<typename F>
    void SetSink(F&& sink)
    {
        _f = std::forward<F>(sink);
        SendString();
    }

protected:
    virtual int overflow(int ch) override
    {
        if (ch != traits_type::eof())
        {
            _vec.push_back(traits_type::to_char_type(ch));
        }
        return ch;
    }

    virtual int sync() override
    {
        SendString();
        return 0;
    }

private:
    void SendString()
    {
        if (_f && !_vec.empty())
        {
            _vec.push_back('\0');
            _f(_vec.data());
            _vec.clear();
        }
    }

    std::function<void(const char*)> _f;
    std::vector<char> _vec;
};

class ZThread : public QThread
{
    Q_OBJECT
public:
    virtual void run() override
    {
        Garaged::Instance().SetLogFileName("garaged.log");
        Garaged::Instance().Exec();
    }
};

int main(int argc, char** argv)
{
    
    OutputStream stream;
    auto oldCoutBuf = std::cout.rdbuf(&stream);
    auto oldCerrBuf = std::cerr.rdbuf(&stream);
    std::cout << "garaged Emu" << std::endl;
    QApplication app(argc, argv);
    MainWnd mainWnd;
    UiConnection lc;
    gUiConnection = &lc;
    QObject::connect(&lc, &UiConnection::SendLog, &mainWnd, &MainWnd::PrintLog, Qt::QueuedConnection);
    QObject::connect(&lc, &UiConnection::SendPinControl, &mainWnd, &MainWnd::PinControl, Qt::QueuedConnection);
    QObject::connect(&lc, &UiConnection::SendNotification, &mainWnd, &MainWnd::EvtNotification, Qt::QueuedConnection);
    stream.SetSink(std::bind(&UiConnection::SendLog, &lc, std::placeholders::_1));
    
    ZThread zThread;
    zThread.start();

    mainWnd.show();
    app.exec();
    Garaged::Instance().Q().PlanEvent(ET_Halt);
    zThread.wait();
    
    std::cout.rdbuf(oldCoutBuf);
    std::cerr.rdbuf(oldCerrBuf);
}
#include "ui.moc"