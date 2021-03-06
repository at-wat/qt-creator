/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "qmlprofilercanvas.h"

#include "qdeclarativecontext2d_p.h"

#include <qpixmap.h>
#include <qpainter.h>

namespace QmlProfiler {
namespace Internal {

QmlProfilerCanvas::QmlProfilerCanvas()
    : m_context2d(new Context2D(this))
{
    setAcceptedMouseButtons(Qt::LeftButton);
    m_drawTimer.setSingleShot(true);
    connect(&m_drawTimer, SIGNAL(timeout()), this, SLOT(draw()));

    m_drawTimer.start();
}

void QmlProfilerCanvas::requestPaint()
{
    if (m_context2d->size().width() != width()
            || m_context2d->size().height() != height()) {
        m_drawTimer.start();
    } else {
        update();
    }
}

void QmlProfilerCanvas::requestRedraw()
{
    m_drawTimer.start();
}

// called from GUI thread. Draws into m_context2d.
void QmlProfilerCanvas::draw()
{
    QMutexLocker lock(&m_pixmapMutex);
    m_context2d->reset();
    m_context2d->setSize(width(), height());

    if (width() > 0 && height() > 0)
        emit drawRegion(m_context2d, QRect(0, 0, width(), height()));
    update();
}

// called from OpenGL thread. Renders m_context2d into OpenGL buffer.
void QmlProfilerCanvas::paint(QPainter *p)
{
    QMutexLocker lock(&m_pixmapMutex);
    p->drawPixmap(0, 0, m_context2d->pixmap());
}

void QmlProfilerCanvas::componentComplete()
{
    const QMetaObject *metaObject = this->metaObject();
    int propertyCount = metaObject->propertyCount();
    int requestPaintMethod = metaObject->indexOfMethod("requestPaint()");
    for (int ii = QmlProfilerCanvas::staticMetaObject.propertyCount(); ii < propertyCount; ++ii) {
        QMetaProperty p = metaObject->property(ii);
        if (p.hasNotifySignal())
            QMetaObject::connect(this, p.notifySignalIndex(), this, requestPaintMethod, 0, 0);
    }
    QQuickItem::componentComplete();
    requestRedraw();
}

void QmlProfilerCanvas::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChanged(newGeometry, oldGeometry);
    requestRedraw();
}

}
}
