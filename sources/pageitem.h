/*

Copyright 2021 S. Razi Alavizadeh
Copyright 2012-2014, 2021 Adam Reichold

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

#ifndef PAGEITEM_H
#define PAGEITEM_H

#include <QCache>
#include <QFutureWatcher>
#include <QGraphicsObject>
#include <QIcon>
#include <QPointer>
#include <QUndoStack>
#include <QSet>

class QGraphicsProxyWidget;

#include "renderparam.h"
#include "model.h"


namespace qpdfview
{

namespace Model
{
struct Link;
class Annotation;
class FormField;
class Page;
}

class Settings;
class RenderTask;
class TileItem;
class PreviewItem;

class PageItem : public QGraphicsObject
{
    Q_OBJECT

    friend class TileItem;
    friend class PreviewItem;

public:
    enum PaintMode
    {
        DefaultMode,
        PresentationMode,
        ThumbnailMode
    };

    PageItem(Model::Page* page, int index, PaintMode paintMode = DefaultMode, QGraphicsItem* parent = 0);
    ~PageItem();

    const QRectF& uncroppedBoundingRect() const { return m_boundingRect; }

    QRectF boundingRect() const;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*);

    int index() const { return m_index; }

    const QSizeF& size() const { return m_size; }

    QSizeF displayedSize() const { return displayedSize(renderParam()); }
    QSizeF displayedSize(const RenderParam& renderParam) const;

    const QList< QRectF >& highlights() const { return m_highlights; }
    void setHighlights(const QList< QRectF >& highlights);

    RubberBandMode rubberBandMode() const { return m_rubberBandMode; }
    void setRubberBandMode(RubberBandMode rubberBandMode);

    bool showsAnnotationOverlay() const { return !m_annotationOverlay.isEmpty(); }
    bool showsFormFieldOverlay() const { return !m_formFieldOverlay.isEmpty(); }

    const RenderParam& renderParam() const { return m_renderParam; }
    void setRenderParam(const RenderParam& renderParam);

    const QTransform& transform() const { return m_transform; }
    const QTransform& normalizedTransform() const { return m_normalizedTransform; }

    QPointF sourcePos(QPointF point) const { return m_transform.inverted().map(point); }
    QPointF normalizedSourcePos(QPointF point) const { return m_normalizedTransform.inverted().map(point); }

    // undo support

    void setUndoStack(QUndoStack* undoStack) { m_undoStack = undoStack; }

    // Annotations are tracked by stable ids, since undo and redo re-create them as new objects.
    void restoreAnnotations(const QList< int >& ids, const QList< QVariant >& states);
    void removeAnnotations(const QList< int >& ids);

    // text selection

    bool hasTextSelection() const { return m_selectionStart < m_selectionEnd; }
    QString selectedText() const;
    void clearTextSelection();

signals:
    void cropRectChanged();

    void linkClicked(bool newTab, int page, qreal left = qQNaN(), qreal top = qQNaN());
    void linkClicked(bool newTab, const QString& fileName, int page);
    void linkClicked(const QString& url);

    void rubberBandStarted();
    void rubberBandFinished();

    void appendTextToBookmarkComment(int page, const QString& text);
    void zoomToSelection(int page, const QRectF& rect);
    void openInSourceEditor(int page, QPointF pos);

    void wasModified();

public slots:
    void refresh(bool keepObsoletePixmaps = false, bool dropCachedPixmaps = false);

    int startRender(bool prefetch = false);
    void cancelRender();

protected slots:
    void showAnnotationOverlay(Model::Annotation* selectedAnnotation);
    void hideAnnotationOverlay(bool deleteLater = true);
    void updateAnnotationOverlay();

    void showFormFieldOverlay(Model::FormField* selectedFormField);
    void hideFormFieldOverlay(bool deleteLater = true);
    void updateFormFieldOverlay();

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent*);
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event);
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*);

    void mousePressEvent(QGraphicsSceneMouseEvent* event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event);
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event);

    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event);

private slots:
    void on_loadInteractiveElements_finished();
    void on_loadTextWords_finished();

private:
    Q_DISABLE_COPY(PageItem)

    static Settings* s_settings;

    Model::Page* m_page;
    QSizeF m_size;

    QRectF m_cropRect;

    void updateCropRect();

    int m_index;
    PaintMode m_paintMode;

    bool presentationMode() const;
    bool thumbnailMode() const;

    bool useTiling() const;

    QList< QRectF > m_highlights;

    // interactive elements

    QFutureWatcher< void >* m_loadInteractiveElements;

    void startLoadInteractiveElements();
    void loadInteractiveElements();

    QList< Model::Link* > m_links;
    QList< Model::Annotation* > m_annotations;
    QList< Model::FormField* > m_formFields;

    RubberBandMode m_rubberBandMode;
    QRectF m_rubberBand;

    bool m_drawing;
    QVector< QPointF > m_drawPoints;

    void startDrawing(const QPointF& pos);
    void continueDrawing(const QPointF& pos);
    void finishDrawing();
    void eraseAnnotationAt(const QPointF& pos);
    void appendNewAnnotation(Model::Annotation* annotation);
    void commitNewAnnotations(const QList< Model::Annotation* >& annotations, const QString& undoText);
    void removeAnnotationsWithUndo(const QList< Model::Annotation* >& annotations, const QString& undoText);
    qreal drawScale() const;

    QPointer< QUndoStack > m_undoStack;

    QHash< int, QPointer< Model::Annotation > > m_annotationIds;
    static int s_nextAnnotationId;

    int annotationId(Model::Annotation* annotation);

    bool handleElementClick(const QPointF& pos);

    // text selection

    // Word boxes are loaded on demand (when the mouse enters the page) and only kept
    // for a few recently used pages, since they are large for text-heavy documents.
    QList< Model::TextWord > m_textWords;
    QFutureWatcher< QList< Model::TextWord > >* m_loadTextWords;
    bool m_textWordsLoaded;
    QVector< int > m_wordFirstChar;
    int m_charCount;

    int m_selectionAnchor;
    int m_selectionStart;
    int m_selectionEnd;
    bool m_selectingText;
    bool m_selectionMoved;
    bool m_selectionForHighlight;
    QPointF m_selectionPressPos;

    static PageItem* s_textSelectionOwner;
    static QList< PageItem* > s_textWordsRecentlyUsed;

    void startLoadTextWords();
    void dropTextWords();

    void beginTextSelection(const QPointF& pos, bool forHighlight);
    bool isNearText(const QPointF& pos) const;
    int wordAt(const QPointF& pos) const;
    int caretAt(const QPointF& pos) const;
    QList< QRectF > selectionRects() const;
    bool selectionContains(const QPointF& pos) const;
    void finishTextSelection();
    void showTextSelectionContextMenu(QPoint screenPos);
    void markSelection(Model::TextMarkupType type);

    void copyToClipboard(QPoint screenPos);
    void addAnnotation(QPoint screenPos);

    void showLinkContextMenu(Model::Link* link, QPoint screenPos);
    void showAnnotationContextMenu(Model::Annotation* annotation, QPoint screenPos);

    // overlay

    typedef QMap< Model::Annotation*, QGraphicsProxyWidget* > AnnotationOverlay;
    AnnotationOverlay m_annotationOverlay;

    typedef QMap< Model::FormField*, QGraphicsProxyWidget* > FormFieldOverlay;
    FormFieldOverlay m_formFieldOverlay;

    template< typename Overlay, typename Element > void showOverlay(Overlay& overlay, const char* hideOverlay, const QList< Element* >& elements, Element* selectedElement);
    template< typename Overlay, typename Element > void addProxy(Overlay& overlay, const char* hideOverlay, Element* element);
    template< typename Overlay > void hideOverlay(Overlay& overlay, bool deleteLater);
    template< typename Overlay > void updateOverlay(const Overlay& overlay) const;

    void setProxyGeometry(Model::Annotation* annotation, QGraphicsProxyWidget* proxy) const;
    void setProxyGeometry(Model::FormField* formField, QGraphicsProxyWidget* proxy) const;

    // geometry

    RenderParam m_renderParam;

    QTransform m_transform;
    QTransform m_normalizedTransform;
    QRectF m_boundingRect;

    void prepareGeometry();

    QVector< TileItem* > m_tileItems;

    // Low-resolution render of the whole page, shown (blurry) where tiles are still rendering.
    PreviewItem* m_previewItem;
    mutable QSet< TileItem* > m_exposedTileItems;

    void prepareTiling();

    // paint

    void paintPage(QPainter* painter, const QRectF& exposedRect) const;

    void paintLinks(QPainter* painter) const;
    void paintFormFields(QPainter* painter) const;

    void paintHighlights(QPainter* painter) const;
    void paintRubberBand(QPainter* painter) const;
    void paintDrawing(QPainter* painter) const;
    void paintTextSelection(QPainter* painter) const;

};

} // qpdfview

#endif // PAGEITEM_H
