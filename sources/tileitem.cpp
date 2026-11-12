/*

Copyright 2012-2014 Adam Reichold

This file is part of qpdfview.

qpdfview is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

qpdfview is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with qpdfview.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "tileitem.h"

#include <QIODevice>
#include <QPainter>

#include "settings.h"
#include "pageitem.h"

namespace qpdfview
{

namespace
{

inline int cacheCost(const QPixmap& pixmap)
{
    return qMax(1, pixmap.width() * pixmap.height() * pixmap.depth() / 8 / 1024);
}

} // anonymous

Settings* TileItem::s_settings = 0;

QCache< TileItem::CacheKey, TileItem::CacheObject > TileItem::s_cache;

TileItem::TileItem(PageItem* page) : RenderTaskParent(),
    m_page(page),
    m_rect(),
    m_cropRect(),
    m_pixmapError(false),
    m_pixmap(),
    m_obsoletePixmap(),
    m_deleteAfterRender(false),
    m_renderTask(m_page->m_page, this)
{
    if(s_settings == 0)
    {
        s_settings = Settings::instance();
    }

    s_cache.setMaxCost(s_settings->pageItem().cacheSize());
}

TileItem::~TileItem()
{
    m_renderTask.cancel(true);
    m_renderTask.wait();
}

void TileItem::setCropRect(const QRectF& cropRect)
{
    if(!m_page->m_renderParam.trimMargins())
    {
        return;
    }

    if(m_cropRect.isNull() && !cropRect.isNull())
    {
        m_cropRect = cropRect;

        m_page->updateCropRect();
    }
}

void TileItem::dropCachedPixmaps(PageItem* page)
{
    foreach(const CacheKey& key, s_cache.keys())
    {
        if(key.first == page)
        {
            s_cache.remove(key);
        }
    }
}

bool TileItem::paint(QPainter* painter, QPointF topLeft)
{
    const QPixmap& pixmap = takePixmap();

    if(!pixmap.isNull())
    {
        // pixmap

        painter->drawPixmap(m_rect.topLeft() + topLeft, pixmap);

        return true;
    }
    else if(!m_obsoletePixmap.isNull())
    {
        // obsolete pixmap

        painter->drawPixmap(QRectF(m_rect).translated(topLeft), m_obsoletePixmap, QRectF());

        return false;
    }
    else if(!m_pixmapError && m_page->m_previewItem != 0 && m_page->m_previewItem->paint(painter, topLeft, m_rect))
    {
        // blurry preview until the tile is rendered

        return false;
    }
    else
    {
        const qreal iconExtent = qMin(0.1 * m_rect.width(), 0.1 * m_rect.height());
        const QRect iconRect(topLeft.x() + m_rect.left() + 0.01 * m_rect.width(),
                             topLeft.y() + m_rect.top() + 0.01 * m_rect.height(),
                             iconExtent, iconExtent);

        if(!m_pixmapError)
        {
            // progress icon

            s_settings->pageItem().progressIcon().paint(painter, iconRect);

            return false;
        }
        else
        {
            // error icon

            s_settings->pageItem().errorIcon().paint(painter, iconRect);

            return true;
        }
    }
}

void TileItem::refresh(bool keepObsoletePixmaps)
{
    if(keepObsoletePixmaps && s_settings->pageItem().keepObsoletePixmaps())
    {
        if(const CacheObject* object = s_cache.object(cacheKey()))
        {
            m_obsoletePixmap = object->first;
        }
    }
    else
    {
        m_obsoletePixmap = QPixmap();
    }

    if(!keepObsoletePixmaps)
    {
        m_cropRect = QRectF();
    }

    m_renderTask.cancel(true);

    m_pixmapError = false;
    m_pixmap = QPixmap();
}

int TileItem::startRender(bool prefetch)
{
    m_page->startLoadInteractiveElements();

    if(m_pixmapError || m_renderTask.isRunning() || (prefetch && s_cache.contains(cacheKey())))
    {
        return 0;
    }

    m_renderTask.start(m_page->m_renderParam, m_rect, prefetch);

    return 1;
}

void TileItem::cancelRender()
{
    m_renderTask.cancel();

    m_pixmap = QPixmap();
    m_obsoletePixmap = QPixmap();
}

void TileItem::deleteAfterRender()
{
    if(!m_renderTask.isRunning())
    {
        m_renderTask.deleteParentLater();
    }
    else
    {
        m_renderTask.cancel(true);

        m_deleteAfterRender = true;
    }
}

void TileItem::on_finished(const RenderParam& renderParam,
                           const QRect& rect, bool prefetch,
                           const QImage& image, const QRectF& cropRect)
{
    if(m_page->m_renderParam != renderParam || m_rect != rect)
    {
        on_finishedOrCanceled();
        return;
    }

    m_obsoletePixmap = QPixmap();

    if(image.isNull())
    {
        m_pixmapError = true;

        on_finishedOrCanceled();
        return;
    }

    if(prefetch && !m_renderTask.wasCanceledForcibly())
    {
        const QPixmap pixmap = QPixmap::fromImage(image);

        s_cache.insert(cacheKey(), new CacheObject(pixmap, cropRect), cacheCost(pixmap));

        setCropRect(cropRect);
    }
    else if(!m_renderTask.wasCanceled())
    {
        m_pixmap = QPixmap::fromImage(image);

        setCropRect(cropRect);
    }

    on_finishedOrCanceled();
}

void TileItem::on_canceled()
{
    on_finishedOrCanceled();
}

void TileItem::on_finishedOrCanceled()
{
    if(m_deleteAfterRender)
    {
        m_renderTask.deleteParentLater();
    }
    else if(!m_page->useTiling() || m_page->m_exposedTileItems.contains(this))
    {
        m_page->update();
    }
}

inline TileItem::CacheKey TileItem::cacheKey() const
{
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);

    stream << m_page->m_renderParam << m_rect;

    return qMakePair(m_page, key);
}

QPixmap TileItem::takePixmap()
{
    const CacheKey key = cacheKey();

    if(const CacheObject* object = s_cache.object(key))
    {
        m_obsoletePixmap = QPixmap();

        setCropRect(object->second);
        return object->first;
    }

    QPixmap pixmap;

    if(!m_pixmap.isNull())
    {
        s_cache.insert(key, new CacheObject(m_pixmap, m_cropRect), cacheCost(m_pixmap));

        pixmap = m_pixmap;
    }
    else
    {
        startRender();
    }

    return pixmap;
}

namespace
{

// Longest side of the preview in device-independent pixels; small enough to render quickly.
const qreal previewExtent = 700.0;

// Memory shared by all page previews.
const qint64 previewBudget = 48 * 1024 * 1024;

inline qint64 pixmapBytes(const QPixmap& pixmap)
{
    return qint64(pixmap.width()) * pixmap.height() * qMax(pixmap.depth(), 8) / 8;
}

} // anonymous

QList< PreviewItem* > PreviewItem::s_recentlyUsed;
qint64 PreviewItem::s_totalBytes = 0;

PreviewItem::PreviewItem(PageItem* page) : RenderTaskParent(),
    m_page(page),
    m_pixmap(),
    m_pixmapParam(),
    m_stale(false),
    m_renderTask(page->m_page, this)
{
}

PreviewItem::~PreviewItem()
{
    m_renderTask.cancel(true);
    m_renderTask.wait();

    forget();
}

RenderParam PreviewItem::previewParam() const
{
    RenderParam renderParam = m_page->m_renderParam;

    const QSizeF size = m_page->displayedSize(renderParam);
    const qreal extent = qMax(size.width(), size.height());

    if(extent > previewExtent)
    {
        renderParam.setScaleFactor(renderParam.scaleFactor() * previewExtent / extent);
    }

    return renderParam;
}

bool PreviewItem::isUsable() const
{
    if(m_pixmap.isNull())
    {
        return false;
    }

    // The preview can be scaled to any zoom level, but not across rotations or colour changes.
    RenderParam renderParam = m_page->m_renderParam;
    renderParam.setScaleFactor(m_pixmapParam.scaleFactor());
    renderParam.setResolution(m_pixmapParam.resolutionX(), m_pixmapParam.resolutionY());
    renderParam.setDevicePixelRatio(m_pixmapParam.devicePixelRatio());

    return renderParam == m_pixmapParam;
}

void PreviewItem::prepare()
{
    if(m_renderTask.isRunning() || (isUsable() && !m_stale))
    {
        return;
    }

    m_stale = false;

    // Higher priority than visible tiles so the preview arrives first.
    m_renderTask.start(previewParam(), QRect(), false, 3);
}

bool PreviewItem::paint(QPainter* painter, QPointF topLeft, const QRectF& tileRect)
{
    prepare();

    if(!isUsable())
    {
        return false;
    }

    const QRectF& pageRect = m_page->m_boundingRect;

    if(pageRect.isEmpty())
    {
        return false;
    }

    const qreal scaleX = m_pixmap.width() / pageRect.width();
    const qreal scaleY = m_pixmap.height() / pageRect.height();

    const QRectF sourceRect(tileRect.left() * scaleX, tileRect.top() * scaleY, tileRect.width() * scaleX, tileRect.height() * scaleY);

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawPixmap(tileRect.translated(topLeft), m_pixmap, sourceRect);
    painter->restore();

    touch();

    return true;
}

void PreviewItem::on_finished(const RenderParam& renderParam,
                              const QRect&, bool,
                              const QImage& image, const QRectF&)
{
    if(!image.isNull() && !m_renderTask.wasCanceled())
    {
        forget();

        m_pixmap = QPixmap::fromImage(image);
        m_pixmapParam = renderParam;

        s_totalBytes += pixmapBytes(m_pixmap);
        touch();

        enforceBudget(this);
    }

    m_page->update();
}

void PreviewItem::on_canceled()
{
}

void PreviewItem::touch()
{
    if(s_recentlyUsed.isEmpty() || s_recentlyUsed.last() != this)
    {
        s_recentlyUsed.removeAll(this);
        s_recentlyUsed.append(this);
    }
}

void PreviewItem::forget()
{
    if(!m_pixmap.isNull())
    {
        s_totalBytes -= pixmapBytes(m_pixmap);
        m_pixmap = QPixmap();
    }

    s_recentlyUsed.removeAll(this);
}

void PreviewItem::enforceBudget(PreviewItem* keep)
{
    while(s_totalBytes > previewBudget && !s_recentlyUsed.isEmpty() && s_recentlyUsed.first() != keep)
    {
        s_recentlyUsed.first()->forget();
    }
}

} // qpdfview
