#pragma once

#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QComboBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTextEdit>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onOpenCloseButtonClicked();
    void onWaveTimerTimeout();
    void onParamTimerTimeout();

private:
    void setupUI();
    void sendPacket(quint8 type, const QByteArray& payload);

    // UI 控件
    QComboBox* m_portComboBox;
    QPushButton* m_openCloseBtn;
    QRadioButton* m_radioNormal;
    QRadioButton* m_radioAbnormal;
    QRadioButton* m_radioFlatline;
    QTextEdit* m_logText;

    //核心对象
    QSerialPort* m_serialPort;
    QTimer* m_waveTimer;  //10ms (100Hz) 波形推流
    QTimer* m_paramTimer; //1000ms (1Hz) 参数推流

    //状态生成变量
    quint64 m_waveCounter;

    QTimer* m_nibpTimer;      //模拟血压加压的3秒定时器
    bool m_hasNIBPResult;     //标记是否产生了新的血压结果
    quint8 m_sysResult;       //临时存储生成的高压
    quint8 m_diaResult;       //临时存储生成的低压

    void onSimulatorReadyRead(); //接收上位机指令
    void onNIBPTimerTimeout();   //3秒加压结束触发
};