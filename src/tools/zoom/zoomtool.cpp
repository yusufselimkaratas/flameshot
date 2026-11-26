// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "zoomtool.h"
#include "utils/colorutils.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QtMath>

namespace {

// Scroll range. Stored as quarters so 0.25 steps stay exact.
constexpr int kZoomMinQuarters = 6;  // 1.5×
constexpr int kZoomMaxQuarters = 24; // 6×
constexpr int kZoomDefaultQuarters = 8; // 2×
// A side that cannot reach this is not worth a callout.
constexpr double kMinFit = 1.25;
constexpr int kGap = 16;
constexpr qreal kFrame = 1.5;

enum Side
{
    SideRight = 0,
    SideLeft = 1,
    SideBottom = 2,
    SideTop = 3
};

struct SideSpace
{
    QRect region;
    int side;
    double scale;
};

QString formatZoom(double scale)
{
    double rounded = qRound(scale * 10.0) / 10.0;
    if (qAbs(rounded - qRound(rounded)) < 0.05) {
        return QString::number(qRound(rounded)) + QChar(0x00D7);
    }
    return QString::number(rounded, 'f', 1) + QChar(0x00D7);
}

} // namespace

ZoomTool::ZoomTool(QObject* parent)
  : AbstractTwoPointTool(parent)
  , m_lockedSide(-1)
  , m_zoomQuarters(kZoomDefaultQuarters)
{
    m_supportsDiagonalAdj = true;
}

QIcon ZoomTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "zoom.svg");
}

QString ZoomTool::name() const
{
    return tr("Zoom");
}

QString ZoomTool::info()
{
    return tr("Zoom %1").arg(zoomLabel());
}

CaptureTool::Type ZoomTool::type() const
{
    return CaptureTool::TYPE_ZOOM;
}

QString ZoomTool::description() const
{
    return tr("Magnify a region beside the selection. Scroll to set the zoom");
}

QString ZoomTool::zoomLabel() const
{
    return formatZoom(displayedScale());
}

double ZoomTool::requestedZoom() const
{
    return m_zoomQuarters * 0.25;
}

double ZoomTool::displayedScale() const
{
    Layout lay = layout();
    return lay.valid ? lay.scale : requestedZoom();
}

bool ZoomTool::handleMouseWheelEvent(int delta,
                                     bool adjustmentButtonPressed,
                                     CaptureContext& context)
{
    Q_UNUSED(adjustmentButtonPressed)
    Q_UNUSED(context)
    m_zoomQuarters =
      qBound(kZoomMinQuarters, m_zoomQuarters + delta, kZoomMaxQuarters);
    return true;
}

ZoomTool::Layout ZoomTool::layout() const
{
    Layout out;
    QRect source = QRect(points().first, points().second).normalized();
    if (m_bounds.isValid()) {
        source = source.intersected(m_bounds);
    }
    out.source = source;
    if (source.width() < 2 || source.height() < 2 || !m_bounds.isValid()) {
        return out;
    }

    const QRect bounds = m_bounds;
    const double want = requestedZoom();
    QVector<SideSpace> spaces;

    auto consider = [&](const QRect& region, int side) {
        if (region.width() < 4 || region.height() < 4) {
            return;
        }
        double fit = qMin(region.width() / double(source.width()),
                          region.height() / double(source.height()));
        if (fit < kMinFit) {
            return;
        }
        spaces.append({ region, side, qMin(want, fit) });
    };

    int rightW = bounds.right() - source.right() - kGap;
    if (rightW > 0) {
        consider(QRect(source.right() + 1 + kGap,
                       bounds.top(),
                       rightW,
                       bounds.height()),
                 SideRight);
    }
    int leftW = source.left() - kGap - bounds.left();
    if (leftW > 0) {
        consider(QRect(bounds.left(), bounds.top(), leftW, bounds.height()),
                 SideLeft);
    }
    int bottomH = bounds.bottom() - source.bottom() - kGap;
    if (bottomH > 0) {
        consider(QRect(bounds.left(),
                       source.bottom() + 1 + kGap,
                       bounds.width(),
                       bottomH),
                 SideBottom);
    }
    int topH = source.top() - kGap - bounds.top();
    if (topH > 0) {
        consider(QRect(bounds.left(), bounds.top(), bounds.width(), topH),
                 SideTop);
    }

    int chosenIndex = -1;
    auto take = [&](bool onlyLocked) {
        for (int i = 0; i < spaces.size(); ++i) {
            if (onlyLocked && spaces.at(i).side != m_lockedSide) {
                continue;
            }
            if (chosenIndex < 0 ||
                spaces.at(i).scale > spaces.at(chosenIndex).scale) {
                chosenIndex = i;
            }
        }
    };
    if (m_lockedSide >= 0) {
        take(true);
    }
    if (chosenIndex < 0) {
        take(false);
    }
    if (chosenIndex < 0) {
        return out;
    }

    const SideSpace& chosen = spaces.at(chosenIndex);
    int insetW = qMin(chosen.region.width(),
                      qMax(1, qRound(source.width() * chosen.scale)));
    int insetH = qMin(chosen.region.height(),
                      qMax(1, qRound(source.height() * chosen.scale)));

    int insetX = chosen.region.left();
    int insetY = chosen.region.top();
    int yCentered =
      qBound(chosen.region.top(),
             source.center().y() - insetH / 2,
             qMax(chosen.region.top(), chosen.region.bottom() - insetH + 1));
    int xCentered =
      qBound(chosen.region.left(),
             source.center().x() - insetW / 2,
             qMax(chosen.region.left(), chosen.region.right() - insetW + 1));

    switch (chosen.side) {
        case SideRight:
            insetX = chosen.region.left();
            insetY = yCentered;
            out.connectorA = QLine(source.topRight(), QPoint(insetX, insetY));
            out.connectorB =
              QLine(source.bottomRight(), QPoint(insetX, insetY + insetH - 1));
            break;
        case SideLeft:
            insetX = chosen.region.right() - insetW + 1;
            insetY = yCentered;
            out.connectorA =
              QLine(source.topLeft(), QPoint(insetX + insetW - 1, insetY));
            out.connectorB =
              QLine(source.bottomLeft(),
                    QPoint(insetX + insetW - 1, insetY + insetH - 1));
            break;
        case SideBottom:
            insetX = xCentered;
            insetY = chosen.region.top();
            out.connectorA =
              QLine(source.bottomLeft(), QPoint(insetX, insetY));
            out.connectorB =
              QLine(source.bottomRight(), QPoint(insetX + insetW - 1, insetY));
            break;
        case SideTop:
            insetX = xCentered;
            insetY = chosen.region.bottom() - insetH + 1;
            out.connectorA =
              QLine(source.topLeft(), QPoint(insetX, insetY + insetH - 1));
            out.connectorB = QLine(
              source.topRight(), QPoint(insetX + insetW - 1, insetY + insetH - 1));
            break;
        default:
            return out;
    }

    out.inset = QRect(insetX, insetY, insetW, insetH);
    out.scale = chosen.scale;
    out.side = chosen.side;
    out.valid = true;
    return out;
}

