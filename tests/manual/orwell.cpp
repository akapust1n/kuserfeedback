/*
    Copyright (C) 2016 Volker Krause <vkrause@kde.org>

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "orwell.h"
#include "ui_orwell.h"

#include <provider/core/provider.h>
#include <provider/core/surveyinfo.h>

#include <QApplication>
#include <QDesktopServices>
#include <QSettings>

static std::unique_ptr<UserFeedback::Provider> provider; // TODO make this nicer

Orwell::Orwell(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::Orwell)
{
    ui->setupUi(this);
    loadStats();

    connect(ui->version, &QLineEdit::textChanged, this, [this]() {
        QCoreApplication::setApplicationVersion(ui->version->text());
    });

    connect(ui->submitButton, &QPushButton::clicked, provider.get(), &UserFeedback::Provider::submit);
    connect(ui->overrideButton, &QPushButton::clicked, this, [this] (){
        writeStats();
        QMetaObject::invokeMethod(provider.get(), "load");
        loadStats();
    });

    connect(provider.get(), &UserFeedback::Provider::surveyAvailable, this, [](const UserFeedback::SurveyInfo &info) {
        QDesktopServices::openUrl(info.url());
        provider->setSurveyCompleted(info);
    });

    connect(ui->actionQuit, &QAction::triggered, qApp, &QCoreApplication::quit);
}

Orwell::~Orwell() = default;

void Orwell::loadStats()
{
    ui->version->setText(QCoreApplication::applicationVersion());

    QSettings settings;
    ui->startCount->setValue(settings.value(QStringLiteral("UserFeedback/ApplicationStartCount")).toInt());
    ui->runtime->setValue(settings.value(QStringLiteral("UserFeedback/ApplicationTime")).toInt());
    ui->surveys->setText(settings.value(QStringLiteral("UserFeedback/CompletedSurveys")).toStringList().join(QStringLiteral(", ")));
}

void Orwell::writeStats()
{
    QSettings settings;
    settings.setValue(QStringLiteral("UserFeedback/ApplicationStartCount"), ui->startCount->value());
    settings.setValue(QStringLiteral("UserFeedback/ApplicationTime"), ui->runtime->value());
    settings.setValue(QStringLiteral("UserFeedback/CompletedSurveys"), ui->surveys->text().split(QStringLiteral(", ")));
}


int main(int argc, char** argv)
{
    QCoreApplication::setApplicationName(QStringLiteral("orwell"));
    QCoreApplication::setOrganizationName(QStringLiteral("KDE"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("kde.org"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1984"));

    QApplication app(argc, argv);

    provider.reset(new UserFeedback::Provider);
    provider->setFeedbackServer(QUrl(QStringLiteral("https://feedback.volkerkrause.eu")));

    Orwell mainWindow;
    mainWindow.show();

    return app.exec();
}
