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

#ifndef USERFEEDBACK_APPLICATIONVERSIONSOURCE_H
#define USERFEEDBACK_APPLICATIONVERSIONSOURCE_H

#include "userfeedbackcore_export.h"
#include "abstractdatasource.h"

namespace UserFeedback {

class USERFEEDBACKCORE_EXPORT ApplicationVersionSource : public AbstractDataSource
{
public:
    ApplicationVersionSource();

    QVariant data() Q_DECL_OVERRIDE;
};

}

#endif // USERFEEDBACK_APPLICATIONVERSIONSOURCE_H