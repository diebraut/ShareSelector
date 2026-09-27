#include "windowgeometryhelper.h"

#include <QMargins>
#include <QWindow>

WindowGeometryHelper::WindowGeometryHelper(QObject *parent)
    : QObject(parent)
{
}

bool WindowGeometryHelper::placeDirectlyBelow(QWindow *upperWindow,
                                              QWindow *lowerWindow,
                                              int lowerContentHeight) const
{
    if (!upperWindow || !lowerWindow)
        return false;

    const QRect upperFrame = upperWindow->frameGeometry();
    const QMargins lowerMargins = lowerWindow->frameMargins();
    if (!upperFrame.isValid())
        return false;

    const int lowerContentX = upperFrame.left() + lowerMargins.left();
    const int lowerContentY = upperFrame.bottom() + 1 + lowerMargins.top();
    const int lowerContentWidth = qMax(
        lowerWindow->minimumWidth(),
        upperFrame.width() - lowerMargins.left() - lowerMargins.right());

    lowerWindow->setGeometry(
        lowerContentX,
        lowerContentY,
        lowerContentWidth,
        qMax(lowerWindow->minimumHeight(), lowerContentHeight));
    return true;
}