QRect ZoomTool::boundingRect() const
{
    Layout lay = layout();
    if (!lay.valid) {
        return AbstractTwoPointTool::boundingRect();
    }
    return lay.source.united(lay.inset).adjusted(-8, -8, 10, 12);
}

QRect ZoomTool::mousePreviewRect(const CaptureContext& context) const
{
    QRect preview(0, 0, 22, 14);
    preview.moveCenter(context.mousePos);
    return preview.adjusted(-2, -2, 2, 2);
}

CaptureTool* ZoomTool::copy(QObject* parent)
{
    auto* tool = new ZoomTool(parent);
    copyParams(this, tool);
    tool->m_bounds = m_bounds;
    tool->m_lockedSide = m_lockedSide;
    tool->m_zoomQuarters = m_zoomQuarters;
    return tool;
}

void ZoomTool::process(QPainter& painter, const QPixmap& pixmap)
{
    Layout lay = layout();
    if (lay.source.width() < 2 || lay.source.height() < 2) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QColor ink = color().isValid() ? color() : QColor(QStringLiteral("#1f4e78"));
    const QColor mount(255, 255, 255);

    if (lay.valid) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(31, 78, 120, 28));
        painter.drawRect(lay.inset.translated(0, 3));

        painter.save();
        painter.setClipRect(lay.inset.adjusted(1, 1, -1, -1));
        const qreal pixelRatio = pixmap.devicePixelRatio();
        QRect selectionScaled(lay.source.topLeft() * pixelRatio,
                              lay.source.bottomRight() * pixelRatio);
        painter.drawPixmap(lay.inset, pixmap.copy(selectionScaled));
        painter.restore();
    }

    painter.setBrush(Qt::NoBrush);
    auto stroke = [&](const QPen& pen) {
        painter.setPen(pen);
        painter.drawRect(lay.source);
        if (!lay.valid) {
            return;
        }
        painter.drawLine(lay.connectorA);
        painter.drawLine(lay.connectorB);
        painter.drawRect(lay.inset);
    };
    stroke(QPen(mount, kFrame + 1.5, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin));
    stroke(QPen(ink, kFrame, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin));

    if (lay.valid && lay.inset.width() >= 36 && lay.inset.height() >= 22) {
        QFont font = painter.font();
        font.setPixelSize(11);
        font.setBold(true);
        painter.setFont(font);
        const QString text = formatZoom(lay.scale);
        QRect chip = QFontMetrics(font).boundingRect(text).adjusted(-5, -2, 5, 2);
        chip.moveBottomRight(lay.inset.bottomRight() - QPoint(6, 6));
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawRect(chip);
        painter.setPen(ColorUtils::colorIsDark(ink) ? Qt::white
                                                    : QColor(QStringLiteral("#1f4e78")));
        painter.drawText(chip, Qt::AlignCenter, text);
    }

    painter.restore();
}

void ZoomTool::drawSearchArea(QPainter& painter, const QPixmap& pixmap)
{
    Q_UNUSED(pixmap)
    Layout lay = layout();
    painter.fillRect(lay.source, Qt::black);
    if (lay.valid) {
        painter.fillRect(lay.inset, Qt::black);
    }
}

void ZoomTool::paintMousePreview(QPainter& painter,
                                 const CaptureContext& context)
{
    painter.setPen(QPen(context.color, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(mousePreviewRect(context).adjusted(2, 2, -2, -2));
}

void ZoomTool::drawStart(const CaptureContext& context)
{
    AbstractTwoPointTool::drawStart(context);
    m_lockedSide = -1;

    const qreal dpr = context.screenshot.devicePixelRatio();
    const qreal ratio = dpr > 0 ? dpr : 1.0;
    if (!context.selection.isNull()) {
        m_bounds = QRect(qRound(context.selection.x() / ratio),
                         qRound(context.selection.y() / ratio),
                         qRound(context.selection.width() / ratio),
                         qRound(context.selection.height() / ratio));
    } else {
        m_bounds = QRect(0,
                         0,
                         qRound(context.screenshot.width() / ratio),
                         qRound(context.screenshot.height() / ratio));
    }
}

void ZoomTool::drawEnd(const QPoint& p)
{
    AbstractTwoPointTool::drawEnd(p);
    m_lockedSide = layout().side;
}

void ZoomTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
}
