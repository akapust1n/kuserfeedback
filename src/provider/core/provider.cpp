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

#include <config-userfeedback-version.h>

#include "logging_p.h"
#include "provider.h"
#include "provider_p.h"
#include "abstractdatasource.h"
#include "startcountsource.h"
#include "surveyinfo.h"
#include "usagetimesource.h"

#include <common/surveytargetexpressionparser.h>
#include <common/surveytargetexpressionevaluator.h>

#include <QCoreApplication>
#include <QDebug>
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#endif
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

#include <algorithm>
#include <numeric>

namespace UserFeedback {
#if QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)
Q_LOGGING_CATEGORY(Log, "org.kde.UserFeedback", QtInfoMsg)
#elif QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)
Q_LOGGING_CATEGORY(Log, "org.kde.UserFeedback")
#endif
}

using namespace UserFeedback;

ProviderPrivate::ProviderPrivate(Provider *qq)
    : q(qq)
    , networkAccessManager(nullptr)
    , submissionInterval(-1)
    , statisticsMode(Provider::NoStatistics)
    , surveyInterval(-1)
    , startCount(0)
    , usageTime(0)
    , encouragementStarts(-1)
    , encouragementTime(-1)
    , encouragementDelay(300)
    , encouragementInterval(-1)
{
    auto domain = QCoreApplication::organizationDomain().split(QLatin1Char('.'));
    std::reverse(domain.begin(), domain.end());
    productId = domain.join(QLatin1String("."));
    if (!productId.isEmpty())
        productId += QLatin1Char('.');
    productId += QCoreApplication::applicationName();

    submissionTimer.setSingleShot(true);
    QObject::connect(&submissionTimer, SIGNAL(timeout()), q, SLOT(submit()));

    startTime.start();

    encouragementTimer.setSingleShot(true);
    QObject::connect(&encouragementTimer, SIGNAL(timeout()), q, SLOT(emitShowEncouragementMessage()));
}

ProviderPrivate::~ProviderPrivate()
{
    qDeleteAll(dataSources);
}

void ProviderPrivate::reset()
{
    startCount = 0;
    usageTime = 0;
    startTime.start();
}

int ProviderPrivate::currentApplicationTime() const
{
    return usageTime + (startTime.elapsed() / 1000);
}

static QMetaEnum statisticsCollectionModeEnum()
{
    const auto idx = Provider::staticMetaObject.indexOfEnumerator("StatisticsCollectionMode");
    Q_ASSERT(idx >= 0);
    return Provider::staticMetaObject.enumerator(idx);
}

std::unique_ptr<QSettings> ProviderPrivate::makeSettings() const
{
    std::unique_ptr<QSettings> s(new QSettings(QStringLiteral("KDE"), QStringLiteral("UserFeedback.") + productId));
    return s;
}

void ProviderPrivate::load()
{
    auto s = makeSettings();
    s->beginGroup(QStringLiteral("UserFeedback"));
    lastSubmitTime = s->value(QStringLiteral("LastSubmission")).toDateTime();

    const auto modeStr = s->value(QStringLiteral("StatisticsCollectionMode")).toByteArray();
    statisticsMode = static_cast<Provider::StatisticsCollectionMode>(std::max(statisticsCollectionModeEnum().keyToValue(modeStr), 0));

    surveyInterval = s->value(QStringLiteral("SurveyInterval"), -1).toInt();
    lastSurveyTime = s->value(QStringLiteral("LastSurvey")).toDateTime();
    completedSurveys = s->value(QStringLiteral("CompletedSurveys"), QStringList()).toStringList();

    startCount = std::max(s->value(QStringLiteral("ApplicationStartCount"), 0).toInt() + 1, 1);
    usageTime = std::max(s->value(QStringLiteral("ApplicationTime"), 0).toInt(), 0);

    lastEncouragementTime = s->value(QStringLiteral("LastEncouragement")).toDateTime();
}

