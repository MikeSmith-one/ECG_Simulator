#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDateTime>
#include <cmath>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_serialPort(new QSerialPort(this))
    , m_waveTimer(new QTimer(this))
    , m_paramTimer(new QTimer(this))
    , m_waveCounter(0)
    , m_nibpTimer(new QTimer(this)), m_hasNIBPResult(false), m_sysResult(0), m_diaResult(0)
{
    setupUI();

    //扫描可用串口
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        m_portComboBox->addItem(info.portName());
    }
    if (m_portComboBox->count() == 0) {
        m_portComboBox->addItems({ "COM1", "COM2" });
    }

    m_nibpTimer->setSingleShot(true); //单次定时器

    //绑定信号槽
    connect(m_nibpTimer, &QTimer::timeout, this, &MainWindow::onNIBPTimerTimeout);

    connect(m_openCloseBtn, &QPushButton::clicked, this, &MainWindow::onOpenCloseButtonClicked);
    connect(m_waveTimer, &QTimer::timeout, this, &MainWindow::onWaveTimerTimeout);
    connect(m_paramTimer, &QTimer::timeout, this, &MainWindow::onParamTimerTimeout);
}

MainWindow::~MainWindow()
{
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
    }
}

void MainWindow::setupUI()
{
    this->setWindowTitle("下位机模拟器 (ECG Simulator)");
    this->resize(400, 300);

    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);

    //串口配置区
    QGroupBox* portGroup = new QGroupBox("串口配置", this);
    QHBoxLayout* portLayout = new QHBoxLayout(portGroup);
    m_portComboBox = new QComboBox(this);
    m_openCloseBtn = new QPushButton("打开串口", this);
    portLayout->addWidget(new QLabel("选择串口:", this));
    portLayout->addWidget(m_portComboBox);
    portLayout->addWidget(m_openCloseBtn);
    mainLayout->addWidget(portGroup);

    //状态控制区
    QGroupBox* stateGroup = new QGroupBox("生理状态模拟", this);
    QHBoxLayout* stateLayout = new QHBoxLayout(stateGroup);
    m_radioNormal = new QRadioButton("正常状态", this);
    m_radioAbnormal = new QRadioButton("异常/超限", this);
    m_radioFlatline = new QRadioButton("导联脱落/归零", this);
    m_radioNormal->setChecked(true); // 默认正常
    stateLayout->addWidget(m_radioNormal);
    stateLayout->addWidget(m_radioAbnormal);
    stateLayout->addWidget(m_radioFlatline);
    mainLayout->addWidget(stateGroup);

    //日志区
    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    mainLayout->addWidget(m_logText);

    this->setCentralWidget(centralWidget);
}

void MainWindow::onOpenCloseButtonClicked()
{
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
        m_waveTimer->stop();
        m_paramTimer->stop();
        m_openCloseBtn->setText("打开串口");
        m_logText->append("串口已关闭，停止推流。");
    }
    else {
        m_serialPort->setPortName(m_portComboBox->currentText());
        m_serialPort->setBaudRate(115200);
        m_serialPort->setDataBits(QSerialPort::Data8);
        m_serialPort->setParity(QSerialPort::NoParity);
        m_serialPort->setStopBits(QSerialPort::OneStop);
        m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

        if (m_serialPort->open(QIODevice::ReadWrite)) {
            m_openCloseBtn->setText("关闭串口");
            m_logText->append("串口已打开，开始 100Hz 推流...");
            m_waveCounter = 0;
            m_waveTimer->start(10);   //10ms = 100Hz 波形
            m_paramTimer->start(1000); //1000ms = 1Hz 参数

            connect(m_serialPort, &QSerialPort::readyRead, this, &MainWindow::onSimulatorReadyRead);
        }
        else {
            m_logText->append("打开失败: " + m_serialPort->errorString());
        }
    }
}

// ==========================================
// 核心：组装并发送数据帧
// ==========================================
void MainWindow::sendPacket(quint8 type, const QByteArray& payload)
{
    if (!m_serialPort->isOpen()) return;

    QByteArray packet;
    packet.append(static_cast<char>(0xAA)); //帧头1
    packet.append(static_cast<char>(0x55)); //帧头2
    packet.append(static_cast<char>(type)); //类型

    quint8 length = static_cast<quint8>(payload.size());
    packet.append(static_cast<char>(length)); //长度

    packet.append(payload); //负载数据

    //计算校验和 (Type + Length + Payload)
    quint8 checksum = type + length;
    for (int i = 0; i < payload.size(); ++i) {
        checksum += static_cast<quint8>(payload.at(i));
    }
    packet.append(static_cast<char>(checksum)); //校验和

    m_serialPort->write(packet);
}

