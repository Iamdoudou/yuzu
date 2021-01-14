// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <sstream>
#include <QCloseEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStringListModel>
#include <QVBoxLayout>
#include "common/logging/log.h"
#include "core/settings.h"
#include "input_common/main.h"
#include "input_common/udp/client.h"
#include "input_common/udp/udp.h"
#include "ui_configure_motion_touch.h"
#include "yuzu/configuration/configure_motion_touch.h"
#include "yuzu/configuration/configure_touch_from_button.h"

CalibrationConfigurationDialog::CalibrationConfigurationDialog(QWidget* parent,
                                                               const std::string& host, u16 port,
                                                               u8 pad_index, u16 client_id)
    : QDialog(parent) {
    layout = new QVBoxLayout;
    status_label = new QLabel(tr("Communicating with the server..."));
    cancel_button = new QPushButton(tr("Cancel"));
    connect(cancel_button, &QPushButton::clicked, this, [this] {
        if (!completed) {
            job->Stop();
        }
        accept();
    });
    layout->addWidget(status_label);
    layout->addWidget(cancel_button);
    setLayout(layout);

    using namespace InputCommon::CemuhookUDP;
    job = std::make_unique<CalibrationConfigurationJob>(
        host, port, pad_index, client_id,
        [this](CalibrationConfigurationJob::Status status) {
            QString text;
            switch (status) {
            case CalibrationConfigurationJob::Status::Ready:
                text = tr("Touch the top left corner <br>of your touchpad.");
                break;
            case CalibrationConfigurationJob::Status::Stage1Completed:
                text = tr("Now touch the bottom right corner <br>of your touchpad.");
                break;
            case CalibrationConfigurationJob::Status::Completed:
                text = tr("Configuration completed!");
                break;
            default:
                break;
            }
            QMetaObject::invokeMethod(this, "UpdateLabelText", Q_ARG(QString, text));
            if (status == CalibrationConfigurationJob::Status::Completed) {
                QMetaObject::invokeMethod(this, "UpdateButtonText", Q_ARG(QString, tr("OK")));
            }
        },
        [this](u16 min_x_, u16 min_y_, u16 max_x_, u16 max_y_) {
            completed = true;
            min_x = min_x_;
            min_y = min_y_;
            max_x = max_x_;
            max_y = max_y_;
        });
}

CalibrationConfigurationDialog::~CalibrationConfigurationDialog() = default;

void CalibrationConfigurationDialog::UpdateLabelText(const QString& text) {
    status_label->setText(text);
}

void CalibrationConfigurationDialog::UpdateButtonText(const QString& text) {
    cancel_button->setText(text);
}

constexpr std::array<std::pair<const char*, const char*>, 2> TouchProviders = {{
    {"emu_window", QT_TRANSLATE_NOOP("ConfigureMotionTouch", "Emulator Window")},
    {"cemuhookudp", QT_TRANSLATE_NOOP("ConfigureMotionTouch", "CemuhookUDP")},
}};

ConfigureMotionTouch::ConfigureMotionTouch(QWidget* parent,
                                           InputCommon::InputSubsystem* input_subsystem_)
    : QDialog(parent), input_subsystem{input_subsystem_},
      ui(std::make_unique<Ui::ConfigureMotionTouch>()) {
    ui->setupUi(this);
    for (const auto& [provider, name] : TouchProviders) {
        ui->touch_provider->addItem(tr(name), QString::fromUtf8(provider));
    }

    ui->udp_learn_more->setOpenExternalLinks(true);
    ui->udp_learn_more->setText(
        tr("<a "
           "href='https://yuzu-emu.org/wiki/"
           "using-a-controller-or-android-phone-for-motion-or-touch-input'><span "
           "style=\"text-decoration: underline; color:#039be5;\">Learn More</span></a>"));

    SetConfiguration();
    UpdateUiDisplay();
    ConnectEvents();
}

ConfigureMotionTouch::~ConfigureMotionTouch() = default;

