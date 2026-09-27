#ifndef WINDOWGEOMETRYHELPER_H
#define WINDOWGEOMETRYHELPER_H

#include <QObject>

class QWindow;

class WindowGeometryHelper : public QObject
{
    Q_OBJECT

public:
    explicit WindowGeometryHelper(QObject *parent = nullptr);

    Q_INVOKABLE bool placeDirectlyBelow(QWindow *upperWindow,
                                        QWindow *lowerWindow,
                                        int lowerContentHeight) const;
};

#endif
