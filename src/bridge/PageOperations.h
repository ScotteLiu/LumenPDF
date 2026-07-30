#pragma once

#include <QObject>
#include <QSharedPointer>
#include <QUrl>
#include <QVector>

namespace lumen {

class PdfDocument;

// Page-level editing -- rotate, reorder, delete -- with undo and redo.
//
// PDFium has no undo of its own, so this keeps a command stack and inverts
// each operation. Rotation and reordering invert arithmetically. Deletion
// cannot, so a deleted page is first copied into a scratch document that
// exists only to hold it; undo imports it back. The scratch document *is* the
// undo buffer, and it keeps the page's annotations with it.
class PageOperations : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stackChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY stackChanged)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY stackChanged)

public:
    explicit PageOperations(QObject *parent = nullptr);

    void setDocument(const QSharedPointer<PdfDocument> &document);

    bool canUndo() const { return m_position > 0; }
    bool canRedo() const { return m_position < m_commands.size(); }
    QString undoLabel() const;
    QString redoLabel() const;

    Q_INVOKABLE bool rotate(int pageIndex, int quarterTurns);
    Q_INVOKABLE bool move(int from, int to);
    Q_INVOKABLE bool remove(int pageIndex);

    // Appends every page of another PDF. Undoable.
    Q_INVOKABLE bool mergeFrom(const QUrl &url);

    // Inserts every page of another PDF at a position. Undoable.
    Q_INVOKABLE bool insertFrom(const QUrl &url, int atIndex);

    // Writes pages out to a new file. Does not modify this document, so it is
    // not on the undo stack. `lastPage` is inclusive.
    Q_INVOKABLE bool extractTo(const QUrl &url, int firstPage, int lastPage);

    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();

    Q_INVOKABLE void clearHistory();

signals:
    void stackChanged();
    void failed(const QString &reason);
    void extracted(const QString &filePath, int pageCount);
    void merged(const QString &filePath, int pageCount);

    // The page count or ordering changed; every model over this document has
    // to be reset, and every cached raster is stale.
    void structureChanged();

    // Only the given page's appearance changed.
    void pageInvalidated(int pageIndex);

private:
    struct Command {
        enum Kind { Rotate, Move, Delete, Insert } kind = Rotate;
        int a = 0;          // rotate: page   move: from   delete: page   insert: at
        int b = 0;          // rotate: turns  move: to     delete: stash   insert: count

        // Insert only. The source document is held open for the lifetime of
        // the command so redo re-imports exactly what was imported the first
        // time, even if the file on disk has since changed or gone away.
        QSharedPointer<PdfDocument> source;
    };

    // Takes a mutable reference: deleting writes back where the page was
    // stashed, which undo needs and the caller stores.
    bool apply(Command &command);
    bool revert(const Command &command);
    void push(const Command &command);

    QSharedPointer<PdfDocument> m_document;

    // Holds deleted pages so they can come back. Created lazily -- most
    // sessions never delete a page and should not pay for an extra document.
    QSharedPointer<PdfDocument> m_stash;

    QVector<Command> m_commands;

    // Index of the next command to redo; everything before it is applied.
    int m_position = 0;
};

} // namespace lumen