void ConfigureMotionTouch::SetConfiguration() {
    const Common::ParamPackage motion_param(Settings::values.motion_device);
    const Common::ParamPackage touch_param(Settings::values.touch_device);
    const std::string motion_engine = motion_param.Get("engine", "motion_emu");
    const std::string touch_engine = touch_param.Get("engine", "emu_window");

    ui->touch_provider->setCurrentIndex(
        ui->touch_provider->findData(QString::fromStdString(touch_engine)));
    ui->touch_from_button_checkbox->setChecked(Settings::values.use_touch_from_button);
    touch_from_button_maps = Settings::values.touch_from_button_maps;
    for (const auto& touch_map : touch_from_button_maps) {
        ui->touch_from_button_map->addItem(QString::fromStdString(touch_map.name));
    }
    ui->touch_from_button_map->setCurrentIndex(Settings::values.touch_from_button_map_index);
    ui->motion_sensitivity->setValue(motion_param.Get("sensitivity", 0.01f));

    min_x = touch_param.Get("min_x", 100);
    min_y = touch_param.Get("min_y", 50);
    max_x = touch_param.Get("max_x", 1800);
    max_y = touch_param.Get("max_y", 850);

    ui->udp_server->setText(QString::fromStdString("127.0.0.1"));
    ui->udp_port->setText(QString::number(26760));

    udp_server_list_model = new QStringListModel(this);
    udp_server_list_model->setStringList({});
    ui->udp_server_list->setModel(udp_server_list_model);

    std::stringstream ss(Settings::values.udp_input_servers);
    std::string token;

    while (std::getline(ss, token, ',')) {
        const int row = udp_server_list_model->rowCount();
        udp_server_list_model->insertRows(row, 1);
        const QModelIndex index = udp_server_list_model->index(row);
        udp_server_list_model->setData(index, QString::fromStdString(token));
    }
}

void ConfigureMotionTouch::UpdateUiDisplay() {
    const QString touch_engine = ui->touch_provider->currentData().toString();
    const QString cemuhook_udp = QStringLiteral("cemuhookudp");

    ui->motion_sensitivity_label->setVisible(true);
    ui->motion_sensitivity->setVisible(true);

    if (touch_engine == cemuhook_udp) {
        ui->touch_calibration->setVisible(true);
        ui->touch_calibration_config->setVisible(true);
        ui->touch_calibration_label->setVisible(true);
        ui->touch_calibration->setText(
            QStringLiteral("(%1, %2) - (%3, %4)").arg(min_x).arg(min_y).arg(max_x).arg(max_y));
    } else {
        ui->touch_calibration->setVisible(false);
        ui->touch_calibration_config->setVisible(false);
        ui->touch_calibration_label->setVisible(false);
    }

    ui->udp_config_group_box->setVisible(true);
}

void ConfigureMotionTouch::ConnectEvents() {
    connect(ui->touch_provider, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) { UpdateUiDisplay(); });
    connect(ui->udp_test, &QPushButton::clicked, this, &ConfigureMotionTouch::OnCemuhookUDPTest);
    connect(ui->udp_add, &QPushButton::clicked, this, &ConfigureMotionTouch::OnUDPAddServer);
    connect(ui->udp_remove, &QPushButton::clicked, this, &ConfigureMotionTouch::OnUDPDeleteServer);
    connect(ui->touch_calibration_config, &QPushButton::clicked, this,
            &ConfigureMotionTouch::OnConfigureTouchCalibration);
    connect(ui->touch_from_button_config_btn, &QPushButton::clicked, this,
            &ConfigureMotionTouch::OnConfigureTouchFromButton);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, [this] {
        if (CanCloseDialog()) {
            reject();
        }
    });
}

void ConfigureMotionTouch::OnUDPAddServer() {
    // Validator for IP address
    QRegExp re(QStringLiteral(
        R"re(^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)re"));
    bool ok;
    QString port_text = ui->udp_port->text();
    QString server_text = ui->udp_server->text();
    const QString server_string = tr("%1:%2").arg(server_text, port_text);
    int port_number = port_text.toInt(&ok, 10);
    int row = udp_server_list_model->rowCount();

    if (!ok) {
        QMessageBox::warning(this, tr("yuzu"), tr("Port number has invalid characters"));
        return;
    }
    if (port_number < 0 || port_number > 65353) {
        QMessageBox::warning(this, tr("yuzu"), tr("Port has to be in range 0 and 65353"));
        return;
    }
    if (!re.exactMatch(server_text)) {
        QMessageBox::warning(this, tr("yuzu"), tr("IP address is not valid"));
        return;
    }
    // Search for duplicates
    for (const auto& item : udp_server_list_model->stringList()) {
        if (item == server_string) {
            QMessageBox::warning(this, tr("yuzu"), tr("This UDP server already exists"));
            return;
        }
    }
    // Limit server count to 8
    if (row == 8) {
        QMessageBox::warning(this, tr("yuzu"), tr("Unable to add more than 8 servers"));
        return;
    }

    udp_server_list_model->insertRows(row, 1);
    QModelIndex index = udp_server_list_model->index(row);
    udp_server_list_model->setData(index, server_string);
    ui->udp_server_list->setCurrentIndex(index);
}

void ConfigureMotionTouch::OnUDPDeleteServer() {
    udp_server_list_model->removeRows(ui->udp_server_list->currentIndex().row(), 1);
}