void ProviderPrivate::store()
{
    auto s = makeSettings();
    s->beginGroup(QStringLiteral("UserFeedback"));
    s->setValue(QStringLiteral("LastSubmission"), lastSubmitTime);
    s->setValue(QStringLiteral("StatisticsCollectionMode"), QString::fromLatin1(statisticsCollectionModeEnum().valueToKey(statisticsMode)));

    s->setValue(QStringLiteral("SurveyInterval"), surveyInterval);
    s->setValue(QStringLiteral("LastSurvey"), lastSurveyTime);
    s->setValue(QStringLiteral("CompletedSurveys"), completedSurveys);

    s->setValue(QStringLiteral("ApplicationStartCount"), startCount);
    s->setValue(QStringLiteral("ApplicationTime"), currentApplicationTime());

    s->setValue(QStringLiteral("LastEncouragement"), lastEncouragementTime);

    foreach (auto source, dataSources)
        source->store(s.get());
}

void ProviderPrivate::aboutToQuit()
{
    qCDebug(Log) << Q_FUNC_INFO;
    store();
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
QByteArray variantMapToJson(const QVariantMap &m)
{
    QByteArray b = "{";
    for (auto it = m.begin(); it != m.end(); ++it) {
        b += " \"" + it.key().toUtf8() + "\": ";
        switch (it.value().type()) {
            case QVariant::String:
                b += '"' + it.value().toString().toUtf8() + '"';
                break;
            case QVariant::Int:
                b += QByteArray::number(it.value().toInt());
                break;
            case QVariant::Double:
                b += QByteArray::number(it.value().toDouble());
                break;
            case QVariant::Map:
                b += variantMapToJson(it.value().toMap());
                break;
            default:
                break;
        }
        if (std::distance(it, m.end()) != 1)
            b += ",\n";
    }
    b += " }";
    return b;
}
#endif

QByteArray ProviderPrivate::jsonData(Provider::StatisticsCollectionMode mode) const
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    QJsonObject obj;
    if (mode != Provider::NoStatistics) {
        foreach (auto source, dataSources) {
            if (mode < source->collectionMode())
                continue;
            const auto data = source->data();
            if (data.canConvert<QVariantMap>())
                obj.insert(source->name(), QJsonObject::fromVariantMap(data.toMap()));
            else if (data.canConvert<QVariantList>())
                obj.insert(source->name(), QJsonArray::fromVariantList(data.value<QVariantList>()));
        }
    }

    QJsonDocument doc(obj);
    return doc.toJson();
#else
    QByteArray b = "{";
    if (mode != Provider::NoStatistics) {
        for (auto it = dataSources.begin(); it != dataSources.end(); ++it) {
            if (mode < (*it)->collectionMode())
                continue;
            const auto data = (*it)->data();
            if (data.canConvert<QVariantList>()) {
                const auto l = data.value<QVariantList>();
                b += " \"" + (*it)->name().toUtf8() + "\": [ ";
                for (auto it2 = l.begin(); it2 != l.end(); ++it2) {
                    b += variantMapToJson((*it2).toMap());
                    if (std::distance(it2, l.end()) != 1)
                        b += ", ";
                }
                b += " ]";
            } else {
                b += " \"" + (*it)->name().toUtf8() + "\": " + variantMapToJson(data.toMap());
            }
            if (std::distance(it, dataSources.end()) != 1)
                b += ",\n";
        }
    }
    b += '}';
    return b;
#endif
}

void ProviderPrivate::scheduleNextSubmission()
{
    submissionTimer.stop();
    if (submissionInterval <= 0 || (statisticsMode == Provider::NoStatistics && surveyInterval < 0))
        return;

    Q_ASSERT(submissionInterval > 0);

    const auto nextSubmission = lastSubmitTime.addDays(submissionInterval);
    const auto now = QDateTime::currentDateTime();
    submissionTimer.start(std::max(0ll, now.msecsTo(nextSubmission)));
}

