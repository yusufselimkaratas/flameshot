// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "tools/abstracttwopointtool.h"

#include <QLine>

class ZoomTool : public AbstractTwoPointTool
{
    Q_OBJECT
public:
    explicit ZoomTool(QObject* parent = nullptr);

    QIcon icon(const QColor& background, bool inEditor) const override;
    QString name() const override;
    QString info() override;
    QString description() const override;
    QString zoomLabel() const;
    QRect boundingRect() const override;
    QRect mousePreviewRect(const CaptureContext& context) const override;
    bool handleMouseWheelEvent(int delta,
                               bool adjustmentButtonPressed,
                               CaptureContext& context) override;

    CaptureTool* copy(QObject* parent = nullptr) override;
    void process(QPainter& painter, const QPixmap& pixmap) override;
    void drawSearchArea(QPainter& painter, const QPixmap& pixmap) override;
    void paintMousePreview(QPainter& painter,
                           const CaptureContext& context) override;

protected:
    CaptureTool::Type type() const override;

public slots:
    void drawStart(const CaptureContext& context) override;
    void drawEnd(const QPoint& p) override;
    void pressed(CaptureContext& context) override;

private:
    // Geometry of one callout: the region the user drew, the magnified
    // copy, and the two lines that tie them together.
    struct Layout
    {
        QRect source;
        QRect inset;
        QLine connectorA;
        QLine connectorB;
        double scale = 0;
        int side = -1;
        bool valid = false;
    };

    Layout layout() const;
    double requestedZoom() const;
    double displayedScale() const;

    // Selection (logical pixels) the inset has to stay inside.
    QRect m_bounds;
    // Side chosen when the drag finished. -1 while the drag is in progress.
    int m_lockedSide;
    // Magnification in quarter steps. 8 is 2×. Scroll changes this.
    int m_zoomQuarters;
};
