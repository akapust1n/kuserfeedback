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

#include "ratiosetaggregationmodel.h"
#include <model/timeaggregationmodel.h>
#include <core/sample.h>

#include <QSet>

using namespace UserFeedback::Analyzer;

RatioSetAggregationModel::RatioSetAggregationModel(QObject *parent) :
    QAbstractTableModel(parent)
{
}

RatioSetAggregationModel::~RatioSetAggregationModel()
{
    delete[] m_data;
}

void RatioSetAggregationModel::setSourceModel(QAbstractItemModel* model)
{
    Q_ASSERT(model);
    m_sourceModel = model;
    connect(model, &QAbstractItemModel::modelReset, this, &RatioSetAggregationModel::recompute);
    recompute();
}

void RatioSetAggregationModel::setAggregationValue(const QString& aggrValue)
{
    m_aggrValue = aggrValue;
    recompute();
}

int RatioSetAggregationModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return m_categories.size() + 1;
}

int RatioSetAggregationModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !m_sourceModel)
        return 0;
    return m_sourceModel->rowCount();
}

QVariant RatioSetAggregationModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_sourceModel)
        return {};

    if (role == TimeAggregationModel::MaximumValueRole)
        return m_maxValue;

    if (index.column() == 0) {
        const auto srcIdx = m_sourceModel->index(index.row(), 0);
        return m_sourceModel->data(srcIdx, role);
    }

    const auto idx = index.row() * m_categories.size() + index.column() - 1;
    if (role == Qt::DisplayRole) {
        return m_data[idx];
    } else if (role == TimeAggregationModel::DataDisplayRole) {
        if (index.column() == 1)
            return m_data[idx];
        return m_data[idx] - m_data[idx - 1];
    }

    return {};
}

QVariant RatioSetAggregationModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && m_sourceModel) {
        if (section == 0)
            return m_sourceModel->headerData(section, orientation, role);
        if (role == Qt::DisplayRole) {
            const auto cat = m_categories.at(section - 1);
            if (cat.isEmpty())
                return tr("<empty>");
            return cat;
        }
    }

    return QAbstractTableModel::headerData(section, orientation, role);
}

void RatioSetAggregationModel::recompute()
{
    if (!m_sourceModel)
        return;

    const auto rowCount = m_sourceModel->rowCount();
    beginResetModel();
    m_categories.clear();
    delete[] m_data;
    m_data = nullptr;
    m_maxValue = 1.0;

    if (rowCount <= 0 || m_aggrValue.isEmpty()) {
        endResetModel();
        return;
    }

    QSet<QString> categories;
    const auto allSamples = m_sourceModel->index(0, 0).data(TimeAggregationModel::AllSamplesRole).value<QVector<Sample>>();
    foreach (const auto &s, allSamples) {
        const auto rs = s.value(m_aggrValue).value<QVariantMap>();
        for (auto it = rs.begin(); it != rs.end(); ++it)
            categories.insert(it.key());
    }
    m_categories.reserve(categories.size());
    foreach (const auto &cat, categories)
        m_categories.push_back(cat);
    std::sort(m_categories.begin(), m_categories.end());
    const auto colCount = m_categories.size();

    // compute the counts per cell, we could do that on demand, but we need the maximum for QtCharts...
    m_data = new double[colCount * rowCount];
    memset(m_data, 0, sizeof(double) * colCount * rowCount);
    for (int row = 0; row < rowCount; ++row) {
        const auto samples = m_sourceModel->index(row, 0).data(TimeAggregationModel::SamplesRole).value<QVector<Sample>>();
        if (samples.isEmpty())
            continue; // avoid division by zero below
        foreach (const auto &sample, samples) {
            const auto rs = sample.value(m_aggrValue).value<QVariantMap>();
            for (auto it = rs.begin(); it != rs.end(); ++it) {
                const auto catIt = std::lower_bound(m_categories.constBegin(), m_categories.constEnd(), it.key());
                Q_ASSERT(catIt != m_categories.constEnd());
                const auto idx = colCount * row + std::distance(m_categories.constBegin(), catIt);
                m_data[idx] += it.value().toMap().value(QLatin1String("property")).toDouble(); // TODO: make this configurable
            }
        }

        // normalize to 1 and accumulate per row for stacked plots
        const double scale = samples.size();
        for (int col = 0; col < colCount; ++col) {
            const auto idx = colCount * row + col;
            m_data[idx] /= scale;
            if (col > 0)
                m_data[idx] += m_data[idx - 1];
        }

        m_maxValue = std::max(m_maxValue, m_data[row * colCount + colCount - 1]);
    }

    endResetModel();
}