void ProviderPrivate::submitFinished()
{
    auto reply = qobject_cast<QNetworkReply*>(q->sender());
    Q_ASSERT(reply);

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(Log) << "failed to submit user feedback:" << reply->errorString() << reply->readAll();
        return;
    }

    lastSubmitTime = QDateTime::currentDateTime();

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
    const auto it = obj.find(QLatin1String("surveys"));
    if (it != obj.end() && surveyInterval >= 0) {
        const auto a = it.value().toArray();
        foreach(const auto &s, a) {
            const auto survey = SurveyInfo::fromJson(s.toObject());
            if (selectSurvey(survey))
                break;
        }
    }
#endif

    scheduleNextSubmission();
}

QVariant ProviderPrivate::sourceData(const QString& sourceName) const
{
    foreach (auto src, dataSources) {
        if (src->name() == sourceName)
            return src->data();
    }
    return QVariant();
}

bool ProviderPrivate::selectSurvey(const SurveyInfo &survey) const
{
    qCDebug(Log) << "got survey:" << survey.url() << survey.target();
    if (!survey.isValid() || completedSurveys.contains(QString::number(survey.id())))
        return false;

    if (lastSurveyTime.addDays(surveyInterval) > QDateTime::currentDateTime())
        return false;

    if (!survey.target().isEmpty()) {
        SurveyTargetExpressionParser parser;
        if (!parser.parse(survey.target())) {
            qCDebug(Log) << "failed to parse target expression";
            return false;
        }

        SurveyTargetExpressionEvaluator eval;
        eval.setDataProvider(this);
        if (!eval.evaluate(parser.expression()))
            return false;
    }

    emit q->surveyAvailable(survey);
    return true;
}

Provider::StatisticsCollectionMode ProviderPrivate::highestStatisticsCollectionMode() const
{
    auto mode = Provider::NoStatistics;
    foreach (auto src, dataSources)
        mode = std::max(mode, src->collectionMode());
    return mode;
}

void ProviderPrivate::scheduleEncouragement()
{
    encouragementTimer.stop();

    // already done, not repetition
    if (lastEncouragementTime.isValid() && encouragementInterval <= 0)
        return;

    if (encouragementStarts < 0 && encouragementTime < 0) // encouragement disabled
        return;

    if (encouragementStarts > startCount) // we need more starts
        return;

    if (statisticsMode >= highestStatisticsCollectionMode() && surveyInterval == 0) // already everything enabled
        return;
    // no repetition if some feedback is enabled
    if (lastEncouragementTime.isValid() && (statisticsMode > Provider::NoStatistics || surveyInterval >= 0))
        return;

    Q_ASSERT(encouragementDelay >= 0);
    int timeToEncouragement = encouragementDelay;
    if (encouragementTime > 0)
        timeToEncouragement = std::max(timeToEncouragement, (encouragementTime * 60) - currentApplicationTime());
    if (lastEncouragementTime.isValid()) {
        Q_ASSERT(encouragementInterval > 0);
        const auto targetTime = lastEncouragementTime.addDays(encouragementDelay);
        timeToEncouragement = std::max(timeToEncouragement, (int)QDateTime::currentDateTime().secsTo(targetTime));
    }
    encouragementTimer.start(timeToEncouragement * 1000);
}

void ProviderPrivate::emitShowEncouragementMessage()
{
    lastEncouragementTime = QDateTime::currentDateTime(); // TODO make this explicit, in case the host application decides to delay?
    emit q->showEncouragementMessage();
    store();
}


Provider::Provider(QObject *parent) :
    QObject(parent),
    d(new ProviderPrivate(this))
{
    qCDebug(Log);

    connect(QCoreApplication::instance(), SIGNAL(aboutToQuit()), this, SLOT(aboutToQuit()));

    d->load();
}

Provider::~Provider()
{
    delete d;
}

