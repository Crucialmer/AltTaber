#include "utils/IconOnlyDelegate.h"
#include "widget.h"

void IconOnlyDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen); //取消边框
    // option.rect.size() == QListWidgetItem::sizeHint()
    if (option.state & QStyle::State_Selected) {
        painter->setBrush(selectedColor);
        painter->drawRoundedRect(option.rect, radius, radius);
    } else if (option.state & QStyle::State_MouseOver) {
        painter->setBrush(hoverColor);
        painter->drawRoundedRect(option.rect, radius, radius);
    }

    // 居中绘制图标
    auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (!icon.isNull()) {
        QRect iconRect{{}, option.decorationSize}; // QListWidget::iconSize()
        iconRect.moveCenter(option.rect.center());
        icon.paint(painter, iconRect);
    }

    // draw badge
    auto num = qvariant_cast<WindowGroup>(index.data(Qt::UserRole)).windows.size();
    if (num > 1) {
        auto text = QString::number(num);
        // 徽标随单元格尺寸等比缩放（基准 80px——自适应压缩图标时保持视觉比例）
        const double scale = option.rect.width() / 80.0;
        const int R = qRound(12 * scale);
        const int extraWidth = qRound(8 * scale) * static_cast<int>(text.size() - 1);
        auto badgeCenter = option.rect.topRight() + QPoint(-(R + qRound(3 * scale)), R + qRound(3 * scale));
        // extra Width for extra number
        auto badgeRect = QRect(badgeCenter + QPoint(-R - extraWidth, -R), QSize(2 * R + extraWidth, 2 * R));
        painter->setPen(QColor(200, 200, 200, 50));
        painter->setBrush(QColor(133, 114, 97)); // learn from iOS
        painter->drawRoundedRect(badgeRect, R, R);

        QFont font{"Microsoft YaHei"};
        font.setPointSizeF(12.8 * scale);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor(214, 192, 171));
        painter->drawText(badgeRect, Qt::AlignCenter, text);
    }
}
