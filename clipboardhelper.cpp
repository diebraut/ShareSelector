#ifndef CLIPBOARDHELPER_H
#define CLIPBOARDHELPER_H

#include <QObject>
#include <QGuiApplication>
#include <QClipboard>

class ClipboardHelper : public QObject
{
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void setText(const QString &text) {
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(text, QClipboard::Clipboard);
    }

    Q_INVOKABLE QString text() const {
        QClipboard *clipboard = QGuiApplication::clipboard();
        return clipboard->text(QClipboard::Clipboard);
    }
};

#endif // CLIPBOARDHELPER_H

Q_DECLARE_METATYPE(ClipboardHelper*)