void Provider::setProductIdentifier(const QString &productId)
{
    Q_ASSERT(!productId.isEmpty());
    d->productId = productId;
}

void Provider::setFeedbackServer(const QUrl &url)
{
    d->serverUrl = url;
}

void Provider::setSubmissionInterval(int days)
{
    d->submissionInterval = days;
    d->scheduleNextSubmission();
}

Provider::StatisticsCollectionMode Provider::statisticsCollectionMode() const
{
    return d->statisticsMode;
}

void Provider::setStatisticsCollectionMode(StatisticsCollectionMode mode)
{
    if (d->statisticsMode == mode)
        return;

    d->statisticsMode = mode;
    d->scheduleNextSubmission();
    d->scheduleEncouragement();
    d->store();
    emit statisticsCollectionModeChanged();
}

void Provider::addDataSource(AbstractDataSource *source, StatisticsCollectionMode mode)
{
    // sanity-check sources
    if (mode == NoStatistics) {
        qCritical() << "Source" << source->name() << "attempts to report data unconditionally, ignoring!";
        delete source;
        return;
    }
    if (source->description().isEmpty()) {
        qCritical() << "Source" << source->name() << "has no description, ignoring!";
        delete source;
        return;
    }

    Q_ASSERT(mode != NoStatistics);
    Q_ASSERT(!source->description().isEmpty());
    source->setCollectionMode(mode);

    // special cases for sources where we track the data here, as it's needed even if we don't report it
    if (auto countSrc = dynamic_cast<StartCountSource*>(source))
        countSrc->setProvider(d);
    if (auto timeSrc = dynamic_cast<UsageTimeSource*>(source))
        timeSrc->setProvider(d);

    d->dataSources.push_back(source);

    auto s = d->makeSettings();
    s->beginGroup(QStringLiteral("UserFeedback"));
    source->load(s.get());
}

QVector<AbstractDataSource*> Provider::dataSources() const
{
    return d->dataSources;
}

int Provider::surveyInterval() const
{
    return d->surveyInterval;
}

void Provider::setSurveyInterval(int days)
{
    if (d->surveyInterval == days)
        return;

    d->surveyInterval = days;
    d->scheduleNextSubmission();
    d->scheduleEncouragement();
    d->store();
    emit surveyIntervalChanged();
}

void Provider::setApplicationStartsUntilEncouragement(int starts)
{
    d->encouragementStarts = starts;
    d->scheduleEncouragement();
}

void Provider::setApplicationUsageTimeUntilEncouragement(int minutes)
{
    d->encouragementTime = minutes;
    d->scheduleEncouragement();
}

void Provider::setEncouragementDelay(int secs)
{
    d->encouragementDelay = std::max(0, secs);
    d->scheduleEncouragement();
}

void Provider::setEncouragementInterval(int days)
{
    d->encouragementInterval = days;
    d->scheduleEncouragement();
}

void Provider::setSurveyCompleted(const SurveyInfo &info)
{
    d->completedSurveys.push_back(QString::number(info.id()));
    d->lastSurveyTime = QDateTime::currentDateTime();
    d->store();
}

void Provider::submit()
{
    if (d->productId.isEmpty()) {
        qCWarning(Log) << "No productId specified!";
        return;
    }
    if (!d->serverUrl.isValid()) {
        qCWarning(Log) << "No feedback server URL specified!";
        return;
    }

    if (!d->networkAccessManager)
        d->networkAccessManager = new QNetworkAccessManager(this);

    auto url = d->serverUrl;
    url.setPath(url.path() + QStringLiteral("/receiver/submit/") + d->productId);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("UserFeedback/") + QStringLiteral(USERFEEDBACK_VERSION));
#endif
    auto reply = d->networkAccessManager->post(request, d->jsonData(d->statisticsMode));
    connect(reply, SIGNAL(finished()), this, SLOT(submitFinished()));
}

#include "moc_provider.cpp"