void ConfigureMotionTouch::OnCemuhookUDPTest() {
    ui->udp_test->setEnabled(false);
    ui->udp_test->setText(tr("Testing"));
    udp_test_in_progress = true;
    InputCommon::CemuhookUDP::TestCommunication(
        ui->udp_server->text().toStdString(), static_cast<u16>(ui->udp_port->text().toInt()), 0,
        24872,
        [this] {
            LOG_INFO(Frontend, "UDP input test success");
            QMetaObject::invokeMethod(this, "ShowUDPTestResult", Q_ARG(bool, true));
        },
        [this] {
            LOG_ERROR(Frontend, "UDP input test failed");
            QMetaObject::invokeMethod(this, "ShowUDPTestResult", Q_ARG(bool, false));
        });
}

void ConfigureMotionTouch::OnConfigureTouchCalibration() {
    ui->touch_calibration_config->setEnabled(false);
    ui->touch_calibration_config->setText(tr("Configuring"));
    CalibrationConfigurationDialog dialog(this, ui->udp_server->text().toStdString(),
                                          static_cast<u16>(ui->udp_port->text().toUInt()), 0,
                                          24872);
    dialog.exec();
    if (dialog.completed) {
        min_x = dialog.min_x;
        min_y = dialog.min_y;
        max_x = dialog.max_x;
        max_y = dialog.max_y;
        LOG_INFO(Frontend,
                 "UDP touchpad calibration config success: min_x={}, min_y={}, max_x={}, max_y={}",
                 min_x, min_y, max_x, max_y);
        UpdateUiDisplay();
    } else {
        LOG_ERROR(Frontend, "UDP touchpad calibration config failed");
    }
    ui->touch_calibration_config->setEnabled(true);
    ui->touch_calibration_config->setText(tr("Configure"));
}

void ConfigureMotionTouch::closeEvent(QCloseEvent* event) {
    if (CanCloseDialog()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void ConfigureMotionTouch::ShowUDPTestResult(bool result) {
    udp_test_in_progress = false;
    if (result) {
        QMessageBox::information(this, tr("Test Successful"),
                                 tr("Successfully received data from the server."));
    } else {
        QMessageBox::warning(this, tr("Test Failed"),
                             tr("Could not receive valid data from the server.<br>Please verify "
                                "that the server is set up correctly and "
                                "the address and port are correct."));
    }
    ui->udp_test->setEnabled(true);
    ui->udp_test->setText(tr("Test"));
}

void ConfigureMotionTouch::OnConfigureTouchFromButton() {
    ConfigureTouchFromButton dialog{this, touch_from_button_maps, input_subsystem,
                                    ui->touch_from_button_map->currentIndex()};
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    touch_from_button_maps = dialog.GetMaps();

    while (ui->touch_from_button_map->count() > 0) {
        ui->touch_from_button_map->removeItem(0);
    }
    for (const auto& touch_map : touch_from_button_maps) {
        ui->touch_from_button_map->addItem(QString::fromStdString(touch_map.name));
    }
    ui->touch_from_button_map->setCurrentIndex(dialog.GetSelectedIndex());
}

bool ConfigureMotionTouch::CanCloseDialog() {
    if (udp_test_in_progress) {
        QMessageBox::warning(this, tr("yuzu"),
                             tr("UDP Test or calibration configuration is in progress.<br>Please "
                                "wait for them to finish."));
        return false;
    }
    return true;
}

void ConfigureMotionTouch::ApplyConfiguration() {
    if (!CanCloseDialog()) {
        return;
    }

    std::string touch_engine = ui->touch_provider->currentData().toString().toStdString();

    Common::ParamPackage touch_param{};
    if (touch_engine == "cemuhookudp") {
        touch_param.Set("min_x", min_x);
        touch_param.Set("min_y", min_y);
        touch_param.Set("max_x", max_x);
        touch_param.Set("max_y", max_y);
    }
    touch_param.Set("engine", std::move(touch_engine));

    Settings::values.touch_device = touch_param.Serialize();
    Settings::values.use_touch_from_button = ui->touch_from_button_checkbox->isChecked();
    Settings::values.touch_from_button_map_index = ui->touch_from_button_map->currentIndex();
    Settings::values.touch_from_button_maps = touch_from_button_maps;
    Settings::values.udp_input_servers = GetUDPServerString();
    input_subsystem->ReloadInputDevices();

    accept();
}

std::string ConfigureMotionTouch::GetUDPServerString() const {
    QString input_servers;

    for (const auto& item : udp_server_list_model->stringList()) {
        input_servers += item;
        input_servers += QLatin1Char{','};
    }

    // Remove last comma
    input_servers.chop(1);
    return input_servers.toStdString();
}