// ==========================================
// 高频波形推流 (100Hz)
// ==========================================
void MainWindow::onWaveTimerTimeout()
{
    qint16 ecg = 0;
    quint8 spo2 = 0;
    quint8 resp = 0;
    quint8 statusFlag = 0x00;

    if (m_radioFlatline->isChecked()) {
        //归零/脱落状态：全发0，置位脱落标志 (Bit 1)
        statusFlag = 0x02;
    }
    else {
        //心率周期计算：正常75bpm(周期80点)，异常130bpm(周期46点)
        int cycle = m_radioNormal->isChecked() ? 80 : 46;
        int t = m_waveCounter % cycle;

        //模拟 ECG 波形 (简易 PQRST 模型)
        if (t == 10) {
            ecg = 15000; //R波波峰
            statusFlag = 0x01; //置位 R波标志 (Bit 0)，触发上位机心跳音
        }
        else if (t == 9 || t == 11) ecg = -3000; // Q/S 波
        else if (t > 20 && t < 40)  ecg = 2000 * std::sin((t - 20) * 3.14159 / 20.0); // T波
        else if (t > 0 && t < 8)    ecg = 1000 * std::sin(t * 3.14159 / 8.0); // P波

        //加上基线随机噪声
        ecg += (rand() % 200 - 100);

        //模拟 SpO2 容积波 (基波+二次谐波)
        spo2 = 128 + 60 * std::sin(m_waveCounter * 0.1) + 20 * std::cos(m_waveCounter * 0.2);

        //模拟 RESP 呼吸波 (低频正弦)
        resp = 128 + 80 * std::sin(m_waveCounter * 0.03);
    }

    //打包 Payload (Type = 0x01)
    QByteArray payload;
    //ECG 是 qint16，小端模式：低位在前，高位在后
    payload.append(static_cast<char>(ecg & 0xFF));
    payload.append(static_cast<char>((ecg >> 8) & 0xFF));
    payload.append(static_cast<char>(spo2));
    payload.append(static_cast<char>(resp));
    payload.append(static_cast<char>(statusFlag));

    sendPacket(0x01, payload);

    m_waveCounter++;
}

// ==========================================
// 低频参数推流 (1Hz)
// ==========================================
void MainWindow::onParamTimerTimeout()
{
    quint8 hr = 0, spo2 = 0, resp = 0, sys = 0, dia = 0;
    quint16 temp = 0;

    //血压发送逻辑
    if (m_hasNIBPResult) {
        sys = m_sysResult;
        dia = m_diaResult;
        m_hasNIBPResult = false; //发送一次后立刻清除标记，下次再发就是 0
    }

    if (m_radioFlatline->isChecked()) {
        //归零状态，参数全为 0
    }
    else if (m_radioNormal->isChecked()) {
        //正常状态，微幅波动
        hr = 75 + (rand() % 4 - 2);
        spo2 = 98 + (rand() % 3);
        resp = 20 + (rand() % 2 - 1);
        temp = 365 + (rand() % 4 - 2); // 36.5℃
    }
    else {
        //异常状态，触发报警
        hr = 135 + (rand() % 5);       // 心动过速
        spo2 = 85 + (rand() % 3);      // 血氧过低
        resp = 35 + (rand() % 3);      // 呼吸急促
        temp = 395 + (rand() % 5);     // 39.5℃ 高烧
    }

    //打包 Payload (Type = 0x02)
    QByteArray payload;
    payload.append(static_cast<char>(hr));
    payload.append(static_cast<char>(spo2));
    payload.append(static_cast<char>(resp));
    //TEMP 是 quint16，小端模式
    payload.append(static_cast<char>(temp & 0xFF));
    payload.append(static_cast<char>((temp >> 8) & 0xFF));
    payload.append(static_cast<char>(sys));
    payload.append(static_cast<char>(dia));

    sendPacket(0x02, payload);

    //在界面上打印一下日志，证明在发数据
    QString log = QString("[%1] 发送参数 -> HR:%2 SpO2:%3")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
        .arg(hr).arg(spo2);
    m_logText->append(log);
}

void MainWindow::onSimulatorReadyRead()
{
    QByteArray data = m_serialPort->readAll();
    //简单判断是否包含指令包类型 0x03 (实际工程中也应用状态机解包，这里为了测试简写)
    if (data.contains(static_cast<char>(0x03))) {
        m_logText->append("收到上位机 [测压指令]，开始模拟袖带加压 3 秒...");
        m_nibpTimer->start(3000); //启动3秒定时器
    }
}

void MainWindow::onNIBPTimerTimeout()
{
    //3秒加压结束，生成一次血压结果
    m_sysResult = 120 + (rand() % 20 - 10);
    m_diaResult = 80 + (rand() % 10 - 5);
    m_hasNIBPResult = true; // 标记有新结果
    m_logText->append(QString("加压完成！生成血压值: %1/%2 mmHg").arg(m_sysResult).arg(m_diaResult));
}